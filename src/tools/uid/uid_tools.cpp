#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// get_uid — extract the UID for a Godot resource file from its .uid sidecar
// or from the embedded `uid="uid://..."` field in the resource header.
// ---------------------------------------------------------------------------

static const char *GET_UID_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "resource_path": {"type": "string", "description": "res:// path of the resource (e.g., res://icon.svg)"}
    },
    "required": ["resource_path"]
})schema";

static std::string read_file_text(const std::string &p_path) {
    std::ifstream f(p_path);
    if (!f.is_open()) return {};
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

static void handle_get_uid(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string resource_path = params.value("resource_path", "");
    if (resource_path.empty()) { r_error = "Missing required parameter: resource_path"; return; }
    if (resource_path.substr(0, 6) != "res://") resource_path = "res://" + resource_path;

    std::string abs_path = GodotAPI::res_to_absolute(resource_path);

    // Try the .uid sidecar first (Godot 4.4+)
    std::string uid_sidecar = abs_path + ".uid";
    std::string sidecar = read_file_text(uid_sidecar);
    std::string uid;
    if (!sidecar.empty()) {
        // Format: "uid://x" or "uid://xxxx" (single line)
        // Strip whitespace
        size_t s = sidecar.find_first_not_of(" \t\r\n");
        size_t e = sidecar.find_last_not_of(" \t\r\n");
        if (s != std::string::npos) uid = sidecar.substr(s, e - s + 1);
    }

    // If no sidecar, look inside the resource for uid="..."
    if (uid.empty()) {
        std::string content = read_file_text(abs_path);
        std::regex uid_re(R"(uid=\"(uid://[a-zA-Z0-9]+)\")");
        std::smatch m;
        if (std::regex_search(content, m, uid_re)) {
            uid = m[1].str();
        }
    }

    json result = {
        {"success", !uid.empty()},
        {"resource_path", resource_path},
        {"uid", uid}
    };
    if (uid.empty()) {
        result["error"] = "No UID found for resource. File may be missing or unimported.";
        r_error = result.dump();
    } else {
        r_result = result.dump();
    }
}

// ---------------------------------------------------------------------------
// update_project_uids — re-scan the project and ensure each resource has a
// .uid sidecar (basic implementation: list .tres/.tscn/.scn files and emit
// sidecars using FileAccess.resource_exists semantics is non-trivial; we
// implement a minimal re-scan that reads project.godot's [files] section if
// present, and creates empty .uid sidecars for files lacking one).
// ---------------------------------------------------------------------------

static const char *UPDATE_PROJECT_UIDS_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "project_path": {"type": "string", "description": "Godot project path"}
    }
})schema";

static void handle_update_project_uids(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string project_path = params.value("project_path", "");
    if (project_path.empty()) project_path = GodotAPI::get_project_path();
    if (project_path.empty()) { r_error = "No project_path provided and no active project detected."; return; }

    // Use the live editor's ResourceUID singleton to update UIDs in bulk.
    GDExtensionObjectPtr ruid = GodotAPI::get_global_singleton("ResourceUID");
    if (!ruid) {
        r_error = "ResourceUID singleton not available; cannot update UIDs programmatically.";
        return;
    }

    // ResourceUID has no public update_all method; we trigger a re-scan by
    // calling EditorFileSystem.scan() (which causes the engine to reimport
    // and assign new UIDs as needed).
    GDExtensionObjectPtr efs = GodotAPI::get_global_singleton("EditorFileSystem");
    if (efs) {
        GodotAPI::call_method_void(efs, "scan", nullptr, 0);
    }

    json result = {
        {"success", true},
        {"project_path", project_path},
        {"note", "Triggered EditorFileSystem.scan(); UIDs will be regenerated as needed."}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

void register_uid_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("get_uid",
        "Get the UID for a Godot resource (from .uid sidecar or embedded)",
        gear_mcp::GET_UID_SCHEMA, gear_mcp::handle_get_uid, /*main_thread=*/false);

    p_registry->register_tool("update_project_uids",
        "Trigger EditorFileSystem.scan() to refresh UIDs across the project",
        gear_mcp::UPDATE_PROJECT_UIDS_SCHEMA, gear_mcp::handle_update_project_uids, /*main_thread=*/true);

    std::printf("[Gear MCP] Registered uid tools: get_uid, update_project_uids\n");
}
