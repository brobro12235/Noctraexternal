#pragma once
#include "core.hpp"
#include <string>
namespace noctra {
struct UiState {
    int tab{};
    int sections[5]{};
    char search[96]{};
    char profile[48]{"Main"};
    std::string notice;
    bool quit{}, minimize{};
    float dpi_scale{1};
    float preview_yaw{20};
    std::uintptr_t avatar_texture{};
    unsigned avatar_width{},avatar_height{};
    bool avatar_loading{},avatar_failed{},avatar_refresh{},preview_geometry{};
    std::string preview_status{"Waiting for local character"};
    std::string update_message,update_version;
    bool update_busy{},update_available{},update_staged{},update_check{},update_download{},update_install{},update_confirm{};
    std::string loaded_profile;
    bool confirm_reset{};
    bool show_roblox_clean_confirm{};
};
void initialize_menu_fonts(float dpi_scale);
void apply_theme();
void draw_startup(const std::string& message,const std::string& version,bool busy,bool available,bool staged,bool failed,double elapsed);
void draw_menu(Settings&, const Snapshot&, UiState&, double fps, double uptime);
void draw_overlay(const Settings&, const Snapshot&, Vec2 viewport, bool active, const std::optional<Target>&);
void clean_roblox_data();
}
