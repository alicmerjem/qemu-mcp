#include "event_log.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>

namespace {

const size_t kMaxEvents = 200;

bool connect_socket(const std::string& path, int& fd_out) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        close(fd);
        return false;
    }
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    int attempts = 0;
    while (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (++attempts > 40) {
            close(fd);
            return false;
        }
        usleep(50 * 1000);
    }

    fd_out = fd;
    return true;
}

bool read_line(int fd, std::string& out) {
    out.clear();
    char c;
    while (true) {
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) return false;
        if (c == '\n') return true;
        out.push_back(c);
    }
}

bool write_line(int fd, const std::string& line) {
    std::string out = line + "\n";
    size_t total = 0;
    while (total < out.size()) {
        ssize_t n = write(fd, out.data() + total, out.size() - total);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

}

QmpEventListener::~QmpEventListener() {
    stop();
}

void QmpEventListener::start(const std::string& socket_path) {
    stop();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.clear();
    }

    running_ = true;
    thread_ = std::thread(&QmpEventListener::run_loop, this, socket_path);
}

void QmpEventListener::stop() {
    running_ = false;
    if (fd_ >= 0) {
        shutdown(fd_, SHUT_RDWR);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    fd_ = -1;
}

std::vector<json> QmpEventListener::get_events() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_;
}

void QmpEventListener::run_loop(std::string socket_path) {
    int fd = -1;
    if (!connect_socket(socket_path, fd)) {
        running_ = false;
        return;
    }
    fd_ = fd;

    std::string line;
    if (!read_line(fd_, line)) {
        running_ = false;
        return;
    }

    if (!write_line(fd_, R"({"execute":"qmp_capabilities"})")) {
        running_ = false;
        return;
    }
    if (!read_line(fd_, line)) {
        running_ = false;
        return;
    }

    while (running_) {
        if (!read_line(fd_, line)) {
            break;
        }
        if (line.empty()) continue;

        json msg;
        try {
            msg = json::parse(line);
        } catch (...) {
            continue;
        }

        if (msg.contains("event")) {
            std::lock_guard<std::mutex> lock(mutex_);
            events_.push_back(msg);
            if (events_.size() > kMaxEvents) {
                events_.erase(events_.begin());
            }
        }
    }

    running_ = false;
}