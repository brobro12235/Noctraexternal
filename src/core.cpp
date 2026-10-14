#include "core.hpp"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <filesystem>
#include <stdexcept>

namespace noctra {
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
float distance(Vec3 a, Vec3 b) { return std::hypot(a.x-b.x, a.y-b.y, a.z-b.z); }
bool valid_address(std::uintptr_t a, std::size_t n) {
    return n > 0 && n <= 65536 && a >= 0x10000 && a < 0x0000800000000000ULL
        && n <= 0x0000800000000000ULL-a;
}
std::optional<std::size_t> child_count(std::uintptr_t start, std::uintptr_t end) {
    if (start == 0 && end == 0) return 0;
    if (end < start || (end-start)%16 || (end-start)/16 > 4096 || !valid_address(start, 16)) return {};
    return static_cast<std::size_t>((end-start)/16);
}
std::optional<Vec2> project(Vec3 p, const Matrix& m, Vec2 v, bool clip) {
    if (!finite(p) || !std::isfinite(v.x) || !std::isfinite(v.y) || v.x<=0 || v.y<=0
        || !std::all_of(m.begin(),m.end(),[](float x){return std::isfinite(x);})) return {};
    const float w=m[12]*p.x+m[13]*p.y+m[14]*p.z+m[15];
    if (!std::isfinite(w) || w <= 0.01f) return {};
    const float x=(m[0]*p.x+m[1]*p.y+m[2]*p.z+m[3])/w;
    const float y=(m[4]*p.x+m[5]*p.y+m[6]*p.z+m[7])/w;
    if (!std::isfinite(x)||!std::isfinite(y) || std::abs(x)>1000 || std::abs(y)>1000
        || (clip && (std::abs(x)>1 || std::abs(y)>1))) return {};
    return Vec2{(x+1)*v.x/2,(1-y)*v.y/2};
}
Vec2 smooth_delta(Vec2 e, float s, float dt) {
    if (!std::isfinite(e.x)||!std::isfinite(e.y)||!std::isfinite(s)||!std::isfinite(dt)||dt<=0) return {};
    const float tau=0.005f+0.004f*std::clamp(s,0.0f,100.0f);
    const float alpha=1-std::exp(-std::min(dt,0.05f)/tau);
    return {e.x*alpha,e.y*alpha};
}
Vec2 aim_delta(Vec2 error,const Settings& settings,float seconds) {
    if(!valid_settings(settings)||!std::isfinite(seconds)||seconds<=0||seconds>0.1f
        ||!std::isfinite(error.x)||!std::isfinite(error.y))return {};
    const float radius=std::hypot(error.x,error.y);
    if(radius<=settings.deadzone)return {};
    const float factor=(radius-settings.deadzone)/radius;
    auto delta=smooth_delta({error.x*factor,error.y*factor},settings.smoothing,seconds);
    delta.x*=settings.mouse_gain;delta.y*=settings.mouse_gain;
    const float magnitude=std::hypot(delta.x,delta.y);
    if(magnitude>settings.max_step){delta.x*=settings.max_step/magnitude;delta.y*=settings.max_step/magnitude;}
    // Prediction: if enabled, extrapolate based on velocity (requires target history, handled elsewhere)
    // For now, we just return delta; the caller will add prediction if they have previous data.
    return delta;
}
std::optional<float> phantom_geometry_range(const std::vector<Part>& proxies,const std::vector<Part>& meshes,Vec3 camera){
    if(proxies.size()!=6||meshes.size()<6||meshes.size()>96||!finite(camera))return {};
    const auto valid=[](const Part& p){
        if(!finite(p.position)||distance(p.position,{})>1e7f||!finite(p.size)||p.size.x<=0||p.size.y<=0||p.size.z<=0)return false;
        for(int row=0;row<3;++row){const auto i=static_cast<std::size_t>(row)*3;const float length=std::hypot(p.rotation[i],p.rotation[i+1],p.rotation[i+2]);if(!std::isfinite(length)||std::abs(length-1)>0.08f)return false;}
        return true;
    };
    Vec3 center{};
    for(const auto& p:proxies){if(!valid(p)||p.size.x>0.02f||p.size.y>0.02f||p.size.z>0.02f)return {};center.x+=p.position.x/6;center.y+=p.position.y/6;center.z+=p.position.z/6;}
    for(const auto& p:proxies)if(distance(p.position,center)>8)return {};
    Vec3 low{std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},high{-low.x,-low.y,-low.z};
    for(const auto& p:meshes){if(!valid(p)||p.size.x>8||p.size.y>8||p.size.z>8||distance(p.position,center)>10)return {};
        for(int mask=0;mask<8;++mask){
            const Vec3 q{p.size.x*((mask&1)?0.5f:-0.5f),p.size.y*((mask&2)?0.5f:-0.5f),p.size.z*((mask&4)?0.5f:-0.5f)};
            const Vec3 point{p.position.x+p.rotation[0]*q.x+p.rotation[1]*q.y+p.rotation[2]*q.z,p.position.y+p.rotation[3]*q.x+p.rotation[4]*q.y+p.rotation[5]*q.z,p.position.z+p.rotation[6]*q.x+p.rotation[7]*q.y+p.rotation[8]*q.z};
            low.x=std::min(low.x,point.x);low.y=std::min(low.y,point.y);low.z=std::min(low.z,point.z);high.x=std::max(high.x,point.x);high.y=std::max(high.y,point.y);high.z=std::max(high.z,point.z);
        }
    }
    const Vec3 extent{high.x-low.x,high.y-low.y,high.z-low.z};
    if(extent.x<0.5f||extent.y<0.5f||extent.z<0.5f||extent.x>16||extent.y>16||extent.z>16)return {};
    const float range=distance(camera,center);if(!std::isfinite(range)||range<8)return {};return range;
}
static const Part* find_part(const Entity& e, const std::string& name) {
    const auto it=std::find_if(e.parts.begin(),e.parts.end(),[&](const Part& p){return p.name==name;});
    return it==e.parts.end()?nullptr:&*it;
}
std::optional<Target> choose_target(const Snapshot& snap, const Settings& cfg, Vec2 viewport,
                                    std::uintptr_t retained_id, std::uintptr_t retained_character,
                                    const Vec2* mousePos) {
    if (!snap.ready || snap.geometry_only || (cfg.team_check&&!snap.teams_verified) || !valid_settings(cfg) || !std::isfinite(viewport.x)||!std::isfinite(viewport.y)||viewport.x<=0||viewport.y<=0) return {};
    std::optional<Target> best, previous;
    Vec2 refPoint;
    if (mousePos) {
        refPoint = *mousePos;
    } else {
        refPoint = {viewport.x/2, viewport.y/2};
    }
    for (const auto& e:snap.entities) {
        if(e.local || e.geometry_only || !std::isfinite(e.health) || e.health<=0 || !std::isfinite(e.range) || e.range>cfg.max_distance
            || (cfg.team_check && (e.neutral || !e.team || e.team==snap.local_team))) continue;
        const Part* preferred=nullptr;
        if(cfg.bone==0) preferred=find_part(e,"Head");
        if(cfg.bone==1) {preferred=find_part(e,"UpperTorso");if(!preferred)preferred=find_part(e,"Torso");}
        if(cfg.bone==2) {preferred=find_part(e,"LowerTorso");if(!preferred)preferred=find_part(e,"Torso");}
        std::optional<Target> entity_target;
        for(const auto& p:e.parts) {
            if(cfg.bone!=3 && &p!=preferred)continue;
            const auto screen=project(p.position,snap.matrix,viewport);
            if(!screen)continue;
            const float score=std::hypot(screen->x-refPoint.x,screen->y-refPoint.y);
            if(score>cfg.fov_pixels)continue;
            if(!entity_target || score<entity_target->score)entity_target=Target{e.id,e.character,*screen,score,{},std::chrono::steady_clock::time_point(),false};
        }
        if(!entity_target)continue;
        // Store current position as previous for next frame (if we have a target)
        // We'll update the Target's previous fields when we have a retained target.
        // For now, we just set the current position; the caller will manage history.
        if(e.id==retained_id&&e.character==retained_character)previous=entity_target;
        if(!best||entity_target->score<best->score)best=entity_target;
    }
    if(previous && (!best || previous->score <= best->score*1.25f+6))return previous;
    return best;
}
std::optional<Bounds> entity_bounds(const Entity& e, const Matrix& m, Vec2 viewport) {
    Bounds b{{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},
             {std::numeric_limits<float>::lowest(),std::numeric_limits<float>::lowest()}};
    std::size_t count=0;
    for(const auto& p:e.parts) {
        if(p.name=="HumanoidRootPart")continue;
        for(int mask=0;mask<8;++mask) {
            Vec3 q{p.size.x*((mask&1)?0.5f:-0.5f),p.size.y*((mask&2)?0.5f:-0.5f),p.size.z*((mask&4)?0.5f:-0.5f)};
            Vec3 world{p.position.x+p.rotation[0]*q.x+p.rotation[1]*q.y+p.rotation[2]*q.z,
                       p.position.y+p.rotation[3]*q.x+p.rotation[4]*q.y+p.rotation[5]*q.z,
                       p.position.z+p.rotation[6]*q.x+p.rotation[7]*q.y+p.rotation[8]*q.z};
            const auto screen=project(world,m,viewport,false);
            if(!screen)return {}; // Avoid giant boxes when a model crosses the near plane.
            b.min.x=std::min(b.min.x,screen->x);b.min.y=std::min(b.min.y,screen->y);
            b.max.x=std::max(b.max.x,screen->x);b.max.y=std::max(b.max.y,screen->y);++count;
        }
    }
    if(!count || b.max.x<0||b.max.y<0||b.min.x>viewport.x||b.min.y>viewport.y)return {};
    return b;
}
std::vector<std::pair<std::string,std::string>> skeleton_links(bool r15) {
    if(!r15)return {{"Head","Torso"},{"Torso","Left Arm"},{"Torso","Right Arm"},{"Torso","Left Leg"},{"Torso","Right Leg"}};
    return {{"Head","UpperTorso"},{"UpperTorso","LowerTorso"},
          {"UpperTorso","LeftUpperArm"},{"LeftUpperArm","LeftLowerArm"},{"LeftLowerArm","LeftHand"},
          {"UpperTorso","RightUpperArm"},{"RightUpperArm","RightLowerArm"},{"RightLowerArm","RightHand"},
          {"LowerTorso","LeftUpperLeg"},{"LeftUpperLeg","LeftLowerLeg"},{"LeftLowerLeg","LeftFoot"},
          {"LowerTorso","RightUpperLeg"},{"RightUpperLeg","RightLowerLeg"},{"RightLowerLeg","RightFoot"}};
}
bool valid_settings(const Settings& s) {
    const auto range=[](float x,float low,float high){return std::isfinite(x)&&x>=low&&x<=high;};
    const bool key=s.aim_key==2||s.aim_key==5||s.aim_key==0xA4||s.aim_key==0xA0;
    const bool menu=s.menu_key==0x2D||s.menu_key==0x24||s.menu_key==0x2E||s.menu_key==0x73;
    return key&&menu&&s.bone>=0&&s.bone<=3&&range(s.smoothing,0,100)&&range(s.fov_pixels,10,1000)
        &&range(s.max_distance,10,10000)&&range(s.opacity,0.4f,1)&&range(s.max_step,1,100)
        &&range(s.mouse_gain,0.1f,5)&&range(s.deadzone,0,20)&&std::all_of(s.color.begin(),s.color.end(),[&](float x){return range(x,0,1);})
        &&range(s.prediction_time,0,2)&&s.predict_enabled>=false&&s.predict_enabled<=true
        &&s.auto_shoot>=false&&s.auto_shoot<=true
        &&s.esp_show_health_number>=false&&s.esp_show_health_number<=true
        &&s.esp_show_distance_number>=false&&s.esp_show_distance_number<=true
        &&s.esp_team_color>=false&&s.esp_team_color<=true
        &&s.esp_outline_box>=false&&s.esp_outline_box<=true
        &&s.esp_filled_box>=false&&s.esp_filled_box<=true
        &&s.dark_mode>=false&&s.dark_mode<=true;
}
std::filesystem::path profile_directory() {
    wchar_t buffer[32768]{};
    const DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,32768);
    if(!length||length>=32768)throw std::runtime_error("LOCALAPPDATA unavailable");
    return std::filesystem::path(buffer)/L"Noctra"/L"profiles";
}
static std::map<std::string,std::string> fields(const Settings& s);

void save_profile(const std::filesystem::path& path,const Settings& s) {
    if(!valid_settings(s))throw std::runtime_error("Invalid settings; nothing saved");
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    auto temporary=path;temporary+=L".tmp";
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot open profile for writing");
     for(const auto& [key,value]:fields(s))out<<key<<'='<<value<<'\n';
     out.flush();if(!out)throw std::runtime_error("Failed writing profile");}
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot atomically replace profile: "+std::to_string(GetLastError()));
}
Settings load_profile(const std::filesystem::path& path) {
    if(std::filesystem::file_size(path)>16384)throw std::runtime_error("Profile exceeds 16 KiB limit");
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read profile");
    std::map<std::string,std::string> data;
    for(std::string line;std::getline(in,line);) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        const auto sep=line.find('=');
        if(sep==std::string::npos||sep==0||!data.emplace(line.substr(0,sep),line.substr(sep+1)).second)
            throw std::runtime_error("Malformed or duplicate profile field");
    }
    if(in.bad())throw std::runtime_error("Profile read failure");
    Settings s;
    const auto schema=data.find("schema");
    if(schema==data.end()||(schema->second!="2"&&schema->second!="3"))
        throw std::runtime_error("Unsupported or incomplete profile schema");
    const bool legacy_schema=schema->second=="2";
    const auto expected_fields=fields(s).size()-(legacy_schema?1u:0u);
    if(data.size()!=expected_fields)throw std::runtime_error("Unsupported or incomplete profile schema");
    const auto get=[&](const char* key,auto& value){
        auto it=data.find(key);if(it==data.end())throw std::runtime_error("Missing profile field");
        std::istringstream stream(it->second);stream>>value;
        if(!stream)throw std::runtime_error("Invalid profile value");
        stream>>std::ws;if(!stream.eof())throw std::runtime_error("Trailing profile value data");
    };
#define GET(field) get(#field,s.field)
    GET(aim);GET(esp);GET(boxes);GET(names);GET(health);GET(skeleton);GET(tracers);GET(distance_text);
    GET(team_check);GET(show_fov);GET(watermark);if(!legacy_schema)GET(dark_mode);
    GET(aim_key);GET(menu_key);GET(bone);GET(smoothing);
    GET(fov_pixels);GET(max_distance);GET(opacity);GET(max_step);GET(mouse_gain);GET(deadzone);
    GET(prediction_time);GET(auto_shoot);GET(esp_show_health_number);GET(esp_show_distance_number);
    GET(esp_team_color);GET(esp_outline_box);GET(esp_filled_box);
#undef GET
    for(int i=0;i<4;i++)get(("color"+std::to_string(i)).c_str(),s.color[static_cast<std::size_t>(i)]);
    if(!valid_settings(s))throw std::runtime_error("Profile setting out of range");
    return s;
}
void clean_roblox_data() {
    try {
        wchar_t buffer[32768];
        DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, 32768);
        if (length == 0 || length >= 32768) {
            throw std::runtime_error("LOCALAPPDATA unavailable");
        }
        std::filesystem::path localAppData(buffer);
        std::filesystem::path robloxPath = localAppData / L"Roblox";

        // Delete logs directory
        std::filesystem::remove_all(robloxPath / L"logs");
        // Delete http cache directory
        std::filesystem::remove_all(robloxPath / L"http");
        // Delete Cookies file
        std::filesystem::remove(robloxPath / L"Cookies");

        // Delete prefetch files related to Roblox
        std::filesystem::path prefetchPath = L"C:\\Windows\\Prefetch";
        for (const auto& entry : std::filesystem::directory_iterator(prefetchPath)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                // Check if filename starts with ROBLOXPLAYERBETA or ROBLOX and ends with .pf
                if ((filename.rfind("ROBLOXPLAYERBETA", 0) == 0 || filename.rfind("ROBLOX", 0) == 0) &&
                    filename.size() > 4 && filename.substr(filename.size() - 3) == ".pf") {
                    std::filesystem::remove(entry.path());
                }
            }
        }
    } catch (const std::exception& e) {
        OutputDebugStringA(("Roblox cleaner error: " + std::string(e.what()) + "\n").c_str());
        throw;
    }
}
static std::map<std::string,std::string> fields(const Settings& s) {
    std::map<std::string,std::string> m{{"schema","3"}};
    const auto put=[&](const char* key,auto value){std::ostringstream out;out<<std::setprecision(9)<<value;m[key]=out.str();};
#define PUT(field) put(#field,s.field)
    PUT(aim);PUT(esp);PUT(boxes);PUT(names);PUT(health);PUT(skeleton);PUT(tracers);PUT(distance_text);
    PUT(team_check);PUT(show_fov);PUT(watermark);PUT(dark_mode);PUT(aim_key);PUT(menu_key);PUT(bone);PUT(smoothing);
    PUT(fov_pixels);PUT(max_distance);PUT(opacity);PUT(max_step);PUT(mouse_gain);PUT(deadzone);
    PUT(prediction_time);PUT(auto_shoot);PUT(esp_show_health_number);PUT(esp_show_distance_number);
    PUT(esp_team_color);PUT(esp_outline_box);PUT(esp_filled_box);
#undef PUT
    for(int i=0;i<4;++i)put(("color"+std::to_string(i)).c_str(),s.color[static_cast<std::size_t>(i)]);
    return m;
}
}
