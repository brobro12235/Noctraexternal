#include "client.hpp"
#include "offsets.hpp"
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace noctra {
namespace {
class Handle {
public:
    HANDLE value{};
    explicit Handle(HANDLE h=nullptr):value(h){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
struct ReadError:std::runtime_error {using std::runtime_error::runtime_error;};
class Memory {
public:
    HANDLE handle;
    explicit Memory(HANDLE h):handle(h){}
    void read(std::uintptr_t a,void* data,std::size_t n)const {
        if(!valid_address(a,n))
            throw ReadError("Invalid read bounds at "+std::to_string(a)+" size "+std::to_string(n));
        SIZE_T count{};
        if(!ReadProcessMemory(handle,reinterpret_cast<LPCVOID>(a),data,n,&count)||count!=n)
            throw ReadError("ReadProcessMemory failed ("+std::to_string(GetLastError())+")");
    }
    template<class T>T at(std::uintptr_t a)const {T v{};read(a,&v,sizeof(v));return v;}
    std::uintptr_t ptr(std::uintptr_t a)const {
        try{return at<std::uintptr_t>(a);}
        catch(const ReadError& error){throw ReadError("Pointer read at "+std::to_string(a)+": "+error.what());}
    }
    std::string string(std::uintptr_t a)const {
        const auto size=at<std::uint64_t>(a+offsets::string_length);
        const auto capacity=at<std::uint64_t>(a+offsets::string_length+sizeof(std::uint64_t));
        if(size>128||capacity<size||capacity>1048576)throw ReadError("Invalid string layout");
        std::string text(static_cast<std::size_t>(size),'\0');
        if(size)read(capacity>=offsets::string_length?ptr(a):a,text.data(),text.size());
        for(char& c:text)if(static_cast<unsigned char>(c)<32)c=' ';
        return text;
    }
    std::string type(std::uintptr_t a)const {
        try {
            const auto descriptor_ptr=ptr(a+offsets::descriptor);
            return string(ptr(descriptor_ptr+offsets::class_name));
        } catch(const ReadError& error) {
            throw ReadError("Type read at "+std::to_string(a)+": "+error.what());
        }
    }
    std::string name(std::uintptr_t a)const {
        const auto name_object=offsets::name_direct
            ? ptr(a+offsets::name)
            : ptr(ptr(a+offsets::name_container)+offsets::name);
        return string(name_object);
    }
    std::vector<std::uintptr_t> children(std::uintptr_t a)const {
        try {
            const auto container=ptr(a+offsets::children);
            if(!container)return {}; // Instances with no child collection are leaves.
            const auto start=ptr(container);
            const auto end=ptr(container+offsets::children_end);
            const auto count=child_count(start,end);
            if(!count)throw ReadError("Invalid children layout");
            std::vector<std::array<std::uintptr_t,2>> raw(*count);
            if(*count)read(start,raw.data(),raw.size()*sizeof(raw[0]));
            std::vector<std::uintptr_t> out;out.reserve(*count);
            for(const auto& entry:raw){if(!valid_address(entry[0],8))throw ReadError("Invalid child pointer");out.push_back(entry[0]);}
            return out;
        } catch(const ReadError& error) {
            throw ReadError("Children read at "+std::to_string(a)+": "+error.what());
        }
    }
};
std::uintptr_t checked_pointer(const Memory& memory,std::uintptr_t address,const char* label) {
    try{return memory.ptr(address);}
    catch(const ReadError& error){throw ReadError(std::string(label)+": "+error.what());}
}
DWORD find_pid() {
    Handle list(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    if(list.value==INVALID_HANDLE_VALUE)throw ReadError("Process enumeration failed");
    PROCESSENTRY32W p{};p.dwSize=sizeof(p);
    if(Process32FirstW(list.value,&p))do{if(_wcsicmp(p.szExeFile,L"RobloxPlayerBeta.exe")==0)return p.th32ProcessID;}while(Process32NextW(list.value,&p));
    return 0;
}
struct WindowSearch {DWORD pid;HWND found{};};
BOOL CALLBACK find_window(HWND w,LPARAM arg) {
    auto& s=*reinterpret_cast<WindowSearch*>(arg);DWORD pid{};GetWindowThreadProcessId(w,&pid);
    if(pid==s.pid&&IsWindowVisible(w)&&!GetWindow(w,GW_OWNER)){s.found=w;return FALSE;}return TRUE;
}
HWND client_window(DWORD pid) {WindowSearch s{pid};EnumWindows(find_window,reinterpret_cast<LPARAM>(&s));return s.found;}
std::uintptr_t service(const Memory& m,std::uintptr_t dm,const std::string& wanted) {
    for(auto a:m.children(dm))if(m.type(a)==wanted){if(m.ptr(a+offsets::parent)!=dm)throw ReadError("Service parent mismatch");return a;}
    throw ReadError("Service unavailable: "+wanted);
}
bool body_name(const std::string& n) {
    static const std::vector<std::string> names={"Head","Torso","HumanoidRootPart","Left Arm","Right Arm","Left Leg","Right Leg",
      "UpperTorso","LowerTorso","LeftUpperArm","LeftLowerArm","LeftHand","RightUpperArm","RightLowerArm","RightHand",
      "LeftUpperLeg","LeftLowerLeg","LeftFoot","RightUpperLeg","RightLowerLeg","RightFoot"};
    return std::find(names.begin(),names.end(),n)!=names.end();
}
Part read_geometry(const Memory& m,std::uintptr_t part,std::uintptr_t parent){
    if(m.ptr(part+offsets::parent)!=parent)throw ReadError("Geometry parent mismatch");
    const auto primitive=m.ptr(part+offsets::primitive);Part p;p.position=m.at<Vec3>(primitive+offsets::position);
    p.size=m.at<Vec3>(primitive+offsets::size);p.rotation=m.at<std::array<float,9>>(primitive+offsets::rotation);return p;
}
void sample_phantom_geometry(const Memory& m,std::uintptr_t workspace,Snapshot& s,std::size_t& skipped){
    // The adapter is deliberately scoped to the live-inspected PF place, not a
    // universal Workspace search. No Lua/module scanning or guessed identities.
    if(s.place!=292439477ULL)return;
    s.geometry_only=true;s.teams_verified=false;
    std::uintptr_t container{};
    for(auto child:m.children(workspace))if(m.type(child)=="Folder"&&m.name(child)=="Players"){
        if(container)throw ReadError("Ambiguous PF character container");container=child;}
    if(!container||m.ptr(container+offsets::parent)!=workspace)return;
    const auto groups=m.children(container);if(groups.size()!=2)return;
    std::size_t model_count{};
    for(auto group:groups){
        if(m.type(group)!="Folder"||m.ptr(group+offsets::parent)!=container)throw ReadError("PF group layout changed");
        for(auto model:m.children(group)){
            if(++model_count>64)throw ReadError("PF model count exceeds bound");
            ++s.geometry_candidates;++s.geometry_rejected;
            try{
                if(m.type(model)!="Model"||m.ptr(model+offsets::parent)!=group)throw ReadError("PF model identity mismatch");
                const auto children=m.children(model);if(children.size()!=7)continue;
                std::vector<Part> proxies,meshes;std::uintptr_t appearance{};
                for(auto part:children){const auto type=m.type(part);
                    if(type=="Part")proxies.push_back(read_geometry(m,part,model));
                    else if(type=="Folder"&&!appearance&&m.ptr(part+offsets::parent)==model)appearance=part;
                    else throw ReadError("PF proxy layout mismatch");}
                if(!appearance||proxies.size()!=6)continue;
                const auto render_parts=m.children(appearance);if(render_parts.size()>96)throw ReadError("PF mesh count exceeds bound");
                for(auto part:render_parts){const auto type=m.type(part);if(type!="MeshPart"&&type!="Part"&&type!="UnionOperation")continue;
                    auto p=read_geometry(m,part,appearance);p.name="Geometry";meshes.push_back(std::move(p));}
                const auto range=phantom_geometry_range(proxies,meshes,s.camera);if(!range)continue;
                // Revalidate membership after reads to avoid using a despawned model.
                if(m.ptr(model+offsets::parent)!=group||m.ptr(appearance+offsets::parent)!=model)throw ReadError("PF model changed during sample");
                Entity e;e.id=model;e.character=model;e.geometry_only=true;e.range=*range;e.parts=std::move(meshes);s.entities.push_back(std::move(e));
                --s.geometry_rejected;
            }catch(const ReadError&){++skipped;}
        }
    }
}
Snapshot sample(const Memory& m,std::uintptr_t base,DWORD pid,const std::string& build,
                const std::string& hash,const std::string& offsets_version) {
    Snapshot s;s.pid=pid;s.build=build;s.hash=hash;s.offsets_version=offsets_version;s.compatible=true;
    s.window=reinterpret_cast<std::uintptr_t>(client_window(pid));
    const auto fake=checked_pointer(m,base+offsets::fake_global,"FakeDataModel global");
    if(!fake)throw ReadError("Waiting for Roblox scene initialization");
    const auto dm=checked_pointer(m,fake+offsets::real_model,"RealDataModel pointer");
    if(!dm)throw ReadError("Waiting for Roblox DataModel");
    if(m.type(dm)!="DataModel")throw ReadError("DataModel identity failed");
    s.place=m.at<std::uint64_t>(dm+offsets::place);
    const auto workspace=service(m,dm,"Workspace"),players=service(m,dm,"Players");
    const auto local=checked_pointer(m,players+offsets::local_player,"LocalPlayer pointer");
    const auto camera=checked_pointer(m,workspace+offsets::camera,"CurrentCamera pointer");
    if(m.type(local)!="Player"||m.type(camera)!="Camera")throw ReadError("Local player/camera identity failed");
    s.local_team=checked_pointer(m,local+offsets::team,"LocalPlayer team pointer");
    if(s.local_team&&m.type(s.local_team)!="Team")throw ReadError("Local team layout not validated");
    s.teams_verified=s.local_team!=0;
    s.camera=m.at<Vec3>(camera+offsets::camera_position);
    const auto fov=m.at<float>(camera+offsets::fov);
    const auto visual=checked_pointer(m,base+offsets::visual_global,"VisualEngine global");
    if(!visual)throw ReadError("Waiting for Roblox renderer initialization");
    s.matrix=m.at<Matrix>(visual+offsets::matrix);s.dimensions=m.at<Vec2>(visual+offsets::dimensions);
    if(!finite(s.camera)||!std::isfinite(fov)||fov<=0||fov>=3.14f||s.dimensions.x<100||s.dimensions.x>16384
        ||s.dimensions.y<100||s.dimensions.y>16384||!std::isfinite(s.dimensions.x)||!std::isfinite(s.dimensions.y)
        ||!std::all_of(s.matrix.begin(),s.matrix.end(),[](float v){return std::isfinite(v);})
        ||!std::any_of(s.matrix.begin(),s.matrix.end(),[](float v){return std::abs(v)>0.001f;}))throw ReadError("Camera/projection layout failed");
    std::size_t skipped=0,player_count=0;bool local_character=false;
    for(auto player:m.children(players)) {
        if(m.type(player)!="Player")continue;
        if(++player_count>256)throw ReadError("Player count exceeds bound");
        try {
            Entity e;e.id=player;e.local=player==local;e.character=m.ptr(player+offsets::character);
            if(!e.character)continue;
            if(m.type(e.character)!="Model")throw ReadError("Character identity failed");
            e.name=m.name(player);e.team=m.ptr(player+offsets::team);
            e.neutral=e.team==0;
            if(e.team&&m.type(e.team)!="Team")throw ReadError("Team identity failed");
            std::uintptr_t humanoid{};
            for(auto part:m.children(e.character)) {
                const auto type=m.type(part);
                if(type=="Humanoid"){humanoid=part;continue;}
                if(type!="Part"&&type!="MeshPart"&&type!="UnionOperation")continue;
                auto name=m.name(part);if(!body_name(name))continue;
                if(m.ptr(part+offsets::parent)!=e.character)throw ReadError("Part parent mismatch");
                const auto primitive=m.ptr(part+offsets::primitive);
                Part p;p.name=std::move(name);p.position=m.at<Vec3>(primitive+offsets::position);
                p.size=m.at<Vec3>(primitive+offsets::size);p.rotation=m.at<std::array<float,9>>(primitive+offsets::rotation);
                if(!finite(p.position)||distance(p.position,{})>1e7f||!finite(p.size)||p.size.x<=0||p.size.y<=0||p.size.z<=0
                    ||p.size.x>1000||p.size.y>1000||p.size.z>1000)throw ReadError("Part position/size failed");
                for(int row=0;row<3;++row){const auto i=static_cast<std::size_t>(row)*3;
                    const auto length=std::hypot(p.rotation[i],p.rotation[i+1],p.rotation[i+2]);
                    if(!std::isfinite(length)||std::abs(length-1)>0.08f)throw ReadError("Part rotation failed");}
                e.parts.push_back(std::move(p));
            }
            if(!humanoid||e.parts.empty())continue;
            e.health=m.at<float>(humanoid+offsets::health);e.max_health=m.at<float>(humanoid+offsets::max_health);
            if(!std::isfinite(e.health)||!std::isfinite(e.max_health)||e.max_health<=0||e.max_health>1e8f||e.health<0||e.health>e.max_health)
                throw ReadError("Humanoid health failed");
            const auto head=std::find_if(e.parts.begin(),e.parts.end(),[](const Part& p){return p.name=="Head";});
            if(head==e.parts.end())continue;
            e.range=distance(s.camera,head->position);
            if(e.local)local_character=true;
            s.entities.push_back(std::move(e));
        }catch(const ReadError&){++skipped;}
    }
    if(!local_character&&s.entities.empty())sample_phantom_geometry(m,workspace,s,skipped);
    if(checked_pointer(m,base+offsets::fake_global,"FakeDataModel recheck")!=fake||
       checked_pointer(m,fake+offsets::real_model,"RealDataModel recheck")!=dm)
        throw ReadError("Scene changed during sample");
    s.ready=(local_character||(s.geometry_only&&!s.entities.empty()))&&s.window!=0;
    if(s.geometry_only){
        s.status=s.entities.empty()?(s.geometry_candidates?"PF models rejected by geometry validation":"PF: no replicated character models available")
                                  :"Experimental PF geometry-only: no identity/health/teams; aim disabled";
        s.status+=" | candidates "+std::to_string(s.geometry_candidates)+", rejected "+std::to_string(s.geometry_rejected);
    }else s.status=s.ready?"Validated standard-rig data":"Waiting for a validated local character";
    if(!s.teams_verified&&!s.geometry_only)s.status+=" | custom/no teams: team-safe aim blocked";
    if(skipped)s.status+=" | skipped "+std::to_string(skipped)+" invalid entities";
    s.captured=Clock::now();return s;
}
}
std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to open file for SHA-256");

    struct AlgorithmHandle {
        BCRYPT_ALG_HANDLE value{};
        ~AlgorithmHandle() { if (value) (void)BCryptCloseAlgorithmProvider(value, 0); }
    } algorithm;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        throw std::runtime_error("Unable to initialize SHA-256");

    ULONG object_length{}, returned{};
    if (!BCRYPT_SUCCESS(BCryptGetProperty(
            algorithm.value, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_length),
            static_cast<ULONG>(sizeof(object_length)), &returned, 0)) || object_length == 0)
        throw std::runtime_error("Unable to query SHA-256 object size");

    ULONG hash_length{};
    if (!BCRYPT_SUCCESS(BCryptGetProperty(
            algorithm.value, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_length),
            static_cast<ULONG>(sizeof(hash_length)), &returned, 0)) || hash_length != 32)
        throw std::runtime_error("Unexpected SHA-256 digest size");

    std::vector<UCHAR> object(object_length);
    struct HashHandle {
        BCRYPT_HASH_HANDLE value{};
        ~HashHandle() { if (value) (void)BCryptDestroyHash(value); }
    } hash;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(
            algorithm.value, &hash.value, object.data(), object_length, nullptr, 0, 0)))
        throw std::runtime_error("Unable to create SHA-256 hash");

    std::array<UCHAR, 64 * 1024> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && !BCRYPT_SUCCESS(BCryptHashData(
                hash.value, buffer.data(), static_cast<ULONG>(count), 0)))
            throw std::runtime_error("Unable to update SHA-256 hash");
    }
    if (!input.eof()) throw std::runtime_error("Unable to read file for SHA-256");

    std::array<UCHAR, 32> digest{};
    if (!BCRYPT_SUCCESS(BCryptFinishHash(hash.value, digest.data(),
                                         static_cast<ULONG>(digest.size()), 0)))
        throw std::runtime_error("Unable to finalize SHA-256 hash");

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const UCHAR byte : digest) output << std::setw(2) << static_cast<unsigned int>(byte);
    return output.str();
}
Client::Client():thread_([this](std::stop_token t){run(t);}){}
Client::~Client(){thread_.request_stop();if(thread_.joinable())thread_.join();}
Snapshot Client::snapshot()const{std::lock_guard lock(mutex_);return snapshot_;}
void Client::run(std::stop_token token) {
    const auto publish=[&](Snapshot s){std::lock_guard lock(mutex_);snapshot_=std::move(s);};
    while(!token.stop_requested()) {
        Snapshot info;
        try {
            const DWORD pid=find_pid();
            if(!pid){publish(info);std::this_thread::sleep_for(std::chrono::milliseconds(500));continue;}
            info.pid=pid;Handle h(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid));
            if(!h.value)throw ReadError("Ordinary process access denied ("+std::to_string(GetLastError())+")");
            wchar_t path[32768]{};DWORD size=32768;
            if(!QueryFullProcessImageNameW(h.value,0,path,&size))throw ReadError("Client path unavailable");
            const std::filesystem::path exe(path);info.build=exe.parent_path().filename().string();info.hash=sha256_file(exe);
            HMODULE module{};DWORD needed{};
            if(!EnumProcessModulesEx(h.value,&module,sizeof(module),&needed,LIST_MODULES_ALL))throw ReadError("Module enumeration failed");
            wchar_t self_path[32768]{};
            const auto self_length=GetModuleFileNameW(GetModuleHandleW(nullptr),self_path,
                                                       static_cast<DWORD>(sizeof(self_path)/sizeof(self_path[0])));
            if(!self_length||self_length>=sizeof(self_path)/sizeof(self_path[0]))
                throw ReadError("NOCTRA path unavailable");
            const std::filesystem::path self_dir=std::filesystem::path(self_path).parent_path();
            offsets::reset_defaults();
            std::string offsets_version,offsets_status;
            if(!offsets::ensure_offsets_for_build(self_dir,info.build,offsets_version,offsets_status))
                throw ReadError(offsets_status);
            info.offsets_version=offsets_version;info.compatible=true;
            Memory memory(h.value);int consistent=0;std::uint64_t previous_place{};std::uintptr_t previous_character{};
            while(!token.stop_requested()) {
                DWORD exit{};if(!GetExitCodeProcess(h.value,&exit)||exit!=STILL_ACTIVE)break;
                const auto start=Clock::now();
                try {auto s=sample(memory,reinterpret_cast<std::uintptr_t>(module),pid,info.build,info.hash,
                                   offsets_version);
                    std::uintptr_t character{};for(const auto& entity:s.entities)if(entity.local)character=entity.character;
                    if(s.place!=previous_place||character!=previous_character)consistent=0;
                    previous_place=s.place;previous_character=character;
                    consistent=s.ready?std::min(consistent+1,3):0;
                    if(consistent<3){s.ready=false;if(consistent>0)s.status="Validating repeated scene samples";}
                    s.read_ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();publish(std::move(s));
                }catch(const std::exception& e){consistent=0;previous_place=0;previous_character=0;Snapshot failure=info;failure.status=e.what();publish(std::move(failure));}
                std::this_thread::sleep_until(start+std::chrono::milliseconds(16));
            }
        }catch(const std::exception& e){info.ready=false;info.status=e.what();publish(std::move(info));}
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
void write_diagnostic(const Snapshot& s,const std::filesystem::path& path) {
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);if(!out)throw ReadError("Cannot write diagnostic");
    out<<"status="<<s.status<<"\nbuild="<<s.build<<"\nsha256="<<s.hash
       <<"\noffsets_version="<<s.offsets_version<<"\nplace="<<s.place
       <<"\ncompatible="<<s.compatible<<"\nready="<<s.ready<<"\ngeometry_only="<<s.geometry_only<<"\nentities="<<s.entities.size()<<"\ngeometry_candidates="<<s.geometry_candidates<<"\ngeometry_rejected="<<s.geometry_rejected<<"\nread_ms="<<s.read_ms<<'\n';
    for(const auto& e:s.entities)out<<"entity local="<<e.local<<" geometry_only="<<e.geometry_only<<" parts="<<e.parts.size()<<" health="<<e.health<<'/'<<e.max_health<<" range="<<e.range<<'\n';
    out.flush();if(!out)throw ReadError("Diagnostic write failed");
}
}
