#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace noctra {
namespace offsets {

// Runtime-resolved offsets. Values are replaced only after a complete,
// schema-validated offsets document has been parsed.
inline std::uintptr_t fake_global = 0x8BCFD50;
inline std::uintptr_t real_model = 0x1F8;
inline std::uintptr_t visual_global = 0x8656E40;
inline std::uintptr_t descriptor = 0x18;
inline std::uintptr_t class_name = 8;
inline std::uintptr_t name_container = 0x70;
inline std::uintptr_t name = 8;
inline bool name_direct = false;
inline std::uintptr_t children = 0x78;
inline std::uintptr_t children_end = 8;
inline std::uintptr_t parent = 0x68;
inline std::uintptr_t string_length = 16;
inline std::uintptr_t place = 0x188;
inline std::uintptr_t user_id = 0xC0;
inline std::uintptr_t local_player = 0x120;
inline std::uintptr_t character = 0x288;
inline std::uintptr_t team = 0x2C8;
inline std::uintptr_t camera = 0x4A8;
inline std::uintptr_t camera_position = 0xEC;
inline std::uintptr_t fov = 0x130;
inline std::uintptr_t primitive = 0x178;
inline std::uintptr_t position = 0xD4;
inline std::uintptr_t size = 0x1BC;
inline std::uintptr_t rotation = 0xB0;
inline std::uintptr_t health = 0x180;
inline std::uintptr_t max_health = 0x198;
inline std::uintptr_t matrix = 0x1B0;
inline std::uintptr_t dimensions = 0xB10;

void reset_defaults();

// Parses the theo offsets.json schema and applies the complete required
// subset atomically. The returned version is the document's Roblox Version.
bool load_offsets_from_json(std::string_view json_text, std::string* version = nullptr,
                            std::string* error = nullptr);

// Fetches offsets for the detected client build, using the pinned version
// path when available and offsets.json/offsets.version as a validated cache.
bool ensure_offsets_for_build(const std::filesystem::path& executable_directory,
                              std::string_view client_build, std::string& loaded_version,
                              std::string& status);

// Legacy local text-file support: each line is "name=value" in hexadecimal.
bool load_offsets_from_file(const std::string& path);

} // namespace offsets
} // namespace noctra
