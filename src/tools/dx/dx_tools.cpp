#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace gear_mcp {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// parse_error_log — scan a text log file (or the project's .godot/ editor
// log if no path is given) for ERROR / WARNING / SCRIPT ERROR lines and
// return them structured.
// ---------------------------------------------------------------------------

static const char *PARSE_ERROR_LOG_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "log_path": {"type": "string", "description": "Path to the log file; defaults to <project>/.godot/editor/editor.log if omitted"},
        "max_lines": {"type": "integer", "description": "Maximum number of error/warn lines to return", "default": 100},
        "min_level": {"type": "string", "description": "Minimum severity: 'warning' (default) or 'error'", "enum": ["warning", "error"], "default": "warning"}
    }
})schema";

struct ParsedLogEntry {
    std::string level;       // "ERROR" or "WARNING"
    std::string message;     // the text
    int line_no = 0;         // line number in the source log
};

static std::vector<ParsedLogEntry> parse_log_lines(const std::string &p_content) {
    std::vector<ParsedLogEntry> out;
    std::istringstream iss(p_content);
    std::string line;
    int n = 0;
    // Heuristics: Godot log lines look like
    //   ERROR: Condition "x" is true. Returning.
    //   WARNING: ...
    //   SCRIPT ERROR: Parse Error: ...
    //   USER ERROR: ...
    std::regex re(R"(\b(ERROR|WARNING|SCRIPT\s+ERROR|USER\s+ERROR|USER\s+WARNING))\b\s*:?\s*(.*)");
    while (std::getline(iss, line)) {
        n++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch m;
        if (std::regex_search(line, m, re)) {
            ParsedLogEntry e;
            std::string raw = m[1].str();
            std::string normalized = raw;
            for (auto &c : normalized) c = (char)std::tolower((unsigned char)c);
            if (normalized.find("script error") != std::string::npos ||
                normalized.find("user error") != std::string::npos ||
                normalized == "error") {
                e.level = "ERROR";
            } else {
                e.level = "WARNING";
            }
            e.message = m[2].str();
            e.line_no = n;
            out.push_back(e);
        }
    }
    return out;
}

static void handle_parse_error_log(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    int max_lines = params.value("max_lines", 100);
    std::string min_level = params.value("min_level", "warning");
    std::string log_path = params.value("log_path", "");

    if (log_path.empty()) {
        std::string proj = GodotAPI::get_project_path();
        if (!proj.empty()) log_path = proj + "/.godot/editor/editor.log";
    }
    if (log_path.empty()) { r_error = "No log_path provided and no project detected"; return; }
    if (!fs::exists(log_path)) {
        r_error = "Log file not found: " + log_path;
        return;
    }

    std::ifstream f(log_path, std::ios::binary);
    if (!f.is_open()) { r_error = "Cannot open log file: " + log_path; return; }
    std::ostringstream ss; ss << f.rdbuf();
    f.close();

    auto entries = parse_log_lines(ss.str());

    // Filter by min_level
    if (min_level == "error") {
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                      [](const ParsedLogEntry &e) { return e.level != "ERROR"; }),
                       entries.end());
    }

    // Trim to max_lines (keep most recent)
    if ((int)entries.size() > max_lines) {
        entries.erase(entries.begin(), entries.end() - max_lines);
    }

    int err_count = 0, warn_count = 0;
    json arr = json::array();
    for (const auto &e : entries) {
        if (e.level == "ERROR") err_count++;
        else if (e.level == "WARNING") warn_count++;
        arr.push_back({{"level", e.level}, {"message", e.message}, {"line_no", e.line_no}});
    }

    json result = {
        {"success", true},
        {"log_path", log_path},
        {"error_count", err_count},
        {"warning_count", warn_count},
        {"entries", arr}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// get_project_health — composite health check (calls validate_project and
// adds extra diagnostics: missing res:// references, empty .gd files,
// suspicious patterns).
// ---------------------------------------------------------------------------

static const char *GET_PROJECT_HEALTH_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "project_path": {"type": "string", "description": "Godot project path (default: active project)"},
        "include_script_scan": {"type": "boolean", "description": "If true, scan all .gd files for empty bodies and syntax-looking issues", "default": true}
    }
})schema";

static void handle_get_project_health(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string project_path = params.value("project_path", "");
    bool scan_scripts = params.value("include_script_scan", true);
    if (project_path.empty()) project_path = GodotAPI::get_project_path();
    if (project_path.empty()) { r_error = "No project_path provided and no active project detected"; return; }

    std::error_code ec;
    json issues = json::array();
    json stats = json::object();
    stats["scene_count"] = 0;
    stats["script_count"] = 0;
    stats["resource_count"] = 0;
    stats["empty_script_count"] = 0;
    stats["broken_ref_count"] = 0;

    // project.godot
    std::string pg = project_path + "/project.godot";
    bool has_pg = fs::exists(pg, ec);
    if (!has_pg) issues.push_back("project.godot missing at " + pg);

    // Engine version
    std::string engine_version;
    GDExtensionObjectPtr engine = GodotAPI::get_global_singleton("Engine");
    if (engine) engine_version = GodotAPI::call_method_string(engine, "get_version_info");

    if (fs::exists(project_path, ec) && fs::is_directory(project_path, ec)) {
        // Pass 1: collect all resources
        std::vector<fs::path> scene_files, script_files, res_files;
        for (auto &e : fs::recursive_directory_iterator(project_path, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            if (ext == ".tscn" || ext == ".scn") {
                scene_files.push_back(e.path());
                stats["scene_count"] = stats["scene_count"].get<int>() + 1;
            } else if (ext == ".gd" || ext == ".cs") {
                script_files.push_back(e.path());
                stats["script_count"] = stats["script_count"].get<int>() + 1;
            } else if (ext == ".tres" || ext == ".res") {
                res_files.push_back(e.path());
                stats["resource_count"] = stats["resource_count"].get<int>() + 1;
            }
        }

        // Empty scripts
        if (scan_scripts) {
            for (const auto &p : script_files) {
                std::error_code ec2;
                int64_t sz = fs::file_size(p, ec2);
                if (sz == 0) {
                    stats["empty_script_count"] = stats["empty_script_count"].get<int>() + 1;
                    issues.push_back("Empty script: " + p.string());
                }
            }
        }

        // Broken references: scan scene/resource files for res://... patterns
        // and check whether each target exists.
        std::set<std::string> existing_res;
        for (const auto &p : scene_files) existing_res.insert(p.string());
        for (const auto &p : res_files) existing_res.insert(p.string());
        for (const auto &p : script_files) existing_res.insert(p.string());

        auto check_broken_refs = [&](const fs::path &p, const std::string &p_kind) {
            std::ifstream f(p);
            if (!f.is_open()) return;
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            f.close();
            std::regex re(R"(res://[A-Za-z0-9_./\-]+\.[A-Za-z0-9]+)");
            std::smatch m;
            std::string s = content;
            int found = 0;
            while (std::regex_search(s, m, re) && found < 50) {
                std::string ref = m.str();
                // Project_path + relative
                std::string abs = GodotAPI::res_to_absolute(ref);
                if (!fs::exists(abs, ec) && !ref.empty() && ref.back() != '/') {
                    stats["broken_ref_count"] = stats["broken_ref_count"].get<int>() + 1;
                    if (issues.size() < 50) {
                        issues.push_back("Broken reference in " + p_kind + " " + p.filename().string() +
                                         ": " + ref);
                    }
                }
                s = m.suffix().str();
                found++;
            }
        };

        for (const auto &p : scene_files) check_broken_refs(p, "scene");
        for (const auto &p : res_files) check_broken_refs(p, "resource");
    }

    // Build a simple health score: 100 - (issues * 5)
    int health_score = 100 - (int)issues.size() * 5;
    if (health_score < 0) health_score = 0;
    std::string status = (health_score >= 80) ? "good" :
                          (health_score >= 50) ? "fair" : "poor";

    json result = {
        {"success", true},
        {"project_path", project_path},
        {"has_project_godot", has_pg},
        {"engine_version", engine_version},
        {"stats", stats},
        {"health_score", health_score},
        {"status", status},
        {"issue_count", (int)issues.size()},
        {"issues", issues}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// find_resource_usages — search project files for references to a resource
// (by res:// path, UID, or script class name).
// ---------------------------------------------------------------------------

static const char *FIND_RESOURCE_USAGES_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "resource_path": {"type": "string", "description": "res:// path to search for (e.g., res://scripts/player.gd)"},
        "project_path": {"type": "string", "description": "Project root; default: active project"},
        "max_results": {"type": "integer", "description": "Maximum number of matches to return", "default": 50}
    },
    "required": ["resource_path"]
})schema";

static void handle_find_resource_usages(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string resource_path = params.value("resource_path", "");
    if (resource_path.empty()) { r_error = "Missing required parameter: resource_path"; return; }
    std::string project_path = params.value("project_path", "");
    int max_results = params.value("max_results", 50);
    if (max_results < 1) max_results = 1;
    if (max_results > 1000) max_results = 1000;
    if (project_path.empty()) project_path = GodotAPI::get_project_path();
    if (project_path.empty()) { r_error = "No project_path provided and no active project detected"; return; }

    std::error_code ec;
    if (!fs::exists(project_path, ec) || !fs::is_directory(project_path, ec)) {
        r_error = "Project path does not exist or is not a directory: " + project_path;
        return;
    }

    // Build the resource stem to search for (path without res:// prefix)
    std::string stem = resource_path;
    if (stem.substr(0, 6) == "res://") stem = stem.substr(6);

    json matches = json::array();
    int scanned = 0;
    for (auto &e : fs::recursive_directory_iterator(project_path, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        if (ext != ".tscn" && ext != ".scn" && ext != ".tres" && ext != ".res" && ext != ".gd" && ext != ".cs") continue;
        scanned++;

        std::ifstream f(e.path(), std::ios::binary);
        if (!f.is_open()) continue;
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        f.close();

        if (content.find(stem) != std::string::npos) {
            // Find the line number
            int lineno = 1;
            for (char c : content) {
                if (c == '\n') lineno++;
                if (content.find(stem, &c - content.data()) == std::string::npos) break;
            }
            // Find first occurrence line
            size_t pos = content.find(stem);
            lineno = 1;
            for (size_t i = 0; i < pos && i < content.size(); i++) {
                if (content[i] == '\n') lineno++;
            }
            // Extract a small context window
            std::string ctx;
            if (pos != std::string::npos) {
                size_t s = (pos >= 80) ? pos - 80 : 0;
                size_t epos = std::min(content.size(), pos + stem.size() + 80);
                ctx = content.substr(s, epos - s);
                // Strip newlines for compactness
                std::replace(ctx.begin(), ctx.end(), '\n', ' ');
                std::replace(ctx.begin(), ctx.end(), '\r', ' ');
                if (ctx.size() > 160) ctx = ctx.substr(0, 160) + "...";
            }
            matches.push_back({
                {"file", e.path().string()},
                {"line", lineno},
                {"context", ctx}
            });
            if ((int)matches.size() >= max_results) break;
        }
    }

    json result = {
        {"success", true},
        {"resource_path", resource_path},
        {"project_path", project_path},
        {"scanned_files", scanned},
        {"match_count", (int)matches.size()},
        {"matches", matches}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// scaffold_gameplay_prototype — create a minimal "scene + script + signal"
// prototype at the given path. Returns the list of created files.
// ---------------------------------------------------------------------------

static const char *SCAFFOLD_GAMEPLAY_PROTOTYPE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scene_path": {"type": "string", "description": "res:// path for the new scene (e.g., res://scenes/prototype.tscn)"},
        "script_path": {"type": "string", "description": "res:// path for the controller script (e.g., res://scripts/prototype.gd)"},
        "root_node_type": {"type": "string", "description": "Root node class", "default": "Node2D"},
        "name": {"type": "string", "description": "Display name of the scene/node", "default": "Prototype"}
    },
    "required": ["scene_path", "script_path"]
})schema";

static void handle_scaffold_gameplay_prototype(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string scene_path = params.value("scene_path", "");
    std::string script_path = params.value("script_path", "");
    std::string root_type = params.value("root_node_type", "Node2D");
    std::string name = params.value("name", "Prototype");
    if (scene_path.empty() || script_path.empty()) {
        r_error = "Missing required parameters: scene_path, script_path";
        return;
    }
    if (scene_path.substr(0, 6) != "res://") scene_path = "res://" + scene_path;
    if (script_path.substr(0, 6) != "res://") script_path = "res://" + script_path;

    std::string proj = GodotAPI::get_project_path();
    if (proj.empty()) { r_error = "No active project detected"; return; }

    std::string scene_abs = GodotAPI::res_to_absolute(scene_path);
    std::string script_abs = GodotAPI::res_to_absolute(script_path);

    // Path traversal check
    if (path_utils::has_traversal(scene_path) || path_utils::has_traversal(script_path)) {
        r_error = "Path traversal detected";
        return;
    }

    std::error_code ec;
    fs::create_directories(fs::path(scene_abs).parent_path(), ec);
    fs::create_directories(fs::path(script_abs).parent_path(), ec);

    // Write the GDScript first (it doesn't depend on the scene file)
    std::string script_class = fs::path(script_path).stem().string();
    std::ostringstream script_ss;
    script_ss << "extends " << root_type << "\n"
              << "\n"
              << "## " << name << " prototype controller.\n"
              << "## Add this script to the root node of a scene to get started.\n"
              << "\n"
              << "signal ready_for_action\n"
              << "signal prototype_event(payload: Dictionary)\n"
              << "\n"
              << "@export var display_name: String = \"" << name << "\"\n"
              << "\n"
              << "func _ready() -> void:\n"
              << "\tprint(\"[\" + display_name + \"] ready\")\n"
              << "\tready_for_action.emit()\n"
              << "\n"
              << "func do_thing(value: int = 0) -> void:\n"
              << "\tprototype_event.emit({\"action\": \"do_thing\", \"value\": value})\n";
    std::ofstream sf(script_abs, std::ios::trunc | std::ios::binary);
    if (!sf.is_open()) { r_error = "Cannot write script: " + script_abs; return; }
    sf << script_ss.str();
    sf.close();

    // Write a minimal .tscn referencing the script
    // Format:
    //   [gd_scene load_steps=2 format=3 uid="uid://abcd"]
    //   [ext_resource type="Script" path="res://scripts/prototype.gd" id="1_xxx"]
    //   [node name="Prototype" type="Node2D"]
    //   script = ExtResource("1_xxx")
    std::string script_res = script_path; // already res://
    std::ostringstream scene_ss;
    scene_ss << "[gd_scene load_steps=2 format=3]\n"
             << "\n"
             << "[ext_resource type=\"Script\" path=\"" << script_res << "\" id=\"1_script\"]\n"
             << "\n"
             << "[node name=\"" << name << "\" type=\"" << root_type << "\"]\n"
             << "script = ExtResource(\"1_script\")\n";
    std::ofstream tf(scene_abs, std::ios::trunc | std::ios::binary);
    if (!tf.is_open()) { r_error = "Cannot write scene: " + scene_abs; return; }
    tf << scene_ss.str();
    tf.close();

    // Refresh filesystem so the editor sees the new files
    GodotAPI::refresh_filesystem();

    json result = {
        {"success", true},
        {"created", json::array({
            scene_path,
            script_path
        })},
        {"scene_path", scene_path},
        {"script_path", script_path},
        {"root_node_type", root_type},
        {"name", name},
        {"signals", json::array({"ready_for_action", "prototype_event"})}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

void register_dx_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("parse_error_log",
        "Parse a log file (or the editor log) and return structured errors/warnings",
        gear_mcp::PARSE_ERROR_LOG_SCHEMA, gear_mcp::handle_parse_error_log, /*main_thread=*/false);

    p_registry->register_tool("get_project_health",
        "Composite health check: project.godot, scene/script counts, broken references, empty scripts",
        gear_mcp::GET_PROJECT_HEALTH_SCHEMA, gear_mcp::handle_get_project_health, /*main_thread=*/false);

    p_registry->register_tool("find_resource_usages",
        "Search project files for references to a res:// path or script class name",
        gear_mcp::FIND_RESOURCE_USAGES_SCHEMA, gear_mcp::handle_find_resource_usages, /*main_thread=*/false);

    p_registry->register_tool("scaffold_gameplay_prototype",
        "Create a minimal scene + GDScript prototype (with signals) at the given paths",
        gear_mcp::SCAFFOLD_GAMEPLAY_PROTOTYPE_SCHEMA, gear_mcp::handle_scaffold_gameplay_prototype, /*main_thread=*/true);

    std::printf("[Gear MCP] Registered dx tools: parse_error_log, get_project_health, find_resource_usages, scaffold_gameplay_prototype\n");
}
