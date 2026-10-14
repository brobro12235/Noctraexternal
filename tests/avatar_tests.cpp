#include "avatar.hpp"
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace noctra;
using Microsoft::WRL::ComPtr;
namespace {
int assertions{};
void expect(bool condition,const char* message){++assertions;if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}expect(rejected,message);}
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Test PNG encoder failure");}
std::vector<std::uint8_t> png(unsigned width,unsigned height){
    ComPtr<IWICImagingFactory> factory;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IStream> stream;check(CreateStreamOnHGlobal(nullptr,TRUE,&stream));
    ComPtr<IWICBitmapEncoder> encoder;check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> properties;check(encoder->CreateNewFrame(&frame,&properties));check(frame->Initialize(properties.Get()));
    check(frame->SetSize(width,height));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;check(frame->SetPixelFormat(&format));
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);for(std::size_t i=0;i<pixels.size();i+=4){pixels[i]=33;pixels[i+1]=22;pixels[i+2]=11;pixels[i+3]=255;}
    check(frame->WritePixels(height,width*4,static_cast<UINT>(pixels.size()),pixels.data()));check(frame->Commit());check(encoder->Commit());
    STATSTG stat{};check(stream->Stat(&stat,STATFLAG_NONAME));std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};check(stream->Seek(zero,STREAM_SEEK_SET,nullptr));ULONG read{};check(stream->Read(bytes.data(),static_cast<ULONG>(bytes.size()),&read));expect(read==bytes.size(),"PNG fixture complete");return bytes;
}
}
int main(int argc,char** argv){try{
    check(CoInitializeEx(nullptr,COINIT_MULTITHREADED));struct ComCleanup{~ComCleanup(){CoUninitialize();}}cleanup;
    expect(valid_avatar_user_id(1)&&valid_avatar_user_id(9007199254740991ULL),"valid IDs");
    expect(!valid_avatar_user_id(0)&&!valid_avatar_user_id(9007199254740992ULL),"invalid IDs");
    expect(valid_avatar_username("potassiumnn")&&valid_avatar_username("Test_User1"),"valid username");
    expect(!valid_avatar_username("x")&&!valid_avatar_username("wrong name")&&!valid_avatar_username("https://evil"),"invalid username");
    expect(parse_avatar_identity(R"({"data":[{"id":1,"requestedUsername":"Builderman"}]})","builderman")==1,"matching official identity");
    rejects([]{parse_avatar_identity(R"({"data":[{"id":1,"requestedUsername":"wrong"}]})","builderman");},"wrong identity rejected");
    rejects([]{parse_avatar_identity(R"({"data":[]})","builderman");},"missing identity rejected");
    rejects([]{parse_avatar_identity(R"({"data":[{"id":-1,"requestedUsername":"builderman"}]})","builderman");},"negative identity rejected");
    expect(allowed_avatar_image_url("https://tr.rbxcdn.com/30DAY-Avatar/Png"),"official CDN allowed");
    for(const auto* url:{"http://tr.rbxcdn.com/a","https://rbxcdn.com.evil.test/a","https://evilrbxcdn.com/a","https://user@tr.rbxcdn.com/a","https://tr.rbxcdn.com:443/a","https://tr.rbxcdn.com/a\\b","https://tr.rbxcdn.com/a#bad","https://tr.rbxcdn.com/a b"})expect(!allowed_avatar_image_url(url),"unsafe URL rejected");
    const auto reply=parse_avatar_reply(R"({"data":[{"targetId":1,"state":"Completed","imageUrl":"https://tr.rbxcdn.com/test.png"}]})",1);
    expect(!reply.pending&&reply.image_url=="https://tr.rbxcdn.com/test.png","completed response");
    expect(parse_avatar_reply(R"({"data":[{"targetId":1,"state":"Pending","imageUrl":null}]})",1).pending,"pending response");
    for(const auto* json:{"not-json",R"({"data":[]})",R"({"data":[{"targetId":2,"state":"Completed"}]})",R"({"data":[{"targetId":-1,"state":"Completed"}]})",R"({"data":[{"targetId":1,"state":"Blocked"}]})",R"({"data":[{"targetId":1,"state":"Completed","imageUrl":"https://evil.test/a"}]})"})rejects([&]{parse_avatar_reply(json,1);},"invalid reply rejected");
    rejects([]{parse_avatar_reply(std::string(65537,' '),1);},"oversized JSON rejected");
    const auto image=decode_avatar_png(png(2,3));expect(image.width==2&&image.height==3&&image.rgba.size()==24,"PNG dimensions and size");
    expect(image.rgba[0]==11&&image.rgba[1]==22&&image.rgba[2]==33&&image.rgba[3]==255,"PNG RGBA channels");
    rejects([]{decode_avatar_png({});},"empty image rejected");
    rejects([]{decode_avatar_png(std::vector<std::uint8_t>(4*1024*1024+1));},"oversized image rejected");
    auto corrupt=png(2,2);corrupt.resize(20);rejects([&]{decode_avatar_png(corrupt);},"truncated PNG rejected");
    const auto huge=png(1025,1);rejects([&]{decode_avatar_png(huge);},"decoded dimensions bounded");
    {AvatarLoader loader;expect(!loader.result().loading,"loader starts idle");loader.request("invalid space");expect(!loader.result().loading,"invalid identity has no network request");}
    std::cout<<"PASS: "<<assertions<<" avatar assertions\n";
    if(argc==3&&std::string_view(argv[1])=="--live"){
        AvatarLoader loader;loader.request(argv[2]);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);AvatarResult result;
        do{result=loader.result();if(!result.loading)break;std::this_thread::sleep_for(std::chrono::milliseconds(100));}while(std::chrono::steady_clock::now()<deadline);
        expect(result.image!=nullptr&&result.error.empty(),result.error.empty()?"live avatar must complete":result.error.c_str());
        expect(result.image->width==720&&result.image->height==720,"live 720px PNG decoded");
        std::cout<<"LIVE PASS: account_id="<<result.user_id<<" image="<<result.image->width<<'x'<<result.image->height<<'\n';
    }
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL after "<<assertions<<" avatar assertions: "<<e.what()<<'\n';return 1;}}
