#include "offsets.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace noctra {
namespace offsets {
namespace {

using json = nlohmann::json;
constexpr wchar_t service_host[] = L"offsets.imtheo.lol";
constexpr std::size_t version_limit = 256;
constexpr std::size_t document_limit = 128 * 1024;

struct Values {
    std::uintptr_t fake_global{0x8BCFD50};
    std::uintptr_t real_model{0x1F8};
    std::uintptr_t visual_global{0x8656E40};
    std::uintptr_t descriptor{0x18};
    std::uintptr_t class_name{8};
    std::uintptr_t name_container{0x70};
    std::uintptr_t name{8};
    bool name_direct{false};
    std::uintptr_t children{0x78};
    std::uintptr_t children_end{8};
    std::uintptr_t parent{0x68};
    std::uintptr_t string_length{16};
    std::uintptr_t place{0x188};
    std::uintptr_t user_id{0xC0};
    std::uintptr_t local_player{0x120};
    std::uintptr_t character{0x288};
    std::uintptr_t team{0x2C8};
    std::uintptr_t camera{0x4A8};
    std::uintptr_t camera_position{0xEC};
    std::uintptr_t fov{0x130};
    std::uintptr_t primitive{0x178};
    std::uintptr_t position{0xD4};
    std::uintptr_t size{0x1BC};
    std::uintptr_t rotation{0xB0};
    std::uintptr_t health{0x180};
    std::uintptr_t max_health{0x198};
    std::uintptr_t matrix{0x1B0};
    std::uintptr_t dimensions{0xB10};
};

Values defaults() {
    return {};
}

Values current() {
    return {
        fake_global, real_model, visual_global, descriptor, class_name,
        name_container, name, name_direct, children, children_end, parent, string_length,
        place, user_id, local_player, character, team, camera, camera_position,
        fov, primitive, position, size, rotation, health, max_health, matrix,
        dimensions
    };
}

void apply(const Values& values) {
    fake_global = values.fake_global;
    real_model = values.real_model;
    visual_global = values.visual_global;
    descriptor = values.descriptor;
    class_name = values.class_name;
    name_container = values.name_container;
    name = values.name;
    name_direct = values.name_direct;
    children = values.children;
    children_end = values.children_end;
    parent = values.parent;
    string_length = values.string_length;
    place = values.place;
    user_id = values.user_id;
    local_player = values.local_player;
    character = values.character;
    team = values.team;
    camera = values.camera;
    camera_position = values.camera_position;
    fov = values.fov;
    primitive = values.primitive;
    position = values.position;
    size = values.size;
    rotation = values.rotation;
    health = values.health;
    max_health = values.max_health;
    matrix = values.matrix;
    dimensions = values.dimensions;
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string trim(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool valid_version(std::string_view value) {
    if (value.size() < 9 || value.size() > 96 || !value.starts_with("version-")) return false;
    return std::all_of(value.begin() + 8, value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-';
    });
}

std::uintptr_t number(const json& group, const char* field, const char* label) {
    const auto it = group.find(field);
    require(it != group.end() && it->is_number_unsigned(), label);
    const auto value = it->get<std::uint64_t>();
    require(value <= static_cast<std::uint64_t>(std::numeric_limits<std::uintptr_t>::max()),
            "Offset exceeds pointer width");
    require(value <= 0x100000000ULL, "Offset exceeds safety bound");
    return static_cast<std::uintptr_t>(value);
}

Values parse_values(const json& root, std::string& version) {
    require(root.is_object() && root.contains("Offsets") && root["Offsets"].is_object(),
            "Invalid offsets document");
    require(root.contains("Roblox Version") && root["Roblox Version"].is_string(),
            "Offsets document version missing");
    version = root["Roblox Version"].get<std::string>();
    require(valid_version(version), "Invalid offsets document version");

    const auto& all = root["Offsets"];
    const auto& visual_engine = all.value("VisualEngine", json::object());
    const auto& fake_data_model = all.value("FakeDataModel", json::object());
    const auto& instance = all.value("Instance", json::object());
    const auto& misc = all.value("Misc", json::object());
    const auto& data_model = all.value("DataModel", json::object());
    const auto& workspace = all.value("Workspace", json::object());
    const auto& player = all.value("Player", json::object());
    const auto& camera_group = all.value("Camera", json::object());
    const auto& base_part = all.value("BasePart", json::object());
    const auto& primitive_group = all.value("Primitive", json::object());
    const auto& humanoid = all.value("Humanoid", json::object());

    Values values = defaults();
    values.fake_global = number(fake_data_model, "Pointer", "FakeDataModel.Pointer missing");
    values.real_model = number(fake_data_model, "RealDataModel", "FakeDataModel.RealDataModel missing");
    values.visual_global = number(visual_engine, "Pointer", "VisualEngine.Pointer missing");
    values.matrix = number(visual_engine, "ViewMatrix", "VisualEngine.ViewMatrix missing");
    values.dimensions = number(visual_engine, "Dimensions", "VisualEngine.Dimensions missing");
    values.descriptor = number(instance, "ClassDescriptor", "Instance.ClassDescriptor missing");
    values.class_name = number(instance, "ClassName", "Instance.ClassName missing");
    if (instance.contains("NameContainer")) {
        values.name_container = number(instance, "NameContainer", "Instance.NameContainer invalid");
        values.name = number(instance, "Name", "Instance.Name missing");
        values.name_direct = false;
    } else {
        values.name_container = 0;
        values.name = number(instance, "Name", "Instance.Name missing");
        values.name_direct = true;
    }
    values.children = number(instance, "ChildrenStart", "Instance.ChildrenStart missing");
    values.children_end = number(instance, "ChildrenEnd", "Instance.ChildrenEnd missing");
    values.parent = number(instance, "Parent", "Instance.Parent missing");
    values.string_length = number(misc, "StringLength", "Misc.StringLength missing");
    values.place = number(data_model, "PlaceId", "DataModel.PlaceId missing");
    values.local_player = number(player, "LocalPlayer", "Player.LocalPlayer missing");
    values.character = number(player, "ModelInstance", "Player.ModelInstance missing");
    values.team = number(player, "Team", "Player.Team missing");
    values.camera = number(workspace, "CurrentCamera", "Workspace.CurrentCamera missing");
    values.camera_position = number(camera_group, "Position", "Camera.Position missing");
    values.fov = number(camera_group, "FieldOfView", "Camera.FieldOfView missing");
    values.primitive = number(base_part, "Primitive", "BasePart.Primitive missing");
    values.position = number(primitive_group, "Position", "Primitive.Position missing");
    values.size = number(primitive_group, "Size", "Primitive.Size missing");
    values.rotation = number(primitive_group, "Rotation", "Primitive.Rotation missing");
    values.health = number(humanoid, "Health", "Humanoid.Health missing");
    values.max_health = number(humanoid, "MaxHealth", "Humanoid.MaxHealth missing");

    require(values.fake_global != 0 && values.visual_global != 0 &&
            values.children_end <= 0x1000 && values.string_length <= 0x1000,
            "Invalid required offset value");
    return values;
}

struct InternetHandle {
    HINTERNET value{};
    explicit InternetHandle(HINTERNET handle) : value(handle) {
        require(value != nullptr, "Offset service initialization failed");
    }
    ~InternetHandle() {
        if (value) WinHttpCloseHandle(value);
    }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
};

std::string http_get(const std::wstring& path, std::size_t limit) {
    InternetHandle session(WinHttpOpen(
        L"NoctraOffsets/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    require(WinHttpSetTimeouts(session.value, 3000, 5000, 5000, 5000) != FALSE,
            "Offset service timeout setup failed");
    InternetHandle connection(WinHttpConnect(session.value, service_host, INTERNET_DEFAULT_HTTPS_PORT, 0));
    InternetHandle request(WinHttpOpenRequest(
        connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    require(WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY,
                             &redirect, sizeof(redirect)) != FALSE &&
            WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE,
                             &disabled, sizeof(disabled)) != FALSE,
            "Offset service privacy setup failed");
    require(WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE &&
            WinHttpReceiveResponse(request.value, nullptr) != FALSE,
            "Offset service request failed");

    DWORD status{}, status_size = sizeof(status);
    require(WinHttpQueryHeaders(
                request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                WINHTTP_NO_HEADER_INDEX) != FALSE,
            "Offset service status unavailable");
    if (status != 200) throw std::runtime_error("Offset service HTTP " + std::to_string(status));

    DWORD content_length{}, content_length_size = sizeof(content_length);
    if (WinHttpQueryHeaders(
            request.value, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &content_length, &content_length_size,
            WINHTTP_NO_HEADER_INDEX) != FALSE) {
        require(content_length <= limit, "Offset service response exceeds size limit");
    }

    std::string body;
    std::array<char, 16384> buffer{};
    for (;;) {
        DWORD count{};
        require(WinHttpReadData(request.value, buffer.data(),
                                static_cast<DWORD>(buffer.size()), &count) != FALSE,
                "Offset service read failed");
        if (!count) break;
        require(count <= limit - body.size(), "Offset service response exceeds size limit");
        body.append(buffer.data(), count);
    }
    require(!body.empty(), "Offset service returned an empty response");
    return body;
}

std::string read_text(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "Offset cache cannot be opened");
    input.seekg(0, std::ios::end);
    const auto file_size = input.tellg();
    require(file_size >= 0 && static_cast<std::uint64_t>(file_size) <= limit,
            "Offset cache exceeds size limit");
    input.seekg(0, std::ios::beg);
    std::string text(static_cast<std::size_t>(file_size), '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    require(static_cast<bool>(input) || input.eof(), "Offset cache read failed");
    return text;
}

void write_text_atomically(const std::filesystem::path& path, std::string_view value) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    require(!error, "Offset cache directory cannot be created");
    auto temporary = path;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId());
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        require(static_cast<bool>(output), "Offset cache cannot be written");
        output.write(value.data(), static_cast<std::streamsize>(value.size()));
        output.flush();
        require(static_cast<bool>(output), "Offset cache write failed");
    }
    require(MoveFileExW(temporary.c_str(), path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE,
            "Offset cache commit failed");
}

bool try_load_cache(const std::filesystem::path& json_path,
                    const std::filesystem::path& version_path,
                    std::string_view expected,
                    std::string& loaded_version,
                    std::string& error) {
    try {
        std::string cached_version;
        if (std::filesystem::exists(version_path)) {
            cached_version = trim(read_text(version_path, version_limit));
        }
        const auto body = read_text(json_path, document_limit);
        std::string document_version;
        if (!load_offsets_from_json(body, &document_version, &error)) return false;
        if (!cached_version.empty() && cached_version != document_version) {
            error = "Offset cache version marker does not match offsets.json";
            return false;
        }
        if (document_version != expected) {
            error = "Offset cache is for a different Roblox client build";
            return false;
        }
        loaded_version = document_version;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace

void reset_defaults() {
    apply(defaults());
}

bool load_offsets_from_json(std::string_view json_text, std::string* version,
                            std::string* error) {
    if (version) version->clear();
    if (error) error->clear();
    try {
        require(!json_text.empty() && json_text.size() <= document_limit,
                "Invalid offsets document size");
        const auto root = json::parse(json_text);
        std::string parsed_version;
        const auto values = parse_values(root, parsed_version);
        apply(values);
        if (version) *version = std::move(parsed_version);
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

bool ensure_offsets_for_build(const std::filesystem::path& executable_directory,
                              std::string_view client_build, std::string& loaded_version,
                              std::string& status) {
    loaded_version.clear();
    status.clear();
    const auto json_path = executable_directory / "offsets.json";
    const auto version_path = executable_directory / "offsets.version";
    std::string target = valid_version(client_build) ? std::string(client_build) : std::string{};
    std::string cache_error;

    if (target.empty()) {
        try {
            target = trim(http_get(L"/roblox/version", version_limit));
            require(valid_version(target), "Offset service returned an invalid Roblox version");
        } catch (const std::exception& exception) {
            status = exception.what();
            return false;
        }
    }

    if (try_load_cache(json_path, version_path, target, loaded_version, cache_error)) {
        status = "Using cached offsets for " + loaded_version;
        return true;
    }

    std::string live_version;
    try {
        live_version = trim(http_get(L"/roblox/version", version_limit));
        require(valid_version(live_version), "Offset service returned an invalid Roblox version");
    } catch (const std::exception&) {
        live_version.clear();
    }

    std::wstring endpoint;
    if (target == live_version) {
        endpoint = L"/offsets.json";
    } else {
        endpoint = L"/";
        endpoint.append(target.begin(), target.end());
        endpoint += L"/offsets.json";
    }

    try {
        const auto body = http_get(endpoint, document_limit);
        std::string document_version;
        std::string parse_error;
        require(load_offsets_from_json(body, &document_version, &parse_error),
                parse_error.empty() ? "Offset document validation failed" : parse_error.c_str());
        require(document_version == target,
                "Offset document does not match the detected Roblox client build");

        try {
            write_text_atomically(json_path, body);
            write_text_atomically(version_path, document_version + "\n");
        } catch (const std::exception& cache_exception) {
            loaded_version = document_version;
            status = "Loaded offsets for " + loaded_version +
                     " (cache unavailable: " + cache_exception.what() + ")";
            return true;
        }

        loaded_version = document_version;
        status = "Updated offsets for " + loaded_version;
        return true;
    } catch (const std::exception& exception) {
        status = "Offset update failed: " + std::string(exception.what());
        if (!cache_error.empty()) status += "; cached data rejected: " + cache_error;
        return false;
    }
}

bool load_offsets_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    auto values = current();
    const std::unordered_map<std::string, std::uintptr_t*> fields = {
        {"fake_global", &values.fake_global},
        {"real_model", &values.real_model},
        {"visual_global", &values.visual_global},
        {"descriptor", &values.descriptor},
        {"class_name", &values.class_name},
        {"name_container", &values.name_container},
        {"name", &values.name},
        {"children", &values.children},
        {"children_end", &values.children_end},
        {"parent", &values.parent},
        {"string_length", &values.string_length},
        {"place", &values.place},
        {"user_id", &values.user_id},
        {"local_player", &values.local_player},
        {"character", &values.character},
        {"team", &values.team},
        {"camera", &values.camera},
        {"camera_position", &values.camera_position},
        {"fov", &values.fov},
        {"primitive", &values.primitive},
        {"position", &values.position},
        {"size", &values.size},
        {"rotation", &values.rotation},
        {"health", &values.health},
        {"max_health", &values.max_health},
        {"matrix", &values.matrix},
        {"dimensions", &values.dimensions}
    };

    std::size_t parsed{};
    for (std::string line; std::getline(file, line);) {
        line.erase(std::remove_if(line.begin(), line.end(),
                                  [](unsigned char c) { return std::isspace(c); }),
                   line.end());
        if (line.empty() || line[0] == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const auto field = fields.find(line.substr(0, equals));
        if (field == fields.end()) continue;
        std::istringstream input(line.substr(equals + 1));
        std::uintptr_t value{};
        input >> std::hex >> value;
        if (input.fail() || value > 0x100000000ULL) continue;
        *field->second = value;
        ++parsed;
    }
    if (parsed == 0) return false;
    apply(values);
    return true;
}

} // namespace offsets
} // namespace noctra
