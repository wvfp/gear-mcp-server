#pragma once

#include <string>
#include <map>
#include <vector>

#include <nlohmann/json.hpp>

namespace gear_mcp {

// ---------------------------------------------------------------------------
// config_parser — INI file parser for Godot project files.
// Uses inih (INIReader) under the hood.
// Handles project.godot and export_presets.cfg formats.
// ---------------------------------------------------------------------------
namespace config_parser {

/// Parse a Godot-style INI file and return as a JSON object.
/// Sections become top-level keys, each containing key-value pairs.
nlohmann::json parse_ini_file(const std::string &p_path);

/// Parse project.godot and return structured data.
nlohmann::json parse_project_godot(const std::string &p_project_path);

/// Parse export_presets.cfg and return export preset list.
nlohmann::json parse_export_presets(const std::string &p_project_path);

/// Get a specific setting from project.godot.
std::string get_project_setting(const std::string &p_project_path, const std::string &p_section, const std::string &p_key);

/// Set a specific setting in project.godot (writes back the file).
bool set_project_setting(const std::string &p_project_path, const std::string &p_section,
                          const std::string &p_key, const std::string &p_value);

/// Get all autoload entries from project.godot.
nlohmann::json get_autoloads(const std::string &p_project_path);

/// Add an autoload entry to project.godot.
bool add_autoload(const std::string &p_project_path, const std::string &p_name, const std::string &p_path);

/// Remove an autoload entry from project.godot.
bool remove_autoload(const std::string &p_project_path, const std::string &p_name);

/// Get the main scene path from project.godot.
std::string get_main_scene(const std::string &p_project_path);

/// Set the main scene path in project.godot.
bool set_main_scene(const std::string &p_project_path, const std::string &p_scene_path);

/// Scan a directory for Godot projects (folders containing project.godot).
nlohmann::json scan_projects(const std::string &p_directory, bool p_recursive = false);

} // namespace config_parser

} // namespace gear_mcp
