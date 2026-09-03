#include "gdb_client.hpp"

#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace {

struct RegSpec {
    const char* name;
    int offset;
    int size;
};

const RegSpec kRegisters[] = {
    {"rax", 0, 8}, {"rbx", 8, 8}, {"rcx", 16, 8}, {"rdx", 24, 8},
    {"rsi", 32, 8}, {"rdi", 40, 8}, {"rbp", 48, 8}, {"rsp", 56, 8},
    {"r8", 64, 8}, {"r9", 72, 8}, {"r10", 80, 8}, {"r11", 88, 8},
    {"r12", 96, 8}, {"r13", 104, 8}, {"r14", 112, 8}, {"r15", 120, 8},
    {"rip", 128, 8}, {"eflags", 136, 4},
    {"cs", 140, 4}, {"ss", 144, 4}, {"ds", 148, 4},
    {"es", 152, 4}, {"fs", 156, 4}, {"gs", 160, 4}
};

uint64_t parse_hex_le(const std::string& hex, int offset_bytes, int size_bytes) {
    uint64_t value = 0;
    for (int i = 0; i < size_bytes; ++i) {
        int byte_offset = offset_bytes + i;
        std::string byte_str = hex.substr(static_cast<size_t>(byte_offset) * 2, 2);
        uint8_t byte_val = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        value |= static_cast<uint64_t>(byte_val) << (8 * i);
    }
    return value;
}

}

std::string to_hex(uint64_t value) {
    std::ostringstream oss;
    oss << std::hex << value;
    return oss.str();
}

GdbClient::~GdbClient() {
    disconnect();
}

void GdbClient::disconnect() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

bool GdbClient::is_connected() const {
    return fd_ >= 0;
}

void GdbClient::connect(int port) {
    disconnect();

    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        throw std::runtime_error(std::string("socket() failed: ") + strerror(errno));
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int attempts = 0;
    while (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (++attempts > 40) {
            close(fd_);
            fd_ = -1;
            throw std::runtime_error(std::string("Failed to connect to GDB stub on port ") + std::to_string(port) + ": " + strerror(errno));
        }
        usleep(50 * 1000);
    }
}

void GdbClient::send_packet(const std::string& payload) {
    unsigned int sum = 0;
    for (char c : payload) {
        sum += static_cast<unsigned char>(c);
    }
    std::ostringstream oss;
    oss << '$' << payload << '#' << std::hex << std::setw(2) << std::setfill('0') << (sum & 0xff);
    std::string packet = oss.str();

    size_t total = 0;
    while (total < packet.size()) {
        ssize_t n = write(fd_, packet.data() + total, packet.size() - total);
        if (n <= 0) {
            throw std::runtime_error("Failed writing to GDB stub socket");
        }
        total += static_cast<size_t>(n);
    }
}

void GdbClient::read_ack() {
    char c;
    ssize_t n = read(fd_, &c, 1);
    if (n <= 0) {
        throw std::runtime_error("GDB stub connection closed while waiting for ack");
    }
    if (c != '+') {
        throw std::runtime_error(std::string("Expected ack '+', got '") + c + "'");
    }
}

std::string GdbClient::read_packet() {
    char c;
    ssize_t n = read(fd_, &c, 1);
    if (n <= 0) {
        throw std::runtime_error("GDB stub connection closed while reading packet");
    }
    while (c != '$') {
        n = read(fd_, &c, 1);
        if (n <= 0) {
            throw std::runtime_error("GDB stub connection closed while reading packet");
        }
    }

    std::string payload;
    while (true) {
        n = read(fd_, &c, 1);
        if (n <= 0) {
            throw std::runtime_error("GDB stub connection closed while reading packet");
        }
        if (c == '#') break;
        payload.push_back(c);
    }

    char checksum[2];
    n = read(fd_, checksum, 2);
    if (n != 2) {
        throw std::runtime_error("GDB stub connection closed while reading checksum");
    }

    char ack = '+';
    write(fd_, &ack, 1);

    return payload;
}

std::string GdbClient::send_and_wait(const std::string& payload) {
    send_packet(payload);
    read_ack();
    return read_packet();
}

json GdbClient::get_registers() {
    std::string reply = send_and_wait("g");
    if (reply.size() < 164 * 2) {
        throw std::runtime_error("Unexpected register reply length: " + std::to_string(reply.size()));
    }

    json regs = json::object();
    for (const auto& reg : kRegisters) {
        uint64_t value = parse_hex_le(reply, reg.offset, reg.size);
        std::ostringstream oss;
        oss << "0x" << std::hex << value;
        regs[reg.name] = oss.str();
    }
    return regs;
}

json GdbClient::read_memory(uint64_t addr, uint32_t length) {
    std::string cmd = "m" + to_hex(addr) + "," + to_hex(length);
    std::string reply = send_and_wait(cmd);
    if (!reply.empty() && reply[0] == 'E') {
        throw std::runtime_error("read_memory failed: " + reply);
    }
    return {
        {"address", "0x" + to_hex(addr)},
        {"length", length},
        {"data_hex", reply}
    };
}

json GdbClient::write_memory(uint64_t addr, const std::string& data_hex) {
    uint32_t length = static_cast<uint32_t>(data_hex.size() / 2);
    std::string cmd = "M" + to_hex(addr) + "," + to_hex(length) + ":" + data_hex;
    std::string reply = send_and_wait(cmd);
    if (reply != "OK") {
        throw std::runtime_error("write_memory failed: " + reply);
    }
    return {
        {"address", "0x" + to_hex(addr)},
        {"length", length},
        {"status", "written"}
    };
}

json GdbClient::set_breakpoint(uint64_t addr) {
    std::string cmd = "Z0," + to_hex(addr) + ",1";
    std::string reply = send_and_wait(cmd);
    if (reply != "OK") {
        throw std::runtime_error("set_breakpoint failed: " + reply);
    }
    return {
        {"address", "0x" + to_hex(addr)},
        {"status", "set"}
    };
}

json GdbClient::remove_breakpoint(uint64_t addr) {
    std::string cmd = "z0," + to_hex(addr) + ",1";
    std::string reply = send_and_wait(cmd);
    if (reply != "OK") {
        throw std::runtime_error("remove_breakpoint failed: " + reply);
    }
    return {
        {"address", "0x" + to_hex(addr)},
        {"status", "removed"}
    };
}

json GdbClient::parse_stop_reply(const std::string& reply) {
    if (reply.empty()) {
        return {{"raw", reply}, {"stop_reason", "unknown"}};
    }
    char kind = reply[0];
    if (kind == 'S' || kind == 'T') {
        int signal = 0;
        if (reply.size() >= 3) {
            signal = std::stoi(reply.substr(1, 2), nullptr, 16);
        }
        return {
            {"raw", reply},
            {"stop_reason", "signal"},
            {"signal", signal}
        };
    }
    if (kind == 'W') {
        return {{"raw", reply}, {"stop_reason", "exited"}};
    }
    if (kind == 'X') {
        return {{"raw", reply}, {"stop_reason", "terminated"}};
    }
    return {{"raw", reply}, {"stop_reason", "unknown"}};
}

json GdbClient::cont() {
    std::string reply = send_and_wait("c");
    return parse_stop_reply(reply);
}

json GdbClient::step() {
    std::string reply = send_and_wait("s");
    return parse_stop_reply(reply);
}