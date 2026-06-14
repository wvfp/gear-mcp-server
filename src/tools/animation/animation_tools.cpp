#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static std::string resolve_res_path(const std::string &p_input) {
    if (p_input.empty()) return p_input;
    if (p_input.substr(0, 6) == "res://") return p_input;
    return std::string("res://") + p_input;
}

static std::string resolve_abs_path(const std::string &p_input) {
    if (p_input.empty()) return p_input;
    if (p_input.substr(0, 6) == "res://") {
        return GodotAPI::res_to_absolute(p_input);
    }
    return p_input;
}

// ---------------------------------------------------------------------------
// create_animation — creates a new Animation resource and saves it as .tres
// ---------------------------------------------------------------------------

static const char *CREATE_ANIMATION_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "resource_path": {"type": "string", "description": "res:// path for the new .tres file (e.g., res://anims/jump.tres)"},
        "animation_name": {"type": "string", "description": "Name to embed in the Animation resource", "default": "default"},
        "length": {"type": "number", "description": "Animation length in seconds", "default": 1.0},
        "loop_mode": {"type": "string", "description": "none|linear|pingpong", "default": "none"},
        "step": {"type": "number", "description": "Step (snap) value", "default": 0.1}
    },
    "required": ["resource_path"]
})schema";

static void handle_create_animation(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string res_path = resolve_res_path(params.value("resource_path", ""));
    if (res_path.empty()) { r_error = "Missing required parameter: resource_path"; return; }

    std::string animation_name = params.value("animation_name", "default");
    double length = params.value("length", 1.0);
    double step = params.value("step", 0.1);
    std::string loop_mode = params.value("loop_mode", "none");

    int loop_int = 0; // LOOP_NONE
    if (loop_mode == "linear") loop_int = 1;
    else if (loop_mode == "pingpong") loop_int = 2;

    std::string abs_path = GodotAPI::res_to_absolute(res_path);
    path_utils::ensure_parent_dirs(abs_path);

    std::ofstream file(abs_path, std::ios::trunc);
    if (!file.is_open()) { r_error = "Cannot create animation file: " + abs_path; return; }

    file << "[gd_resource type=\"Animation\" format=3]\n\n";
    file << "[resource]\n";
    file << "length = " << length << "\n";
    file << "loop_mode = " << loop_int << "\n";
    file << "step = " << step << "\n";
    file.close();

    json result = {
        {"success", true},
        {"resource_path", res_path},
        {"animation_name", animation_name},
        {"length", length},
        {"loop_mode", loop_mode}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// add_animation_track — append a track to an existing Animation .tres file
// ---------------------------------------------------------------------------

static const char *ADD_ANIMATION_TRACK_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "resource_path": {"type": "string", "description": "res:// path to the Animation .tres file"},
        "track_type": {"type": "string", "description": "value|method (Animation.TYPE_VALUE/TYPE_METHOD)"},
        "track_path": {"type": "string", "description": "NodePath:track-path (e.g., Player:position:x)"},
        "keyframes": {
            "type": "array",
            "description": "List of {time, value} for value tracks or {time, method, args} for method tracks"
        }
    },
    "required": ["resource_path", "track_path"]
})schema";

static void handle_add_animation_track(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string res_path = resolve_res_path(params.value("resource_path", ""));
    if (res_path.empty()) { r_error = "Missing required parameter: resource_path"; return; }

    std::string abs_path = GodotAPI::res_to_absolute(res_path);
    std::ifstream in_file(abs_path);
    if (!in_file.is_open()) { r_error = "Cannot open animation file: " + abs_path; return; }

    std::stringstream ss; ss << in_file.rdbuf();
    std::string content = ss.str();
    in_file.close();

    // Strip the [gd_resource ...] and [resource] header; we'll re-emit the file.
    // We append tracks to the existing content. Godot's animation format is one
    // track = two lines (tracks/N/type, tracks/N/path, ...) plus key entries.
    // Here we use a simple per-track append: each track is described with a
    // "tracks/<i>/path" and "tracks/<i>/type" plus nested key entries.
    // We compute the next index by scanning for existing "tracks/N/" prefixes.
    int next_idx = 0;
    {
        const std::string token = "tracks/";
        size_t pos = 0;
        while ((pos = content.find(token, pos)) != std::string::npos) {
            size_t slash = content.find('/', pos + token.size());
            if (slash == std::string::npos) break;
            std::string num = content.substr(pos + token.size(), slash - pos - token.size());
            try { next_idx = std::max(next_idx, std::stoi(num) + 1); } catch (...) {}
            pos = slash + 1;
        }
    }

    std::string track_type = params.value("track_type", "value");
    int type_int = (track_type == "method") ? 2 : 0; // 0=TYPE_VALUE, 2=TYPE_METHOD

    std::string track_path = params.value("track_path", "");

    std::ofstream out_file(abs_path, std::ios::trunc);
    if (!out_file.is_open()) { r_error = "Cannot write to animation file: " + abs_path; return; }
    out_file << content;
    if (!content.empty() && content.back() != '\n') out_file << "\n";

    out_file << "tracks/" << next_idx << "/type = \"" << (type_int == 2 ? "method" : "value") << "\"\n";
    out_file << "tracks/" << next_idx << "/path = NodePath(\"" << track_path << "\")\n";

    int key_count = 0;
    if (params.contains("keyframes") && params["keyframes"].is_array()) {
        for (size_t i = 0; i < params["keyframes"].size(); i++) {
            const auto &kf = params["keyframes"][i];
            double time = kf.value("time", 0.0);
            out_file << "tracks/" << next_idx << "/keys/" << i << "/time = " << time << "\n";
            if (type_int == 2) {
                std::string method = kf.value("method", "");
                out_file << "tracks/" << next_idx << "/keys/" << i << "/method = &\"" << method << "\"\n";
                if (kf.contains("args") && kf["args"].is_array()) {
                    for (size_t a = 0; a < kf["args"].size(); a++) {
                        out_file << "tracks/" << next_idx << "/keys/" << i << "/args/" << a << " = "
                                 << kf["args"][a].dump() << "\n";
                    }
                }
            } else {
                out_file << "tracks/" << next_idx << "/keys/" << i << "/value = "
                         << kf.value("value", json(nullptr)).dump() << "\n";
            }
            key_count++;
        }
    }
    out_file.close();

    json result = {
        {"success", true},
        {"resource_path", res_path},
        {"track_index", next_idx},
        {"track_type", track_type},
        {"key_count", key_count}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// create_animation_tree — adds an AnimationTree node to a scene
// ---------------------------------------------------------------------------

static const char *CREATE_ANIMATION_TREE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scene_path": {"type": "string", "description": "res:// path to the scene"},
        "parent_path": {"type": "string", "description": "Parent node path in scene (default '.')", "default": "."},
        "node_name": {"type": "string", "description": "Name for the new AnimationTree node", "default": "AnimationTree"},
        "anim_player_path": {"type": "string", "description": "NodePath to the AnimationPlayer", "default": ""},
        "root_type": {"type": "string", "description": "StateMachine|BlendTree|BlendSpace1D|BlendSpace2D", "default": "StateMachine"}
    },
    "required": ["scene_path"]
})schema";

static void handle_create_animation_tree(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string scene_path = params.value("scene_path", "");
    std::string parent_path = params.value("parent_path", ".");
    std::string node_name = params.value("node_name", "AnimationTree");
    std::string anim_player_path = params.value("anim_player_path", "");
    std::string root_type = params.value("root_type", "StateMachine");

    if (scene_path.empty()) { r_error = "Missing required parameter: scene_path"; return; }

    // Try the live editor scene first
    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (scene_root) {
        std::string edited = GodotAPI::get_property_json(scene_root, "scene_file_path");
        if (edited.size() >= 2 && edited.front() == '"' && edited.back() == '"') {
            edited = edited.substr(1, edited.size() - 2);
        }
        if (edited == scene_path) {
            GDExtensionObjectPtr tree_node = GodotAPI::create_node("AnimationTree");
            if (!tree_node) { r_error = "Failed to create AnimationTree node"; return; }

            // Resolve parent
            GDExtensionObjectPtr parent = scene_root;
            std::string root_name = GodotAPI::get_node_name(scene_root);
            if (parent_path != "." && parent_path != root_name) {
                GDExtensionObjectPtr resolved = GodotAPI::get_node_child(scene_root, parent_path);
                if (!resolved) {
                    GodotAPI::queue_free(tree_node);
                    r_error = "Parent node not found: " + parent_path;
                    return;
                }
                parent = resolved;
            }

            // Set the name
            {
                uint8_t name_v[64] = {0};
                GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)name_v, node_name);
                GodotAPI::set_property(tree_node, "name", (GDExtensionConstVariantPtr)name_v);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)name_v);
            }

            // Add to parent FIRST, then set owner (owner must be an ancestor).
            GodotAPI::add_child(parent, tree_node);
            GodotAPI::set_owner_recursive(tree_node, scene_root);

            // Set anim_player property if provided
            if (!anim_player_path.empty()) {
                uint8_t path_v[64] = {0};
                GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)path_v, anim_player_path);
                GodotAPI::set_property(tree_node, "anim_player", (GDExtensionConstVariantPtr)path_v);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)path_v);
            }

            json result = {
                {"success", true},
                {"node_name", node_name},
                {"root_type", root_type},
                {"persisted", false}
            };
            r_result = result.dump();
            return;
        }
    }

    // Fallback: file I/O (only safe if scene file already has a [gd_scene] header)
    std::string abs_path = GodotAPI::res_to_absolute(scene_path);
    std::ifstream check(abs_path);
    bool has_header = false;
    if (check.is_open()) {
        std::string first_line;
        std::getline(check, first_line);
        has_header = (first_line.find("[gd_scene") != std::string::npos);
        check.close();
    }
    if (!has_header) {
        r_error = "Cannot append to scene file: missing [gd_scene] header. Create the scene first via create_scene().";
        return;
    }
    std::ofstream file(abs_path, std::ios::app);
    if (!file.is_open()) { r_error = "Cannot open scene file: " + abs_path; return; }

    file << "[node name=\"" << node_name << "\" type=\"AnimationTree\"";
    if (parent_path == ".") file << " parent=\".\"";
    else file << " parent=\"" << parent_path << "\"";
    file << "]\n";
    if (!anim_player_path.empty()) {
        file << "anim_player = NodePath(\"" << anim_player_path << "\")\n";
    }
    file.close();

    json result = {
        {"success", true},
        {"node_name", node_name},
        {"root_type", root_type},
        {"persisted", true}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// add_animation_state — adds an AnimationNodeStateMachine state via the
// state machine's tree_root (live editor API path). Falls back to no-op
// stub when the scene isn't open.
// ---------------------------------------------------------------------------

static const char *ADD_ANIMATION_STATE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scene_path": {"type": "string", "description": "res:// path of the scene containing the AnimationTree"},
        "anim_tree_path": {"type": "string", "description": "Node path to the AnimationTree"},
        "state_name": {"type": "string", "description": "Name for the new state"},
        "animation_name": {"type": "string", "description": "Animation to play in this state"}
    },
    "required": ["scene_path", "anim_tree_path", "state_name", "animation_name"]
})schema";

static void handle_add_animation_state(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string state_name = params.value("state_name", "");
    std::string animation_name = params.value("animation_name", "");
    std::string anim_tree_path = params.value("anim_tree_path", "");

    if (state_name.empty() || animation_name.empty() || anim_tree_path.empty()) {
        r_error = "Missing required parameter(s): anim_tree_path, state_name, animation_name";
        return;
    }

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (!scene_root) { r_error = "No scene is currently being edited"; return; }

    GDExtensionObjectPtr anim_tree = GodotAPI::get_node_child(scene_root, anim_tree_path);
    if (!anim_tree) { r_error = "AnimationTree not found at: " + anim_tree_path; return; }

    // Get the state machine via "tree_root" property
    GDExtensionObjectPtr sm = GodotAPI::get_property_object(anim_tree, "tree_root");
    if (!sm) {
        // No state machine assigned; create one and assign
        sm = GodotAPI::create_node("AnimationNodeStateMachine");
        if (!sm) { r_error = "Failed to create AnimationNodeStateMachine"; return; }
        uint8_t vbuf[64] = {0};
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)vbuf, sm);
        GodotAPI::set_property(anim_tree, "tree_root", (GDExtensionConstVariantPtr)vbuf);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)vbuf);
    }

    // Build an AnimationNodeAnimation + assign its animation name
    GDExtensionObjectPtr anim_node = GodotAPI::create_node("AnimationNodeAnimation");
    if (!anim_node) { r_error = "Failed to create AnimationNodeAnimation"; return; }

    {
        uint8_t vbuf[64] = {0};
        GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)vbuf, animation_name);
        GodotAPI::set_property(anim_node, "animation", (GDExtensionConstVariantPtr)vbuf);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)vbuf);
    }

    // add_node(name, node) on the state machine
    {
        uint8_t name_v[64] = {0}, node_v[64] = {0};
        GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)name_v, state_name);
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)node_v, anim_node);
        GDExtensionConstVariantPtr args[] = {
            (GDExtensionConstVariantPtr)name_v,
            (GDExtensionConstVariantPtr)node_v
        };
        GodotAPI::call_method_void(sm, "add_node", args, 2);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)name_v);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)node_v);
    }

    json result = {
        {"success", true},
        {"state_name", state_name},
        {"animation_name", animation_name}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// connect_animation_states — adds a transition between two states
// ---------------------------------------------------------------------------

static const char *CONNECT_ANIMATION_STATES_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scene_path": {"type": "string", "description": "res:// path of the scene containing the AnimationTree"},
        "anim_tree_path": {"type": "string", "description": "Node path to the AnimationTree"},
        "from_state": {"type": "string", "description": "Source state name"},
        "to_state": {"type": "string", "description": "Target state name"},
        "transition_type": {"type": "string", "description": "immediate|sync|at_end", "default": "immediate"},
        "advance_condition": {"type": "string", "description": "Optional condition parameter name", "default": ""}
    },
    "required": ["scene_path", "anim_tree_path", "from_state", "to_state"]
})schema";

static void handle_connect_animation_states(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string from_state = params.value("from_state", "");
    std::string to_state = params.value("to_state", "");
    std::string anim_tree_path = params.value("anim_tree_path", "");
    std::string transition_type = params.value("transition_type", "immediate");
    std::string advance_condition = params.value("advance_condition", "");

    if (from_state.empty() || to_state.empty() || anim_tree_path.empty()) {
        r_error = "Missing required parameters: anim_tree_path, from_state, to_state";
        return;
    }

    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (!scene_root) { r_error = "No scene is currently being edited"; return; }

    GDExtensionObjectPtr anim_tree = GodotAPI::get_node_child(scene_root, anim_tree_path);
    if (!anim_tree) { r_error = "AnimationTree not found at: " + anim_tree_path; return; }

    GDExtensionObjectPtr sm = GodotAPI::get_property_object(anim_tree, "tree_root");
    if (!sm) { r_error = "AnimationNodeStateMachine not configured"; return; }

    // Build a transition object and set switch_mode
    GDExtensionObjectPtr transition = GodotAPI::create_node("AnimationNodeStateMachineTransition");
    if (!transition) { r_error = "Failed to create AnimationNodeStateMachineTransition"; return; }

    int switch_mode = 0; // IMMEDIATE
    if (transition_type == "sync") switch_mode = 1;
    else if (transition_type == "at_end") switch_mode = 2;
    {
        uint8_t vbuf[64] = {0};
        GodotAPI::make_variant_int((GDExtensionUninitializedVariantPtr)vbuf, switch_mode);
        GodotAPI::set_property(transition, "switch_mode", (GDExtensionConstVariantPtr)vbuf);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)vbuf);
    }

    if (!advance_condition.empty()) {
        uint8_t vbuf[64] = {0};
        GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)vbuf, advance_condition);
        GodotAPI::set_property(transition, "advance_condition", (GDExtensionConstVariantPtr)vbuf);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)vbuf);
    }

    // sm.add_transition(from, to, transition)
    {
        uint8_t f_v[64] = {0}, t_v[64] = {0}, tr_v[64] = {0};
        GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)f_v, from_state);
        GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)t_v, to_state);
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)tr_v, transition);
        GDExtensionConstVariantPtr args[] = {
            (GDExtensionConstVariantPtr)f_v,
            (GDExtensionConstVariantPtr)t_v,
            (GDExtensionConstVariantPtr)tr_v
        };
        GodotAPI::call_method_void(sm, "add_transition", args, 3);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)f_v);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)t_v);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)tr_v);
    }

    json result = {
        {"success", true},
        {"from", from_state},
        {"to", to_state},
        {"transition_type", transition_type}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_animation_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("create_animation",
        "Create a new Animation resource (.tres) at the given res:// path",
        gear_mcp::CREATE_ANIMATION_SCHEMA, gear_mcp::handle_create_animation, /*main_thread=*/false);

    p_registry->register_tool("add_animation_track",
        "Append a value or method track (with keyframes) to an Animation .tres file",
        gear_mcp::ADD_ANIMATION_TRACK_SCHEMA, gear_mcp::handle_add_animation_track, /*main_thread=*/false);

    p_registry->register_tool("create_animation_tree",
        "Add an AnimationTree node to a scene (live editor when open, file I/O otherwise)",
        gear_mcp::CREATE_ANIMATION_TREE_SCHEMA, gear_mcp::handle_create_animation_tree, /*main_thread=*/true);

    p_registry->register_tool("add_animation_state",
        "Add a state to the AnimationNodeStateMachine assigned to an AnimationTree",
        gear_mcp::ADD_ANIMATION_STATE_SCHEMA, gear_mcp::handle_add_animation_state, /*main_thread=*/true);

    p_registry->register_tool("connect_animation_states",
        "Connect two states in an AnimationNodeStateMachine with a transition",
        gear_mcp::CONNECT_ANIMATION_STATES_SCHEMA, gear_mcp::handle_connect_animation_states, /*main_thread=*/true);

    std::printf("[Gear MCP] Registered animation tools: create_animation, add_animation_track, create_animation_tree, add_animation_state, connect_animation_states\n");
}
