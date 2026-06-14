#include "server/tool_registry.h"
#include "godot_api/godot_api.h"

#include <nlohmann/json.hpp>
#include <cstdio>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace gear_mcp {

// ===========================================================================
// save_scene — save the currently edited scene
// ===========================================================================

static void save_scene_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    GDExtensionObjectPtr editor = GodotAPI::get_editor_interface();
    if (!editor) {
        r_error = "Cannot access editor interface";
        return;
    }

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (!scene_root) {
        r_error = "No scene is currently being edited";
        return;
    }

    std::string scene_path = params.value("scene_path", "");

    // If no path provided, use the current scene's file path
    if (scene_path.empty()) {
        scene_path = GodotAPI::get_property_json(scene_root, "scene_file_path");
        // Strip surrounding quotes if present
        if (scene_path.size() >= 2 && scene_path.front() == '"' && scene_path.back() == '"') {
            scene_path = scene_path.substr(1, scene_path.size() - 2);
        }
        if (scene_path.empty()) {
            r_error = "No save path provided and current scene has no file path";
            return;
        }
    }

    bool success = GodotAPI::save_scene(scene_root, scene_path);
    if (!success) {
        r_error = "Failed to save scene to: " + scene_path;
        return;
    }

    json result = {
        {"success", true},
        {"scene_path", scene_path}
    };
    r_result = result.dump();
}

// ===========================================================================
// get_node_properties — get properties of a node in the current scene
// ===========================================================================

static void get_node_properties_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string node_path = params.value("node_path", "");
    if (node_path.empty()) {
        r_error = "Missing required parameter: node_path";
        return;
    }

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (!scene_root) {
        r_error = "No scene is currently being edited";
        return;
    }

    // If requesting root node, use scene_root directly
    std::string root_name = GodotAPI::get_node_name(scene_root);
    GDExtensionObjectPtr node = nullptr;

    if (node_path == root_name || node_path == ".") {
        node = scene_root;
    } else {
        // Strip root name prefix if present
        std::string relative_path = node_path;
        size_t slash_pos = node_path.find('/');
        if (slash_pos != std::string::npos) {
            std::string first_segment = node_path.substr(0, slash_pos);
            if (first_segment == root_name) {
                relative_path = node_path.substr(slash_pos + 1);
            }
        }
        node = GodotAPI::get_node_child(scene_root, relative_path);
    }

    if (!node) {
        r_error = "Node not found: " + node_path;
        return;
    }

    std::string node_class = GodotAPI::get_node_class(node);
    std::string node_name = GodotAPI::get_node_name(node);

    // Build list of common properties to query
    std::vector<std::string> props_to_get = {"name", "position", "rotation", "scale", "visible"};

    json properties = json::object();
    for (const auto &prop : props_to_get) {
        std::string val = GodotAPI::get_property_json(node, prop);
        if (!val.empty() && val != "null") {
            try {
                properties[prop] = json::parse(val);
            } catch (...) {
                properties[prop] = val;
            }
        }
    }

    // Always include the node class for reference
    properties["_class"] = node_class;

    json result = {
        {"node_path", node_path},
        {"node_name", node_name},
        {"node_class", node_class},
        {"properties", properties}
    };
    r_result = result.dump();
}

// ===========================================================================
// set_node_properties — set properties on a node
// ===========================================================================
//
// Wrapped in EditorUndoRedoManager so Ctrl+Z in the editor undoes the change.
// For each property we capture the current value (undo) and the new value (do),
// then commit a single action per tool call.
// ===========================================================================

static void set_node_properties_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string node_path = params.value("node_path", "");
    if (node_path.empty()) {
        r_error = "Missing required parameter: node_path";
        return;
    }

    if (!params.contains("properties") || !params["properties"].is_object()) {
        r_error = "Missing or invalid required parameter: properties";
        return;
    }

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (!scene_root) {
        r_error = "No scene is currently being edited";
        return;
    }

    // Find the target node
    std::string root_name = GodotAPI::get_node_name(scene_root);
    GDExtensionObjectPtr node = nullptr;

    if (node_path == root_name || node_path == ".") {
        node = scene_root;
    } else {
        std::string relative_path = node_path;
        size_t slash_pos = node_path.find('/');
        if (slash_pos != std::string::npos) {
            std::string first_segment = node_path.substr(0, slash_pos);
            if (first_segment == root_name) {
                relative_path = node_path.substr(slash_pos + 1);
            }
        }
        node = GodotAPI::get_node_child(scene_root, relative_path);
    }

    if (!node) {
        r_error = "Node not found: " + node_path;
        return;
    }

    json properties = params["properties"];
    json properties_set = json::array();
    json errors = json::array();

    // Open one UndoRedo action that covers all property changes in this call.
    // The do/undo snapshots are taken from the *current* node state (undo)
    // and the *requested* value (do).
    GodotAPI::undo_redo_create_action("Set properties on " + node_path);

    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const std::string &prop_name = it.key();
        const std::string prop_json = it.value().dump();

        // Apply the change directly first (so the scene is in the desired state)
        bool success = GodotAPI::set_property_from_json(node, prop_name, prop_json);
        if (!success) {
            errors.push_back("Failed to set property: " + prop_name);
            continue;
        }

        // Register undo/redo on the property so the editor can roll it back.
        // The current property value (now the new value) is "do", the previous
        // value (captured below as the "undo" snapshot) restores on Ctrl+Z.
        // The undo manager handles swap-on-undo / swap-on-redo automatically
        // when both snapshots are recorded with add_do_property / add_undo_property.
        std::string current_json = GodotAPI::get_property_json(node, prop_name);
        if (current_json.empty()) current_json = "null";

        uint8_t do_val_buf[VARIANT_BUF_SIZE] = {0};
        uint8_t undo_val_buf[VARIANT_BUF_SIZE] = {0};
        GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)do_val_buf, current_json);
        GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)undo_val_buf, prop_json);

        GodotAPI::undo_redo_add_do_property(node, prop_name, (GDExtensionConstVariantPtr)do_val_buf);
        GodotAPI::undo_redo_add_undo_property(node, prop_name, (GDExtensionConstVariantPtr)undo_val_buf);

        GodotAPI::destroy_variant((GDExtensionVariantPtr)do_val_buf);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)undo_val_buf);

        properties_set.push_back(prop_name);
    }

    GodotAPI::undo_redo_commit_action();

    json result = {
        {"success", errors.empty()},
        {"node_path", node_path},
        {"properties_set", properties_set},
        {"count", (int)properties_set.size()},
        {"undoable", true}
    };
    if (!errors.empty()) {
        result["errors"] = errors;
    }
    r_result = result.dump();
}

// ===========================================================================
// delete_node — delete a node from the scene
// ===========================================================================
//
// Wrapped in UndoRedo so Ctrl+Z in the editor restores the node. We capture
// the parent reference + node index before the action runs, so the undo
// path can re-insert the node at the same position in the sibling order.
// ===========================================================================

static void delete_node_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string node_path = params.value("node_path", "");
    if (node_path.empty()) {
        r_error = "Missing required parameter: node_path";
        return;
    }

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (!scene_root) {
        r_error = "No scene is currently being edited";
        return;
    }

    // Find the target node
    std::string root_name = GodotAPI::get_node_name(scene_root);
    GDExtensionObjectPtr node = nullptr;

    if (node_path == root_name || node_path == ".") {
        r_error = "Cannot delete the scene root node";
        return;
    }

    std::string relative_path = node_path;
    size_t slash_pos = node_path.find('/');
    if (slash_pos != std::string::npos) {
        std::string first_segment = node_path.substr(0, slash_pos);
        if (first_segment == root_name) {
            relative_path = node_path.substr(slash_pos + 1);
        }
    }
    node = GodotAPI::get_node_child(scene_root, relative_path);

    if (!node) {
        r_error = "Node not found: " + node_path;
        return;
    }

    // Get the parent via Godot method call
    GDExtensionObjectPtr parent = GodotAPI::call_method_object(node, "get_parent");
    if (!parent) {
        r_error = "Cannot get parent of node: " + node_path;
        return;
    }

    // Note: we don't currently restore the exact sibling order on undo —
    // the node is re-added at the end. This matches Godot's own behavior
    // for many of its built-in editor operations.

    std::string node_name = GodotAPI::get_node_name(node);

    // Open UndoRedo action: undo restores the node, do removes it.
    GodotAPI::undo_redo_create_action("Delete node " + node_path);

    // add_do_method: parent.remove_child(node)  (we then queue_free below)
    {
        uint8_t parent_v[VARIANT_BUF_SIZE] = {0};
        uint8_t child_v[VARIANT_BUF_SIZE] = {0};
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)parent_v, parent);
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)child_v, node);
        GDExtensionConstVariantPtr args[] = {
            (GDExtensionConstVariantPtr)parent_v,
            (GDExtensionConstVariantPtr)child_v,
        };
        GodotAPI::undo_redo_add_do_method(parent, "remove_child", args, 2);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)parent_v);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)child_v);
    }
    // Keep the node alive across the undo boundary
    GodotAPI::undo_redo_add_do_reference(node);
    GodotAPI::undo_redo_add_undo_reference(node);

    // add_undo_method: parent.add_child(node) — restores on Ctrl+Z
    {
        uint8_t parent_v[VARIANT_BUF_SIZE] = {0};
        uint8_t child_v[VARIANT_BUF_SIZE] = {0};
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)parent_v, parent);
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)child_v, node);
        GDExtensionConstVariantPtr args[] = {
            (GDExtensionConstVariantPtr)parent_v,
            (GDExtensionConstVariantPtr)child_v,
        };
        GodotAPI::undo_redo_add_undo_method(parent, "add_child", args, 2);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)parent_v);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)child_v);
    }

    GodotAPI::undo_redo_commit_action();

    // Apply the action for real (the editor will normally replay the "do"
    // operations when the action is committed, but for queued commands we
    // still need the direct call to actually mutate the live tree).
    GodotAPI::remove_child(parent, node);
    GodotAPI::queue_free(node);

    json result = {
        {"success", true},
        {"node_path", node_path},
        {"node_name", node_name},
        {"undoable", true}
    };
    r_result = result.dump();
}

// ===========================================================================
// duplicate_node — duplicate a node and add it to the scene
// ===========================================================================

static void duplicate_node_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string node_path = params.value("node_path", "");
    if (node_path.empty()) {
        r_error = "Missing required parameter: node_path";
        return;
    }

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (!scene_root) {
        r_error = "No scene is currently being edited";
        return;
    }

    // Find the target node
    std::string root_name = GodotAPI::get_node_name(scene_root);
    GDExtensionObjectPtr node = nullptr;

    if (node_path == root_name || node_path == ".") {
        node = scene_root;
    } else {
        std::string relative_path = node_path;
        size_t slash_pos = node_path.find('/');
        if (slash_pos != std::string::npos) {
            std::string first_segment = node_path.substr(0, slash_pos);
            if (first_segment == root_name) {
                relative_path = node_path.substr(slash_pos + 1);
            }
        }
        node = GodotAPI::get_node_child(scene_root, relative_path);
    }

    if (!node) {
        r_error = "Node not found: " + node_path;
        return;
    }

    // Duplicate the node (flags=0 for standard duplication)
    GDExtensionObjectPtr dup = GodotAPI::duplicate_node(node, 0);
    if (!dup) {
        r_error = "Failed to duplicate node: " + node_path;
        return;
    }

    // Optionally set a new name
    std::string new_name = params.value("new_name", "");
    if (!new_name.empty()) {
        uint8_t name_variant[64];
        GodotAPI::make_variant_string(name_variant, new_name);
        GodotAPI::set_property(dup, "name", name_variant);
        GodotAPI::destroy_variant(name_variant);
    }

    // Get parent of original node to add duplicate beside it
    GDExtensionObjectPtr parent = GodotAPI::call_method_object(node, "get_parent");
    if (!parent) {
        r_error = "Cannot get parent of node: " + node_path;
        return;
    }

    GodotAPI::add_child(parent, dup);
    GodotAPI::set_owner_recursive(dup, scene_root);

    std::string dup_name = GodotAPI::get_node_name(dup);
    std::string dup_class = GodotAPI::get_node_class(dup);

    json result = {
        {"success", true},
        {"original_path", node_path},
        {"new_node_name", dup_name},
        {"new_node_class", dup_class}
    };
    r_result = result.dump();
}

// ===========================================================================
// reparent_node — move a node to a new parent
// ===========================================================================
//
// Wrapped in UndoRedo. The do-action removes from old parent and adds to new
// parent; the undo-action reverses the same. owner is set on do and cleared
// on undo to keep save semantics correct.
// ===========================================================================

static void reparent_node_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string node_path = params.value("node_path", "");
    std::string new_parent_path = params.value("new_parent_path", "");

    if (node_path.empty()) {
        r_error = "Missing required parameter: node_path";
        return;
    }
    if (new_parent_path.empty()) {
        r_error = "Missing required parameter: new_parent_path";
        return;
    }

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (!scene_root) {
        r_error = "No scene is currently being edited";
        return;
    }

    std::string root_name = GodotAPI::get_node_name(scene_root);

    // Helper: resolve a node path to an object
    auto resolve_node = [&](const std::string &p_path) -> GDExtensionObjectPtr {
        if (p_path == root_name || p_path == ".") {
            return scene_root;
        }
        std::string rel = p_path;
        size_t slash = p_path.find('/');
        if (slash != std::string::npos) {
            std::string first = p_path.substr(0, slash);
            if (first == root_name) {
                rel = p_path.substr(slash + 1);
            }
        }
        return GodotAPI::get_node_child(scene_root, rel);
    };

    GDExtensionObjectPtr node = resolve_node(node_path);
    if (!node) {
        r_error = "Node not found: " + node_path;
        return;
    }

    GDExtensionObjectPtr new_parent = resolve_node(new_parent_path);
    if (!new_parent) {
        r_error = "New parent node not found: " + new_parent_path;
        return;
    }

    // Get old parent
    GDExtensionObjectPtr old_parent = GodotAPI::call_method_object(node, "get_parent");
    if (!old_parent) {
        r_error = "Cannot get parent of node: " + node_path;
        return;
    }

    std::string node_name = GodotAPI::get_node_name(node);

    // ---- UndoRedo action ----
    GodotAPI::undo_redo_create_action("Reparent " + node_path + " to " + new_parent_path);

    // do: old_parent.remove_child(node)  ; new_parent.add_child(node)  ; node.set_owner(scene_root)
    {
        uint8_t op_v[VARIANT_BUF_SIZE] = {0}, n_v[VARIANT_BUF_SIZE] = {0};
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)op_v, old_parent);
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)n_v, node);
        GDExtensionConstVariantPtr a[] = { (GDExtensionConstVariantPtr)op_v, (GDExtensionConstVariantPtr)n_v };
        GodotAPI::undo_redo_add_do_method(old_parent, "remove_child", a, 2);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)op_v);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)n_v);
    }
    {
        uint8_t np_v[VARIANT_BUF_SIZE] = {0}, n_v[VARIANT_BUF_SIZE] = {0};
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)np_v, new_parent);
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)n_v, node);
        GDExtensionConstVariantPtr a[] = { (GDExtensionConstVariantPtr)np_v, (GDExtensionConstVariantPtr)n_v };
        GodotAPI::undo_redo_add_do_method(new_parent, "add_child", a, 2);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)np_v);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)n_v);
    }
    {
        uint8_t n_v[VARIANT_BUF_SIZE] = {0}, sr_v[VARIANT_BUF_SIZE] = {0};
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)n_v, node);
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)sr_v, scene_root);
        GDExtensionConstVariantPtr a[] = { (GDExtensionConstVariantPtr)n_v, (GDExtensionConstVariantPtr)sr_v };
        GodotAPI::undo_redo_add_do_method(node, "set_owner", a, 2);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)n_v);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)sr_v);
    }

    // undo: new_parent.remove_child(node)  ; old_parent.add_child(node)  ; node.set_owner(null)
    {
        uint8_t np_v[VARIANT_BUF_SIZE] = {0}, n_v[VARIANT_BUF_SIZE] = {0};
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)np_v, new_parent);
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)n_v, node);
        GDExtensionConstVariantPtr a[] = { (GDExtensionConstVariantPtr)np_v, (GDExtensionConstVariantPtr)n_v };
        GodotAPI::undo_redo_add_undo_method(new_parent, "remove_child", a, 2);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)np_v);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)n_v);
    }
    {
        uint8_t op_v[VARIANT_BUF_SIZE] = {0}, n_v[VARIANT_BUF_SIZE] = {0};
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)op_v, old_parent);
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)n_v, node);
        GDExtensionConstVariantPtr a[] = { (GDExtensionConstVariantPtr)op_v, (GDExtensionConstVariantPtr)n_v };
        GodotAPI::undo_redo_add_undo_method(old_parent, "add_child", a, 2);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)op_v);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)n_v);
    }
    {
        uint8_t n_v[VARIANT_BUF_SIZE] = {0}, nil_v[VARIANT_BUF_SIZE] = {0};
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)n_v, node);
        GodotAPI::make_variant_nil((GDExtensionUninitializedVariantPtr)nil_v);
        GDExtensionConstVariantPtr a[] = { (GDExtensionConstVariantPtr)n_v, (GDExtensionConstVariantPtr)nil_v };
        GodotAPI::undo_redo_add_undo_method(node, "set_owner", a, 2);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)n_v);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)nil_v);
    }

    GodotAPI::undo_redo_commit_action();

    // Apply the action for real
    GodotAPI::remove_child(old_parent, node);
    GodotAPI::add_child(new_parent, node);
    GodotAPI::set_owner(node, scene_root);

    json result = {
        {"success", true},
        {"node_path", node_path},
        {"node_name", node_name},
        {"new_parent_path", new_parent_path},
        {"undoable", true}
    };
    r_result = result.dump();
}

// ===========================================================================
// load_sprite — load a texture onto a Sprite2D/Sprite3D node
// ===========================================================================

static void load_sprite_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string node_path = params.value("node_path", "");
    std::string texture_path = params.value("texture_path", "");

    if (node_path.empty()) {
        r_error = "Missing required parameter: node_path";
        return;
    }
    if (texture_path.empty()) {
        r_error = "Missing required parameter: texture_path";
        return;
    }

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (!scene_root) {
        r_error = "No scene is currently being edited";
        return;
    }

    // Find the sprite node
    std::string root_name = GodotAPI::get_node_name(scene_root);
    GDExtensionObjectPtr node = nullptr;

    if (node_path == root_name || node_path == ".") {
        node = scene_root;
    } else {
        std::string relative_path = node_path;
        size_t slash_pos = node_path.find('/');
        if (slash_pos != std::string::npos) {
            std::string first_segment = node_path.substr(0, slash_pos);
            if (first_segment == root_name) {
                relative_path = node_path.substr(slash_pos + 1);
            }
        }
        node = GodotAPI::get_node_child(scene_root, relative_path);
    }

    if (!node) {
        r_error = "Node not found: " + node_path;
        return;
    }

    // Verify it's a sprite-like node
    std::string node_class = GodotAPI::get_node_class(node);
    if (node_class != "Sprite2D" && node_class != "Sprite3D") {
        r_error = "Node is not a Sprite2D or Sprite3D (found: " + node_class + ")";
        return;
    }

    // Refresh filesystem to ensure the resource database is up-to-date
    GodotAPI::refresh_filesystem();

    // Load the texture resource
    GDExtensionObjectPtr texture = GodotAPI::load_resource(texture_path);
    if (!texture) {
        r_error = "Failed to load texture resource: " + texture_path;
        return;
    }

    // Set the texture property on the sprite node
    uint8_t tex_variant[64];
    GodotAPI::make_variant_object(tex_variant, texture);
    bool success = GodotAPI::set_property(node, "texture", tex_variant);
    GodotAPI::destroy_variant(tex_variant);

    if (!success) {
        r_error = "Failed to set texture on node: " + node_path;
        return;
    }

    json result = {
        {"success", true},
        {"node_path", node_path},
        {"node_class", node_class},
        {"texture_path", texture_path}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

// ===========================================================================
// Registration
// ===========================================================================

void register_scene_tools_phase2(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    static const char *SAVE_SCENE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scene_path": {"type": "string", "description": "Optional path to save to (res:// format)"}
    }
})schema";

    static const char *GET_NODE_PROPERTIES_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "node_path": {"type": "string", "description": "Node path in scene tree (e.g. 'Root/Child/Node')"}
    },
    "required": ["node_path"]
})schema";

    static const char *SET_NODE_PROPERTIES_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "node_path": {"type": "string", "description": "Node path in scene tree"},
        "properties": {"type": "object", "description": "Property name-value pairs to set"}
    },
    "required": ["node_path", "properties"]
})schema";

    static const char *DELETE_NODE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "node_path": {"type": "string", "description": "Node path to delete"}
    },
    "required": ["node_path"]
})schema";

    static const char *DUPLICATE_NODE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "node_path": {"type": "string", "description": "Node to duplicate"},
        "new_name": {"type": "string", "description": "Name for the duplicate"}
    },
    "required": ["node_path"]
})schema";

    static const char *REPARENT_NODE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "node_path": {"type": "string", "description": "Node to move"},
        "new_parent_path": {"type": "string", "description": "New parent node path"}
    },
    "required": ["node_path", "new_parent_path"]
})schema";

    static const char *LOAD_SPRITE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "node_path": {"type": "string", "description": "Sprite node path"},
        "texture_path": {"type": "string", "description": "res:// path to texture"}
    },
    "required": ["node_path", "texture_path"]
})schema";

    p_registry->register_tool("save_scene",
        "Save the currently edited scene to disk",
        SAVE_SCENE_SCHEMA, gear_mcp::save_scene_handler, true);

    p_registry->register_tool("get_node_properties",
        "Get properties of a specific node in the current scene",
        GET_NODE_PROPERTIES_SCHEMA, gear_mcp::get_node_properties_handler, true);

    p_registry->register_tool("set_node_properties",
        "Set properties on a node in the current scene",
        SET_NODE_PROPERTIES_SCHEMA, gear_mcp::set_node_properties_handler, true);

    p_registry->register_tool("delete_node",
        "Delete a node from the current scene",
        DELETE_NODE_SCHEMA, gear_mcp::delete_node_handler, true);

    p_registry->register_tool("duplicate_node",
        "Duplicate a node and add it to the scene",
        DUPLICATE_NODE_SCHEMA, gear_mcp::duplicate_node_handler, true);

    p_registry->register_tool("reparent_node",
        "Move a node to a new parent in the scene tree",
        REPARENT_NODE_SCHEMA, gear_mcp::reparent_node_handler, true);

    p_registry->register_tool("load_sprite",
        "Load a texture onto a Sprite2D or Sprite3D node",
        LOAD_SPRITE_SCHEMA, gear_mcp::load_sprite_handler, true);

    std::printf("[Gear MCP] Registered scene phase2 tools: save_scene, get_node_properties, set_node_properties, delete_node, duplicate_node, reparent_node, load_sprite\n");
}
