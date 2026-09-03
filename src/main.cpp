#include "mcp_server.hpp"

int main() {
    McpServer server("qemu-mcp", "0.1.0");

    server.register_tool({
        "ping",
        "Health check — returns pong. Placeholder until QEMU tools are wired up.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        [](const json& /*arguments*/) -> json {
            return {{"status", "pong"}};
        }
    });

    server.run();
    return 0;
}