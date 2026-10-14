#include "ui.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <string>
using namespace noctra;
namespace {
int assertions=0;
void expect(bool condition,const char* message){++assertions;if(!condition)throw std::runtime_error(message);}
struct Fixture {
    Settings settings;
    Snapshot snapshot;
    UiState state;
    Fixture(){ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={805,520};io.DeltaTime=1.0f/60;
        initialize_menu_fonts(1);apply_theme();unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        expect(pixels&&width>0&&height>0,"embedded font atlas builds");io.Fonts->SetTexID(1);}
    ~Fixture(){ImGui::DestroyContext();}
    std::string rendered_text;
    void frame(bool capture=false){
        if(snapshot.ready)snapshot.captured=Clock::now();ImGui::NewFrame();
        if(capture){ImGui::SetNextWindowPos({-1000,-1000});ImGui::SetNextWindowSize({1,1});
            ImGui::Begin("test-log",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs);ImGui::LogToBuffer();}
        draw_menu(settings,snapshot,state,100,10);
        if(capture){rendered_text=GImGui->LogBuffer.c_str();ImGui::LogFinish();ImGui::End();}
        ImGui::Render();
    }
    void click(float x,float y){auto& io=ImGui::GetIO();io.AddMousePosEvent(x,y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();}
    ImGuiWindow* window(const char* suffix){for(ImGuiWindow* w:GImGui->Windows)if(std::string(w->Name).find(suffix)!=std::string::npos)return w;
        throw std::runtime_error(std::string("Missing child ")+suffix);}
};
}
int main(){try{
    Fixture f;f.frame();f.frame();
    expect(ImGui::GetDrawData()->TotalVtxCount>500,"menu produces draw geometry");
    expect(f.state.preview_status=="Waiting for a fresh local character","empty preview honest waiting state");
    const auto background=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);expect(background.x>0.9f&&background.y>0.9f,"enQ light palette");
    f.settings.dark_mode=true;f.frame();f.frame();
    const auto dark_background=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    expect(dark_background.x<0.2f&&dark_background.y<0.2f&&dark_background.z<0.25f,"dark palette applies");
    f.settings.dark_mode=false;f.frame();f.frame();
    expect(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x>0.9f,"light palette restores");
    f.state.tab=2;f.state.sections[2]=1;f.frame();
    expect(ImGui::GetDrawData()->TotalVtxCount>100,"interface page renders dark mode control");
    expect(f.window("/controls_")->Pos.x==210,"enQ control column");
    expect(f.window("/categories_")->Pos.x==50,"enQ category column");
    f.click(20,140);expect(f.state.tab==1,"visuals icon navigation");
    auto* controls=f.window("/controls_");const float x=controls->Pos.x+35;const float y=controls->Pos.y+controls->WindowPadding.y+42;
    f.click(x,y);expect(f.settings.esp,"ESP master checkbox bound to settings");
    f.click(x,y+30);expect(!f.settings.boxes,"boxes separate from ESP master");
    f.click(90,125);expect(f.state.sections[1]==1,"appearance subtab navigation");
    f.click(20,90);expect(f.state.tab==0,"aim icon navigation");
    f.click(90,125);expect(f.state.sections[0]==1,"response subtab navigation");
    controls=f.window("/controls_");f.click(controls->Pos.x+controls->Size.x-22,controls->Pos.y+controls->WindowPadding.y+72);
    expect(f.settings.smoothing>80,"native smoothing slider accepts input");
    // Ctrl-click opens numeric input; public slider behavior clamps values.
    auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,true);f.click(controls->Pos.x+100,controls->Pos.y+controls->WindowPadding.y+72);
    io.AddKeyEvent(ImGuiMod_Ctrl,false);io.AddInputCharactersUTF8("999");f.frame();io.AddKeyEvent(ImGuiKey_Enter,true);f.frame();io.AddKeyEvent(ImGuiKey_Enter,false);f.frame();
    expect(f.settings.smoothing==100,"numeric slider input is clamped");
    f.click(20,190);expect(f.state.tab==2,"misc icon navigation");
    f.click(20,240);expect(f.state.tab==3,"config icon navigation");
    f.click(20,290);expect(f.state.tab==4,"cleaner icon navigation");
    f.frame(true);expect(f.rendered_text.find("Clean Roblox Data")!=std::string::npos,"cleaner page renders its action");
    f.click(20,240);expect(f.state.tab==3,"return to config after cleaner navigation");
    controls=f.window("/controls_");f.click(controls->Pos.x+100,controls->Pos.y+200);
    expect(f.state.confirm_reset,"reset requires confirmation");
    f.click(controls->Pos.x+45,controls->Pos.y+273);expect(!f.state.confirm_reset,"reset confirmed");
    expect(!f.settings.aim&&!f.settings.esp,"reset disarms modules");
    f.click(20,445);expect(f.state.tab==2&&f.state.sections[2]==1,"settings shortcut navigation");
    for(int tab=0;tab<5;++tab)for(int section=0;section<3;++section){f.state.tab=tab;f.state.sections[tab]=section;f.frame();f.frame();
        expect(ImGui::GetDrawData()->TotalVtxCount>100,"each page draws");}
    f.state.tab=1;f.state.sections[1]=0;ImGui::GetIO().DisplaySize={805,480};f.frame();f.frame();
    expect(f.window("/controls_")->Size.x>=280,"minimum-size controls have enough width");
    expect(f.window("/controls_")->ScrollMax.y<1,"ESP elements fit without clipping at minimum height");
    Entity local;local.local=true;local.character=123;local.name="LiveTester";local.health=75;local.max_health=100;
    Part root;root.name="HumanoidRootPart";root.size={2,2,1};root.rotation={1,0,0,0,1,0,0,0,1};local.parts.push_back(root);
    Part torso=root;torso.name="Torso";local.parts.push_back(torso);Part head=root;head.name="Head";head.position={0,1.5f,0};head.size={1,1,1};local.parts.push_back(head);
    f.snapshot.ready=true;f.snapshot.entities={local};f.frame();f.frame();expect(f.state.preview_status=="Live R6 - 2 body parts","UI uses local snapshot character");
    f.state.avatar_texture=1;f.state.avatar_width=720;f.state.avatar_height=720;f.state.preview_geometry=false;
    f.frame();expect(f.state.preview_status=="Account avatar image","UI prefers profile avatar image over geometry");
    f.settings.boxes=false;f.settings.health=false;f.settings.skeleton=false;f.settings.tracers=false;f.settings.names=false;f.settings.distance_text=false;f.frame();
    const auto base_vertices=f.window("/preview_")->DrawList->VtxBuffer.Size;
    for(auto* toggle:{&f.settings.boxes,&f.settings.health,&f.settings.skeleton,&f.settings.tracers,&f.settings.names,&f.settings.distance_text}){
        *toggle=true;f.frame();expect(f.window("/preview_")->DrawList->VtxBuffer.Size>base_vertices,"avatar ESP toggle adds preview geometry/text");*toggle=false;}
    f.state.preview_geometry=true;f.frame();expect(f.state.preview_status=="Live R6 - 2 body parts","geometry fallback can be restored");
    f.state.avatar_texture=0;f.state.preview_geometry=false;f.snapshot.ready=false;f.frame();expect(f.state.preview_status=="Waiting for a fresh local character","missing local data clears preview");
    // Removed status/debug copy must stay absent on every page; profile action
    // errors and confirmations remain visible only where they are relevant.
    f.snapshot.build="version-test-build";f.snapshot.status="UNWANTED_RUNTIME_STATUS";
    for(int tab=0;tab<5;++tab)for(int section=0;section<3;++section){
        f.state.tab=tab;f.state.sections[tab]=section;f.frame();
        f.frame(true);
        const std::string& log=f.rendered_text;
        expect(log.find("UNWANTED_RUNTIME_STATUS")==std::string::npos,"runtime status not rendered in menu");
        expect(log.find("Local profile schema")==std::string::npos,"profile diagnostics removed");
        expect(log.find("Live pose and proportions")==std::string::npos,"preview description removed");
        if((tab==2||tab==3)&&section==2)expect(log.find("version-test-build")!=std::string::npos,"version page retains version");
    }
    f.snapshot.geometry_only=true;f.snapshot.geometry_candidates=2;f.snapshot.geometry_rejected=2;
    f.state.tab=1;f.state.sections[1]=0;f.frame();controls=f.window("/controls_");
    const float pf_x=controls->Pos.x+35,pf_y=controls->Pos.y+controls->WindowPadding.y+72;
    const bool names_before=f.settings.names;f.click(pf_x,pf_y+60);expect(f.settings.names==names_before,"PF names checkbox disabled");
    const bool health_before=f.settings.health;f.click(pf_x,pf_y+90);expect(f.settings.health==health_before,"PF health checkbox disabled");
    const bool skeleton_before=f.settings.skeleton;f.click(pf_x,pf_y+120);expect(f.settings.skeleton==skeleton_before,"PF skeleton checkbox disabled");
    const bool tracers_before=f.settings.tracers;f.click(pf_x,pf_y+150);expect(f.settings.tracers!=tracers_before,"PF tracer checkbox remains usable");
    f.frame(true);expect(f.rendered_text.find("Experimental PF")!=std::string::npos,"PF limitations shown beside ESP controls");
    f.state.tab=0;f.state.sections[0]=0;f.frame();controls=f.window("/controls_");f.settings.aim=false;
    f.click(controls->Pos.x+35,controls->Pos.y+controls->WindowPadding.y+72);expect(!f.settings.aim,"PF aim checkbox disabled");
    for(int tab:{2,3}){f.state.tab=tab;f.state.sections[tab]=2;f.frame(true);
        expect(f.rendered_text.find("Experimental Phantom Forces")!=std::string::npos,"PF version page explains partial support");
        expect(f.rendered_text.find("Models: 2 | Rejected: 2")!=std::string::npos,"PF model counters visible");}
    Snapshot pf;pf.ready=true;pf.geometry_only=true;pf.matrix[0]=pf.matrix[5]=pf.matrix[10]=pf.matrix[15]=1;
    Entity anonymous;anonymous.geometry_only=true;anonymous.range=100;anonymous.name="NotVerified";anonymous.health=50;anonymous.max_health=100;
    Part mesh;mesh.name="Geometry";mesh.size={0.2f,0.2f,0.2f};mesh.rotation={1,0,0,0,1,0,0,0,1};anonymous.parts={mesh};pf.entities={anonymous};
    Settings visual;visual.esp=true;visual.watermark=false;visual.show_fov=false;visual.names=false;visual.health=false;visual.skeleton=false;visual.distance_text=false;
    const auto overlay_vertices=[&](){ImGui::NewFrame();draw_overlay(visual,pf,{805,480},true,{});ImGui::Render();return ImGui::GetDrawData()->TotalVtxCount;};
    const auto box_vertices=overlay_vertices();expect(box_vertices>0,"PF boxes render despite unknown health");
    visual.names=true;visual.health=true;visual.skeleton=true;expect(overlay_vertices()==box_vertices,"PF does not fabricate names health or skeleton");
    visual.tracers=true;expect(overlay_vertices()>box_vertices,"PF tracers render");visual.tracers=false;
    visual.distance_text=true;expect(overlay_vertices()>box_vertices,"PF distance labels render");visual.distance_text=false;
    visual.boxes=false;expect(overlay_vertices()==0,"PF box toggle removes boxes");
    f.snapshot.geometry_only=false;
    expect(UiState{}.notice.empty(),"no default startup status copy");
    f.state.tab=2;f.state.sections[2]=2;f.state.update_available=true;f.state.update_staged=true;f.state.update_version="1.2.0";
    f.frame();controls=f.window("/controls_");
    f.click(controls->Pos.x+70,controls->Pos.y+164);expect(f.state.update_confirm,"updater asks before restart");
    expect(!f.state.update_install,"restart not requested before confirmation");
    f.frame(true);expect(f.rendered_text.find("Unsaved settings will be lost")!=std::string::npos,"update consent warning visible");
    f.state.update_confirm=false;f.state.update_staged=false;f.state.update_available=false;
    // All module headers, columns and controls must remain stable across DPI.
    for(float dpi:{1.0f,1.25f,1.5f,2.0f}){
        f.state.dpi_scale=dpi;ImGui::GetIO().FontGlobalScale=dpi;apply_theme();ImGui::GetStyle().ScaleAllSizes(dpi);
        ImGui::GetIO().DisplaySize={805*dpi,520*dpi};
        for(int tab=0;tab<5;++tab)for(int section=0;section<3;++section){f.state.tab=tab;f.state.sections[tab]=section;f.frame();f.frame();
            auto* panel=f.window("/controls_");expect(std::abs(panel->Pos.x-210*dpi)<1.1f,"DPI column alignment");
            expect(panel->Size.x>=280*dpi-1,"DPI controls minimum width");
            auto* preview=f.window("/preview_");expect(panel->Pos.x+panel->Size.x<=preview->Pos.x+1,"columns never overlap");
        }
    }
    f.state.dpi_scale=1;ImGui::GetIO().FontGlobalScale=1;apply_theme();
    ImGui::GetIO().DisplaySize={1200,850};f.frame();f.frame();expect(f.window("/preview_")->Size.x>300,"preview expands on resize");
    std::cout<<"PASS: "<<assertions<<" UI assertions\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL after "<<assertions<<" UI assertions: "<<e.what()<<'\n';return 1;}}
