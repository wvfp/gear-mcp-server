#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Handler: query_classes
// ---------------------------------------------------------------------------
static void handle_query_classes(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) {
        r_error = "Invalid JSON parameters.";
        return;
    }

    std::string filter;
    std::string parent;
    int limit = 100;

    if (params.contains("filter") && params["filter"].is_string()) {
        filter = params["filter"].get<std::string>();
    }
    if (params.contains("parent") && params["parent"].is_string()) {
        parent = params["parent"].get<std::string>();
    }
    if (params.contains("limit") && params["limit"].is_number_integer()) {
        limit = params["limit"].get<int>();
    }

    std::vector<std::string> all_classes = GodotAPI::classdb_get_class_list();

    json classes_arr = json::array();
    int count = 0;

    for (const std::string &class_name : all_classes) {
        if (count >= limit) {
            break;
        }

        // Apply filter if specified
        if (!filter.empty()) {
            if (class_name.find(filter) == std::string::npos) {
                continue;
            }
        }

        // Apply parent filter if specified
        if (!parent.empty()) {
            std::string class_parent = GodotAPI::classdb_get_parent_class(class_name);
            if (class_parent != parent) {
                continue;
            }
        }

        classes_arr.push_back(class_name);
        count++;
    }

    json res;
    res["classes"] = classes_arr;
    res["count"] = count;
    res["total_available"] = (int)all_classes.size();
    r_result = res.dump();
}

// ---------------------------------------------------------------------------
// Handler: query_class_info
// ---------------------------------------------------------------------------
static void handle_query_class_info(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) {
        r_error = "Invalid JSON parameters.";
        return;
    }

    if (!params.contains("class_name") || !params["class_name"].is_string()) {
        r_error = "Missing required parameter: class_name";
        return;
    }

    std::string class_name = params["class_name"].get<std::string>();

    bool exists = GodotAPI::classdb_class_exists(class_name);

    json res;
    res["class_name"] = class_name;
    res["exists"] = exists;

    if (exists) {
        std::string parent_class = GodotAPI::classdb_get_parent_class(class_name);
        res["parent_class"] = parent_class.empty() ? nullptr : parent_class;
    } else {
        res["parent_class"] = nullptr;
    }

    r_result = res.dump();
}

// ---------------------------------------------------------------------------
// Handler: inspect_inheritance
// ---------------------------------------------------------------------------
static void handle_inspect_inheritance(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) {
        r_error = "Invalid JSON parameters.";
        return;
    }

    if (!params.contains("class_name") || !params["class_name"].is_string()) {
        r_error = "Missing required parameter: class_name";
        return;
    }

    std::string class_name = params["class_name"].get<std::string>();

    bool exists = GodotAPI::classdb_class_exists(class_name);
    if (!exists) {
        r_error = "Class does not exist: " + class_name;
        return;
    }

    // Walk up the inheritance chain
    json chain = json::array();
    std::string current = class_name;

    while (!current.empty()) {
        chain.push_back(current);
        std::string par = GodotAPI::classdb_get_parent_class(current);
        if (par == current || par.empty()) {
            break;
        }
        current = par;
    }

    json res;
    res["class_name"] = class_name;
    res["inheritance_chain"] = chain;
    res["depth"] = (int)chain.size();
    r_result = res.dump();
}

} // namespace gear_mcp

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
void register_classdb_tools(gear_mcp::ToolRegistry *p_registry) {

    // query_classes
    p_registry->register_tool("query_classes",
        "List all available ClassDB classes with optional filtering.",
        R"({"type":"object","properties":{"filter":{"type":"string","description":"Filter classes by name substring"},"parent":{"type":"string","description":"Filter by parent class"},"limit":{"type":"integer","description":"Max results","default":100}}})",
        gear_mcp::handle_query_classes, /*main_thread=*/true);

    // query_class_info
    p_registry->register_tool("query_class_info",
        "Get detailed information about a specific ClassDB class.",
        R"({"type":"object","properties":{"class_name":{"type":"string","description":"ClassDB class name"}},"required":["class_name"]})",
        gear_mcp::handle_query_class_info, /*main_thread=*/true);

    // inspect_inheritance
    p_registry->register_tool("inspect_inheritance",
        "Query the inheritance chain for a ClassDB class.",
        R"({"type":"object","properties":{"class_name":{"type":"string","description":"ClassDB class name"}},"required":["class_name"]})",
        gear_mcp::handle_inspect_inheritance, /*main_thread=*/true);
}
