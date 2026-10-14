#pragma once
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>
namespace noctra {
struct UpdateManifest {
    std::uint64_t sequence{},size{},expires{};
    std::string version,url,sha256;
};
std::vector<unsigned char> hex_bytes(const std::string&);
std::string bytes_hex(std::span<const unsigned char>);
std::vector<unsigned char> update_digest(std::span<const unsigned char>);
std::string update_file_hash(const std::filesystem::path&);
std::vector<unsigned char> read_update_file(const std::filesystem::path&,std::size_t limit);
UpdateManifest verify_update_manifest(const std::string& envelope,const std::string& public_key,
                                     std::uint64_t current_sequence,std::uint64_t now);
void verify_update_payload(const std::filesystem::path&,const UpdateManifest&);
void replace_update_file(const std::filesystem::path& target,const std::filesystem::path& pending,const std::filesystem::path& backup);
void rollback_update_file(const std::filesystem::path& target,const std::filesystem::path& backup,const std::filesystem::path& rejected);
std::filesystem::path update_executable_path();
// Helper mode returns an exit code; it never starts the game reader or updater.
std::optional<int> run_update_helper();
void acknowledge_update_health();
struct UpdateStatus {
    bool busy{},available{},staged{};
    std::string message{"Checking for updates..."},version;
};
class UpdateService {
public:
    explicit UpdateService(bool enabled);
    ~UpdateService();
    void check();
    void download();
    UpdateStatus status() const;
    bool install(std::string& error);
private:
    void run(std::stop_token);
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    int action_{};
    UpdateStatus status_;
    std::string envelope_;
    std::optional<UpdateManifest> manifest_;
    std::filesystem::path stage_;
    std::jthread worker_;
};
}
