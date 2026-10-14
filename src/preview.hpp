#pragma once
#include "core.hpp"
namespace noctra {
struct PreviewFace {
    std::array<Vec2,4> points{};
    float depth{}, shade{};
};
struct PreviewNode {std::string name;Vec2 point{};};
struct CharacterPreview {
    std::vector<PreviewFace> faces;
    std::vector<PreviewNode> nodes;
    Bounds bounds{};
    std::size_t part_count{};
    bool r15{};
};
const Entity* preview_local_entity(const Snapshot&, Clock::time_point now);
std::optional<CharacterPreview> build_character_preview(const Entity&,Vec2 viewport,float yaw_degrees);
}
