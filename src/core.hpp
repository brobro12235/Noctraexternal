#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace noctra {
using Clock = std::chrono::steady_clock;
struct Vec2 { float x{}, y{}; };
struct Vec3 { float x{}, y{}, z{}; };
using Matrix = std::array<float, 16>;
bool finite(Vec3 v);
float distance(Vec3 a, Vec3 b);
std::optional<Vec2> project(Vec3 p, const Matrix& matrix, Vec2 viewport, bool clip = true);
Vec2 smooth_delta(Vec2 error, float smoothness, float seconds);
Vec2 aim_delta(Vec2 error, const struct Settings& settings, float seconds);
bool valid_address(std::uintptr_t address, std::size_t bytes);
std::optional<std::size_t> child_count(std::uintptr_t start, std::uintptr_t end);
struct Settings {
    bool aim{}, esp{}, boxes{true}, names{true}, health{true}, skeleton{}, tracers{},
         distance_text{true}, team_check{true}, show_fov{true}, watermark{true};
    int aim_key{2}, menu_key{0x2D}, bone{};
    float smoothing{68}, fov_pixels{140}, max_distance{1000}, opacity{0.98f},
          max_step{35}, mouse_gain{1}, deadzone{2};
    std::array<float, 4> color{0.61f, 0.53f, 0.96f, 1};
    // New settings
    bool predict_enabled{false};
    float prediction_time{0.05f};
    bool auto_shoot{false};
    bool esp_show_health_number{false};
    bool esp_show_distance_number{false};
    bool esp_team_color{false};
    bool esp_outline_box{true};
    bool esp_filled_box{false};
};
bool valid_settings(const Settings& settings);
std::filesystem::path profile_directory();
void save_profile(const std::filesystem::path& path, const Settings& settings);
Settings load_profile(const std::filesystem::path& path);
void clean_roblox_data();

struct Part { std::string name; Vec3 position{}, size{}; std::array<float, 9> rotation{}; };
struct Entity {
    std::uintptr_t id{}, character{}, team{};
    std::string name;
    float health{}, max_health{}, range{};
    bool local{}, neutral{true}, geometry_only{};
    std::vector<Part> parts;
};
struct Snapshot {
    Clock::time_point captured{};
    std::string status{"Waiting for Roblox"}, build, hash, offsets_version;
    std::uint64_t place{};
    std::uint32_t pid{};
    std::uintptr_t window{}, local_team{};
    Vec3 camera{};
    Vec2 dimensions{};
    Matrix matrix{};
    bool compatible{}, ready{}, teams_verified{}, geometry_only{};
    double read_ms{};
    std::size_t geometry_candidates{}, geometry_rejected{};
    std::vector<Entity> entities;
};
// PF replicated models are anonymous visual geometry, never aim/player identity.
std::optional<float> phantom_geometry_range(const std::vector<Part>& proxies,
                                          const std::vector<Part>& meshes, Vec3 camera);
struct Target {
    std::uintptr_t id{}, character{};
    Vec2 position{};
    Vec2 previous_position{};
    Clock::time_point previous_time{};
    bool has_previous{};
    float score{};
};
std::optional<Target> choose_target(const Snapshot& snap, const Settings& cfg, Vec2 viewport,
                                  std::uintptr_t retained_id = 0, std::uintptr_t retained_character = 0,
                                  const Vec2* mousePos = nullptr);
struct Bounds { Vec2 min{}, max{}; };
std::optional<Bounds> entity_bounds(const Entity&, const Matrix&, Vec2 viewport);
std::vector<std::pair<std::string, std::string>> skeleton_links(bool r15);
}
