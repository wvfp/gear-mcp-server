#pragma once

#include <string>

namespace gear_mcp {

// ---------------------------------------------------------------------------
// path_utils — path handling utilities for Godot projects.
// ---------------------------------------------------------------------------
namespace path_utils {

/// Convert a res:// path to an absolute filesystem path.
std::string to_absolute(const std::string &p_res_path, const std::string &p_project_root);

/// Convert an absolute path to a res:// path.
std::string to_res_path(const std::string &p_abs_path, const std::string &p_project_root);

/// Check if a path contains traversal sequences (..).
bool has_traversal(const std::string &p_path);

/// Normalize path separators to forward slashes.
std::string normalize(const std::string &p_path);

/// Ensure parent directories exist for a given path.
bool ensure_parent_dirs(const std::string &p_path);

} // namespace path_utils

} // namespace gear_mcp
