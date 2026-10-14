#include "update.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace noctra;
namespace {
int assertions{};
void expect(bool ok,const char* why){++assertions;if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char* why){bool caught=false;try{f();}catch(const std::exception&){caught=true;}expect(caught,why);}
struct Signer{
    BCRYPT_ALG_HANDLE alg{};BCRYPT_KEY_HANDLE key{};std::string public_key;
    Signer(){expect(BCryptOpenAlgorithmProvider(&alg,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0,"test signing provider");
        expect(BCryptGenerateKeyPair(alg,&key,256,0)>=0&&BCryptFinalizeKeyPair(key,0)>=0,"test signing key");ULONG size{};BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,nullptr,0,&size,0);std::vector<unsigned char> bytes(size);
        expect(BCryptExportKey(key,nullptr,BCRYPT_ECCPUBLIC_BLOB,bytes.data(),size,&size,0)>=0,"test public key");public_key=bytes_hex(bytes);}
    ~Signer(){BCryptDestroyKey(key);BCryptCloseAlgorithmProvider(alg,0);}
    std::string sign(nlohmann::json payload){const auto raw=payload.dump();const auto hash=update_digest({reinterpret_cast<const unsigned char*>(raw.data()),raw.size()});std::vector<unsigned char> sig(64);ULONG n{};
        expect(BCryptSignHash(key,nullptr,const_cast<PUCHAR>(hash.data()),32,sig.data(),64,&n,0)>=0&&n==64,"test signature");return nlohmann::json{{"payload",raw},{"signature",bytes_hex(sig)}}.dump();}
};
void write(const std::filesystem::path& path,const std::string& data){std::ofstream out(path,std::ios::binary);out<<data;out.close();expect(static_cast<bool>(out),"test fixture write");}
}
int main(int argc,char** argv){try{
    if(argc==2&&std::string(argv[1])=="--live-check"){
        UpdateService service(true);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(100);UpdateStatus status;
        do{status=service.status();if(!status.busy)break;std::this_thread::sleep_for(std::chrono::milliseconds(100));}while(std::chrono::steady_clock::now()<deadline);
        expect(!status.busy,"real launch-time update discovery completes");std::cout<<"LIVE update discovery: "<<status.message<<'\n';return 0;
    }
    Signer signer;
    nlohmann::json data={{"schema",1},{"product","Noctra"},{"arch","windows-x64"},{"sequence",4u},{"version","1.3.0"},{"url","https://github.com/brobro12235/Noctraexternal/releases/download/v1.3.0/Noctra.exe"},{"size",1024u},{"sha256",std::string(64,'a')},{"expires",2000001000u}};
    auto envelope=signer.sign(data);const auto m=verify_update_manifest(envelope,signer.public_key,3,2000000000);
    expect(m.sequence==4&&m.version=="1.3.0","signed valid release parsed");
    rejects([&]{verify_update_manifest(envelope,signer.public_key,4,2000000000);},"same version rejected");
    rejects([&]{verify_update_manifest(envelope,signer.public_key,5,2000000000);},"rollback rejected");
    rejects([&]{verify_update_manifest(envelope,signer.public_key,3,2000001000);},"expired rejected");
    auto modified=nlohmann::json::parse(envelope);modified["payload"]=data.dump()+" ";rejects([&]{verify_update_manifest(modified.dump(),signer.public_key,3,2000000000);},"tampered payload rejected");
    modified=nlohmann::json::parse(envelope);modified["signature"]=std::string(128,'0');rejects([&]{verify_update_manifest(modified.dump(),signer.public_key,3,2000000000);},"forged signature rejected");
    for(const auto* url:{"http://github.com/brobro12235/Noctraexternal/releases/download/v1/Noctra.exe","https://evil.test/Noctra.exe","https://github.com/other/repo/releases/download/v1/Noctra.exe","https://github.com/brobro12235/Noctraexternal/releases/download/../Noctra.exe?x","https://github.com/brobro12235/Noctraexternal/releases/download/v1/bad.exe"}){auto v=data;v["url"]=url;const auto signed_bad=signer.sign(v);rejects([&]{verify_update_manifest(signed_bad,signer.public_key,3,2000000000);},"bad update URL rejected");}
    for(const auto* field:{"product","arch","version"}){auto v=data;v[field]="wrong";const auto signed_bad=signer.sign(v);rejects([&]{verify_update_manifest(signed_bad,signer.public_key,3,2000000000);},"bad signed metadata rejected");}
    rejects([&]{verify_update_manifest(std::string(32769,' '),signer.public_key,3,2000000000);},"manifest bound enforced");
    const auto root=std::filesystem::temp_directory_path()/("noctra-update-tests-"+std::to_string(GetCurrentProcessId()));expect(std::filesystem::create_directory(root),"isolated update fixtures");
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);}}cleanup{root};
    const auto target=root/L"Noctra.exe",pending=root/L"Noctra.pending.exe",backup=root/L"Noctra.previous.exe",rejected=root/L"Noctra.rejected.exe";
    write(target,"original");write(pending,"replacement");replace_update_file(target,pending,backup);
    expect(update_file_hash(target)==bytes_hex(update_digest(std::span<const unsigned char>(reinterpret_cast<const unsigned char*>("replacement"),11))),"real replacement installed");
    expect(read_update_file(backup,100).size()==8,"old file preserved");rollback_update_file(target,backup,rejected);expect(read_update_file(target,100).size()==8,"rollback restored old file");expect(read_update_file(rejected,100).size()==11,"rejected file retained");
    write(pending,"retry");write(backup,"existing-backup");rejects([&]{replace_update_file(target,pending,backup);},"existing backup protected");expect(read_update_file(target,100).size()==8,"failed replace preserves target");
    rejects([&]{read_update_file(target,2);},"file size bound");rejects([&]{verify_update_payload(target,m);},"wrong payload rejected");
    rejects([&]{replace_update_file(target,root/L"missing",root/L"new-backup");},"missing stage rejected");
    std::cout<<"PASS: "<<assertions<<" updater assertions\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL after "<<assertions<<" updater assertions: "<<e.what()<<'\n';return 1;}}
