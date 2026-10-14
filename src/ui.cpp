#include "ui.hpp"
#include "preview.hpp"
#include "update_config.hpp"
#include "imgui.h"
#include "../third_party/enq/byte.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

// Layout, palette, and widget geometry ported from the user-supplied enQ DX9
// example and its modified ImGui widgets. Uses the project's pinned ImGui/DX11.
namespace noctra {
namespace {
constexpr ImVec4 ink{50.0f/255,50.0f/255,55.0f/255,1};
constexpr ImVec4 muted{112.0f/255,112.0f/255,123.0f/255,1};
constexpr ImVec4 faint{146.0f/255,146.0f/255,157.0f/255,1};
struct Fonts {ImFont* body{};ImFont* small{};ImFont* icons{};};
// Fonts are owned by each context's atlas. No cross-context font pointers.
Fonts fonts() {
    auto& io=ImGui::GetIO();
    if(io.Fonts->Fonts.Size>=3)return {io.Fonts->Fonts[0],io.Fonts->Fonts[1],io.Fonts->Fonts[2]};
    return {io.FontDefault,io.FontDefault,io.FontDefault};
}
float scale(){return fonts().body->FontSize*ImGui::GetIO().FontGlobalScale/17.0f;}
ImVec2 point(Vec2 v){return {v.x,v.y};}
ImU32 tint(ImVec4 color){color.w*=ImGui::GetStyle().Alpha;return ImGui::ColorConvertFloat4ToU32(color);}
void text_muted(const char* value){ImGui::PushTextWrapPos(0);ImGui::TextColored(muted,"%s",value);ImGui::PopTextWrapPos();}
void draw_text(ImDrawList* draw,ImVec2 position,ImU32 color,const char* text) {
    draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),position,color,text);
}
void focused_outline(ImVec2 at,ImVec2 size) {
    if(ImGui::IsItemFocused())ImGui::GetWindowDrawList()->AddRect(at,{at.x+size.x,at.y+size.y},tint(ink),4*scale());
}
bool matches(const UiState& state,const char* label){
    std::string a=label,b=state.search;
    const auto lower=[](unsigned char c){return static_cast<char>(std::tolower(c));};
    std::transform(a.begin(),a.end(),a.begin(),lower);std::transform(b.begin(),b.end(),b.begin(),lower);
    return a.find(b)!=std::string::npos;
}
void section_title(const char* label) {
    ImGui::Spacing();ImGui::TextColored(ink,"%s",label);ImGui::Spacing();
}
void row(const UiState& state,const char* label,bool& value,const char* description=nullptr) {
    if(!matches(state,label))return;
    const float z=scale();const auto at=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;
    const bool pressed=ImGui::InvisibleButton(label,{width,30*z},ImGuiButtonFlags_EnableNav);if(pressed)value=!value;
    const bool hovered=ImGui::IsItemHovered();if(hovered)ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    auto* draw=ImGui::GetWindowDrawList();
    auto* storage=ImGui::GetStateStorage();const ImGuiID id=ImGui::GetID(label);
    float animation=storage->GetFloat(id,value?1.0f:0.0f);
    const float fraction=1-std::exp(-13.2f*ImGui::GetIO().DeltaTime);
    animation+=((value?1.0f:0.0f)-animation)*fraction;storage->SetFloat(id,animation);
    draw->AddRect({at.x,at.y+5*z},{at.x+17*z,at.y+22*z},tint({206.0f/255,206.0f/255,211.0f/255,1}),2*z);
    draw->AddRectFilled({at.x+3.5f*z,at.y+8.5f*z},{at.x+13.5f*z,at.y+18.5f*z},tint({ink.x,ink.y,ink.z,animation}),2*z);
    const ImVec4 text{faint.x+(ink.x-faint.x)*animation,faint.y+(ink.y-faint.y)*animation,faint.z+(ink.z-faint.z)*animation,1};
    draw_text(draw,{at.x+26*z,at.y+5*z},tint(hovered?ink:text),label);
    if(hovered&&description&&*description)ImGui::SetTooltip("%s",description);
    focused_outline(at,{width,30*z});
}
void slider(const UiState& state,const char* label,float& value,float low,float high,const char* format) {
    if(!matches(state,label))return;
    ImGui::PushID(label);const float z=scale();
    // Label/value get a complete row, and the native slider its own row. This
    // removes label/track collisions and restores keyboard and Ctrl-click input.
    const auto at=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted(label);
    char buffer[64]{};std::snprintf(buffer,sizeof(buffer),format,static_cast<double>(value));
    ImGui::PushFont(fonts().small);const auto value_size=ImGui::CalcTextSize(buffer);
    draw_text(ImGui::GetWindowDrawList(),{at.x+width-value_size.x,at.y+2*z},tint(muted),buffer);ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{7*z,2*z});
    ImGui::SliderFloat("##value",&value,low,high,"",ImGuiSliderFlags_AlwaysClamp);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s: %s\nArrow keys adjust. Ctrl-click for numeric input.",label,buffer);
    ImGui::PopStyleVar();ImGui::Spacing();ImGui::PopID();
}
void combo(const UiState& state,const char* label,int& value,const char* const* labels,const int* values,int count) {
    if(!matches(state,label))return;
    int selected=0;for(int i=0;i<count;++i)if(value==values[i])selected=i;
    ImGui::TextColored(ink,"%s",label);ImGui::SetNextItemWidth(-1);
    if(ImGui::Combo((std::string("##")+label).c_str(),&selected,labels,count))value=values[selected];
    ImGui::Spacing();
}
bool subtab(const char* label,bool selected) {
    const float z=scale();const auto at=ImGui::GetCursorScreenPos();
    const float width=ImGui::GetContentRegionAvail().x;
    const bool pressed=ImGui::InvisibleButton(label,{width,30*z},ImGuiButtonFlags_EnableNav);const bool hovered=ImGui::IsItemHovered();
    if(hovered)ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    auto* draw=ImGui::GetWindowDrawList();draw_text(draw,{at.x,at.y+6*z},tint(selected||hovered?ink:faint),label);
    focused_outline(at,{width,30*z});
    if(selected)draw->AddLine({at.x+width-2*z,at.y+7*z},{at.x+width-2*z,at.y+23*z},tint(ink),2*z);
    return pressed;
}
bool icon_tab(const char* id,const char* glyph,const char* tooltip,bool selected,float z) {
    ImGui::PushID(id);const auto at=ImGui::GetCursorScreenPos();
    const bool pressed=ImGui::InvisibleButton("tab",{31*z,34*z},ImGuiButtonFlags_EnableNav);const bool hovered=ImGui::IsItemHovered();
    if(hovered){ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);ImGui::SetTooltip("%s",tooltip);}
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled({at.x+3*z,at.y+4*z},{at.x+25*z,at.y+29*z},tint({242.0f/255,242.0f/255,247.0f/255,1}),4*z);
    draw->AddRect({at.x+3*z,at.y+4*z},{at.x+25*z,at.y+29*z},tint({193.0f/255,193.0f/255,197.0f/255,1}),4*z);
    ImGui::PushFont(fonts().icons);
    auto* icon_font=ImGui::GetFont();const auto* shape=icon_font->FindGlyph(static_cast<ImWchar>(glyph[0]));
    if(shape){
        // Center the visible glyph, not its advance box: the supplied icon font
        // has nonstandard bearings, which placed icons above/left of the button.
        const float glyph_scale=ImGui::GetFontSize()/icon_font->FontSize;
        const ImVec2 position{at.x+14*z-(shape->X0+shape->X1)*glyph_scale/2,
                              at.y+16.5f*z-(shape->Y0+shape->Y1)*glyph_scale/2};
        draw_text(draw,position,tint(selected||hovered?ink:faint),glyph);
    }
    ImGui::PopFont();
    focused_outline(at,{31*z,34*z});
    if(selected)draw->AddLine({at.x+31*z,at.y+8*z},{at.x+31*z,at.y+25*z},tint(ink),2*z);
    ImGui::PopID();return pressed;
}
void preview(const Settings& settings,const Snapshot& snapshot,UiState& state,float width) {
    const float z=scale();const auto at=ImGui::GetCursorScreenPos();
    const float height=std::clamp(ImGui::GetContentRegionAvail().y-114*z,160*z,350*z);
    const auto* local=preview_local_entity(snapshot,Clock::now());
    const bool avatar=local&&state.avatar_texture&&state.avatar_width&&state.avatar_height&&!state.preview_geometry;
    ImGui::InvisibleButton("character_view",{width,height});
    if(!avatar&&ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left)){
        state.preview_yaw=std::remainder(state.preview_yaw+ImGui::GetIO().MouseDelta.x*0.6f,360.0f);
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",avatar?"Account avatar image with illustrative ESP guides. The skeleton/box are approximate, not live joint positions.":"Live geometry fallback. Drag to rotate.");
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(at,{at.x+width,at.y+height},true);
    draw->AddRectFilled(at,{at.x+width,at.y+height},tint({0.95f,0.95f,0.97f,1}),7*z);
    for(float y=20*z;y<height;y+=24*z)draw->AddLine({at.x,at.y+y},{at.x+width,at.y+y},tint({0.89f,0.89f,0.93f,0.45f}));
    const auto model=local&&!avatar?build_character_preview(*local,{width,height},state.preview_yaw):std::nullopt;
    if(avatar){
        const float fit=std::min((width-16*z)/static_cast<float>(state.avatar_width),(height-16*z)/static_cast<float>(state.avatar_height));
        const ImVec2 size{static_cast<float>(state.avatar_width)*fit,static_cast<float>(state.avatar_height)*fit};
        const ImVec2 low{at.x+(width-size.x)/2,at.y+(height-size.y)/2};
        draw->AddImage(static_cast<ImTextureID>(state.avatar_texture),low,{low.x+size.x,low.y+size.y},{0,0},{1,1},tint({1,1,1,1}));
        // The PNG has no live joints. These normalized preview guides show
        // the selected ESP style only; they do not claim pose reconstruction.
        const ImU32 accent=tint({settings.color[0],settings.color[1],settings.color[2],settings.color[3]});
        const ImVec2 body_low{low.x+size.x*0.17f,low.y+size.y*0.07f},body_high{low.x+size.x*0.83f,low.y+size.y*0.96f};
        if(settings.boxes)draw->AddRect(body_low,body_high,accent);
        if(settings.health&&local->max_health>0){const float fraction=std::clamp(local->health/local->max_health,0.0f,1.0f);
            draw->AddRectFilled({body_low.x-7*z,body_low.y},{body_low.x-4*z,body_high.y},tint({0.82f,0.82f,0.85f,1}));
            draw->AddRectFilled({body_low.x-7*z,body_high.y-(body_high.y-body_low.y)*fraction},{body_low.x-4*z,body_high.y},tint({0.28f,0.64f,0.40f,1}));}
        if(settings.skeleton){
            const auto joint=[&](float x,float y){return ImVec2{low.x+size.x*x,low.y+size.y*y};};
            const ImVec2 neck=joint(0.5f,0.29f),hip=joint(0.5f,0.60f);
            draw->AddLine(joint(0.5f,0.17f),neck,accent,1.4f*z);draw->AddLine(neck,hip,accent,1.4f*z);
            for(float side:{-1.0f,1.0f}){
                const ImVec2 shoulder=joint(0.5f+side*0.16f,0.33f),elbow=joint(0.5f+side*0.23f,0.48f),hand=joint(0.5f+side*0.25f,0.63f);
                draw->AddLine(neck,shoulder,accent,1.4f*z);draw->AddLine(shoulder,elbow,accent,1.4f*z);draw->AddLine(elbow,hand,accent,1.4f*z);
            }
        }
        if(settings.tracers)draw->AddLine({at.x+width/2,at.y+height-4*z},{(body_low.x+body_high.x)/2,body_high.y},accent,1.0f*z);
        if(settings.names){const auto label_size=ImGui::CalcTextSize(local->name.c_str());draw_text(draw,{low.x+(size.x-label_size.x)/2,low.y},tint(ink),local->name.c_str());}
        if(settings.distance_text){const auto label=std::to_string(static_cast<int>(local->range))+" studs";const auto label_size=ImGui::CalcTextSize(label.c_str());
            draw_text(draw,{low.x+(size.x-label_size.x)/2,low.y+size.y-label_size.y},tint(ink),label.c_str());}
        state.preview_status="Account avatar image";
    }else if(!model){state.preview_status=local?"Character geometry unavailable":"Waiting for a fresh local character";
        const auto text=ImGui::CalcTextSize("No live character");draw_text(draw,{at.x+(width-text.x)/2,at.y+height/2},tint(muted),"No live character");
    }else{
        state.preview_status="Live "+std::string(model->r15?"R15":"R6")+" - "+std::to_string(model->part_count)+" body parts";
        for(const auto& face:model->faces){ImVec2 points[4];for(std::size_t i=0;i<4;++i)points[i]={at.x+face.points[i].x,at.y+face.points[i].y};
            draw->AddConvexPolyFilled(points,4,tint({face.shade,face.shade,face.shade+0.035f,1}));
            draw->AddPolyline(points,4,tint({0.46f,0.46f,0.51f,0.5f}),ImDrawFlags_Closed,0.7f*z);}
        const ImU32 accent=tint({settings.color[0],settings.color[1],settings.color[2],settings.color[3]});
        const ImVec2 low{at.x+model->bounds.min.x,at.y+model->bounds.min.y},high{at.x+model->bounds.max.x,at.y+model->bounds.max.y};
        if(settings.boxes)draw->AddRect(low,high,accent);
        if(settings.health&&local->max_health>0){const float fraction=std::clamp(local->health/local->max_health,0.0f,1.0f);
            draw->AddRectFilled({low.x-7*z,low.y},{low.x-4*z,high.y},tint({0.82f,0.82f,0.85f,1}));
            draw->AddRectFilled({low.x-7*z,high.y-(high.y-low.y)*fraction},{low.x-4*z,high.y},tint({0.28f,0.64f,0.40f,1}));}
        const auto node=[&](const std::string& name)->const PreviewNode*{for(const auto& n:model->nodes)if(n.name==name)return &n;return nullptr;};
        if(settings.skeleton)for(const auto& [a,b]:skeleton_links(model->r15)){const auto* first=node(a);const auto* second=node(b);
            if(first&&second)draw->AddLine({at.x+first->point.x,at.y+first->point.y},{at.x+second->point.x,at.y+second->point.y},accent,1.4f*z);}
        if(settings.tracers)draw->AddLine({at.x+width/2,at.y+height-4*z},{(low.x+high.x)/2,high.y},accent);
    }
    draw->PopClipRect();ImGui::Spacing();
    if(local){ImGui::TextWrapped("%s",local->name.c_str());ImGui::Text("Health %.0f / %.0f",local->health,local->max_health);}
    if(avatar){ImGui::PushFont(fonts().small);text_muted("Avatar image - ESP style preview");ImGui::PopFont();}
    else if(local&&!state.preview_geometry){ImGui::PushFont(fonts().small);text_muted(state.avatar_loading?"Loading avatar...":state.avatar_failed?"Avatar unavailable; live geometry":"Live geometry");ImGui::PopFont();}
    if(ImGui::SmallButton("Refresh avatar"))state.avatar_refresh=true;
    if(local&&state.avatar_texture){ImGui::SameLine();if(ImGui::SmallButton(state.preview_geometry?"Avatar image":"Live geometry"))state.preview_geometry=!state.preview_geometry;}
    if(!avatar&&ImGui::SmallButton("Reset preview view"))state.preview_yaw=20;
}
std::filesystem::path profile_path(const char* name) {
    const std::string value=name;
    if(value.empty()||value.size()>40||!std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isalnum(c)||c==' '||c=='_'||c=='-';}))
        throw std::runtime_error("Use 1-40 letters, digits, spaces, underscores or hyphens");
    return profile_directory()/(value+".noctra");
}
const Part* part(const Entity& e,const std::string& name){for(const auto& p:e.parts)if(p.name==name)return &p;return nullptr;}
void outlined_text(ImDrawList* d,ImVec2 at,ImU32 color,const std::string& label) {
    d->AddText({at.x+1,at.y+1},IM_COL32(0,0,0,240),label.c_str());d->AddText(at,color,label.c_str());
}
void panel_begin(const char* id,ImVec2 position,ImVec2 size,float z) {
    ImGui::SetCursorPos(position);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{15*z,12*z});
    ImGui::BeginChild(id,size,ImGuiChildFlags_AlwaysUseWindowPadding);ImGui::PopStyleVar();
}
}
void initialize_menu_fonts(float dpi_scale) {
    ImFontConfig config;config.FontDataOwnedByAtlas=false;config.OversampleH=2;config.OversampleV=1;config.PixelSnapH=true;
    static const ImWchar ranges[]={0x0020,0x00FF,0x0400,0x044F,0};
    auto* atlas=ImGui::GetIO().Fonts;
    atlas->AddFontFromMemoryTTF(gilroyfont,static_cast<int>(sizeof(gilroyfont)),17*dpi_scale,&config,ranges);
    atlas->AddFontFromMemoryTTF(gilroyfont,static_cast<int>(sizeof(gilroyfont)),13*dpi_scale,&config,ranges);
    atlas->AddFontFromMemoryTTF(icon,static_cast<int>(sizeof(icon)),24*dpi_scale,&config,ranges);
}
void apply_theme() {
    auto& style=ImGui::GetStyle();style=ImGuiStyle{};
    style.WindowRounding=12;style.ChildRounding=0;style.FrameRounding=4;style.PopupRounding=4;
    style.WindowPadding={0,0};style.FramePadding={8,5};style.ItemSpacing={8,5};style.ItemInnerSpacing={5,5};
    style.WindowBorderSize=0;style.ChildBorderSize=0;style.ScrollbarSize=3;style.ScrollbarRounding=12;style.GrabRounding=4;
    auto* c=style.Colors;c[ImGuiCol_Text]=ink;c[ImGuiCol_TextDisabled]=muted;
    c[ImGuiCol_WindowBg]={235.0f/255,235.0f/255,240.0f/255,1};c[ImGuiCol_ChildBg]={0,0,0,0};
    c[ImGuiCol_Border]={202.0f/255,202.0f/255,207.0f/255,1};c[ImGuiCol_FrameBg]={242.0f/255,242.0f/255,247.0f/255,1};
    c[ImGuiCol_FrameBgHovered]={0.97f,0.97f,0.99f,1};c[ImGuiCol_FrameBgActive]={0.9f,0.9f,0.94f,1};
    c[ImGuiCol_Button]={0.92f,0.92f,0.95f,1};c[ImGuiCol_ButtonHovered]={0.96f,0.96f,0.98f,1};c[ImGuiCol_ButtonActive]={0.84f,0.84f,0.89f,1};
    c[ImGuiCol_CheckMark]=ink;c[ImGuiCol_SliderGrab]=ink;c[ImGuiCol_SliderGrabActive]=ink;
    c[ImGuiCol_Header]={0.87f,0.87f,0.91f,1};c[ImGuiCol_HeaderHovered]={0.9f,0.9f,0.94f,1};c[ImGuiCol_HeaderActive]={0.83f,0.83f,0.89f,1};
    c[ImGuiCol_PopupBg]={0.94f,0.94f,0.97f,1};c[ImGuiCol_ScrollbarBg]={0,0,0,0};c[ImGuiCol_ScrollbarGrab]={0.68f,0.68f,0.73f,1};
    c[ImGuiCol_Separator]={220.0f/255,220.0f/255,230.0f/255,1};
}
void draw_menu(Settings& s,const Snapshot& snap,UiState& state,double,double) {
    const float z=state.dpi_scale;const auto display=ImGui::GetIO().DisplaySize;
    constexpr int tab_count=5;
    const int section_counts[tab_count]={3,3,3,3,1};
    state.tab=std::clamp(state.tab,0,tab_count-1);
    int& selected=state.sections[state.tab];
    selected=std::clamp(selected,0,section_counts[state.tab]-1);
    ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(display);ImGui::PushStyleVar(ImGuiStyleVar_Alpha,s.opacity);
    ImGui::Begin("NOCTRA enQ",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollWithMouse);
    const auto origin=ImGui::GetWindowPos();auto* draw=ImGui::GetWindowDrawList();
    const float rail=50*z,categories=160*z,header=43*z,footer=34*z;
    const float controls_x=rail+categories;
    const float available=display.x-controls_x;
    const float controls_width=std::clamp(available*0.47f,280*z,365*z);
    const float preview_x=controls_x+controls_width;
    draw->AddRectFilled(origin,{origin.x+display.x,origin.y+display.y},tint({235.0f/255,235.0f/255,240.0f/255,1}),12*z);
    for(float x:{rail,controls_x,preview_x})draw->AddLine({origin.x+x,origin.y},{origin.x+x,origin.y+display.y-footer},tint({220.0f/255,220.0f/255,230.0f/255,1}));
    draw->AddLine({origin.x,origin.y+display.y-footer},{origin.x+display.x,origin.y+display.y-footer},tint({220.0f/255,220.0f/255,230.0f/255,1}));
    ImGui::SetCursorPos({17*z,9*z});ImGui::PushFont(fonts().icons);ImGui::TextColored(ink,"K");ImGui::PopFont();
    const char* tabs[]={"Aim","Visuals","Misc","Config","Cleaner"};const char* glyphs[]={"A","B","C","D","E"};
    for(int i=0;i<tab_count;++i){ImGui::SetCursorPos({9*z,(72+50.0f*static_cast<float>(i))*z});if(icon_tab(tabs[i],glyphs[i],tabs[i],state.tab==i,z))state.tab=i;}
    ImGui::SetCursorPos({9*z,display.y-footer-42*z});if(icon_tab("settings","L","Interface settings",false,z)){state.tab=2;state.sections[2]=1;}
    state.tab=std::clamp(state.tab,0,tab_count-1);
    int& section=state.sections[state.tab];
    section=std::clamp(section,0,section_counts[state.tab]-1);
    const char* menu_sections[tab_count][3]={{"Targeting","Response","Filters"},{"ESP","Appearance","Filters"},{"General","Interface","Version"},{"Profiles","Saved","Version"},{"Clean","",""}};
    draw_text(draw,{origin.x+rail+15*z,origin.y+12*z},tint(ink),tabs[state.tab]);
    draw_text(draw,{origin.x+controls_x+15*z,origin.y+12*z},tint(ink),menu_sections[state.tab][section]);
    draw_text(draw,{origin.x+preview_x+15*z,origin.y+12*z},tint(ink),"Your character");
    ImGui::SetCursorPos({display.x-69*z,8*z});if(ImGui::Button("_",{25*z,25*z}))state.minimize=true;
    ImGui::SameLine(0,3*z);if(ImGui::Button("X",{25*z,25*z}))state.quit=true;
    panel_begin("categories",{rail,header},{categories,display.y-header-footer},z);
    ImGui::PushFont(fonts().small);ImGui::TextColored(faint,"%s",state.tab==1?"PLAYER":"MODULE");ImGui::PopFont();
    ImGui::Spacing();for(int i=0;i<section_counts[state.tab];++i)if(subtab(menu_sections[state.tab][i],section==i))section=i;
    ImGui::EndChild();
    panel_begin("controls",{controls_x,header},{controls_width,display.y-header-footer},z);
    ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##search","Search settings",state.search,sizeof(state.search));ImGui::Spacing();
    if(state.tab==0){
        if(section==0){ImGui::BeginDisabled(snap.geometry_only);row(state,"Aim assist",s.aim,"Hold the selected key with the menu hidden");ImGui::EndDisabled();
            const char* keys[]={"Mouse 2","Mouse 4","Left Alt","Left Shift"};const int values[]={2,5,0xA4,0xA0};combo(state,"Aim key",s.aim_key,keys,values,4);
            const char* bones[]={"Head","Upper torso","Lower torso","Closest"};const int ids[]={0,1,2,3};combo(state,"Target bone",s.bone,bones,ids,4);
            slider(state,"FOV radius",s.fov_pixels,10,1000,"%.0f px");slider(state,"Max distance",s.max_distance,10,10000,"%.0f studs");
            if(snap.geometry_only)text_muted("Phantom Forces: aiming unavailable; models have no verified player identity.");}
        if(section==1){slider(state,"Smoothness",s.smoothing,0,100,"%.0f%%");slider(state,"Mouse gain",s.mouse_gain,0.1f,5,"%.2f");
            slider(state,"Max input step",s.max_step,1,100,"%.0f counts");slider(state,"Aim deadzone",s.deadzone,0,20,"%.1f px");}
        if(section==2){row(state,"Team check",s.team_check,"Unknown team data blocks aiming");}
    }
    if(state.tab==1){
        if(section==0){row(state,"Player ESP",s.esp);row(state,"Bounding boxes",s.boxes);
            ImGui::BeginDisabled(snap.geometry_only);row(state,"Player names",s.names);row(state,"Health bars",s.health);row(state,"Skeleton",s.skeleton);ImGui::EndDisabled();
            row(state,"Tracers",s.tracers);row(state,"Distance labels",s.distance_text);
            if(snap.geometry_only)text_muted("Experimental PF: boxes, tracers and distance only. Teams are unknown.");}
        if(section==1){section_title("ESP accent");ImGui::ColorEdit4("##color",s.color.data(),ImGuiColorEditFlags_AlphaBar);
            if(ImGui::SmallButton("Reset accent"))s.color=Settings{}.color;
            row(state,"Show FOV circle",s.show_fov);row(state,"Watermark",s.watermark);}
        if(section==2){row(state,"Team check",s.team_check,"Unknown team data blocks aiming");slider(state,"Max distance",s.max_distance,10,10000,"%.0f studs");}
    }
    if(state.tab==2){
        if(section==0){row(state,"Show FOV circle",s.show_fov);row(state,"Watermark",s.watermark);
            if(ImGui::Button("Disarm modules",{-1,30*z})){s.aim=false;s.esp=false;state.notice="Live modules disarmed";}}
        if(section==1){slider(state,"Menu opacity",s.opacity,0.4f,1,"%.2f");const char* labels[]={"Insert","Home","Delete","F4"};
            const int values[]={0x2D,0x24,0x2E,0x73};combo(state,"Menu key",s.menu_key,labels,values,4);}
        if(section==2){ImGui::Text("NOCTRA %s",app_version);if(!snap.build.empty())ImGui::TextWrapped("%s",snap.build.c_str());
            ImGui::Spacing();ImGui::TextUnformatted("Application updates");
            if(!state.update_message.empty())ImGui::TextWrapped("%s",state.update_message.c_str());
            ImGui::BeginDisabled(state.update_busy);
            if(ImGui::Button("Check for updates"))state.update_check=true;
            if(state.update_available&&!state.update_staged){ImGui::Text("Release %s",state.update_version.c_str());if(ImGui::Button("Download update"))state.update_download=true;}
            if(state.update_staged&&ImGui::Button("Install and restart"))state.update_confirm=true;
            ImGui::EndDisabled();
            if(state.update_confirm){ImGui::TextWrapped("Replace this copy and restart? Unsaved settings will be lost.");
                if(ImGui::Button("Confirm restart")){state.update_install=true;state.update_confirm=false;}
                ImGui::SameLine();if(ImGui::Button("Not now"))state.update_confirm=false;}
            if(snap.geometry_only){ImGui::Separator();ImGui::TextWrapped("Experimental Phantom Forces: boxes, tracers and distance only; identity, health, teams, skeleton and aiming unavailable.");
                ImGui::Text("Models: %zu | Rejected: %zu",snap.geometry_candidates,snap.geometry_rejected);
                if(!snap.ready)text_muted(snap.geometry_candidates?"No accepted models; see --diagnose for details.":"No replicated models available; enter a spawned round.");}}
    }
    if(state.tab==3){
        if(section==0){ImGui::TextUnformatted("Profile name");ImGui::SetNextItemWidth(-1);ImGui::InputText("##profile",state.profile,sizeof(state.profile));ImGui::Spacing();
            if(ImGui::Button("Save configuration",{-1,30*z}))try{save_profile(profile_path(state.profile),s);state.loaded_profile=state.profile;state.notice="Configuration saved";}catch(const std::exception& e){state.notice=e.what();}
            if(ImGui::Button("Load configuration",{-1,30*z}))try{s=load_profile(profile_path(state.profile));s.aim=false;state.loaded_profile=state.profile;state.notice="Loaded; aim remains off";}catch(const std::exception& e){state.notice=e.what();}
            if(ImGui::Button("Reset settings",{-1,30*z}))state.confirm_reset=true;
            if(state.confirm_reset){ImGui::TextWrapped("Restore defaults and disarm modules?");
                if(ImGui::Button("Confirm reset")){s=Settings{};state.loaded_profile.clear();state.notice="Defaults restored; modules off";state.confirm_reset=false;}
                ImGui::SameLine();if(ImGui::Button("Cancel"))state.confirm_reset=false;}
            if(!state.loaded_profile.empty())ImGui::TextWrapped("Last saved/loaded: %s",state.loaded_profile.c_str());
            if(!state.notice.empty())ImGui::TextWrapped("%s",state.notice.c_str());}
        if(section==1){ImGui::TextWrapped("Select a profile, then load it in Profiles.");ImGui::Spacing();
            try{const auto folder=profile_directory();if(std::filesystem::exists(folder))for(const auto& entry:std::filesystem::directory_iterator(folder)){
                if(entry.is_regular_file()&&entry.path().extension()==".noctra"){const auto name=entry.path().stem().string();if(ImGui::Selectable(name.c_str(),name==state.profile))strncpy_s(state.profile,name.c_str(),_TRUNCATE);}
            }}catch(const std::exception& e){ImGui::TextWrapped("%s",e.what());}}
        if(section==2){ImGui::Text("NOCTRA %s",app_version);if(!snap.build.empty())ImGui::TextWrapped("%s",snap.build.c_str());
            ImGui::Spacing();ImGui::TextUnformatted("Application updates");
            if(!state.update_message.empty())ImGui::TextWrapped("%s",state.update_message.c_str());
            ImGui::BeginDisabled(state.update_busy);
            if(ImGui::Button("Check for updates"))state.update_check=true;
            if(state.update_available&&!state.update_staged){ImGui::Text("Release %s",state.update_version.c_str());if(ImGui::Button("Download update"))state.update_download=true;}
            if(state.update_staged&&ImGui::Button("Install and restart"))state.update_confirm=true;
            ImGui::EndDisabled();
            if(state.update_confirm){ImGui::TextWrapped("Replace this copy and restart? Unsaved settings will be lost.");
                if(ImGui::Button("Confirm restart")){state.update_install=true;state.update_confirm=false;}
                ImGui::SameLine();if(ImGui::Button("Not now"))state.update_confirm=false;}
            if(snap.geometry_only){ImGui::Separator();ImGui::TextWrapped("Experimental Phantom Forces: boxes, tracers and distance only; identity, health, teams, skeleton and aiming unavailable.");
                ImGui::Text("Models: %zu | Rejected: %zu",snap.geometry_candidates,snap.geometry_rejected);
                if(!snap.ready)text_muted(snap.geometry_candidates?"No accepted models; see --diagnose for details.":"No replicated models available; enter a spawned round.");}}
    }
    if(state.tab==4&&section==0){
        ImGui::TextUnformatted("Roblox data cleanup");
        ImGui::Spacing();
        ImGui::TextWrapped("Remove Roblox-related cache, logs, HTTP data, cookies, and matching prefetch files.");
        ImGui::Spacing();
        if(ImGui::Button("Clean Roblox Data",{-1,30*z}))state.show_roblox_clean_confirm=true;
        if(state.show_roblox_clean_confirm){
            ImGui::TextWrapped("Are you sure you want to clean Roblox-related cache, logs, and prefetch files? This action cannot be undone.");
            if(ImGui::Button("Yes, clean data",{-1,30*z})){
                try{
                    clean_roblox_data();
                    state.notice = "Roblox data cleaned successfully.";
                }catch(const std::exception& e){
                    state.notice = std::string("Failed to clean Roblox data: ") + e.what();
                }
                state.show_roblox_clean_confirm = false;
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel",{-1,30*z}))state.show_roblox_clean_confirm = false;
        }
        if(!state.notice.empty())ImGui::TextWrapped("%s",state.notice.c_str());
    }
    ImGui::EndChild();
    panel_begin("preview",{preview_x,header},{display.x-preview_x,display.y-header-footer},z);
    preview(s,snap,state,ImGui::GetContentRegionAvail().x);
    ImGui::EndChild();
    ImGui::SetCursorPos({15*z,display.y-footer+8*z});ImGui::PushFont(fonts().small);
    const std::string footer_text="NOCTRA "+std::string(app_version)+(snap.build.empty()?"":"  |  "+snap.build);
    const auto footer_at=ImGui::GetCursorScreenPos();draw->PushClipRect(footer_at,{origin.x+display.x-12*z,origin.y+display.y},true);
    draw_text(draw,footer_at,tint(muted),footer_text.c_str());draw->PopClipRect();ImGui::Dummy({display.x-30*z,18*z});
    ImGui::PopFont();
    ImGui::End();ImGui::PopStyleVar();
}
void draw_overlay(const Settings& s,const Snapshot& snap,Vec2 v,bool active,const std::optional<Target>& target) {
    if(!active)return;
    auto* d=ImGui::GetBackgroundDrawList();
    const ImU32 base_color=ImGui::ColorConvertFloat4ToU32({s.color[0],s.color[1],s.color[2],s.color[3]});
    if(s.watermark)outlined_text(d,{16,14},base_color,"NOCTRA "+std::string(app_version)+(snap.build.empty()?"":" | "+snap.build));
    if(!snap.ready)return;
    Vec2 fovCenter{v.x/2, v.y/2}; // default to screen center
    if (snap.window) {
        POINT mousePos;
        GetCursorPos(&mousePos);
        ScreenToClient(reinterpret_cast<HWND>(snap.window), &mousePos);
        fovCenter = {static_cast<float>(mousePos.x), static_cast<float>(mousePos.y)};
    }
    if(s.show_fov)d->AddCircle(point(fovCenter), s.fov_pixels, base_color, 100);
    if(target){d->AddCircle(point(target->position),5,base_color,24,1.5f);}
    if(!s.esp)return;
    for(const auto& e:snap.entities) {
        if(e.local||(!e.geometry_only&&e.health<=0)||e.range>s.max_distance||(s.team_check&&e.team&&e.team==snap.local_team))continue;
        const auto b=entity_bounds(e,snap.matrix,v);if(!b)continue;
        // Determine color based on team and settings
        ImU32 col = base_color;
        if (s.esp_team_color && e.team) {
            // Simple friendly/enemy distinction: if on same team as local player, use green; else red.
            if (e.team == snap.local_team)
                col = ImGui::ColorConvertFloat4ToU32({0.0f,1.0f,0.0f,1.0f}); // friendly green
            else
                col = ImGui::ColorConvertFloat4ToU32({1.0f,0.0f,0.0f,1.0f}); // enemy red
        }
        // Draw box
        if (s.boxes) {
            // Outline box (black) if requested
            if (s.esp_outline_box) {
                d->AddRect({b->min.x-2,b->min.y-2},{b->max.x+2,b->max.y+2},
                           IM_COL32(0,0,0,200),0,0,2);
            }
            // Filled box with transparency if requested
            if (s.esp_filled_box) {
                // Extract RGB from col and apply alpha 0.15f
                float r = ((col >> 16) & 0xFF) / 255.0f;
                float g = ((col >> 8) & 0xFF) / 255.0f;
                float blue = (col & 0xFF) / 255.0f;
                d->AddRectFilled({b->min.x, b->min.y}, {b->max.x, b->max.y},
                                 ImGui::ColorConvertFloat4ToU32({r, g, blue, 0.15f}));
            }
            // Main box
            d->AddRect({b->min.x-1,b->min.y-1},{b->max.x+1,b->max.y+1},
                       IM_COL32(0,0,0,200),0,0,3);
            d->AddRect(point(b->min), point(b->max), col);
        }
        // Name and distance
        if ((s.names&&!e.geometry_only)||s.distance_text) {
            std::string name = s.names&&!e.geometry_only?e.name:"";
            if(s.distance_text)name+=(!name.empty()?"  ":"")+std::to_string(static_cast<int>(e.range))+" studs";
            const auto size=ImGui::CalcTextSize(name.c_str());
            outlined_text(d,{(b->min.x+b->max.x-size.x)/2,b->min.y-size.y-4},
                          IM_COL32(225,225,235,255),name);
        }
        // Health bar and health number
        if (s.health&&!e.geometry_only) {
            const float h=b->max.y-b->min.y;
            const float f=std::clamp(e.health/e.max_health,0.0f,1.0f);
            // Background
            d->AddRectFilled({b->min.x-7,b->min.y-1},{b->min.x-3,b->max.y+1},
                             IM_COL32(0,0,0,220));
            // Health fill
            d->AddRectFilled({b->min.x-6,b->max.y-h*f},
                             {b->min.x-4,b->max.y},
                             IM_COL32(97,202,142,255));
            // Health number
            if (s.esp_show_health_number) {
                std::string health_text = std::to_string(static_cast<int>(e.health)) + "/" +
                                          std::to_string(static_cast<int>(e.max_health));
                auto health_size = ImGui::CalcTextSize(health_text.c_str());
                d->AddText({b->min.x - health_size.x - 4, (b->min.y + b->max.y - health_size.y) / 2},
                           IM_COL32(255,255,255,255), health_text.c_str());
            }
        }
        // Distance number
        if (s.esp_show_distance_number&&!e.geometry_only) {
            std::string dist_text = std::to_string(static_cast<int>(e.range)) + " studs";
            auto dist_size = ImGui::CalcTextSize(dist_text.c_str());
            d->AddText({(b->min.x+b->max.x)/2 - dist_size.x/2, b->max.y + 2},
                       IM_COL32(255,255,255,255), dist_text.c_str());
        }
        // Tracers
        if (s.tracers)
            d->AddLine({v.x/2,v.y-3},{(b->min.x+b->max.x)/2,b->max.y},col);
        // Skeleton
        if (s.skeleton&&!e.geometry_only)
            for(const auto&[a,bone]:skeleton_links(part(e,"UpperTorso")!=nullptr)){
                const auto* pa=part(e,a);const auto* pb=part(e,bone);if(!pa||!pb)continue;
                const auto sa=project(pa->position,snap.matrix,v),sb=project(pb->position,snap.matrix,v);
                if(sa&&sb)d->AddLine(point(*sa),point(*sb),col,1.4f);
            }
    }
}
}
