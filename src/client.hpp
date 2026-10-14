#pragma once
#include "core.hpp"
#include <mutex>
#include <thread>

namespace noctra {
class Client {
public:
    Client();
    ~Client();
    Client(const Client&)=delete;
    Client& operator=(const Client&)=delete;
    Snapshot snapshot() const;
private:
    void run(std::stop_token token);
    mutable std::mutex mutex_;
    Snapshot snapshot_;
    std::jthread thread_;
};
std::string sha256_file(const std::filesystem::path& path);
void write_diagnostic(const Snapshot&, const std::filesystem::path&);
}
