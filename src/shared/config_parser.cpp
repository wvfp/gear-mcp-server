#include "config_parser.h"
#include "path_utils.h"

#include <INIReader.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

using json = nlohmann::json;

namespace gear_mcp {
namespace config_parser {

// ===========================================================================
// Internal helpers
// ===========================================================================

// Parse a Godot INI value string into a JSON value
static json parse_godot_value(const std::string &p_value) {
    std::string v = p_value;
    // Trim whitespace
    size_t s = v.find_first_not_of(" \t\r\n");
    size_t e = v.find_last_not_of(" \t\r\n");
    if (s != std::string::npos) v = v.substr(s, e - s + 1);
    else return "";

    // Boolean
    if (v == "true") return true;
    if (v == "false") return false;

    // Integer
    try {
        size_t pos = 0;
        long long int_val = std::stoll(v, &pos);
        if (pos == v.size()) return int_val;
    } catch (...) {}

    // Float
    try {
        size_t pos = 0;
        double float_val = std::stod(v, &pos);
        if (pos == v.size()) return float_val;
    } catch (...) {}

    // String (quoted)
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        std::string unescaped = v.substr(1, v.size() - 2);
        // Unescape common sequences
        std::string result;
        for (size_t i = 0; i < unescaped.size(); i++) {
            if (unescaped[i] == '\\' && i + 1 < unescaped.size()) {
                switch (unescaped[i + 1]) {
                    case '"': result += '"'; i++; break;
                    case '\\': result += '\\'; i++; break;
                    case 'n': result += '\n'; i++; break;
                    case 't': result += '\t'; i++; break;
                    default: result += unescaped[i]; break;
                }
            } else {
                result += unescaped[i];
            }
        }
        return result;
    }

    // Typed values: Vector2(x, y), Color(r, g, b, a), etc.
    // Return as string for now
    return v;
}

// Read a file into string
static std::string read_file(const std::string &p_path) {
    std::ifstream file(p_path);
    if (!file.is_open()) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Write string to file
static bool write_file(const std::string &p_path, const std::string &p_content) {
    std::ofstream file(p_path, std::ios::trunc);
    if (!file.is_open()) return false;
    file << p_content;
    return true;
}

// ===========================================================================
// INI parsing
// ===========================================================================

json parse_ini_file(const std::string &p_path) {
    INIReader reader(p_path);
    if (reader.ParseError() != 0) {
        return json::object();
    }

    json result = json::object();
    // INIReader doesn't expose section/key iteration directly in older versions
    // We need to re-parse the file manually for full access

    std::string content = read_file(p_path);
    if (content.empty()) return result;

    std::string current_section;
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        // Trim
        size_t s = line.find_first_not_of(" \t\r\n");
        if (s == std::string::npos) continue;
        line = line.substr(s);
        size_t e = line.find_last_not_of(" \t\r\n");
        if (e != std::string::npos) line = line.substr(0, e + 1);

        // Skip comments
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        // Section header
        if (line[0] == '[') {
            size_t end = line.find(']');
            if (end != std::string::npos) {
                current_section = line.substr(1, end - 1);
                if (!result.contains(current_section)) {
                    result[current_section] = json::object();
                }
            }
            continue;
        }

        // Key=Value
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);

            // Trim key
            e = key.find_last_not_of(" \t");
            if (e != std::string::npos) key = key.substr(0, e + 1);

            if (current_section.empty()) {
                result[key] = parse_godot_value(value);
            } else {
                result[current_section][key] = parse_godot_value(value);
            }
        }
    }

    return result;
}

json parse_project_godot(const std::string &p_project_path) {
    std::string path = path_utils::normalize(p_project_path) + "/project.godot";
    return parse_ini_file(path);
}

json parse_export_presets(const std::string &p_project_path) {
    std::string path = path_utils::normalize(p_project_path) + "/export_presets.cfg";

    json all_presets = parse_ini_file(path);
    if (all_presets.empty()) return json::array();

    // Extract preset entries (sections like "preset.0", "preset.1", etc.)
    json result = json::array();
    for (auto &[key, value] : all_presets.items()) {
        if (key.substr(0, 7) == "preset." && key.size() > 7) {
            // Check if it's a preset definition (not preset.0.options)
            std::string suffix = key.substr(7);
            // Only include if suffix is a number (the main preset entry)
            bool is_number = true;
            for (char c : suffix) {
                if (!std::isdigit(c)) { is_number = false; break; }
            }
            if (is_number && value.is_object()) {
                json preset = value;
                preset["index"] = std::stoi(suffix);

                // Merge options from preset.N.options
                std::string options_key = "preset." + suffix + ".options";
                if (all_presets.contains(options_key)) {
                    preset["options"] = all_presets[options_key];
                }

                result.push_back(preset);
            }
        }
    }

    // Sort by index
    std::sort(result.begin(), result.end(), [](const json &a, const json &b) {
        return a.value("index", 0) < b.value("index", 0);
    });

    return result;
}

// ===========================================================================
// Project settings
// ===========================================================================

std::string get_project_setting(const std::string &p_project_path, const std::string &p_section, const std::string &p_key) {
    json proj = parse_project_godot(p_project_path);

    if (p_section.empty()) {
        // Look in top-level keys
        if (proj.contains(p_key)) {
            return proj[p_key].dump();
        }
    } else {
        if (proj.contains(p_section) && proj[p_section].contains(p_key)) {
            return proj[p_section][p_key].dump();
        }
    }

    return "";
}

bool set_project_setting(const std::string &p_project_path, const std::string &p_section,
                          const std::string &p_key, const std::string &p_value) {
    std::string path = path_utils::normalize(p_project_path) + "/project.godot";
    std::string content = read_file(path);
    if (content.empty()) return false;

    // Find or create the section
    std::string section_header = "[" + p_section + "]";

    // Godot's ConfigFile requires string values to be wrapped in double
    // quotes. Detect whether the existing entry was quoted, and if so,
    // also quote the replacement. For new entries, quote anything that
    // is not a bare token (number, boolean, or simple identifier) so
    // the parser does not choke.
    auto needs_quotes = [](const std::string &v) -> bool {
        if (v.empty()) return false;
        // Already wrapped
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') return false;
        if (v.front() == '"') return true;
        // Bare tokens that the INI parser accepts unquoted
        static const char *bare_ok[] = {
            "true", "false", "null", "Inf", "NaN", "self", "Resource"
        };
        for (const char *tok : bare_ok) {
            if (v == tok) return false;
        }
        // Pure numbers
        bool seen_dot = false;
        size_t i = 0;
        if (v[0] == '-' || v[0] == '+') i = 1;
        bool any_digit = false;
        for (; i < v.size(); ++i) {
            char c = v[i];
            if (c >= '0' && c <= '9') { any_digit = true; continue; }
            if ((c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') && !seen_dot) {
                if (c == '.') seen_dot = true;
                continue;
            }
            // Contains a non-numeric, non-token char -> needs quotes
            return true;
        }
        return !any_digit; // all-letters word like "vulkan" -> needs quotes
    };

    auto format_value = [&](const std::string &existing_line_value, const std::string &new_value) -> std::string {
        // existing_line_value is the substring between '=' and end-of-line
        // (excluding the trailing newline). Detect if it was quoted.
        bool was_quoted = !existing_line_value.empty() &&
                          existing_line_value.front() == '"';
        std::string inner = new_value;
        // Strip any quotes the caller included so we control them.
        if (inner.size() >= 2 && inner.front() == '"' && inner.back() == '"') {
            inner = inner.substr(1, inner.size() - 2);
        }
        if (was_quoted || needs_quotes(inner)) {
            return std::string("\"") + inner + "\"";
        }
        return inner;
    };

    size_t section_pos = content.find(section_header);
    if (section_pos == std::string::npos) {
        // Section doesn't exist, add it at the end
        if (!content.empty() && content.back() != '\n') content += "\n";
        std::string new_value = format_value("", p_value);
        content += "\n" + section_header + "\n" + p_key + "=" + new_value + "\n";
    } else {
        // Find the next section or end of file
        size_t next_section = content.find("\n[", section_pos + section_header.size());
        if (next_section == std::string::npos) next_section = content.size();

        // Look for existing key in this section
        std::string key_prefix = p_key + "=";
        size_t key_pos = content.find(key_prefix, section_pos + section_header.size());

        if (key_pos != std::string::npos && key_pos < next_section) {
            // Key exists, replace its line. Capture the existing value
            // (between '=' and the line terminator) to detect quoting.
            size_t val_start = key_pos + key_prefix.size();
            size_t line_end = content.find('\n', key_pos);
            if (line_end == std::string::npos) line_end = content.size();
            std::string existing_value = content.substr(val_start, line_end - val_start);
            std::string new_value = format_value(existing_value, p_value);
            content.replace(key_pos, line_end - key_pos, p_key + "=" + new_value);
        } else {
            // Key doesn't exist, add it after the section header
            size_t insert_pos = section_pos + section_header.size();
            if (insert_pos < content.size() && content[insert_pos] == '\n') insert_pos++;
            std::string new_value = format_value("", p_value);
            content.insert(insert_pos, p_key + "=" + new_value + "\n");
        }
    }

    return write_file(path, content);
}

// ===========================================================================
// Autoload management
// ===========================================================================

json get_autoloads(const std::string &p_project_path) {
    json proj = parse_project_godot(p_project_path);
    json result = json::array();

    if (proj.contains("autoload")) {
        for (auto &[name, path] : proj["autoload"].items()) {
            json entry = {
                {"name", name},
                {"path", path.is_string() ? path.get<std::string>() : path.dump()}
            };
            // Check if it's a singleton (prefixed with *)
            std::string path_str = path.is_string() ? path.get<std::string>() : path.dump();
            bool is_singleton = !path_str.empty() && path_str[0] == '*';
            entry["singleton"] = is_singleton;
            if (is_singleton) {
                entry["path"] = path_str.substr(1);
            }
            result.push_back(entry);
        }
    }

    return result;
}

bool add_autoload(const std::string &p_project_path, const std::string &p_name, const std::string &p_path) {
    return set_project_setting(p_project_path, "autoload", p_name, "\"" + p_path + "\"");
}

bool remove_autoload(const std::string &p_project_path, const std::string &p_name) {
    std::string path = path_utils::normalize(p_project_path) + "/project.godot";
    std::string content = read_file(path);
    if (content.empty()) return false;

    // Find [autoload] section
    std::string section_header = "[autoload]";
    size_t section_pos = content.find(section_header);
    if (section_pos == std::string::npos) return false;

    // Find the key line
    std::string key_prefix = p_name + "=";
    size_t key_pos = content.find(key_prefix, section_pos + section_header.size());

    // Check we're still in the autoload section
    size_t next_section = content.find("\n[", section_pos + section_header.size());
    if (key_pos == std::string::npos || (next_section != std::string::npos && key_pos > next_section)) {
        return false;
    }

    // Remove the line
    size_t line_start = key_pos;
    // Go back to include the preceding newline if present
    if (line_start > 0 && content[line_start - 1] == '\n') line_start--;
    size_t line_end = content.find('\n', key_pos);
    if (line_end == std::string::npos) line_end = content.size();
    else line_end++; // Include the newline

    content.erase(line_start, line_end - line_start);

    return write_file(path, content);
}

// ===========================================================================
// Main scene
// ===========================================================================

std::string get_main_scene(const std::string &p_project_path) {
    json proj = parse_project_godot(p_project_path);
    if (proj.contains("application") && proj["application"].contains("run/main_scene")) {
        auto val = proj["application"]["run/main_scene"];
        return val.is_string() ? val.get<std::string>() : val.dump();
    }
    return "";
}

bool set_main_scene(const std::string &p_project_path, const std::string &p_scene_path) {
    return set_project_setting(p_project_path, "application", "run/main_scene", "\"" + p_scene_path + "\"");
}

// ===========================================================================
// Project scanning
// ===========================================================================

json scan_projects(const std::string &p_directory, bool p_recursive) {
    json result = json::array();

#ifdef _WIN32
    std::string search_path = p_directory + "\\*";
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return result;

    do {
        std::string name(ffd.cFileName);
        if (name == "." || name == "..") continue;

        std::string full = p_directory + "\\" + name;
        bool is_dir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (is_dir) {
            std::string proj_file = full + "\\project.godot";
            WIN32_FIND_DATAA proj_ffd;
            HANDLE proj_find = FindFirstFileA(proj_file.c_str(), &proj_ffd);
            if (proj_find != INVALID_HANDLE_VALUE) {
                FindClose(proj_find);
                // Found a project
                json proj_data = parse_project_godot(full);
                json entry = {
                    {"path", full},
                    {"name", ""}
                };
                if (proj_data.contains("application") && proj_data["application"].contains("config/name")) {
                    entry["name"] = proj_data["application"]["config/name"];
                }
                result.push_back(entry);
            }
            if (p_recursive) {
                json sub = scan_projects(full, true);
                for (auto &item : sub) result.push_back(item);
            }
        }
    } while (FindNextFileA(hFind, &ffd) != 0);
    FindClose(hFind);
#else
    DIR *dir = opendir(p_directory.c_str());
    if (!dir) return result;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name == "." || name == "..") continue;

        std::string full = p_directory + "/" + name;
        bool is_dir = false;
        struct stat st;
        if (stat(full.c_str(), &st) == 0) is_dir = S_ISDIR(st.st_mode);

        if (is_dir) {
            std::string proj_file = full + "/project.godot";
            struct stat proj_st;
            if (stat(proj_file.c_str(), &proj_st) == 0) {
                json proj_data = parse_project_godot(full);
                json proj_entry = {
                    {"path", full},
                    {"name", ""}
                };
                if (proj_data.contains("application") && proj_data["application"].contains("config/name")) {
                    proj_entry["name"] = proj_data["application"]["config/name"];
                }
                result.push_back(proj_entry);
            }
            if (p_recursive) {
                json sub = scan_projects(full, true);
                for (auto &item : sub) result.push_back(item);
            }
        }
    }
    closedir(dir);
#endif

    return result;
}

} // namespace config_parser
} // namespace gear_mcp
