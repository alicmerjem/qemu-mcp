#pragma once

#include <string>
#include <cstdint>
#include "json.hpp"

using json = nlohmann::json;

class GdbClient {
public:
    GdbClient() = default;
    ~GdbClient();

    void connect(int port);
    bool is_connected() const;
    void disconnect();

    json get_registers();
    json read_memory(uint64_t addr, uint32_t length);
    json write_memory(uint64_t addr, const std::string& data_hex);
    json set_breakpoint(uint64_t addr);
    json remove_breakpoint(uint64_t addr);
    json cont();
    json step();

private:
    int fd_ = -1;

    void send_packet(const std::string& payload);
    void read_ack();
    std::string read_packet();
    std::string send_and_wait(const std::string& payload);
    json parse_stop_reply(const std::string& reply);
};

std::string to_hex(uint64_t value);