#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include "json.hpp"

using json = nlohmann::json;

class QmpEventListener {
public:
    ~QmpEventListener();

    void start(const std::string& socket_path);
    void stop();
    std::vector<json> get_events() const;

private:
    std::thread thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::vector<json> events_;
    int fd_ = -1;

    void run_loop(std::string socket_path);
};