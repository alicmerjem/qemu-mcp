#include "mcp_server.hpp"
#include <iostream>
#include <sstream>

McpServer::McpServer(std::string server_name, std::string server_version)
    : name_(std::move(server_name)), version_(std::move(server_version)) {}

void McpServer::register_tool(McpTool tool) {
    tools_.push_back(std::move(tool));
}

json McpServer::make_error(const json& id, int code, const std::string& message) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {{"code", code}, {"message", message}}}
    };
}

json McpServer::make_result(const json& id, const json& result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

json McpServer::handle_initialize(const json& /*params*/) {
    return {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {{"tools", json::object()}}},
        {"serverInfo", {{"name", name_}, {"version", version_}}}
    };
}

json McpServer::handle_tools_list() {
    json tools_json = json::array();
    for (const auto& tool : tools_) {
        tools_json.push_back({
            {"name", tool.name},
            {"description", tool.description},
            {"inputSchema", tool.input_schema}
        });
    }
    return {{"tools", tools_json}};
}

json McpServer::handle_tools_call(const json& params) {
    const std::string tool_name = params.at("name").get<std::string>();
    const json arguments = params.contains("arguments") ? params.at("arguments") : json::object();

    for (const auto& tool : tools_) {
        if (tool.name == tool_name) {
            try {
                json result = tool.handler(arguments);
                return {
                    {"content", json::array({
                        {{"type", "text"}, {"text", result.dump()}}
                    })},
                    {"isError", false}
                };
            } catch (const std::exception& e) {
                return {
                    {"content", json::array({
                        {{"type", "text"}, {"text", std::string("Tool error: ") + e.what()}}
                    })},
                    {"isError", true}
                };
            }
        }
    }

    return {
        {"content", json::array({
            {{"type", "text"}, {"text", "Unknown tool: " + tool_name}}
        })},
        {"isError", true}
    };
}

std::optional<json> McpServer::handle_message(const json& msg) {
    // Notifications have no "id" and expect no response.
    const bool is_notification = !msg.contains("id");
    const std::string method = msg.value("method", "");
    const json params = msg.value("params", json::object());
    const json id = is_notification ? json(nullptr) : msg.at("id");

    if (method == "initialize") {
        return make_result(id, handle_initialize(params));
    }
    if (method == "notifications/initialized") {
        return std::nullopt; // no response required
    }
    if (method == "tools/list") {
        return make_result(id, handle_tools_list());
    }
    if (method == "tools/call") {
        return make_result(id, handle_tools_call(params));
    }

    if (is_notification) {
        return std::nullopt;
    }
    return make_error(id, -32601, "Method not found: " + method);
}

void McpServer::run() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        json msg;
        try {
            msg = json::parse(line);
        } catch (const std::exception& e) {
            json err = make_error(nullptr, -32700, std::string("Parse error: ") + e.what());
            std::cout << err.dump() << std::endl;
            continue;
        }

        auto response = handle_message(msg);
        if (response.has_value()) {
            std::cout << response->dump() << std::endl;
            std::cout.flush();
        }
    }
}