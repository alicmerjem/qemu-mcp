#include "mcp_server.hpp"
#include "qemu_instance.hpp"
#include "qmp_client.hpp"
#include "gdb_client.hpp"
#include "event_log.hpp"

#include <fstream>
#include <sstream>

int main() {
    McpServer server("qemu-mcp", "0.1.0");

    static QemuInstance qemu;
    static GdbClient gdb;
    static QmpEventListener event_log;

    auto ensure_gdb = []() {
        if (!qemu.is_alive()) {
            throw std::runtime_error("No running QEMU instance. Call launch_qemu first.");
        }
        if (!gdb.is_connected()) {
            gdb.connect(qemu.gdb_port());
        }
    };

    auto parse_address = [](const json& arguments) -> uint64_t {
        std::string s = arguments.at("address").get<std::string>();
        return std::stoull(s, nullptr, 0);
    };

    server.register_tool({
        "ping",
        "Health check - returns pong. Placeholder until QEMU tools are wired up.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        [](const json&) -> json {
            return {{"status", "pong"}};
        }
    });

    server.register_tool({
        "launch_qemu",
        "Launches QEMU booting an ISO image with the GDB stub, QMP socket, serial logging, and a background QMP event listener all pre-wired. Fails if an instance is already running.",
        {
            {"type", "object"},
            {"properties", {
                {"iso_path", {{"type", "string"}, {"description", "Path to the ISO image to boot (default: image.iso)"}}},
                {"machine_type", {{"type", "string"}, {"description", "QEMU -machine value (optional, uses QEMU default if omitted)"}}},
                {"memory_mb", {{"type", "integer"}, {"description", "RAM in MB (default: 128)"}}},
                {"gdb_port", {{"type", "integer"}, {"description", "TCP port for the GDB stub (default: 1234)"}}},
                {"extra_args", {
                    {"type", "array"},
                    {"items", {{"type", "string"}}},
                    {"description", "Additional raw QEMU flags, appended as-is"}
                }}
            }},
            {"required", json::array()}
        },
        [](const json& arguments) -> json {
            QemuLaunchParams params;
            if (arguments.contains("iso_path")) {
                params.iso_path = arguments.at("iso_path").get<std::string>();
            }
            if (arguments.contains("machine_type")) {
                params.machine_type = arguments.at("machine_type").get<std::string>();
            }
            if (arguments.contains("memory_mb")) {
                params.memory_mb = arguments.at("memory_mb").get<int>();
            }
            if (arguments.contains("gdb_port")) {
                params.gdb_port = arguments.at("gdb_port").get<int>();
            }
            if (arguments.contains("extra_args")) {
                for (const auto& a : arguments.at("extra_args")) {
                    params.extra_args.push_back(a.get<std::string>());
                }
            }

            qemu.launch(params);
            event_log.start(qemu.qmp_events_socket_path());

            return {
                {"status", "launched"},
                {"pid", qemu.pid()},
                {"gdb_port", qemu.gdb_port()},
                {"qmp_socket", qemu.qmp_socket_path()},
                {"serial_log", qemu.serial_log_path()}
            };
        }
    });

    server.register_tool({
        "capture_screen",
        "Captures the current QEMU framebuffer as a PNG image, via QMP screendump. Requires a running instance from launch_qemu.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        [](const json&) -> json {
            if (!qemu.is_alive()) {
                throw std::runtime_error("No running QEMU instance. Call launch_qemu first.");
            }

            QmpClient qmp(qemu.qmp_socket_path());
            qmp.connect();

            std::string ppm_path = qemu.work_dir() + "/screenshot.ppm";
            std::string png_base64 = qmp.capture_screen_png_base64(ppm_path);

            return {
                {"__mcp_content__", json::array({
                    {{"type", "image"}, {"data", png_base64}, {"mimeType", "image/png"}}
                })}
            };
        }
    });

    server.register_tool({
        "get_registers",
        "Reads the current CPU general-purpose registers via the GDB stub (rax-r15, rip, eflags, segment registers). Requires a running instance.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        [ensure_gdb](const json&) -> json {
            ensure_gdb();
            return gdb.get_registers();
        }
    });

    server.register_tool({
        "read_memory",
        "Reads raw memory at a given address via the GDB stub. Returns hex-encoded bytes.",
        {
            {"type", "object"},
            {"properties", {
                {"address", {{"type", "string"}, {"description", "Address as hex (e.g. '0x1000') or decimal string"}}},
                {"length", {{"type", "integer"}, {"description", "Number of bytes to read"}}}
            }},
            {"required", json::array({"address", "length"})}
        },
        [ensure_gdb, parse_address](const json& arguments) -> json {
            ensure_gdb();
            uint64_t addr = parse_address(arguments);
            uint32_t length = arguments.at("length").get<uint32_t>();
            return gdb.read_memory(addr, length);
        }
    });

    server.register_tool({
        "write_memory",
        "Writes raw hex-encoded bytes to memory at a given address via the GDB stub.",
        {
            {"type", "object"},
            {"properties", {
                {"address", {{"type", "string"}, {"description", "Address as hex (e.g. '0x1000') or decimal string"}}},
                {"data_hex", {{"type", "string"}, {"description", "Hex-encoded bytes to write, e.g. 'deadbeef'"}}}
            }},
            {"required", json::array({"address", "data_hex"})}
        },
        [ensure_gdb, parse_address](const json& arguments) -> json {
            ensure_gdb();
            uint64_t addr = parse_address(arguments);
            std::string data_hex = arguments.at("data_hex").get<std::string>();
            return gdb.write_memory(addr, data_hex);
        }
    });

    server.register_tool({
        "set_breakpoint",
        "Sets a software breakpoint at the given address via the GDB stub.",
        {
            {"type", "object"},
            {"properties", {
                {"address", {{"type", "string"}, {"description", "Address as hex (e.g. '0x1000') or decimal string"}}}
            }},
            {"required", json::array({"address"})}
        },
        [ensure_gdb, parse_address](const json& arguments) -> json {
            ensure_gdb();
            uint64_t addr = parse_address(arguments);
            return gdb.set_breakpoint(addr);
        }
    });

    server.register_tool({
        "remove_breakpoint",
        "Removes a previously set software breakpoint at the given address.",
        {
            {"type", "object"},
            {"properties", {
                {"address", {{"type", "string"}, {"description", "Address as hex (e.g. '0x1000') or decimal string"}}}
            }},
            {"required", json::array({"address"})}
        },
        [ensure_gdb, parse_address](const json& arguments) -> json {
            ensure_gdb();
            uint64_t addr = parse_address(arguments);
            return gdb.remove_breakpoint(addr);
        }
    });

    server.register_tool({
        "continue_execution",
        "Resumes CPU execution and blocks until the next stop (breakpoint, exception, or crash). No timeout - if the kernel runs indefinitely without stopping, this call will not return.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        [ensure_gdb](const json&) -> json {
            ensure_gdb();
            return gdb.cont();
        }
    });

    server.register_tool({
        "step",
        "Executes a single CPU instruction and returns the resulting stop reason.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        [ensure_gdb](const json&) -> json {
            ensure_gdb();
            return gdb.step();
        }
    });

    server.register_tool({
        "read_serial_log",
        "Returns serial console output captured since launch. Defaults to the last 4000 bytes.",
        {
            {"type", "object"},
            {"properties", {
                {"tail_bytes", {{"type", "integer"}, {"description", "Number of bytes to return from the end of the log (default: 4000)"}}}
            }}
        },
        [](const json& arguments) -> json {
            if (!qemu.is_alive()) {
                throw std::runtime_error("No running QEMU instance. Call launch_qemu first.");
            }

            std::streamsize tail_bytes = 4000;
            if (arguments.contains("tail_bytes")) {
                tail_bytes = arguments.at("tail_bytes").get<std::streamsize>();
            }

            std::ifstream f(qemu.serial_log_path(), std::ios::binary | std::ios::ate);
            if (!f) {
                throw std::runtime_error("Failed to open serial log: " + qemu.serial_log_path());
            }

            std::streamsize size = f.tellg();
            std::streamsize start = size > tail_bytes ? size - tail_bytes : 0;
            f.seekg(start);

            std::ostringstream oss;
            oss << f.rdbuf();

            return {
                {"total_bytes", size},
                {"returned_bytes", static_cast<std::streamsize>(oss.str().size())},
                {"content", oss.str()}
            };
        }
    });

    server.register_tool({
        "read_event_log",
        "Returns timestamped QMP lifecycle events captured since launch (RESET, SHUTDOWN, GUEST_PANICKED, etc.), most recent up to 200 kept.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        [](const json&) -> json {
            if (!qemu.is_alive()) {
                throw std::runtime_error("No running QEMU instance. Call launch_qemu first.");
            }
            return {{"events", event_log.get_events()}};
        }
    });

    server.run();
    return 0;
}