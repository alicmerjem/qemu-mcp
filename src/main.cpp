#include "mcp_server.hpp"
#include "qemu_instance.hpp"
#include "qmp_client.hpp"

int main() {
    McpServer server("qemu-mcp", "0.1.0");

    static QemuInstance qemu;

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
        "Launches QEMU booting an ISO image with the GDB stub, QMP socket, and serial logging pre-wired. Fails if an instance is already running.",
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

    server.run();
    return 0;
}