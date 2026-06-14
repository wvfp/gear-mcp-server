#include "type_codec.h"
#include "godot_api/godot_api.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace gear_mcp {
namespace type_codec {

// ===========================================================================
// Type name mapping
// ===========================================================================

const char *variant_type_name(GDExtensionVariantType p_type) {
    switch (p_type) {
        case GDEXTENSION_VARIANT_TYPE_NIL: return "Nil";
        case GDEXTENSION_VARIANT_TYPE_BOOL: return "bool";
        case GDEXTENSION_VARIANT_TYPE_INT: return "int";
        case GDEXTENSION_VARIANT_TYPE_FLOAT: return "float";
        case GDEXTENSION_VARIANT_TYPE_STRING: return "String";
        case GDEXTENSION_VARIANT_TYPE_VECTOR2: return "Vector2";
        case GDEXTENSION_VARIANT_TYPE_VECTOR2I: return "Vector2i";
        case GDEXTENSION_VARIANT_TYPE_RECT2: return "Rect2";
        case GDEXTENSION_VARIANT_TYPE_RECT2I: return "Rect2i";
        case GDEXTENSION_VARIANT_TYPE_VECTOR3: return "Vector3";
        case GDEXTENSION_VARIANT_TYPE_VECTOR3I: return "Vector3i";
        case GDEXTENSION_VARIANT_TYPE_TRANSFORM2D: return "Transform2D";
        case GDEXTENSION_VARIANT_TYPE_VECTOR4: return "Vector4";
        case GDEXTENSION_VARIANT_TYPE_VECTOR4I: return "Vector4i";
        case GDEXTENSION_VARIANT_TYPE_PLANE: return "Plane";
        case GDEXTENSION_VARIANT_TYPE_QUATERNION: return "Quaternion";
        case GDEXTENSION_VARIANT_TYPE_AABB: return "AABB";
        case GDEXTENSION_VARIANT_TYPE_BASIS: return "Basis";
        case GDEXTENSION_VARIANT_TYPE_TRANSFORM3D: return "Transform3D";
        case GDEXTENSION_VARIANT_TYPE_PROJECTION: return "Projection";
        case GDEXTENSION_VARIANT_TYPE_COLOR: return "Color";
        case GDEXTENSION_VARIANT_TYPE_STRING_NAME: return "StringName";
        case GDEXTENSION_VARIANT_TYPE_NODE_PATH: return "NodePath";
        case GDEXTENSION_VARIANT_TYPE_RID: return "RID";
        case GDEXTENSION_VARIANT_TYPE_OBJECT: return "Object";
        case GDEXTENSION_VARIANT_TYPE_CALLABLE: return "Callable";
        case GDEXTENSION_VARIANT_TYPE_SIGNAL: return "Signal";
        case GDEXTENSION_VARIANT_TYPE_DICTIONARY: return "Dictionary";
        case GDEXTENSION_VARIANT_TYPE_ARRAY: return "Array";
        case GDEXTENSION_VARIANT_TYPE_PACKED_BYTE_ARRAY: return "PackedByteArray";
        case GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY: return "PackedInt32Array";
        case GDEXTENSION_VARIANT_TYPE_PACKED_INT64_ARRAY: return "PackedInt64Array";
        case GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY: return "PackedFloat32Array";
        case GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT64_ARRAY: return "PackedFloat64Array";
        case GDEXTENSION_VARIANT_TYPE_PACKED_STRING_ARRAY: return "PackedStringArray";
        case GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR2_ARRAY: return "PackedVector2Array";
        case GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR3_ARRAY: return "PackedVector3Array";
        case GDEXTENSION_VARIANT_TYPE_PACKED_COLOR_ARRAY: return "PackedColorArray";
        default: return "Unknown";
    }
}

// ===========================================================================
// String parsing helpers
// ===========================================================================

// Parse a comma-separated list of floats from "(x, y)" or "x, y" format
static std::vector<double> parse_float_list(const std::string &p_str) {
    std::vector<double> result;
    std::string cleaned = p_str;
    // Remove parentheses
    for (char &c : cleaned) {
        if (c == '(' || c == ')') c = ' ';
    }
    std::istringstream ss(cleaned);
    std::string token;
    while (std::getline(ss, token, ',')) {
        try {
            size_t s = token.find_first_not_of(" \t");
            if (s != std::string::npos) {
                result.push_back(std::stod(token.substr(s)));
            }
        } catch (...) {
            result.push_back(0.0);
        }
    }
    return result;
}

// ===========================================================================
// Variant -> JSON
// ===========================================================================

json variant_to_json(GDExtensionConstVariantPtr p_variant) {
    if (!p_variant) return nullptr;

    GDExtensionVariantType type = GodotAPI::get_variant_type(p_variant);

    switch (type) {
        case GDEXTENSION_VARIANT_TYPE_NIL:
            return nullptr;

        case GDEXTENSION_VARIANT_TYPE_BOOL:
            return GodotAPI::variant_to_bool(p_variant);

        case GDEXTENSION_VARIANT_TYPE_INT:
            return GodotAPI::variant_to_int(p_variant);

        case GDEXTENSION_VARIANT_TYPE_FLOAT:
            return GodotAPI::variant_to_float(p_variant);

        case GDEXTENSION_VARIANT_TYPE_STRING:
        case GDEXTENSION_VARIANT_TYPE_STRING_NAME:
        case GDEXTENSION_VARIANT_TYPE_NODE_PATH: {
            return GodotAPI::variant_to_string(p_variant);
        }

        case GDEXTENSION_VARIANT_TYPE_VECTOR2: {
            std::string str = GodotAPI::variant_to_string(p_variant);
            auto vals = parse_float_list(str);
            json j = {{"type", "Vector2"}};
            j["x"] = vals.size() > 0 ? vals[0] : 0.0;
            j["y"] = vals.size() > 1 ? vals[1] : 0.0;
            return j;
        }

        case GDEXTENSION_VARIANT_TYPE_VECTOR2I: {
            std::string str = GodotAPI::variant_to_string(p_variant);
            auto vals = parse_float_list(str);
            json j = {{"type", "Vector2i"}};
            j["x"] = vals.size() > 0 ? (int64_t)vals[0] : 0;
            j["y"] = vals.size() > 1 ? (int64_t)vals[1] : 0;
            return j;
        }

        case GDEXTENSION_VARIANT_TYPE_VECTOR3: {
            std::string str = GodotAPI::variant_to_string(p_variant);
            auto vals = parse_float_list(str);
            json j = {{"type", "Vector3"}};
            j["x"] = vals.size() > 0 ? vals[0] : 0.0;
            j["y"] = vals.size() > 1 ? vals[1] : 0.0;
            j["z"] = vals.size() > 2 ? vals[2] : 0.0;
            return j;
        }

        case GDEXTENSION_VARIANT_TYPE_VECTOR3I: {
            std::string str = GodotAPI::variant_to_string(p_variant);
            auto vals = parse_float_list(str);
            json j = {{"type", "Vector3i"}};
            j["x"] = vals.size() > 0 ? (int64_t)vals[0] : 0;
            j["y"] = vals.size() > 1 ? (int64_t)vals[1] : 0;
            j["z"] = vals.size() > 2 ? (int64_t)vals[2] : 0;
            return j;
        }

        case GDEXTENSION_VARIANT_TYPE_VECTOR4: {
            std::string str = GodotAPI::variant_to_string(p_variant);
            auto vals = parse_float_list(str);
            json j = {{"type", "Vector4"}};
            j["x"] = vals.size() > 0 ? vals[0] : 0.0;
            j["y"] = vals.size() > 1 ? vals[1] : 0.0;
            j["z"] = vals.size() > 2 ? vals[2] : 0.0;
            j["w"] = vals.size() > 3 ? vals[3] : 0.0;
            return j;
        }

        case GDEXTENSION_VARIANT_TYPE_COLOR: {
            std::string str = GodotAPI::variant_to_string(p_variant);
            auto vals = parse_float_list(str);
            json j = {{"type", "Color"}};
            if (vals.size() >= 4) {
                j["r"] = vals[0];
                j["g"] = vals[1];
                j["b"] = vals[2];
                j["a"] = vals[3];
            } else if (vals.size() >= 3) {
                j["r"] = vals[0];
                j["g"] = vals[1];
                j["b"] = vals[2];
                j["a"] = 1.0;
            } else {
                // Try hex format
                j["hex"] = str;
            }
            return j;
        }

        case GDEXTENSION_VARIANT_TYPE_RECT2: {
            std::string str = GodotAPI::variant_to_string(p_variant);
            auto vals = parse_float_list(str);
            json j = {{"type", "Rect2"}};
            j["x"] = vals.size() > 0 ? vals[0] : 0.0;
            j["y"] = vals.size() > 1 ? vals[1] : 0.0;
            j["w"] = vals.size() > 2 ? vals[2] : 0.0;
            j["h"] = vals.size() > 3 ? vals[3] : 0.0;
            return j;
        }

        case GDEXTENSION_VARIANT_TYPE_RECT2I: {
            std::string str = GodotAPI::variant_to_string(p_variant);
            auto vals = parse_float_list(str);
            json j = {{"type", "Rect2i"}};
            j["x"] = vals.size() > 0 ? (int64_t)vals[0] : 0;
            j["y"] = vals.size() > 1 ? (int64_t)vals[1] : 0;
            j["w"] = vals.size() > 2 ? (int64_t)vals[2] : 0;
            j["h"] = vals.size() > 3 ? (int64_t)vals[3] : 0;
            return j;
        }

        case GDEXTENSION_VARIANT_TYPE_PLANE: {
            std::string str = GodotAPI::variant_to_string(p_variant);
            auto vals = parse_float_list(str);
            json j = {{"type", "Plane"}};
            j["x"] = vals.size() > 0 ? vals[0] : 0.0;
            j["y"] = vals.size() > 1 ? vals[1] : 0.0;
            j["z"] = vals.size() > 2 ? vals[2] : 0.0;
            j["d"] = vals.size() > 3 ? vals[3] : 0.0;
            return j;
        }

        case GDEXTENSION_VARIANT_TYPE_QUATERNION: {
            std::string str = GodotAPI::variant_to_string(p_variant);
            auto vals = parse_float_list(str);
            json j = {{"type", "Quaternion"}};
            j["x"] = vals.size() > 0 ? vals[0] : 0.0;
            j["y"] = vals.size() > 1 ? vals[1] : 0.0;
            j["z"] = vals.size() > 2 ? vals[2] : 0.0;
            j["w"] = vals.size() > 3 ? vals[3] : 0.0;
            return j;
        }

        case GDEXTENSION_VARIANT_TYPE_OBJECT: {
            GDExtensionObjectPtr obj = GodotAPI::variant_to_object(p_variant);
            json j = {{"type", "Object"}};
            if (obj) {
                std::string class_name = GodotAPI::call_method_string(obj, "get_class");
                j["class"] = class_name.empty() ? "Object" : class_name;
                // Get resource path if it's a Resource
                std::string res_path = GodotAPI::call_method_string(obj, "get_path");
                if (!res_path.empty()) j["path"] = res_path;
            } else {
                j["class"] = "null";
            }
            return j;
        }

        case GDEXTENSION_VARIANT_TYPE_DICTIONARY:
        case GDEXTENSION_VARIANT_TYPE_ARRAY:
        case GDEXTENSION_VARIANT_TYPE_PACKED_STRING_ARRAY:
        case GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY:
        case GDEXTENSION_VARIANT_TYPE_PACKED_INT64_ARRAY:
        case GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY:
        case GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT64_ARRAY:
        case GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR2_ARRAY:
        case GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR3_ARRAY:
        case GDEXTENSION_VARIANT_TYPE_PACKED_COLOR_ARRAY: {
            // For complex container types, use stringify as a string representation
            std::string str = GodotAPI::variant_to_string(p_variant);
            json j = {{"type", variant_type_name(type)}, {"value", str}};
            return j;
        }

        default: {
            // Fallback: stringify
            std::string str = GodotAPI::variant_to_string(p_variant);
            if (str.empty()) return nullptr;
            return str;
        }
    }
}

std::string serialize(GDExtensionConstVariantPtr p_variant) {
    return variant_to_json(p_variant).dump();
}

std::string property_to_json(GDExtensionConstVariantPtr p_variant) {
    if (!p_variant) return "null";
    return variant_to_json(p_variant).dump();
}

// ===========================================================================
// JSON -> Variant
// ===========================================================================

bool json_to_variant(const json &p_json, GDExtensionUninitializedVariantPtr r_variant) {
    if (!r_variant) return false;

    // Null
    if (p_json.is_null()) {
        GodotAPI::make_variant_nil(r_variant);
        return true;
    }

    // Boolean
    if (p_json.is_boolean()) {
        GodotAPI::make_variant_bool(r_variant, p_json.get<bool>());
        return true;
    }

    // Integer
    if (p_json.is_number_integer()) {
        GodotAPI::make_variant_int(r_variant, p_json.get<int64_t>());
        return true;
    }

    // Float
    if (p_json.is_number_float()) {
        GodotAPI::make_variant_float(r_variant, p_json.get<double>());
        return true;
    }

    // String (no type tag)
    if (p_json.is_string()) {
        GodotAPI::make_variant_string(r_variant, p_json.get<std::string>());
        return true;
    }

    // Object with type tag
    if (p_json.is_object() && p_json.contains("type")) {
        std::string type = p_json.value("type", "");

        if (type == "Vector2") {
            // Can't construct Vector2 variants easily without ptr constructors
            // Store as string representation for now
            double x = p_json.value("x", 0.0);
            double y = p_json.value("y", 0.0);
            std::string str = std::to_string(x) + ", " + std::to_string(y);
            GodotAPI::make_variant_string(r_variant, str);
            return true;
        }

        if (type == "Vector2i") {
            int64_t x = p_json.value("x", 0);
            int64_t y = p_json.value("y", 0);
            std::string str = std::to_string(x) + ", " + std::to_string(y);
            GodotAPI::make_variant_string(r_variant, str);
            return true;
        }

        if (type == "Vector3") {
            double x = p_json.value("x", 0.0);
            double y = p_json.value("y", 0.0);
            double z = p_json.value("z", 0.0);
            std::string str = std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z);
            GodotAPI::make_variant_string(r_variant, str);
            return true;
        }

        if (type == "Vector3i") {
            int64_t x = p_json.value("x", 0);
            int64_t y = p_json.value("y", 0);
            int64_t z = p_json.value("z", 0);
            std::string str = std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z);
            GodotAPI::make_variant_string(r_variant, str);
            return true;
        }

        if (type == "Color") {
            if (p_json.contains("hex")) {
                GodotAPI::make_variant_string(r_variant, p_json.value("hex", ""));
            } else {
                double r = p_json.value("r", 1.0);
                double g = p_json.value("g", 1.0);
                double b = p_json.value("b", 1.0);
                double a = p_json.value("a", 1.0);
                std::string str = std::to_string(r) + ", " + std::to_string(g) + ", " +
                                   std::to_string(b) + ", " + std::to_string(a);
                GodotAPI::make_variant_string(r_variant, str);
            }
            return true;
        }

        if (type == "NodePath") {
            GodotAPI::make_variant_string(r_variant, p_json.value("path", ""));
            return true;
        }

        // Unknown type - try as string
        GodotAPI::make_variant_string(r_variant, p_json.dump());
        return true;
    }

    // Object without type tag - store as string
    if (p_json.is_object()) {
        GodotAPI::make_variant_string(r_variant, p_json.dump());
        return true;
    }

    // Fallback
    GodotAPI::make_variant_nil(r_variant);
    return true;
}

bool parse(const std::string &p_json_str, GDExtensionUninitializedVariantPtr r_variant) {
    try {
        json j = json::parse(p_json_str);
        return json_to_variant(j, r_variant);
    } catch (...) {
        // Try as plain string
        GodotAPI::make_variant_string(r_variant, p_json_str);
        return true;
    }
}

} // namespace type_codec
} // namespace gear_mcp
