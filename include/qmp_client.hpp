#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "json.hpp"

using json = nlohmann::json;

struct PpmImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;
};

class QmpClient {
public:
    explicit QmpClient(std::string socket_path);

    void connect();
    std::string capture_screen_png_base64(const std::string& ppm_scratch_path);

private:
    std::string socket_path_;
    int fd_ = -1;

    json send_command(const json& cmd);
    std::string read_line();
    void write_line(const std::string& line);
};

PpmImage read_ppm(const std::string& path);
std::string encode_png_base64(const PpmImage& img);
std::string base64_encode(const std::vector<uint8_t>& data);