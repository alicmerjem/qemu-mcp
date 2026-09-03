#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include "json.hpp"

using json = nlohmann::json;

struct McpTool {
    std::string name;
    std::string description;
    json input_schema;
    std::function<json(const json& arguments)> handler;
};

class McpServer {
public:
    McpServer(std::string server_name, std::string server_version);

    void register_tool(McpTool tool);
    void run();

private:
    std::string name_;
    std::string version_;
    std::vector<McpTool> tools_;

    std::optional<json> handle_message(const json& msg);

    json handle_initialize(const json& params);
    json handle_tools_list();
    json handle_tools_call(const json& params);

    static json make_error(const json& id, int code, const std::string& message);
    static json make_result(const json& id, const json& result);
};