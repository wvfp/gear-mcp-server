#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/config_parser.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string resolve_project_path(const std::string &p_path) {
	if (p_path.empty()) {
		return GodotAPI::get_project_path();
	}
	return p_path;
}

// ---------------------------------------------------------------------------
// Tool: add_autoload
// ---------------------------------------------------------------------------

static const char *ADD_AUTOLOAD_SCHEMA = R"({
	"type": "object",
	"properties": {
		"project_path": {
			"type": "string"
		},
		"name": {
			"type": "string",
			"description": "Autoload singleton name"
		},
		"path": {
			"type": "string",
			"description": "res:// path to script/scene"
		}
	},
	"required": ["name", "path"]
})";

static void handle_add_autoload(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
	json params = json::parse(p_params_json, nullptr, false);
	if (params.is_discarded()) {
		r_error = "Invalid JSON parameters.";
		return;
	}

	std::string project_path = resolve_project_path(params.value("project_path", ""));
	if (project_path.empty()) {
		r_error = "No project path provided and no active Godot project detected.";
		return;
	}

	std::string name = params.value("name", "");
	std::string path = params.value("path", "");

	if (name.empty()) {
		r_error = "Parameter 'name' is required.";
		return;
	}
	if (path.empty()) {
		r_error = "Parameter 'path' is required.";
		return;
	}

	bool ok = config_parser::add_autoload(project_path, name, path);

	json result;
	result["success"] = ok;
	if (!ok) {
		result["error"] = "Failed to add autoload. Check that the project.godot file is writable and the name is valid.";
	} else {
		result["name"] = name;
		result["path"] = path;
	}
	r_result = result.dump();
}

// ---------------------------------------------------------------------------
// Tool: remove_autoload
// ---------------------------------------------------------------------------

static const char *REMOVE_AUTOLOAD_SCHEMA = R"({
	"type": "object",
	"properties": {
		"project_path": {
			"type": "string"
		},
		"name": {
			"type": "string",
			"description": "Autoload name to remove"
		}
	},
	"required": ["name"]
})";

static void handle_remove_autoload(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
	json params = json::parse(p_params_json, nullptr, false);
	if (params.is_discarded()) {
		r_error = "Invalid JSON parameters.";
		return;
	}

	std::string project_path = resolve_project_path(params.value("project_path", ""));
	if (project_path.empty()) {
		r_error = "No project path provided and no active Godot project detected.";
		return;
	}

	std::string name = params.value("name", "");
	if (name.empty()) {
		r_error = "Parameter 'name' is required.";
		return;
	}

	bool ok = config_parser::remove_autoload(project_path, name);

	json result;
	result["success"] = ok;
	if (!ok) {
		result["error"] = "Failed to remove autoload. The autoload entry may not exist or the project.godot file is not writable.";
	} else {
		result["name"] = name;
	}
	r_result = result.dump();
}

// ---------------------------------------------------------------------------
// Tool: list_autoloads
// ---------------------------------------------------------------------------

static const char *LIST_AUTOLOADS_SCHEMA = R"({
	"type": "object",
	"properties": {
		"project_path": {
			"type": "string"
		}
	}
})";

static void handle_list_autoloads(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
	json params = json::parse(p_params_json, nullptr, false);
	if (params.is_discarded()) {
		r_error = "Invalid JSON parameters.";
		return;
	}

	std::string project_path = resolve_project_path(params.value("project_path", ""));
	if (project_path.empty()) {
		r_error = "No project path provided and no active Godot project detected.";
		return;
	}

	json autoloads = config_parser::get_autoloads(project_path);

	json result;
	result["success"] = true;
	result["autoloads"] = autoloads;
	r_result = result.dump();
}

// ---------------------------------------------------------------------------
// Tool: set_main_scene
// ---------------------------------------------------------------------------

static const char *SET_MAIN_SCENE_SCHEMA = R"({
	"type": "object",
	"properties": {
		"project_path": {
			"type": "string"
		},
		"scene_path": {
			"type": "string",
			"description": "res:// path to the main scene"
		}
	},
	"required": ["scene_path"]
})";

static void handle_set_main_scene(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
	json params = json::parse(p_params_json, nullptr, false);
	if (params.is_discarded()) {
		r_error = "Invalid JSON parameters.";
		return;
	}

	std::string project_path = resolve_project_path(params.value("project_path", ""));
	if (project_path.empty()) {
		r_error = "No project path provided and no active Godot project detected.";
		return;
	}

	std::string scene_path = params.value("scene_path", "");
	if (scene_path.empty()) {
		r_error = "Parameter 'scene_path' is required.";
		return;
	}

	bool ok = config_parser::set_main_scene(project_path, scene_path);

	json result;
	result["success"] = ok;
	if (!ok) {
		result["error"] = "Failed to set main scene. Check that the project.godot file is writable and the scene path is valid.";
	} else {
		result["scene_path"] = scene_path;
	}
	r_result = result.dump();
}

} // namespace gear_mcp

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_autoload_tools(gear_mcp::ToolRegistry *p_registry) {
	p_registry->register_tool(
			"add_autoload",
			"Add an autoload singleton entry to the Godot project configuration.",
			gear_mcp::ADD_AUTOLOAD_SCHEMA,
			gear_mcp::handle_add_autoload,
			/*main_thread=*/false);

	p_registry->register_tool(
			"remove_autoload",
			"Remove an autoload singleton entry from the Godot project configuration by name.",
			gear_mcp::REMOVE_AUTOLOAD_SCHEMA,
			gear_mcp::handle_remove_autoload,
			/*main_thread=*/false);

	p_registry->register_tool(
			"list_autoloads",
			"List all autoload singleton entries configured in the Godot project.",
			gear_mcp::LIST_AUTOLOADS_SCHEMA,
			gear_mcp::handle_list_autoloads,
			/*main_thread=*/false);

	p_registry->register_tool(
			"set_main_scene",
			"Set the main scene for the Godot project by specifying a res:// path.",
			gear_mcp::SET_MAIN_SCENE_SCHEMA,
			gear_mcp::handle_set_main_scene,
			/*main_thread=*/false);
}
