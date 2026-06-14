#include "path_utils.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gear_mcp {
namespace path_utils {

std::string to_absolute(const std::string &p_res_path, const std::string &p_project_root) {
    if (p_res_path.size() > 6 && p_res_path.substr(0, 6) == "res://") {
        return normalize(p_project_root + "/" + p_res_path.substr(6));
    }
    return normalize(p_res_path);
}

std::string to_res_path(const std::string &p_abs_path, const std::string &p_project_root) {
    std::string norm_abs = normalize(p_abs_path);
    std::string norm_root = normalize(p_project_root);

    if (norm_abs.size() > norm_root.size() && norm_abs.substr(0, norm_root.size()) == norm_root) {
        return "res://" + norm_abs.substr(norm_root.size() + 1);
    }
    return norm_abs;
}

bool has_traversal(const std::string &p_path) {
    return p_path.find("..") != std::string::npos;
}

std::string normalize(const std::string &p_path) {
    std::string result = p_path;
    std::replace(result.begin(), result.end(), '\\', '/');
    // Remove trailing slash
    while (result.size() > 1 && result.back() == '/') {
        result.pop_back();
    }
    return result;
}

bool ensure_parent_dirs(const std::string &p_path) {
    size_t sep = p_path.find_last_of("/\\");
    if (sep == std::string::npos || sep == 0) return true;

    std::string parent = p_path.substr(0, sep);

    size_t pos = 0;
#ifdef _WIN32
    if (parent.size() > 1 && parent[1] == ':') pos = 2;
    if (parent.size() > 2 && parent[0] == '\\' && parent[1] == '\\') pos = 2;
#endif
    if (!parent.empty() && (parent[0] == '/' || parent[0] == '\\')) pos = 1;

    while (pos < parent.size()) {
        size_t next = parent.find_first_of("/\\", pos);
        if (next == std::string::npos) next = parent.size();
        std::string segment = parent.substr(0, next);
#ifdef _WIN32
        _mkdir(segment.c_str());
#else
        mkdir(segment.c_str(), 0755);
#endif
        pos = next + 1;
    }
    return true;
}

} // namespace path_utils
} // namespace gear_mcp
