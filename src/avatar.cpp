#include "avatar.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <limits>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace noctra {
namespace {
constexpr std::size_t image_limit=4*1024*1024,json_limit=65536;
constexpr auto request_deadline=std::chrono::seconds(30);
struct HttpHandle {
    HINTERNET h{};
    explicit HttpHandle(HINTERNET value):h(value){if(!h)throw std::runtime_error("Avatar HTTPS initialization failed");}
    ~HttpHandle(){if(h)WinHttpCloseHandle(h);}
    HttpHandle(const HttpHandle&)=delete;HttpHandle& operator=(const HttpHandle&)=delete;
};
struct ComScope {
    HRESULT hr{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};
    ComScope(){if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)throw std::runtime_error("Avatar decoder COM initialization failed");}
    ~ComScope(){if(SUCCEEDED(hr))CoUninitialize();}
};
void require(HRESULT hr,const char* what){if(FAILED(hr))throw std::runtime_error(what);}
std::vector<std::uint8_t> https_get(const std::wstring& url,std::size_t limit,std::stop_token stop,
                                    std::chrono::steady_clock::time_point deadline,std::string_view body={}) {
    URL_COMPONENTS components{};components.dwStructSize=sizeof(components);
    components.dwHostNameLength=components.dwUrlPathLength=components.dwExtraInfoLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&components)||components.nScheme!=INTERNET_SCHEME_HTTPS
        ||components.nPort!=INTERNET_DEFAULT_HTTPS_PORT)throw std::runtime_error("Invalid avatar HTTPS URL");
    const std::wstring host(components.lpszHostName,components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath,components.dwUrlPathLength);
    if(components.dwExtraInfoLength)path.append(components.lpszExtraInfo,components.dwExtraInfoLength);
    HttpHandle session(WinHttpOpen(L"NoctraAvatar/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    // Stay within the one overall request deadline, even while TLS/connect/send waits.
    const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
    if(remaining<=0)throw std::runtime_error("Avatar request timed out");
    const int total_timeout=static_cast<int>(std::min<std::int64_t>(remaining,std::numeric_limits<int>::max()));
    if(!WinHttpSetTimeouts(session.h,total_timeout,total_timeout,total_timeout,total_timeout))throw std::runtime_error("Avatar timeouts setup failed");
    HttpHandle connection(WinHttpConnect(session.h,host.c_str(),INTERNET_DEFAULT_HTTPS_PORT,0));
    HttpHandle request(WinHttpOpenRequest(connection.h,body.empty()?L"GET":L"POST",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if(!WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect)))throw std::runtime_error("Avatar redirect policy setup failed");
    // Never send browser/account cookies or follow a response onto an arbitrary host.
    DWORD disable=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;
    if(!WinHttpSetOption(request.h,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable)))throw std::runtime_error("Avatar privacy options setup failed");
    if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Avatar request cancelled or timed out");
    if(!WinHttpSendRequest(request.h,body.empty()?WINHTTP_NO_ADDITIONAL_HEADERS:L"Content-Type: application/json\r\n",body.empty()?0:static_cast<DWORD>(-1),
        body.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(body.data()),static_cast<DWORD>(body.size()),static_cast<DWORD>(body.size()),0)||!WinHttpReceiveResponse(request.h,nullptr))
        throw std::runtime_error("Avatar HTTPS request failed ("+std::to_string(GetLastError())+")");
    DWORD status{},size=sizeof(status);
    if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("Avatar HTTP status unavailable");
    if(status!=200)throw std::runtime_error("Avatar service HTTP "+std::to_string(status));
    DWORD content_length{},length_size=sizeof(content_length);
    if(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_CONTENT_LENGTH|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&content_length,&length_size,WINHTTP_NO_HEADER_INDEX)
        &&content_length>limit)throw std::runtime_error("Avatar response exceeds size limit");
    wchar_t content_type[64]{};DWORD content_type_size=sizeof(content_type);
    if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_CONTENT_TYPE,WINHTTP_HEADER_NAME_BY_INDEX,content_type,&content_type_size,WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("Avatar content type unavailable");
    std::wstring actual_type(content_type,content_type_size/sizeof(wchar_t));
    std::transform(actual_type.begin(),actual_type.end(),actual_type.begin(),[](wchar_t c){return c>=L'A'&&c<=L'Z'?static_cast<wchar_t>(c+(L'a'-L'A')):c;});
    const auto type_end=actual_type.find(L'\0');if(type_end!=std::wstring::npos)actual_type.resize(type_end);
    const bool is_json=actual_type.find(L"application/json")!=std::wstring::npos;
    const bool is_png=actual_type.find(L"image/png")!=std::wstring::npos;
    if((body.empty()&&!is_json&&!is_png)||(!body.empty()&&!is_json))throw std::runtime_error("Unexpected avatar content type");
    std::vector<std::uint8_t> bytes;std::array<std::uint8_t,16384> chunk{};
    for(;;){if(stop.stop_requested()||std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Avatar request cancelled or timed out");
        DWORD count{};if(!WinHttpReadData(request.h,chunk.data(),static_cast<DWORD>(chunk.size()),&count))throw std::runtime_error("Avatar download failed");
        if(!count)break;if(count>limit-bytes.size())throw std::runtime_error("Avatar response exceeds size limit");
        bytes.insert(bytes.end(),chunk.begin(),chunk.begin()+count);
    }
    if(bytes.empty())throw std::runtime_error("Avatar service returned an empty response");return bytes;
}
std::shared_ptr<const AvatarImage> fetch(std::uint64_t id,std::stop_token token) {
    const std::wstring endpoint=L"https://thumbnails.roblox.com/v1/users/avatar?userIds="+std::to_wstring(id)+L"&size=720x720&format=Png&isCircular=false";
    const auto deadline=std::chrono::steady_clock::now()+request_deadline;
    for(int attempt=0;attempt<4;++attempt){
        const auto bytes=https_get(endpoint,json_limit,token,deadline);
        const auto reply=parse_avatar_reply(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()),id);
        if(!reply.pending){const std::wstring url(reply.image_url.begin(),reply.image_url.end());
            return std::make_shared<AvatarImage>(decode_avatar_png(https_get(url,image_limit,token,deadline)));}
        for(int tick=0;tick<(1<<attempt)*10;++tick){if(token.stop_requested()||std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Avatar thumbnail timed out");std::this_thread::sleep_for(std::chrono::milliseconds(100));}
    }
    throw std::runtime_error("Avatar thumbnail is still pending; try Refresh avatar later");
}
}
bool valid_avatar_user_id(std::uint64_t id){return id>0&&id<=9007199254740991ULL;}
bool valid_avatar_username(std::string_view name){return name.size()>=3&&name.size()<=20&&std::all_of(name.begin(),name.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';});}
std::uint64_t parse_avatar_identity(std::string_view text,std::string_view username){
    if(text.empty()||text.size()>json_limit||!valid_avatar_username(username))throw std::runtime_error("Invalid avatar identity bounds");
    const auto json=nlohmann::json::parse(text);
    if(!json.is_object()||!json.contains("data")||!json["data"].is_array()||json["data"].size()!=1)throw std::runtime_error("Avatar account not found");
    const auto& row=json["data"][0];
    if(!row.is_object()||!row.contains("id")||!row["id"].is_number_unsigned()||!row.contains("requestedUsername")||!row["requestedUsername"].is_string())throw std::runtime_error("Avatar identity schema mismatch");
    const auto name=row["requestedUsername"].get<std::string>();
    if(name.size()!=username.size()||!std::equal(name.begin(),name.end(),username.begin(),[](unsigned char a,unsigned char b){return std::tolower(a)==std::tolower(b);}))throw std::runtime_error("Avatar account identity mismatch");
    const auto id=row["id"].get<std::uint64_t>();if(!valid_avatar_user_id(id))throw std::runtime_error("Invalid avatar account ID");return id;
}
bool allowed_avatar_image_url(std::string_view url) {
    if(url.size()>2048||!url.starts_with("https://"))return false;
    const auto start=std::size_t{8},slash=url.find('/',start);
    if(slash==std::string_view::npos)return false;
    const auto host=url.substr(start,slash-start);
    if(host!="rbxcdn.com"&&!host.ends_with(".rbxcdn.com"))return false;
    if(host.empty()||!std::all_of(host.begin(),host.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='-';}))return false;
    return std::all_of(url.begin(),url.end(),[](unsigned char c){return c>=33&&c<=126&&c!='\\'&&c!='#';});
}
ThumbnailReply parse_avatar_reply(std::string_view text,std::uint64_t expected) {
    if(text.empty()||text.size()>json_limit||!valid_avatar_user_id(expected))throw std::runtime_error("Invalid avatar response bounds");
    const auto json=nlohmann::json::parse(text);
    if(!json.is_object()||!json.contains("data")||!json["data"].is_array()||json["data"].size()!=1)throw std::runtime_error("Unexpected avatar response schema");
    const auto& row=json["data"][0];
    if(!row.is_object()||!row.contains("targetId")||!row["targetId"].is_number_unsigned()||row["targetId"].get<std::uint64_t>()!=expected
        ||!row.contains("state")||!row["state"].is_string())throw std::runtime_error("Avatar response identity mismatch");
    const auto state=row["state"].get<std::string>();
    if(state=="Pending")return {true,{}};
    if(state!="Completed")throw std::runtime_error("Avatar thumbnail unavailable: "+state);
    if(!row.contains("imageUrl")||!row["imageUrl"].is_string())throw std::runtime_error("Avatar image URL missing");
    auto url=row["imageUrl"].get<std::string>();if(!allowed_avatar_image_url(url))throw std::runtime_error("Avatar CDN URL rejected");
    return {false,std::move(url)};
}
AvatarImage decode_avatar_png(std::span<const std::uint8_t> bytes) {
    constexpr std::array<std::uint8_t,8> signature={137,80,78,71,13,10,26,10};
    if(bytes.size()<signature.size()||bytes.size()>image_limit||!std::equal(signature.begin(),signature.end(),bytes.begin()))throw std::runtime_error("Avatar is not a bounded PNG");
    ComScope scope;ComPtr<IWICImagingFactory> factory;
    require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory failed");
    ComPtr<IWICStream> stream;require(factory->CreateStream(&stream),"WIC stream failed");
    // WIC's interface is non-const, but the decoder reads this buffer only.
    require(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size())),"WIC memory stream failed");
    ComPtr<IWICBitmapDecoder> decoder;require(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"PNG decoder failed");
    ComPtr<IWICBitmapFrameDecode> frame;require(decoder->GetFrame(0,&frame),"PNG frame unavailable");
    AvatarImage image;require(frame->GetSize(&image.width,&image.height),"PNG dimensions unavailable");
    if(image.width==0||image.height==0||image.width>1024||image.height>1024)throw std::runtime_error("Avatar image dimensions exceed limit");
    ComPtr<IWICFormatConverter> converter;require(factory->CreateFormatConverter(&converter),"WIC converter failed");
    require(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"RGBA conversion failed");
    image.rgba.resize(static_cast<std::size_t>(image.width)*image.height*4);
    require(converter->CopyPixels(nullptr,image.width*4,static_cast<UINT>(image.rgba.size()),image.rgba.data()),"Avatar pixel decoding failed");
    return image;
}
AvatarLoader::AvatarLoader():worker_([this](std::stop_token stop){run(stop);}){}
AvatarLoader::~AvatarLoader(){worker_.request_stop();wake_.notify_all();if(worker_.joinable())worker_.join();}
void AvatarLoader::request(std::string_view username,bool refresh) {
    const std::string name=valid_avatar_username(username)?std::string(username):std::string{};
    std::lock_guard lock(mutex_);if(name==requested_&&!refresh)return;
    requested_=name;refresh_=refresh;++generation_;result_={0,generation_,{}, {},!name.empty()};wake_.notify_all();
}
AvatarResult AvatarLoader::result()const{std::lock_guard lock(mutex_);return result_;}
void AvatarLoader::run(std::stop_token stop) {
    std::uint64_t completed_generation=0,cached_id=0;std::string cached_name;std::shared_ptr<const AvatarImage> cached;
    while(!stop.stop_requested()){
        std::uint64_t id{},generation{};std::string name;bool refresh{};
        {std::unique_lock lock(mutex_);wake_.wait(lock,stop,[&]{return generation_!=completed_generation;});
            if(stop.stop_requested())break;name=requested_;generation=generation_;refresh=refresh_;}
        std::shared_ptr<const AvatarImage> image;std::string error;
        if(!name.empty())try{if(name==cached_name&&cached&&!refresh){id=cached_id;image=cached;}else{
            const auto body=nlohmann::json{{"usernames",nlohmann::json::array({name})},{"excludeBannedUsers",true}}.dump();
            const auto identity_deadline=std::chrono::steady_clock::now()+request_deadline;
            const auto bytes=https_get(L"https://users.roblox.com/v1/usernames/users",json_limit,stop,identity_deadline,body);
            id=parse_avatar_identity(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()),name);
            image=fetch(id,stop);}}
            catch(const std::exception& e){error=e.what();}
        {std::lock_guard lock(mutex_);completed_generation=generation;
            if(generation_!=generation)continue;
            if(image){cached_id=id;cached_name=name;cached=image;}
            result_={id,generation,image,std::move(error),false};}
    }
}
}
