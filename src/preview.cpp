#include "preview.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace noctra {
const Entity* preview_local_entity(const Snapshot& snapshot,Clock::time_point now) {
    const auto age=std::chrono::duration<double>(now-snapshot.captured).count();
    if(!snapshot.ready||age<0||age>0.25)return nullptr;
    for(const auto& e:snapshot.entities)if(e.local&&e.character&&!e.parts.empty())return &e;
    return nullptr;
}
std::optional<CharacterPreview> build_character_preview(const Entity& entity,Vec2 viewport,float yaw) {
    if(!std::isfinite(viewport.x)||!std::isfinite(viewport.y)||viewport.x<80||viewport.y<80
        ||!std::isfinite(yaw)||entity.parts.empty()||entity.parts.size()>32)return {};
    const Part* root=nullptr;
    for(const auto& p:entity.parts)if(p.name=="HumanoidRootPart")root=&p;
    if(!root)for(const auto& p:entity.parts)if(p.name=="Torso"||p.name=="LowerTorso")root=&p;
    if(!root)return {};
    const auto rotation_valid=[](const Part& p){
        if(!finite(p.position)||!finite(p.size)||p.size.x<=0||p.size.y<=0||p.size.z<=0
            ||p.size.x>1000||p.size.y>1000||p.size.z>1000)return false;
        for(int row=0;row<3;++row){const auto i=static_cast<std::size_t>(row)*3;
            const float length=std::hypot(p.rotation[i],p.rotation[i+1],p.rotation[i+2]);
            if(!std::isfinite(length)||std::abs(length-1)>0.08f)return false;}
        return true;
    };
    if(!rotation_valid(*root))return {};
    const float angle=yaw*0.01745329252f,cs=std::cos(angle),sn=std::sin(angle);
    const auto view=[&](Vec3 world){
        const Vec3 d{world.x-root->position.x,world.y-root->position.y,world.z-root->position.z};
        // Inverse rigid rotation puts the pose into the root's own coordinate frame.
        const Vec3 local{root->rotation[0]*d.x+root->rotation[3]*d.y+root->rotation[6]*d.z,
                         root->rotation[1]*d.x+root->rotation[4]*d.y+root->rotation[7]*d.z,
                         root->rotation[2]*d.x+root->rotation[5]*d.y+root->rotation[8]*d.z};
        const float x=cs*local.x+sn*local.z,z=-sn*local.x+cs*local.z;
        // A small fixed pitch gives depth without changing pose proportions.
        return Vec3{x,0.985f*local.y-0.174f*z,0.174f*local.y+0.985f*z};
    };
    struct Box {std::string name;std::array<Vec3,8> vertices;Vec3 center;};
    std::vector<Box> boxes;bool r15=false;
    Vec2 low{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},
         high{std::numeric_limits<float>::lowest(),std::numeric_limits<float>::lowest()};
    const bool has_r15=std::any_of(entity.parts.begin(),entity.parts.end(),[](const Part& p){return p.name=="UpperTorso";});
    for(const auto& p:entity.parts){
        if(p.name=="HumanoidRootPart")continue;
        if(has_r15&&(p.name=="Torso"||p.name=="Left Arm"||p.name=="Right Arm"||p.name=="Left Leg"||p.name=="Right Leg"))continue;
        if(!rotation_valid(p)||distance(p.position,root->position)>1000)return {};
        Box b;b.name=p.name;b.center=view(p.position);r15|=p.name=="UpperTorso";
        for(int i=0;i<8;++i){const Vec3 q{p.size.x*((i&1)?0.5f:-0.5f),p.size.y*((i&2)?0.5f:-0.5f),p.size.z*((i&4)?0.5f:-0.5f)};
            const Vec3 world{p.position.x+p.rotation[0]*q.x+p.rotation[1]*q.y+p.rotation[2]*q.z,
                p.position.y+p.rotation[3]*q.x+p.rotation[4]*q.y+p.rotation[5]*q.z,
                p.position.z+p.rotation[6]*q.x+p.rotation[7]*q.y+p.rotation[8]*q.z};
            b.vertices[static_cast<std::size_t>(i)]=view(world);const auto v=b.vertices[static_cast<std::size_t>(i)];
            if(!finite(v))return {};
            low.x=std::min(low.x,v.x);low.y=std::min(low.y,v.y);high.x=std::max(high.x,v.x);high.y=std::max(high.y,v.y);
        }boxes.push_back(std::move(b));
    }
    if(boxes.empty()||high.x-low.x<0.01f||high.y-low.y<0.01f)return {};
    const float padding=std::max(16.0f,std::min(viewport.x,viewport.y)*0.13f);
    const float fit=std::min((viewport.x-2*padding)/(high.x-low.x),(viewport.y-2*padding)/(high.y-low.y));
    const Vec2 middle{(low.x+high.x)/2,(low.y+high.y)/2};
    const auto screen=[&](Vec3 v){return Vec2{viewport.x/2+(v.x-middle.x)*fit,viewport.y/2-(v.y-middle.y)*fit};};
    CharacterPreview result;result.r15=r15;result.part_count=boxes.size();
    result.bounds={screen({low.x,high.y,0}),screen({high.x,low.y,0})};
    // Winding is normalized before handing polygons to ImGui's convex fill.
    constexpr int indices[6][4]={{0,2,3,1},{4,5,7,6},{0,1,5,4},{2,6,7,3},{0,4,6,2},{1,3,7,5}};
    for(const auto& b:boxes){result.nodes.push_back({b.name,screen(b.center)});
        for(int face=0;face<6;++face){PreviewFace f;f.shade=0.62f+static_cast<float>(face)*0.035f;
            for(int i=0;i<4;++i){const auto vertex=b.vertices[static_cast<std::size_t>(indices[face][i])];f.points[static_cast<std::size_t>(i)]=screen(vertex);f.depth+=vertex.z/4;}
            float area=0;for(int i=0;i<4;++i){const auto a=f.points[static_cast<std::size_t>(i)],next=f.points[static_cast<std::size_t>((i+1)%4)];area+=a.x*next.y-next.x*a.y;}
            if(std::abs(area)<0.001f)continue;
            if(area<0)std::reverse(f.points.begin(),f.points.end());result.faces.push_back(f);
        }
    }
    std::stable_sort(result.faces.begin(),result.faces.end(),[](const PreviewFace& a,const PreviewFace& b){return a.depth<b.depth;});
    return result;
}
}
