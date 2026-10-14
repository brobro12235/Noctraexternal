#pragma once
#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
namespace noctra {
bool valid_avatar_user_id(std::uint64_t id);
bool valid_avatar_username(std::string_view name);
std::uint64_t parse_avatar_identity(std::string_view json,std::string_view username);
bool allowed_avatar_image_url(std::string_view url);
struct ThumbnailReply {bool pending{};std::string image_url;};
ThumbnailReply parse_avatar_reply(std::string_view json,std::uint64_t expected_user);
struct AvatarImage {
    unsigned width{},height{};
    std::vector<std::uint8_t> rgba;
};
AvatarImage decode_avatar_png(std::span<const std::uint8_t> bytes);
struct AvatarResult {
    std::uint64_t user_id{}, revision{};
    std::shared_ptr<const AvatarImage> image;
    std::string error;
    bool loading{};
};
class AvatarLoader {
public:
    AvatarLoader();
    ~AvatarLoader();
    AvatarLoader(const AvatarLoader&)=delete;
    AvatarLoader& operator=(const AvatarLoader&)=delete;
    void request(std::string_view username,bool refresh=false);
    AvatarResult result()const;
private:
    void run(std::stop_token);
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::string requested_;
    std::uint64_t generation_{};
    bool refresh_{};
    AvatarResult result_;
    std::jthread worker_;
};
}
