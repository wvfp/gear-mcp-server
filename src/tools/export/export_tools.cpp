#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"
#include "shared/config_parser.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using json = nlohmann::json;

namespace gear_mcp {

static void list_export_presets(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    try {
        json params = json::parse(p_params_json);
        std::string project_path = params.value("project_path", GodotAPI::get_project_path());

        json presets = config_parser::parse_export_presets(project_path);

        json result;
        result["success"] = true;
        result["presets"] = presets;
        r_result = result.dump();
    } catch (const std::exception &e) {
        r_error = std::string("Failed to list export presets: ") + e.what();
    }
}

// ===========================================================================
// export_project — actually runs the export by re-invoking the Godot editor
// binary in headless mode with --export-release. This is the same command
// path the editor's "Export Project" button takes internally.
// ===========================================================================

static void export_project(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    try {
        json params = json::parse(p_params_json);
        std::string preset_name = params.at("preset_name").get<std::string>();
        std::string output_path = params.at("output_path").get<std::string>();
        std::string project_path = params.value("project_path", GodotAPI::get_project_path());
        bool debug = params.value("debug", false); // false → --export-release, true → --export-debug

        if (project_path.empty()) {
            r_error = "Could not determine project path (set project_path param or run from a project)";
            return;
        }

        // Locate the Godot binary. The extension runs inside the editor
        // process, so OS::get_executable_path() returns the path to Godot.
        std::string godot_bin = GodotAPI::get_executable_path();
        if (godot_bin.empty()) {
            r_error = "Could not determine Godot executable path (OS::get_executable_path returned empty)";
            return;
        }

        // Build the command line. We use system() so the user can see the
        // export output in the Godot editor's own stderr (it inherits).
        // The export is synchronous — we block until it finishes.
        const char *export_flag = debug ? "--export-debug" : "--export-release";

        // Quote arguments to handle spaces in paths.
        std::string cmd = "\"" + godot_bin + "\" --headless --path \"" + project_path
                        + "\" " + std::string(export_flag) + " \"" + preset_name + "\" \""
                        + output_path + "\"";

        std::fprintf(stderr, "[Gear MCP] Running export: %s\n", cmd.c_str());
        int rc = std::system(cmd.c_str());
        // system() returns the exit status. On POSIX, the high byte carries
        // the child's exit code when the shell ran successfully.
        int exit_code = rc;
#ifdef _WIN32
        // On Windows, system() returns the value from the shell, which is
        // typically 0 on success and non-zero on failure. We trust it as-is.
#else
        if (WIFEXITED(rc)) exit_code = WEXITSTATUS(rc);
#endif

        bool success = (exit_code == 0);

        // Verify the output file actually exists (some export failures exit 0
        // when the output was already up-to-date; we treat file presence as
        // the source of truth).
        std::string abs_output = output_path;
        if (abs_output.substr(0, 6) == "res://") {
            abs_output = GodotAPI::res_to_absolute(abs_output);
        }
        bool file_exists = std::filesystem::exists(abs_output);
        size_t file_size = file_exists ? std::filesystem::file_size(abs_output) : 0;

        if (!file_exists) {
            r_error = "Export command exited " + std::to_string(exit_code)
                    + " but output file was not produced at: " + output_path;
            return;
        }

        json result;
        result["success"] = success;
        result["preset_name"] = preset_name;
        result["output_path"] = output_path;
        result["project_path"] = project_path;
        result["exit_code"] = exit_code;
        result["file_size_bytes"] = file_size;
        result["export_mode"] = debug ? "debug" : "release";
        r_result = result.dump();
    } catch (const std::exception &e) {
        r_error = std::string("Failed to export project: ") + e.what();
    }
}

} // namespace gear_mcp

void register_export_tools(gear_mcp::ToolRegistry *p_registry) {
    const char *list_export_presets_schema = R"schema({
    "type": "object",
    "properties": {
        "project_path": {"type": "string", "description": "Path to Godot project"}
    }
})schema";

    const char *export_project_schema = R"schema({
    "type": "object",
    "properties": {
        "preset_name":  {"type": "string", "description": "Export preset name (must match a [preset.N] name in export_presets.cfg)"},
        "output_path":  {"type": "string", "description": "Output file path (e.g. ./build/game.exe)"},
        "project_path": {"type": "string", "description": "Project path (defaults to current editor's project)"},
        "debug":        {"type": "boolean","description": "Use --export-debug instead of --export-release", "default": false}
    },
    "required": ["preset_name", "output_path"]
})schema";

    p_registry->register_tool("list_export_presets", "List export presets from export_presets.cfg", list_export_presets_schema, gear_mcp::list_export_presets, false);
    p_registry->register_tool("export_project", "Export the project by re-invoking Godot in headless mode with --export-release/--export-debug", export_project_schema, gear_mcp::export_project, true);
}
