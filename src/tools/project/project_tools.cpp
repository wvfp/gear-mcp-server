#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/config_parser.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

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

// Simple glob matching supporting '*' and '?' wildcards.
static bool glob_match(const std::string &p_pattern, const std::string &p_str) {
	size_t pi = 0, si = 0;
	size_t star_pi = std::string::npos, star_si = 0;

	while (si < p_str.size()) {
		if (pi < p_pattern.size() && (p_pattern[pi] == '?' || p_pattern[pi] == p_str[si])) {
			++pi;
			++si;
		} else if (pi < p_pattern.size() && p_pattern[pi] == '*') {
			star_pi = pi;
			star_si = si;
			++pi;
		} else if (star_pi != std::string::npos) {
			pi = star_pi + 1;
			++star_si;
			si = star_si;
		} else {
			return false;
		}
	}

	while (pi < p_pattern.size() && p_pattern[pi] == '*') {
		++pi;
	}

	return pi == p_pattern.size();
}

// Extract file extension including the dot, e.g. ".gd" from "script.gd".
static std::string get_extension(const std::string &p_filename) {
	size_t pos = p_filename.rfind('.');
	if (pos == std::string::npos) {
		return "";
	}
	return p_filename.substr(pos);
}

// ---------------------------------------------------------------------------
// search_project: recursive directory walking with platform-specific APIs
// ---------------------------------------------------------------------------

struct SearchMatch {
	std::string file;
	int line;
	std::string content;
};

static void search_directory(
		const std::string &p_dir,
		const std::string &p_query,
		const std::string &p_file_pattern,
		int p_max_results,
		std::vector<SearchMatch> &r_matches) {

	if ((int)r_matches.size() >= p_max_results) {
		return;
	}

#ifdef _WIN32
	std::string search_path = p_dir + "\\*";
	WIN32_FIND_DATAA find_data;
	HANDLE h_find = FindFirstFileA(search_path.c_str(), &find_data);
	if (h_find == INVALID_HANDLE_VALUE) {
		return;
	}

	do {
		std::string name(find_data.cFileName);
		if (name == "." || name == "..") {
			continue;
		}
		// Skip hidden directories and .godot / .git
		if (name[0] == '.') {
			continue;
		}

		std::string full_path = p_dir + "\\" + name;

		if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			search_directory(full_path, p_query, p_file_pattern, p_max_results, r_matches);
			if ((int)r_matches.size() >= p_max_results) {
				break;
			}
		} else {
			// Check file pattern match.
			if (!glob_match(p_file_pattern, name)) {
				continue;
			}

			std::ifstream file(full_path);
			if (!file.is_open()) {
				continue;
			}

			std::string line;
			int line_number = 0;
			while (std::getline(file, line)) {
				++line_number;
				if (line.find(p_query) != std::string::npos) {
					SearchMatch match;
					match.file = full_path;
					match.line = line_number;
					match.content = line;
					r_matches.push_back(match);
					if ((int)r_matches.size() >= p_max_results) {
						break;
					}
				}
			}
		}
	} while (FindNextFileA(h_find, &find_data) != 0);

	FindClose(h_find);
#else
	DIR *dir = opendir(p_dir.c_str());
	if (!dir) {
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != nullptr) {
		std::string name(entry->d_name);
		if (name == "." || name == "..") {
			continue;
		}
		// Skip hidden directories and .godot / .git
		if (name[0] == '.') {
			continue;
		}

		std::string full_path = p_dir + "/" + name;

		struct stat st;
		if (stat(full_path.c_str(), &st) != 0) {
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			search_directory(full_path, p_query, p_file_pattern, p_max_results, r_matches);
			if ((int)r_matches.size() >= p_max_results) {
				break;
			}
		} else if (S_ISREG(st.st_mode)) {
			if (!glob_match(p_file_pattern, name)) {
				continue;
			}

			std::ifstream file(full_path);
			if (!file.is_open()) {
				continue;
			}

			std::string line;
			int line_number = 0;
			while (std::getline(file, line)) {
				++line_number;
				if (line.find(p_query) != std::string::npos) {
					SearchMatch match;
					match.file = full_path;
					match.line = line_number;
					match.content = line;
					r_matches.push_back(match);
					if ((int)r_matches.size() >= p_max_results) {
						break;
					}
				}
			}
		}
	}

	closedir(dir);
#endif
}

// ---------------------------------------------------------------------------
// Tool: list_projects
// ---------------------------------------------------------------------------

static const char *LIST_PROJECTS_SCHEMA = R"({
	"type": "object",
	"properties": {
		"directory": {
			"type": "string",
			"description": "Directory to scan"
		},
		"recursive": {
			"type": "boolean",
			"default": false
		}
	},
	"required": ["directory"]
})";

static void handle_list_projects(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
	json params = json::parse(p_params_json, nullptr, false);
	if (params.is_discarded()) {
		r_error = "Invalid JSON parameters.";
		return;
	}

	std::string directory = params.value("directory", "");
	if (directory.empty()) {
		r_error = "Parameter 'directory' is required.";
		return;
	}

	if (path_utils::has_traversal(directory)) {
		r_error = "Invalid directory path: path traversal detected.";
		return;
	}

	bool recursive = params.value("recursive", false);

	json projects = config_parser::scan_projects(directory, recursive);

	json result;
	result["success"] = true;
	result["projects"] = projects;
	r_result = result.dump();
}

// ---------------------------------------------------------------------------
// Tool: get_project_info
// ---------------------------------------------------------------------------

static const char *GET_PROJECT_INFO_SCHEMA = R"({
	"type": "object",
	"properties": {
		"project_path": {
			"type": "string",
			"description": "Path to Godot project root"
		}
	}
})";

static void handle_get_project_info(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
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

	json config = config_parser::parse_project_godot(project_path);
	if (config.contains("error")) {
		r_error = config["error"].get<std::string>();
		return;
	}

	// Extract application settings.
	std::string project_name;
	std::string godot_version;
	std::string main_scene;
	json features = json::array();

	if (config.contains("application")) {
		json app = config["application"];
		if (app.contains("config/name")) {
			project_name = app["config/name"].get<std::string>();
		}
		if (app.contains("config/version")) {
			godot_version = app["config/version"].get<std::string>();
		}
		if (app.contains("run/main_scene")) {
			main_scene = app["run/main_scene"].get<std::string>();
		}
		if (app.contains("config/features")) {
			features = app["config/features"];
		}
	}

	json result;
	result["success"] = true;
	result["project_path"] = project_path;
	result["project_name"] = project_name;
	result["godot_version"] = godot_version;
	result["main_scene"] = main_scene;
	result["features"] = features;

	// Include autoloads if present.
	json autoloads = config_parser::get_autoloads(project_path);
	if (!autoloads.empty()) {
		result["autoloads"] = autoloads;
	}

	r_result = result.dump();
}

// ---------------------------------------------------------------------------
// Tool: search_project
// ---------------------------------------------------------------------------

static const char *SEARCH_PROJECT_SCHEMA = R"({
	"type": "object",
	"properties": {
		"project_path": {
			"type": "string"
		},
		"query": {
			"type": "string",
			"description": "Text to search for"
		},
		"file_pattern": {
			"type": "string",
			"description": "Glob pattern like *.gd",
			"default": "*"
		},
		"max_results": {
			"type": "integer",
			"default": 50
		}
	},
	"required": ["query"]
})";

static void handle_search_project(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
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

	std::string query = params.value("query", "");
	if (query.empty()) {
		r_error = "Parameter 'query' is required and must not be empty.";
		return;
	}

	std::string file_pattern = params.value("file_pattern", "*");
	int max_results = params.value("max_results", 50);
	if (max_results <= 0) {
		max_results = 50;
	}
	if (max_results > 500) {
		max_results = 500;
	}

	std::vector<SearchMatch> matches;
	search_directory(project_path, query, file_pattern, max_results, matches);

	json matches_array = json::array();
	for (const auto &m : matches) {
		json entry;
		entry["file"] = m.file;
		entry["line"] = m.line;
		entry["content"] = m.content;
		matches_array.push_back(entry);
	}

	json result;
	result["success"] = true;
	result["project_path"] = project_path;
	result["query"] = query;
	result["file_pattern"] = file_pattern;
	result["total_matches"] = (int)matches.size();
	result["matches"] = matches_array;
	r_result = result.dump();
}

// ---------------------------------------------------------------------------
// Tool: get_project_setting
// ---------------------------------------------------------------------------

static const char *GET_PROJECT_SETTING_SCHEMA = R"({
	"type": "object",
	"properties": {
		"project_path": {
			"type": "string"
		},
		"section": {
			"type": "string",
			"description": "INI section like 'application'"
		},
		"key": {
			"type": "string",
			"description": "Setting key like 'config/name'"
		}
	},
	"required": ["key"]
})";

static void handle_get_project_setting(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
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

	std::string section = params.value("section", "");
	std::string key = params.value("key", "");
	if (key.empty()) {
		r_error = "Parameter 'key' is required.";
		return;
	}

	std::string value = config_parser::get_project_setting(project_path, section, key);

	json result;
	result["success"] = true;
	result["section"] = section;
	result["key"] = key;
	result["value"] = value;
	r_result = result.dump();
}

// ---------------------------------------------------------------------------
// Tool: set_project_setting
// ---------------------------------------------------------------------------

static const char *SET_PROJECT_SETTING_SCHEMA = R"SCHEMA({
	"type": "object",
	"properties": {
		"project_path": {
			"type": "string"
		},
		"section": {
			"type": "string"
		},
		"key": {
			"type": "string"
		},
		"value": {
			"type": "string",
			"description": "New value (as raw string)"
		}
	},
	"required": ["key", "value"]
})SCHEMA";

static void handle_set_project_setting(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
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

	std::string section = params.value("section", "");
	std::string key = params.value("key", "");
	std::string value = params.value("value", "");

	if (key.empty()) {
		r_error = "Parameter 'key' is required.";
		return;
	}

	bool ok = config_parser::set_project_setting(project_path, section, key, value);

	json result;
	result["success"] = ok;
	if (!ok) {
		result["error"] = "Failed to set project setting. Check that the project.godot file is writable.";
	} else {
		result["section"] = section;
		result["key"] = key;
		result["value"] = value;
	}
	r_result = result.dump();
}

} // namespace gear_mcp

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_project_tools(gear_mcp::ToolRegistry *p_registry) {
	p_registry->register_tool(
			"list_projects",
			"Scan a directory for Godot projects and return their paths and metadata.",
			gear_mcp::LIST_PROJECTS_SCHEMA,
			gear_mcp::handle_list_projects,
			/*main_thread=*/false);

	p_registry->register_tool(
			"get_project_info",
			"Read project.godot metadata and return structured project information including name, version, main scene, and autoloads.",
			gear_mcp::GET_PROJECT_INFO_SCHEMA,
			gear_mcp::handle_get_project_info,
			/*main_thread=*/false);

	p_registry->register_tool(
			"search_project",
			"Search for a text string across project files. Returns matching lines with file path and line number.",
			gear_mcp::SEARCH_PROJECT_SCHEMA,
			gear_mcp::handle_search_project,
			/*main_thread=*/false);

	p_registry->register_tool(
			"get_project_setting",
			"Read a single ProjectSettings value from the project.godot file by section and key.",
			gear_mcp::GET_PROJECT_SETTING_SCHEMA,
			gear_mcp::handle_get_project_setting,
			/*main_thread=*/false);

	p_registry->register_tool(
			"set_project_setting",
			"Modify a ProjectSettings value in the project.godot file by section and key.",
			gear_mcp::SET_PROJECT_SETTING_SCHEMA,
			gear_mcp::handle_set_project_setting,
			/*main_thread=*/false);
}
