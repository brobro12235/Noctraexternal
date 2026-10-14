#include "core.hpp"
#include "offsets.hpp"
#include "preview.hpp"
#include <Windows.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace noctra;
namespace {
int assertions=0;
void expect(bool condition,const char* label){++assertions;if(!condition)throw std::runtime_error(label);}
template<class F>void rejects(F fn,const char* label){bool threw=false;try{fn();}catch(const std::exception&){threw=true;}expect(threw,label);}
Matrix identity(){Matrix m{};m[0]=m[5]=m[10]=m[15]=1;return m;}
Entity entity(std::uintptr_t id,float x,float y){Entity e;e.id=id;e.character=id+100;e.health=100;e.max_health=100;e.range=100;e.team=id+1000;e.neutral=false;
    Part p;p.name="Head";p.position={x,y,0};p.size={0.2f,0.2f,0.2f};p.rotation={1,0,0,0,1,0,0,0,1};e.parts.push_back(p);return e;}
void math_tests(){auto m=identity();auto p=project({0,0,0},m,{1920,1080});expect(p&&p->x==960&&p->y==540,"center projection");
    expect(!project({2,0,0},m,{100,100}),"offscreen clipped");expect(project({2,0,0},m,{100,100},false).has_value(),"bounds un-clipped projection");
    m[15]=-1;expect(!project({0,0,0},m,{100,100}),"behind camera rejected");m=identity();m[3]=std::numeric_limits<float>::quiet_NaN();
    expect(!project({0,0,0},m,{100,100}),"NaN matrix rejected");m=identity();
    expect(!project({0,0,0},m,{0,100}),"zero viewport rejected");expect(!project({0,0,std::numeric_limits<float>::infinity()},m,{100,100}),"infinite position rejected");
    expect(!project({0,0,0},Matrix{},{100,100}),"zero matrix rejected");
    float a=100,b=100;for(int i=0;i<60;++i)a-=smooth_delta({a,0},68,1.0f/60).x;for(int i=0;i<144;++i)b-=smooth_delta({b,0},68,1.0f/144).x;
    expect(std::abs(a-b)<0.001f,"frame-rate independent smoothing");expect(smooth_delta({10,10},50,-1).x==0,"invalid dt");
    Settings aim;expect(aim_delta({1,1},aim,0.016f).x==0,"deadzone prevents jitter");
    auto movement=aim_delta({1000,1000},aim,0.016f);expect(std::hypot(movement.x,movement.y)<=aim.max_step+0.001f,"radial step cap");
    expect(aim_delta({100,100},aim,0.5f).x==0,"long frame suppresses aim");
    expect(aim_delta({100,0},aim,0.016f).x>0,"aim movement toward target");
    expect(distance({0,0,0},{3,4,0})==5,"stud distance");
    auto e=entity(1,0,0);auto bounds=entity_bounds(e,m,{100,100});expect(bounds&&std::abs(bounds->min.x-45)<0.01f&&std::abs(bounds->max.y-55)<0.01f,"oriented bounds");
    e.parts[0].position.x=4;expect(!entity_bounds(e,m,{100,100}),"offscreen bounds omitted");
    expect(skeleton_links(false).size()==5&&skeleton_links(true).size()==14,"rig links");
}
void target_tests(){Snapshot s;s.ready=true;s.teams_verified=true;s.matrix=identity();s.entities={entity(1,0.05f,0),entity(2,0.06f,0)};Settings c;c.fov_pixels=100;
    auto t=choose_target(s,c,{1000,1000});expect(t&&t->id==1,"nearest target");
    s.geometry_only=true;c.team_check=false;expect(!choose_target(s,c,{1000,1000}),"PF geometry snapshot cannot aim with team check off");s.geometry_only=false;
    s.entities[0].geometry_only=true;s.entities[1].geometry_only=true;expect(!choose_target(s,c,{1000,1000}),"anonymous geometry never eligible for aim");
    s.entities[0].geometry_only=false;s.entities[1].geometry_only=false;c.team_check=true;
    s.teams_verified=false;expect(!choose_target(s,c,{1000,1000}),"unknown team data blocks aim");s.teams_verified=true;
    s.entities[0].neutral=true;s.entities[1].neutral=true;expect(!choose_target(s,c,{1000,1000}),"unclassified candidates excluded");
    s.entities[0].neutral=false;s.entities[1].neutral=false;
    t=choose_target(s,c,{1000,1000},2,102);expect(t&&t->id==2,"retained target hysteresis");
    t=choose_target(s,c,{1000,1000},2,999);expect(t&&t->id==1,"respawn invalidates retention");
    s.entities[0].local=true;t=choose_target(s,c,{1000,1000});expect(t&&t->id==2,"self filter");
    s.entities[1].health=0;expect(!choose_target(s,c,{1000,1000}),"dead filter");s.entities[1].health=100;
    s.entities[1].range=2000;expect(!choose_target(s,c,{1000,1000}),"range filter");s.entities[1].range=100;
    s.local_team=5;s.entities[1].team=5;expect(!choose_target(s,c,{1000,1000}),"team filter");c.team_check=false;
    expect(choose_target(s,c,{1000,1000}).has_value(),"team filter optional");c.fov_pixels=10;expect(!choose_target(s,c,{1000,1000}),"FOV gate");c.fov_pixels=100;
    s.ready=false;expect(!choose_target(s,c,{1000,1000}),"unready snapshot blocks targeting");s.ready=true;
    c.bone=1;expect(!choose_target(s,c,{1000,1000}),"missing bone not silently substituted");
    s.entities[1].parts[0].name="Torso";expect(choose_target(s,c,{1000,1000}).has_value(),"R6 torso fallback");
    c.bone=3;expect(choose_target(s,c,{1000,1000}).has_value(),"closest part");
}
void geometry_tests(){
    Part p;p.name="Geometry";p.size={0.001f,0.001f,0.001f};p.rotation={1,0,0,0,1,0,0,0,1};
    std::vector<Part> proxies(6,p),meshes;for(std::size_t i=0;i<proxies.size();++i){proxies[i].position={static_cast<float>(i%2),static_cast<float>(i/2),0};auto mesh=proxies[i];mesh.size={1,2,1};meshes.push_back(mesh);}
    expect(phantom_geometry_range(proxies,meshes,{0,0,-100}).has_value(),"coherent replicated PF geometry accepted");
    expect(!phantom_geometry_range(proxies,meshes,{0,0,0}),"near-camera geometry excluded");
    auto bad=proxies;bad.pop_back();expect(!phantom_geometry_range(bad,meshes,{0,0,-100}),"exact proxy topology required");
    bad=proxies;bad[0].position.x=100;expect(!phantom_geometry_range(bad,meshes,{0,0,-100}),"disconnected proxy rejected");
    bad=meshes;bad[0].position.x=100;expect(!phantom_geometry_range(proxies,bad,{0,0,-100}),"disconnected mesh rejected");
    bad=meshes;bad[0].size.x=9;expect(!phantom_geometry_range(proxies,bad,{0,0,-100}),"oversized mesh rejected");
    bad=meshes;bad[0].rotation[0]=std::numeric_limits<float>::quiet_NaN();expect(!phantom_geometry_range(proxies,bad,{0,0,-100}),"NaN geometry rejected");
    bad=meshes;bad.resize(97,p);expect(!phantom_geometry_range(proxies,bad,{0,0,-100}),"PF mesh budget enforced");
    Entity e;e.geometry_only=true;e.parts=meshes;expect(entity_bounds(e,identity(),{100,100}).has_value(),"anonymous visual mesh bounds render");
}
void safety_tests(){expect(valid_address(0x10000,8),"valid address");expect(!valid_address(0,8),"null address");
    expect(!valid_address(0x7fffffffffffULL,8),"address overflow");expect(!valid_address(0x10000,65537),"read bound");
    expect(child_count(0x10000,0x10030)==3,"collection count");expect(child_count(0,0)==0,"empty collection");
    expect(!child_count(0x10000,0x10001),"collection stride");expect(!child_count(0x20000,0x10000),"collection reverse");
    expect(!child_count(0x10000,0x10000+16*4097),"collection maximum");
    const std::string offset_document=R"json({
      "Roblox Version":"version-test-offsets",
      "Offsets":{
        "VisualEngine":{"Pointer":100,"Dimensions":104,"ViewMatrix":108},
        "FakeDataModel":{"Pointer":112,"RealDataModel":116},
        "Instance":{"ClassDescriptor":120,"ClassName":124,"NameContainer":128,"Name":132,
          "ChildrenStart":136,"ChildrenEnd":8,"Parent":140},
        "Misc":{"StringLength":16},
        "DataModel":{"PlaceId":144},
        "Workspace":{"CurrentCamera":148},
        "Player":{"LocalPlayer":152,"ModelInstance":156,"Team":160},
        "Camera":{"Position":164,"FieldOfView":168},
        "BasePart":{"Primitive":172},
        "Primitive":{"Position":176,"Size":180,"Rotation":184},
        "Humanoid":{"Health":188,"MaxHealth":192}
      }
    })json";
    offsets::reset_defaults();std::string loaded_version,offset_error;
    expect(offsets::load_offsets_from_json(offset_document,&loaded_version,&offset_error),"offset JSON accepted");
    expect(loaded_version=="version-test-offsets"&&offsets::fake_global==112&&
           offsets::children==136&&offsets::children_end==8&&offsets::string_length==16&&
           !offsets::name_direct,
           "offset JSON values applied");
    auto direct_name=offset_document;
    const auto name_container=direct_name.find("\"NameContainer\":128,\"Name\":132");
    expect(name_container!=std::string::npos,"indirect name fixture found");
    direct_name.replace(name_container,std::string("\"NameContainer\":128,\"Name\":132").size(),"\"Name\":176");
    expect(offsets::load_offsets_from_json(direct_name,&loaded_version,&offset_error)&&
           offsets::name_direct&&offsets::name==176,"direct name offset accepted");
    auto broken=offset_document;broken.replace(broken.find("\"Health\":188"),12,"\"Health\":\"bad\"");
    const auto before_fake=offsets::fake_global;
    expect(!offsets::load_offsets_from_json(broken,nullptr,&offset_error)&&
           offsets::fake_global==before_fake,"invalid offset JSON rejected atomically");
    offsets::reset_defaults();
    auto* page=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS);expect(page!=nullptr,"allocate inaccessible page");
    char byte{};SIZE_T count{};const auto ok=ReadProcessMemory(GetCurrentProcess(),page,&byte,1,&count);
    VirtualFree(page,0,MEM_RELEASE);expect(!ok&&count==0,"real inaccessible read fails");
}
void preview_tests(){
    Entity e;e.local=true;e.character=123;e.health=80;e.max_health=100;
    Part root;root.name="HumanoidRootPart";root.size={2,2,1};root.rotation={1,0,0,0,1,0,0,0,1};e.parts.push_back(root);
    Part torso=root;torso.name="Torso";e.parts.push_back(torso);
    Part head=root;head.name="Head";head.position={0,1.5f,0};head.size={1,1,1};e.parts.push_back(head);
    Part leg=root;leg.name="Left Leg";leg.position={-0.5f,-2,0};leg.size={1,2,1};e.parts.push_back(leg);
    auto result=build_character_preview(e,{280,260},20);expect(result.has_value(),"local geometry preview builds");
    expect(result->part_count==3&&!result->r15,"root excluded from R6 visible body");
    expect(!result->faces.empty()&&result->nodes.size()==3,"faces and skeleton nodes produced");
    for(const auto& face:result->faces)for(auto p:face.points)expect(p.x>=0&&p.x<=280&&p.y>=0&&p.y<=260,"preview fitted inside viewport");
    for(std::size_t i=1;i<result->faces.size();++i)expect(result->faces[i-1].depth<=result->faces[i].depth,"preview face depth order");
    const auto original=*result;for(auto& p:e.parts){p.position.x+=100;p.position.y+=50;p.position.z-=70;}
    auto translated=build_character_preview(e,{280,260},20);expect(translated&&std::abs(translated->bounds.min.x-original.bounds.min.x)<0.001f,"preview is world-translation independent");
    const auto rotation=build_character_preview(e,{280,260},100);expect(rotation&&std::abs(rotation->bounds.min.x-translated->bounds.min.x)>0.01f,"yaw changes view");
    e.parts[1].name="UpperTorso";result=build_character_preview(e,{280,260},20);expect(result&&result->r15,"R15 detection");
    e.parts[2].size.x=-1;expect(!build_character_preview(e,{280,260},20),"invalid preview size rejected");e.parts[2].size.x=1;
    e.parts[2].rotation[0]=std::numeric_limits<float>::quiet_NaN();expect(!build_character_preview(e,{280,260},20),"invalid preview rotation rejected");
    expect(!build_character_preview(e,{0,260},20),"invalid preview viewport rejected");
    expect(!build_character_preview(Entity{},{280,260},20),"missing preview body rejected");
    Snapshot s;s.ready=true;s.captured=Clock::now();s.entities.push_back(e);
    expect(preview_local_entity(s,s.captured)!=nullptr,"fresh local snapshot chosen");
    expect(preview_local_entity(s,s.captured+std::chrono::seconds(1))==nullptr,"stale preview hidden");
    s.entities[0].local=false;expect(preview_local_entity(s,s.captured)==nullptr,"remote entity never used for local preview");
    s.entities[0].local=true;s.ready=false;expect(preview_local_entity(s,s.captured)==nullptr,"unready preview hidden");
}
void config_tests(){const auto folder=std::filesystem::temp_directory_path()/("noctra-tests-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);const auto path=folder/"roundtrip.noctra";
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}}cleanup{folder};
    Settings c;c.aim=true;c.esp=true;c.skeleton=true;c.dark_mode=true;c.bone=2;c.menu_key=0x73;c.smoothing=23.125f;c.color={0.2f,0.5f,0.7f,0.8f};save_profile(path,c);
    auto loaded=load_profile(path);expect(loaded.aim&&loaded.esp&&loaded.skeleton&&loaded.dark_mode&&loaded.bone==2&&loaded.menu_key==0x73&&loaded.smoothing==c.smoothing&&loaded.color==c.color,"profile roundtrip");
    c.smoothing=std::numeric_limits<float>::quiet_NaN();rejects([&]{save_profile(path,c);},"NaN settings rejected");
    expect(load_profile(path).smoothing==23.125f,"invalid save preserves file");
    {std::ofstream out(path,std::ios::app);out<<"schema=2\n";}rejects([&]{load_profile(path);},"duplicate rejected");
    {std::ofstream out(path);out<<"schema=999\n";}rejects([&]{load_profile(path);},"unknown schema rejected");
    c=Settings{};save_profile(path,c);{std::ifstream in(path);std::string text((std::istreambuf_iterator<char>(in)),{});in.close();
       const auto pos=text.find("fov_pixels=140");expect(pos!=std::string::npos,"test fixture field exists");text.replace(pos,14,"fov_pixels=99999");std::ofstream out(path);out<<text;}
    rejects([&]{load_profile(path);},"out-of-range field rejected");
    save_profile(path,c);{std::ofstream out(path,std::ios::app);out<<"unknown=value\n";}rejects([&]{load_profile(path);},"unknown field rejected");
    {std::ofstream out(path);out<<std::string(17000,'x');}rejects([&]{load_profile(path);},"oversized profile rejected");
    expect(!valid_settings(Settings{.aim_key=1}),"invalid aim key");
}
}
int main(){try{math_tests();target_tests();geometry_tests();safety_tests();preview_tests();config_tests();std::cout<<"PASS: "<<assertions<<" assertions\n";return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL after "<<assertions<<" assertions: "<<e.what()<<'\n';return 1;}}
