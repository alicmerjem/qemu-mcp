#include "qmp_client.hpp"

#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <cctype>
#include <fstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

QmpClient::QmpClient(std::string socket_path) : socket_path_(std::move(socket_path)) {}

void QmpClient::connect() {
    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
        throw std::runtime_error(std::string("socket() failed: ") + strerror(errno));
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path_.size() >= sizeof(addr.sun_path)) {
        throw std::runtime_error("QMP socket path too long: " + socket_path_);
    }
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    int attempts = 0;
    while (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (++attempts > 40) {
            close(fd_);
            fd_ = -1;
            throw std::runtime_error("Failed to connect to QMP socket " + socket_path_ + ": " + strerror(errno));
        }
        usleep(50 * 1000);
    }

    read_line();

    json caps = send_command({{"execute", "qmp_capabilities"}});
    if (!caps.contains("return")) {
        throw std::runtime_error("QMP capabilities negotiation failed");
    }
}

std::string QmpClient::read_line() {
    std::string line;
    char c;
    while (true) {
        ssize_t n = read(fd_, &c, 1);
        if (n <= 0) {
            throw std::runtime_error("QMP connection closed while reading");
        }
        if (c == '\n') break;
        line.push_back(c);
    }
    return line;
}

void QmpClient::write_line(const std::string& line) {
    std::string out = line + "\n";
    size_t total = 0;
    while (total < out.size()) {
        ssize_t n = write(fd_, out.data() + total, out.size() - total);
        if (n <= 0) {
            throw std::runtime_error("Failed writing to QMP socket");
        }
        total += static_cast<size_t>(n);
    }
}

json QmpClient::send_command(const json& cmd) {
    write_line(cmd.dump());
    while (true) {
        std::string line = read_line();
        if (line.empty()) continue;
        json msg = json::parse(line);
        if (msg.contains("return") || msg.contains("error")) {
            return msg;
        }
    }
}

std::string QmpClient::capture_screen_png_base64(const std::string& ppm_scratch_path) {
    json result = send_command({
        {"execute", "screendump"},
        {"arguments", {{"filename", ppm_scratch_path}}}
    });
    if (result.contains("error")) {
        throw std::runtime_error("screendump failed: " + result["error"].dump());
    }

    PpmImage img = read_ppm(ppm_scratch_path);
    return encode_png_base64(img);
}

PpmImage read_ppm(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("Failed to open PPM file: " + path);
    }

    std::string magic;
    f >> magic;
    if (magic != "P6") {
        throw std::runtime_error("Unsupported PPM format (expected P6): " + magic);
    }

    auto skip_whitespace_and_comments = [&]() {
        int c;
        while ((c = f.peek()) != EOF) {
            if (c == '#') {
                std::string dummy;
                std::getline(f, dummy);
            } else if (isspace(c)) {
                f.get();
            } else {
                break;
            }
        }
    };

    int width = 0, height = 0, maxval = 0;
    skip_whitespace_and_comments();
    f >> width;
    skip_whitespace_and_comments();
    f >> height;
    skip_whitespace_and_comments();
    f >> maxval;
    f.get();

    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid PPM dimensions");
    }

    PpmImage img;
    img.width = width;
    img.height = height;
    img.rgb.resize(static_cast<size_t>(width) * height * 3);
    f.read(reinterpret_cast<char*>(img.rgb.data()), static_cast<std::streamsize>(img.rgb.size()));
    if (!f) {
        throw std::runtime_error("Truncated PPM pixel data");
    }

    return img;
}

namespace {

void png_write_callback(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

}

std::string encode_png_base64(const PpmImage& img) {
    std::vector<uint8_t> png_bytes;
    int ok = stbi_write_png_to_func(png_write_callback, &png_bytes, img.width, img.height, 3, img.rgb.data(), img.width * 3);
    if (!ok) {
        throw std::runtime_error("PNG encoding failed");
    }
    return base64_encode(png_bytes);
}

std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back(table[(n >> 6) & 0x3F]);
        out.push_back(table[n & 0x3F]);
        i += 3;
    }

    size_t remaining = data.size() - i;
    if (remaining == 1) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back(table[(n >> 6) & 0x3F]);
        out.push_back('=');
    }

    return out;
}