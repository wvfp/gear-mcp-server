#include "server/tool_registry.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <string>

using json = nlohmann::json;

namespace gear_mcp {

// ===========================================================================
// read_file handler
// ===========================================================================

static void read_file_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string path = params.value("path", "");
    if (path.empty()) {
        r_error = "Missing required parameter: 'path'";
        return;
    }

    if (path_utils::has_traversal(path)) {
        r_error = "Path traversal detected: '..' is not allowed";
        return;
    }

    int max_bytes = params.value("max_bytes", 256 * 1024);
    int offset = params.value("offset", 0);
    if (max_bytes <= 0) max_bytes = 256 * 1024;
    if (offset < 0) offset = 0;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        r_error = "Cannot open file: " + path;
        return;
    }

    std::streamsize file_size = file.tellg();
    if (offset > file_size) {
        file.close();
        r_error = "Offset exceeds file size";
        return;
    }

    file.seekg(offset, std::ios::beg);

    int to_read = max_bytes;
    if (offset + to_read > file_size) {
        to_read = (int)(file_size - offset);
    }
    if (to_read < 0) to_read = 0;

    std::string content;
    if (to_read > 0) {
        content.resize(to_read);
        file.read(&content[0], to_read);
        std::streamsize actual = file.gcount();
        if (actual < to_read) content.resize(actual < 0 ? 0 : (size_t)actual);
    }
    file.close();

    bool truncated = (offset + max_bytes < file_size);

    json result = {
        {"path", path},
        {"content", content},
        {"truncated", truncated},
        {"size", (int)file_size}
    };
    r_result = result.dump();
}

// ===========================================================================
// write_file handler
// ===========================================================================

static void write_file_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string path = params.value("path", "");
    if (path.empty()) {
        r_error = "Missing required parameter: 'path'";
        return;
    }

    std::string content = params.value("content", "");
    std::string mode = params.value("mode", "write");

    if (path_utils::has_traversal(path)) {
        r_error = "Path traversal detected: '..' is not allowed";
        return;
    }

    path_utils::ensure_parent_dirs(path);

    std::ios::openmode flags = std::ios::binary;
    if (mode == "append") {
        flags |= std::ios::app;
    } else {
        flags |= std::ios::trunc;
    }

    std::ofstream file(path, flags);
    if (!file.is_open()) {
        r_error = "Cannot open file for writing: " + path;
        return;
    }

    file.write(content.data(), content.size());
    file.close();

    json result = {
        {"path", path},
        {"bytes_written", (int)content.size()}
    };
    r_result = result.dump();
}

// ===========================================================================
// list_directory handler
// ===========================================================================

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static bool pattern_match(const std::string &p_name, const std::string &p_pattern) {
    if (p_pattern.empty()) return true;
    size_t pi = 0, ni = 0;
    size_t wildcard_pos = std::string::npos, match_pos = 0;

    while (ni < p_name.size()) {
        if (pi < p_pattern.size() && (p_pattern[pi] == '?' || p_pattern[pi] == p_name[ni])) {
            pi++; ni++;
        } else if (pi < p_pattern.size() && p_pattern[pi] == '*') {
            wildcard_pos = pi; match_pos = ni; pi++;
        } else if (wildcard_pos != std::string::npos) {
            pi = wildcard_pos + 1; match_pos++; ni = match_pos;
        } else {
            return false;
        }
    }
    while (pi < p_pattern.size() && p_pattern[pi] == '*') pi++;
    return pi == p_pattern.size();
}

static void list_recursive(const std::string &p_path, bool p_recursive,
                            const std::string &p_pattern, json &r_entries) {
#ifdef _WIN32
    std::string search_path = p_path + "\\*";
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        std::string name(ffd.cFileName);
        if (name == "." || name == "..") continue;
        std::string full = p_path + "\\" + name;
        bool is_dir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (pattern_match(name, p_pattern)) {
            r_entries.push_back({{"name", name}, {"path", full}, {"is_dir", is_dir}});
        }
        if (is_dir && p_recursive) {
            list_recursive(full, true, p_pattern, r_entries);
        }
    } while (FindNextFileA(hFind, &ffd) != 0);
    FindClose(hFind);
#else
    DIR *dir = opendir(p_path.c_str());
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        std::string full = p_path + "/" + name;
        bool is_dir = false;
        struct stat st;
        if (stat(full.c_str(), &st) == 0) is_dir = S_ISDIR(st.st_mode);

        if (pattern_match(name, p_pattern)) {
            r_entries.push_back({{"name", name}, {"path", full}, {"is_dir", is_dir}});
        }
        if (is_dir && p_recursive) {
            list_recursive(full, true, p_pattern, r_entries);
        }
    }
    closedir(dir);
#endif
}

static void list_dir_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string path = params.value("path", "");
    if (path.empty()) {
        r_error = "Missing required parameter: 'path'";
        return;
    }

    bool recursive = params.value("recursive", false);
    std::string pattern = params.value("pattern", "");

    if (path_utils::has_traversal(path)) {
        r_error = "Path traversal detected: '..' is not allowed";
        return;
    }

    json entries = json::array();
    list_recursive(path, recursive, pattern, entries);

    json result = {
        {"path", path},
        {"entries", entries},
        {"count", (int)entries.size()}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

// ===========================================================================
// Registration (C linkage for cross-TU calls)
// ===========================================================================

void register_file_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    static const char *READ_FILE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "path": {"type": "string", "description": "Absolute or res:// path to the file"},
        "max_bytes": {"type": "integer", "description": "Maximum bytes to read (default 256KB)", "default": 262144},
        "offset": {"type": "integer", "description": "Byte offset to start reading from", "default": 0}
    },
    "required": ["path"]
})schema";

    static const char *WRITE_FILE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "path": {"type": "string", "description": "Destination file path"},
        "content": {"type": "string", "description": "Text content to write"},
        "mode": {"type": "string", "description": "Write mode: 'write' or 'append'", "default": "write", "enum": ["write", "append"]}
    },
    "required": ["path", "content"]
})schema";

    static const char *LIST_DIR_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "path": {"type": "string", "description": "Directory path to list"},
        "recursive": {"type": "boolean", "description": "Recurse into subdirectories", "default": false},
        "pattern": {"type": "string", "description": "Optional glob filter (e.g. *.gd)", "default": ""}
    },
    "required": ["path"]
})schema";

    p_registry->register_tool("read_file",
        "Read text content of a file with optional offset and max_bytes limit",
        READ_FILE_SCHEMA, gear_mcp::read_file_handler, false);

    p_registry->register_tool("write_file",
        "Write text content to a file. Creates parent directories as needed.",
        WRITE_FILE_SCHEMA, gear_mcp::write_file_handler, false);

    p_registry->register_tool("list_directory",
        "List files and directories. Supports recursive mode and glob pattern filter.",
        LIST_DIR_SCHEMA, gear_mcp::list_dir_handler, false);

    std::printf("[Gear MCP] Registered file tools: read_file, write_file, list_directory\n");
}
