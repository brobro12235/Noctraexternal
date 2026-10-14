#include "update.hpp"
#include "update_config.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Algorithm{BCRYPT_ALG_HANDLE h{};Algorithm(){check(BCryptOpenAlgorithmProvider(&h,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0,"Signing provider unavailable");}~Algorithm(){BCryptCloseAlgorithmProvider(h,0);}};
struct Key{BCRYPT_KEY_HANDLE h{};~Key(){if(h)BCryptDestroyKey(h);}};
struct Blob{DATA_BLOB data{};~Blob(){if(data.pbData){SecureZeroMemory(data.pbData,data.cbData);LocalFree(data.pbData);}}};
std::filesystem::path key_path(){wchar_t path[32768]{};const auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);check(n>0&&n<32768,"LOCALAPPDATA unavailable");return std::filesystem::path(path)/L"NoctraPublisher"/L"signing-key.dpapi";}
std::vector<unsigned char> export_key(BCRYPT_KEY_HANDLE key,LPCWSTR type){ULONG n{};check(BCryptExportKey(key,nullptr,type,nullptr,0,&n,0)>=0,"Key export failed");std::vector<unsigned char> blob(n);check(BCryptExportKey(key,nullptr,type,blob.data(),n,&n,0)>=0,"Key export failed");return blob;}
void write(const std::filesystem::path& path,std::span<const unsigned char> bytes){check(!std::filesystem::exists(path),"Output already exists; refusing to overwrite it");std::ofstream out(path,std::ios::binary);check(static_cast<bool>(out),"Cannot write release file");out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));out.close();check(static_cast<bool>(out),"Cannot finish release file");}
void write_text(const std::filesystem::path& path,const std::string& value){write(path,std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(value.data()),value.size()));}
}
int main(int argc,char** argv){try{
    Algorithm algorithm;Key key;const auto path=key_path();
    if(argc==3&&std::string(argv[1])=="generate-key"){
        check(!std::filesystem::exists(path),"A publisher signing key already exists; do not rotate it accidentally");
        check(!std::filesystem::exists(argv[2]),"Public-key destination already exists");
        check(BCryptGenerateKeyPair(algorithm.h,&key.h,256,0)>=0&&BCryptFinalizeKeyPair(key.h,0)>=0,"Signing key generation failed");
        auto private_blob=export_key(key.h,BCRYPT_ECCPRIVATE_BLOB);DATA_BLOB input{static_cast<DWORD>(private_blob.size()),private_blob.data()};Blob encrypted;
        const bool protected_ok=CryptProtectData(&input,L"Noctra release signing key",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted.data)!=FALSE;
        SecureZeroMemory(private_blob.data(),private_blob.size());check(protected_ok,"Signing key protection failed");
        std::filesystem::create_directories(path.parent_path());write(path,{encrypted.data.pbData,encrypted.data.cbData});
        write_text(argv[2],noctra::bytes_hex(export_key(key.h,BCRYPT_ECCPUBLIC_BLOB))+"\n");
        std::cout<<"Public verification key written; private signing key protected under this Windows user in LOCALAPPDATA/NoctraPublisher (not in the package).\n";return 0;
    }
    if(argc==8&&std::string(argv[1])=="sign"){
        const auto encrypted_bytes=noctra::read_update_file(path,4096);DATA_BLOB encrypted{static_cast<DWORD>(encrypted_bytes.size()),const_cast<BYTE*>(encrypted_bytes.data())};Blob decrypted;
        check(CryptUnprotectData(&encrypted,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&decrypted.data)!=FALSE,"Cannot unlock publisher key under this Windows user");
        check(BCryptImportKeyPair(algorithm.h,nullptr,BCRYPT_ECCPRIVATE_BLOB,&key.h,decrypted.data.pbData,decrypted.data.cbData,0)>=0,"Invalid publisher signing key");
        check(noctra::bytes_hex(export_key(key.h,BCRYPT_ECCPUBLIC_BLOB))==noctra::update_public_key,"Publisher key does not match the public key pinned in this build");
        const auto sequence=std::stoull(argv[3]),days=std::stoull(argv[6]);check(days>0&&days<=365,"Expiry days must be 1-365");
        const auto bytes=noctra::read_update_file(argv[2],32*1024*1024);
        const auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        const std::string url="https://github.com/"+std::string(noctra::update_repository)+"/releases/download/"+argv[5]+"/Noctra.exe";
        const auto payload=nlohmann::json{{"schema",1},{"product","Noctra"},{"arch","windows-x64"},{"sequence",sequence},{"version",argv[4]},{"url",url},{"size",bytes.size()},{"sha256",noctra::bytes_hex(noctra::update_digest(bytes))},{"expires",now+days*86400}}.dump();
        const auto hash=noctra::update_digest({reinterpret_cast<const unsigned char*>(payload.data()),payload.size()});std::vector<unsigned char> signature(64);ULONG written{};
        check(BCryptSignHash(key.h,nullptr,const_cast<PUCHAR>(hash.data()),32,signature.data(),64,&written,0)>=0&&written==64,"Release signing failed");
        const auto envelope=nlohmann::json{{"payload",payload},{"signature",noctra::bytes_hex(signature)}}.dump();
        const auto manifest=noctra::verify_update_manifest(envelope,noctra::update_public_key,0,now);noctra::verify_update_payload(argv[2],manifest);
        write_text(argv[7],envelope+"\n");std::cout<<"Signed release manifest written. Nothing uploaded.\n";return 0;
    }
    std::cerr<<"Usage: noctra_release generate-key config/update-public-key.txt\n"
             <<"       noctra_release sign EXE SEQUENCE VERSION TAG EXPIRY_DAYS OUTPUT_JSON\n";return 2;
}catch(const std::exception& e){std::cerr<<"Release tool error: "<<e.what()<<'\n';return 1;}}
