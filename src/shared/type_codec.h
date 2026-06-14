#pragma once

#include <gdextension_interface.h>
#include <string>

#include <nlohmann/json.hpp>

namespace gear_mcp {

// ---------------------------------------------------------------------------
// type_codec — Variant <-> JSON bidirectional encoding/decoding.
//
// Phase 2: full type support for all common Godot types.
// Supports: bool, int, float, String, Vector2, Vector2i, Vector3, Vector3i,
// Color, Rect2, Transform2D, Transform3D, NodePath, Basis, Quaternion,
// Plane, AABB, Dictionary, Array, and more.
// ---------------------------------------------------------------------------
namespace type_codec {

/// Variant -> JSON (nlohmann::json object).
/// Returns a JSON representation appropriate for the variant's type.
nlohmann::json variant_to_json(GDExtensionConstVariantPtr p_variant);

/// Variant -> JSON string.
/// Convenience wrapper around variant_to_json.
std::string serialize(GDExtensionConstVariantPtr p_variant);

/// Property value -> JSON string (safe for Resources, Objects, etc.)
/// Handles non-serializable types gracefully.
std::string property_to_json(GDExtensionConstVariantPtr p_variant);

/// JSON value -> Variant.
/// Parses a nlohmann::json value and creates the appropriate Variant.
/// For typed values, expects format: {"type": "Vector2", "x": 1.0, "y": 2.0}
/// For basic types (string, int, float, bool), uses direct conversion.
bool json_to_variant(const nlohmann::json &p_json, GDExtensionUninitializedVariantPtr r_variant);

/// JSON string -> Variant.
/// Convenience wrapper.
bool parse(const std::string &p_json_str, GDExtensionUninitializedVariantPtr r_variant);

/// Get the Godot type name string for a variant type enum.
const char *variant_type_name(GDExtensionVariantType p_type);

} // namespace type_codec

} // namespace gear_mcp
