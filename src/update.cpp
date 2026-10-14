#include "update.hpp"
#include "update_config.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>
namespace noctra {
namespace {
constexpr std::size_t manifest_limit=32768,payload_limit=32*1024*1024;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Handle {HANDLE h{};explicit Handle(HANDLE value):h(value){}~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}Handle(const Handle&)=delete;};
struct Http {HINTERNET h{};explicit Http(HINTERNET value):h(value){require(h!=nullptr,"Update HTTPS setup failed");}~Http(){WinHttpCloseHandle(h);}Http(const Http&)=delete;};
struct Alg {BCRYPT_ALG_HANDLE h{};explicit Alg(LPCWSTR name){require(BCryptOpenAlgorithmProvider(&h,name,nullptr,0)>=0,"Update crypto setup failed");}~Alg(){BCryptCloseAlgorithmProvider(h,0);}};
struct Key {BCRYPT_KEY_HANDLE h{};~Key(){if(h)BCryptDestroyKey(h);}};
std::uint64_t unix_now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
std::string text(const std::vector<unsigned char>& bytes){return {bytes.begin(),bytes.end()};}
void regular_path(const std::filesystem::path& path,bool must_exist=true){
    auto current=path;
    while(!current.empty()){
        const auto attr=GetFileAttributesW(current.c_str());
        if(attr==INVALID_FILE_ATTRIBUTES){if(current==path&&!must_exist){current=current.parent_path();continue;}throw std::runtime_error("Update path unavailable");}
        require((attr&FILE_ATTRIBUTE_REPARSE_POINT)==0,"Update paths cannot contain reparse points");
        const auto parent=current.parent_path();if(parent==current)break;current=parent;
    }

}
std::string release_prefix(){return "https://github.com/"+std::string(update_repository)+"/releases/download/";}
bool release_url(const std::string& url,const char* asset){
    const auto prefix=release_prefix();if(!url.starts_with(prefix))return false;
    const auto tail=url.substr(prefix.size());const auto slash=tail.find('/');
    if(slash==std::string::npos||tail.substr(slash+1)!=asset||slash==0||slash>64)return false;
    return std::all_of(tail.begin(),tail.begin()+static_cast<std::ptrdiff_t>(slash),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_';});
}
struct HttpResult {DWORD status{};std::vector<unsigned char> bytes;};
HttpResult get(std::string url,std::size_t limit,std::stop_token stop){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
    for(int redirects=0;redirects<4;++redirects){
        require(url.size()<4096&&url.starts_with("https://"),"Invalid update HTTPS URL");
        const std::wstring wide(url.begin(),url.end());URL_COMPONENTS c{};c.dwStructSize=sizeof(c);
        c.dwHostNameLength=c.dwUrlPathLength=c.dwExtraInfoLength=static_cast<DWORD>(-1);
        require(WinHttpCrackUrl(wide.c_str(),static_cast<DWORD>(wide.size()),0,&c)&&c.nScheme==INTERNET_SCHEME_HTTPS&&c.nPort==443,"Invalid update URL");
        const std::wstring host(c.lpszHostName,c.dwHostNameLength);
        require(host==L"api.github.com"||host==L"github.com"||host==L"release-assets.githubusercontent.com"||host==L"objects.githubusercontent.com","Untrusted update redirect host");
        std::wstring path(c.lpszUrlPath,c.dwUrlPathLength);if(c.dwExtraInfoLength)path.append(c.lpszExtraInfo,c.dwExtraInfoLength);
        Http session(WinHttpOpen(L"NoctraUpdater/1.1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
        require(WinHttpSetTimeouts(session.h,3000,5000,5000,5000)!=FALSE,"Update timeout setup failed");
        Http connection(WinHttpConnect(session.h,host.c_str(),443,0));
        Http request(WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
        DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER,disable=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;
        require(WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect))&&WinHttpSetOption(request.h,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable)),"Update privacy setup failed");
        require(!stop.stop_requested()&&std::chrono::steady_clock::now()<deadline,"Update cancelled or timed out");
        require(WinHttpSendRequest(request.h,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request.h,nullptr),"Update HTTPS request failed");
        DWORD status{},size=sizeof(status);require(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr)!=FALSE,"Update HTTP status missing");
        if(status==301||status==302||status==303||status==307||status==308){
            wchar_t location[4096]{};DWORD bytes=sizeof(location);require(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_LOCATION,nullptr,location,&bytes,nullptr)!=FALSE,"Update redirect missing");
            url.clear();for(const wchar_t v:std::wstring(location)){require(v>=33&&v<=126,"Invalid update redirect");url.push_back(static_cast<char>(v));}continue;
        }
        if(status!=200)return {status,{}};
        std::vector<unsigned char> bytes;std::array<unsigned char,16384> chunk{};
        for(;;){require(!stop.stop_requested()&&std::chrono::steady_clock::now()<deadline,"Update cancelled or timed out");
            DWORD count{};require(WinHttpReadData(request.h,chunk.data(),static_cast<DWORD>(chunk.size()),&count)!=FALSE,"Update download interrupted");
            if(!count)break;require(count<=limit-bytes.size(),"Update download exceeds size limit");bytes.insert(bytes.end(),chunk.begin(),chunk.begin()+count);}
        return {status,std::move(bytes)};
    }
    throw std::runtime_error("Too many update redirects");
}
void write_file(const std::filesystem::path& path,std::span<const unsigned char> bytes){
    regular_path(path,false);Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    require(file.h!=INVALID_HANDLE_VALUE,"Cannot create update staging file");DWORD count{};
    require(WriteFile(file.h,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size()&&FlushFileBuffers(file.h),"Cannot write staged update");
}
std::filesystem::path new_stage(){
    std::array<unsigned char,16> random{};require(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Update randomness failed");
    const auto path=update_executable_path().parent_path()/("Noctra-update-"+bytes_hex(random));regular_path(path.parent_path());
    require(CreateDirectoryW(path.c_str(),nullptr)!=FALSE,"Cannot create update folder beside EXE; move Noctra to a writable folder");return path;
}
// An argument is never passed to a shell. Paths are quoted with Windows CRT rules.
std::wstring quote(const std::wstring& value){
    std::wstring out=L"\"";std::size_t slashes=0;
    for(wchar_t c:value){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){out.append(slashes*2+1,L'\\');out+=c;}else{out.append(slashes,L'\\');out+=c;}slashes=0;}
    out.append(slashes*2,L'\\');return out+L"\"";
}
HANDLE spawn(const std::filesystem::path& exe,const std::wstring& arguments){
    auto command=quote(exe.wstring())+L" "+arguments;STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
    require(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,exe.parent_path().c_str(),&start,&process)!=FALSE,"Cannot launch update process");
    CloseHandle(process.hThread);return process.hProcess;
}
std::vector<std::wstring> arguments(){int count{};LPWSTR* raw=CommandLineToArgvW(GetCommandLineW(),&count);require(raw!=nullptr,"Cannot parse update arguments");
    std::vector<std::wstring> result(raw,raw+count);LocalFree(raw);return result;}
std::filesystem::path state_path(){wchar_t buffer[32768]{};const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,32768);require(length>0&&length<32768,"Update user storage unavailable");return std::filesystem::path(buffer)/L"Noctra"/L"update-highwater.txt";}
std::uint64_t highwater(){const auto path=state_path();if(!std::filesystem::exists(path))return update_sequence;const auto data=read_update_file(path,32);const auto value=text(data);
    require(!value.empty()&&std::all_of(value.begin(),value.end(),[](char c){return c>='0'&&c<='9';}),"Invalid update rollback state");return std::max(update_sequence,std::stoull(value));}
// Permit another older copy to reach the already-known newest release, while
// rejecting versions below the high-water mark or at/below this binary.
std::uint64_t update_threshold(){const auto watermark=highwater();return std::max(update_sequence,watermark?watermark-1:0);}
void save_highwater(std::uint64_t value){const auto path=state_path();std::filesystem::create_directories(path.parent_path());regular_path(path.parent_path());
    auto temporary=path;temporary+=L".tmp";regular_path(temporary,false);std::ofstream out(temporary,std::ios::binary|std::ios::trunc);require(static_cast<bool>(out),"Cannot save update state");out<<value;out.close();require(static_cast<bool>(out),"Cannot flush update state");
    require(MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"Cannot commit update state");}
}
std::vector<unsigned char> hex_bytes(const std::string& value){require(value.size()%2==0&&value.size()<=65536,"Invalid update hex length");std::vector<unsigned char> result;
    const auto digit=[](char c)->unsigned {if(c>='0'&&c<='9')return static_cast<unsigned>(c-'0');if(c>='a'&&c<='f')return static_cast<unsigned>(c-'a'+10);throw std::runtime_error("Invalid update hex");};
    for(std::size_t i=0;i<value.size();i+=2)result.push_back(static_cast<unsigned char>((digit(value[i])<<4)|digit(value[i+1])));return result;}
std::string bytes_hex(std::span<const unsigned char> bytes){constexpr char digits[]="0123456789abcdef";std::string text;for(auto b:bytes){text+=digits[b>>4];text+=digits[b&15];}return text;}
std::vector<unsigned char> update_digest(std::span<const unsigned char> bytes){Alg alg(BCRYPT_SHA256_ALGORITHM);std::vector<unsigned char> hash(32);
    require(BCryptHash(alg.h,nullptr,0,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),hash.data(),32)>=0,"Update hash failed");return hash;}
std::vector<unsigned char> read_update_file(const std::filesystem::path& path,std::size_t limit){regular_path(path);require(std::filesystem::is_regular_file(path),"Update file is not regular");Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));require(file.h!=INVALID_HANDLE_VALUE,"Cannot read update file");
    LARGE_INTEGER size{};require(GetFileSizeEx(file.h,&size)&&size.QuadPart>0&&static_cast<std::uint64_t>(size.QuadPart)<=limit,"Invalid update file size");std::vector<unsigned char> bytes(static_cast<std::size_t>(size.QuadPart));DWORD count{};
    require(ReadFile(file.h,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size(),"Update file read interrupted");return bytes;}
std::string update_file_hash(const std::filesystem::path& path){return bytes_hex(update_digest(read_update_file(path,payload_limit)));}
UpdateManifest verify_update_manifest(const std::string& envelope,const std::string& public_key,std::uint64_t current,std::uint64_t now){
    require(envelope.size()<=manifest_limit,"Update manifest too large");const auto outer=nlohmann::json::parse(envelope);
    require(outer.is_object()&&outer.size()==2&&outer.contains("payload")&&outer["payload"].is_string()&&outer.contains("signature")&&outer["signature"].is_string(),"Invalid signed update envelope");
    const auto payload=outer["payload"].get<std::string>();const auto signature=hex_bytes(outer["signature"].get<std::string>()),blob=hex_bytes(public_key);
    require(blob.size()==72&&signature.size()==64,"Invalid update signing key/signature");Alg alg(BCRYPT_ECDSA_P256_ALGORITHM);Key key;
    require(BCryptImportKeyPair(alg.h,nullptr,BCRYPT_ECCPUBLIC_BLOB,&key.h,const_cast<PUCHAR>(blob.data()),static_cast<ULONG>(blob.size()),0)>=0,"Invalid update signing key");
    const auto hash=update_digest(std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(payload.data()),payload.size()));
    require(BCryptVerifySignature(key.h,nullptr,const_cast<PUCHAR>(hash.data()),32,const_cast<PUCHAR>(signature.data()),64,0)>=0,"Update signature verification failed");
    const auto json=nlohmann::json::parse(payload);require(json.is_object()&&json.size()==9,"Invalid update manifest schema");
    require(json.at("schema")==1&&json.at("product")=="Noctra"&&json.at("arch")=="windows-x64","Wrong update product/schema/architecture");
    require(json.at("sequence").is_number_unsigned()&&json.at("size").is_number_unsigned()&&json.at("expires").is_number_unsigned(),"Invalid update numeric fields");
    UpdateManifest m;m.sequence=json.at("sequence").get<std::uint64_t>();m.size=json.at("size").get<std::uint64_t>();m.expires=json.at("expires").get<std::uint64_t>();
    m.version=json.at("version").get<std::string>();m.url=json.at("url").get<std::string>();m.sha256=json.at("sha256").get<std::string>();
    require(m.sequence>current&&m.sequence<1000000000ULL,"Update is not newer than this installation");require(m.size>0&&m.size<=payload_limit,"Update size out of range");
    require(m.expires>now&&m.expires-now<=366ULL*86400,"Update metadata expired or expiry too far ahead");
    require(!m.version.empty()&&m.version.size()<=32&&std::all_of(m.version.begin(),m.version.end(),[](char c){return (c>='0'&&c<='9')||c=='.';}),"Invalid update version");
    require(m.sha256.size()==64&&hex_bytes(m.sha256).size()==32&&release_url(m.url,"Noctra.exe"),"Invalid update hash or download URL");return m;
}
void verify_update_payload(const std::filesystem::path& path,const UpdateManifest& m){const auto bytes=read_update_file(path,payload_limit);require(bytes.size()==m.size&&bytes_hex(update_digest(bytes))==m.sha256,"Update payload hash/size mismatch");
    require(bytes.size()>256&&bytes[0]=='M'&&bytes[1]=='Z',"Update is not an EXE");std::uint32_t offset{};std::memcpy(&offset,bytes.data()+60,4);
    require(offset<=bytes.size()-26&&std::memcmp(bytes.data()+offset,"PE\0\0",4)==0&&bytes[offset+4]==0x64&&bytes[offset+5]==0x86&&bytes[offset+24]==0x0b&&bytes[offset+25]==0x02,"Update is not Windows x64 PE");}
void replace_update_file(const std::filesystem::path& target,const std::filesystem::path& pending,const std::filesystem::path& backup){regular_path(target);regular_path(pending);regular_path(backup,false);
    require(!std::filesystem::exists(backup),"Update backup already exists");require(ReplaceFileW(target.c_str(),pending.c_str(),backup.c_str(),0,nullptr,nullptr)!=FALSE,"Update replacement failed; original files retained where possible");}
void rollback_update_file(const std::filesystem::path& target,const std::filesystem::path& backup,const std::filesystem::path& rejected){regular_path(target);regular_path(backup);regular_path(rejected,false);
    require(!std::filesystem::exists(rejected),"Rejected update path already exists");require(ReplaceFileW(target.c_str(),backup.c_str(),rejected.c_str(),0,nullptr,nullptr)!=FALSE,"Update rollback failed; use backup manually");}
std::filesystem::path update_executable_path(){wchar_t path[32768]{};const auto size=GetModuleFileNameW(nullptr,path,32768);require(size>0&&size<32768,"Cannot locate updater EXE");return std::filesystem::path(path);}
std::optional<int> run_update_helper(){
    const auto args=arguments();if(args.size()<2||args[1]!=L"--apply-update")return {};
    try{
        require(args.size()==3,"Invalid helper invocation");const auto helper=update_executable_path(),stage=helper.parent_path();
        require(helper.filename()==L"Updater.exe"&&stage.filename().wstring().starts_with(L"Noctra-update-"),"Invalid update staging folder");
        const auto target=stage.parent_path()/L"Noctra.exe",pending=stage/L"Noctra.pending.exe",backup=stage/L"Noctra.previous.exe";
        const auto pid=std::stoul(args[2]);require(pid>0,"Invalid update parent PID");Handle parent(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));require(parent.h!=nullptr,"Update parent unavailable");
        wchar_t parent_path[32768]{};DWORD length=32768;require(QueryFullProcessImageNameW(parent.h,0,parent_path,&length)&&std::filesystem::equivalent(target,std::filesystem::path(parent_path)),"Update parent identity mismatch");
        const auto envelope=text(read_update_file(stage/L"update.json",manifest_limit));const auto m=verify_update_manifest(envelope,update_public_key,update_threshold(),unix_now());verify_update_payload(pending,m);
        const auto token=stage.filename().wstring().substr(14);const std::wstring name=L"Local\\NoctraUpdateHealth-"+token;
        Handle event(CreateEventW(nullptr,TRUE,FALSE,name.c_str()));require(event.h!=nullptr&&GetLastError()!=ERROR_ALREADY_EXISTS,"Cannot create update health event");
        Handle ready(OpenEventW(EVENT_MODIFY_STATE,FALSE,(L"Local\\NoctraUpdateReady-"+token).c_str()));require(ready.h&&SetEvent(ready.h),"Update parent handshake unavailable");
        require(WaitForSingleObject(parent.h,30000)==WAIT_OBJECT_0,"Noctra did not exit; update not applied");
        verify_update_payload(pending,m);replace_update_file(target,pending,backup);
        HANDLE launched{};
        try{launched=spawn(target,L"--update-health "+token);}
        catch(...){rollback_update_file(target,backup,stage/L"Noctra.rejected.exe");Handle restored(spawn(target,L""));throw;}
        Handle replacement(launched);
        const HANDLE wait_handles[]={event.h,replacement.h};
        const auto health=WaitForMultipleObjects(2,wait_handles,FALSE,60000);
        if(health!=WAIT_OBJECT_0){
            if(WaitForSingleObject(replacement.h,0)!=WAIT_OBJECT_0){
                require(TerminateProcess(replacement.h,1)!=FALSE,"Cannot stop failed update; restore backup manually");
                require(WaitForSingleObject(replacement.h,10000)==WAIT_OBJECT_0,"Failed update remains locked; restore backup manually");}
            rollback_update_file(target,backup,stage/L"Noctra.rejected.exe");Handle restored(spawn(target,L""));
            throw std::runtime_error("New release did not confirm startup; previous version restored");
        }
        save_highwater(std::max(m.sequence,highwater()));
        return 0;
    }catch(const std::exception& e){MessageBoxA(nullptr,e.what(),"Noctra update failed",MB_OK|MB_ICONERROR);return 1;}
}
void acknowledge_update_health(){const auto args=arguments();if(args.size()!=3||args[1]!=L"--update-health")return;
    const auto& token=args[2];if(token.size()!=32||!std::all_of(token.begin(),token.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}))return;
    Handle event(OpenEventW(EVENT_MODIFY_STATE,FALSE,(L"Local\\NoctraUpdateHealth-"+token).c_str()));if(event.h)SetEvent(event.h);}
UpdateService::UpdateService(bool enabled):worker_([this](std::stop_token stop){run(stop);}){if(enabled)check();else{std::lock_guard lock(mutex_);status_.message="Update checks disabled in test mode";}}
UpdateService::~UpdateService(){worker_.request_stop();wake_.notify_all();if(worker_.joinable())worker_.join();}
void UpdateService::check(){std::lock_guard lock(mutex_);if(status_.busy)return;manifest_.reset();envelope_.clear();status_={true,false,false,"Checking for updates...",{}};action_=1;wake_.notify_all();}
void UpdateService::download(){std::lock_guard lock(mutex_);if(status_.busy||!manifest_)return;status_.busy=true;status_.message="Downloading verified update...";action_=2;wake_.notify_all();}
UpdateStatus UpdateService::status()const{std::lock_guard lock(mutex_);return status_;}
bool UpdateService::install(std::string& error){std::lock_guard lock(mutex_);if(!status_.staged||!manifest_){error="No verified update staged";return false;}
    try{const auto exe=update_executable_path();require(exe.filename()==L"Noctra.exe","Restore the EXE name to Noctra.exe before updating");
        verify_update_payload(stage_/L"Noctra.pending.exe",*manifest_);
        const auto token=stage_.filename().wstring().substr(14);Handle ready(CreateEventW(nullptr,TRUE,FALSE,(L"Local\\NoctraUpdateReady-"+token).c_str()));
        require(ready.h&&GetLastError()!=ERROR_ALREADY_EXISTS,"Cannot create update handshake");
        Handle helper(spawn(stage_/L"Updater.exe",L"--apply-update "+std::to_wstring(GetCurrentProcessId())));
        require(WaitForSingleObject(ready.h,5000)==WAIT_OBJECT_0,"Update helper not ready; app remains open");status_.staged=false;return true;
    }catch(const std::exception& e){error=e.what();return false;}}
void UpdateService::run(std::stop_token stop){while(!stop.stop_requested()){
    int action{};{std::unique_lock lock(mutex_);wake_.wait(lock,stop,[&]{return action_!=0;});if(stop.stop_requested())return;action=action_;action_=0;}
    try{
        if(action==1){const auto response=get("https://api.github.com/repos/"+std::string(update_repository)+"/releases/latest",65536,stop);
            require(response.status==200||response.status==404,"GitHub update service unavailable or rate-limited");
            if(response.status==404){std::lock_guard lock(mutex_);status_={false,false,false,"No public release available yet",{}};continue;}
            const auto json=nlohmann::json::parse(text(response.bytes));require(json.is_object()&&!json.at("draft").get<bool>()&&!json.at("prerelease").get<bool>()&&json.at("assets").is_array(),"Invalid GitHub release response");
            std::string url;for(const auto& asset:json.at("assets"))if(asset.at("name")=="update.json"){require(url.empty(),"Duplicate update manifest");url=asset.at("browser_download_url").get<std::string>();}
            if(url.empty()){std::lock_guard lock(mutex_);status_={false,false,false,"Release has no signed update manifest",{}};continue;}
            require(release_url(url,"update.json"),"Unexpected update manifest host/path");const auto response_manifest=get(url,manifest_limit,stop);require(response_manifest.status==200,"Cannot download signed update manifest");
            auto envelope=text(response_manifest.bytes);
            // Authenticate before deciding whether the advertised sequence is newer.
            const auto candidate=verify_update_manifest(envelope,update_public_key,0,unix_now());
            if(candidate.sequence<=update_sequence||candidate.sequence<highwater()){std::lock_guard lock(mutex_);status_={false,false,false,"No newer update available",{}};continue;}
            auto m=verify_update_manifest(envelope,update_public_key,update_threshold(),unix_now());std::lock_guard lock(mutex_);envelope_=std::move(envelope);manifest_=m;status_={false,true,false,"Update available",m.version};
        }else{
            UpdateManifest m;std::string envelope;{std::lock_guard lock(mutex_);require(manifest_.has_value(),"No update selected");m=*manifest_;envelope=envelope_;}
            m=verify_update_manifest(envelope,update_public_key,update_threshold(),unix_now());const auto response=get(m.url,static_cast<std::size_t>(m.size),stop);require(response.status==200,"Update download failed");
            const auto stage=new_stage();write_file(stage/L"Noctra.pending.exe",response.bytes);verify_update_payload(stage/L"Noctra.pending.exe",m);
            write_file(stage/L"update.json",std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(envelope.data()),envelope.size()));
            const auto exe=update_executable_path();regular_path(exe);require(CopyFileW(exe.c_str(),(stage/L"Updater.exe").c_str(),TRUE)!=FALSE,"Cannot stage update helper");
            std::lock_guard lock(mutex_);stage_=stage;status_={false,true,true,"Verified update ready; restart requires approval",m.version};
        }
    }catch(const std::exception& e){std::lock_guard lock(mutex_);status_.busy=false;status_.message=e.what();}
}}
}
