#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace gear_mcp {

// ===========================================================================
// create_scene — creates a minimal .tscn file
// ===========================================================================

static void create_scene_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string scene_path = params.value("scenePath", "");
    std::string root_type = params.value("rootNodeType", "Node");
    std::string root_name = params.value("rootName", "");

    if (scene_path.empty()) {
        r_error = "Missing required parameter: scenePath";
        return;
    }

    // Default root name to the type name
    if (root_name.empty()) root_name = root_type;

    // Resolve to absolute path (uses ProjectSettings.globalize_path, CWD-independent)
    std::string abs_path = GodotAPI::res_to_absolute(scene_path);

    // Create parent directories
    path_utils::ensure_parent_dirs(abs_path);

    // Write minimal .tscn file
    // Format: Godot scene text format
    std::ofstream file(abs_path, std::ios::trunc);
    if (!file.is_open()) {
        r_error = "Cannot create scene file: " + abs_path;
        return;
    }

    file << "[gd_scene format=3]\n\n";
    file << "[node name=\"" << root_name << "\" type=\"" << root_type << "\"]\n";
    file.close();

    json result = {
        {"scenePath", scene_path},
        {"rootNode", root_name},
        {"rootType", root_type}
    };
    r_result = result.dump();
}

// ===========================================================================
// list_scene_nodes — parses .tscn to extract node hierarchy
// ===========================================================================

static void list_scene_nodes_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string scene_path = params.value("scenePath", "");
    if (scene_path.empty()) {
        r_error = "Missing required parameter: scenePath";
        return;
    }

    // Resolve path (uses ProjectSettings.globalize_path, CWD-independent)
    std::string abs_path = GodotAPI::res_to_absolute(scene_path);

    std::ifstream file(abs_path);
    if (!file.is_open()) {
        r_error = "Cannot open scene file: " + abs_path;
        return;
    }

    // Parse [node] sections from .tscn
    // Format: [node name="NodeName" type="TypeName"]
    //         [node name="Child" type="Type" parent="."]
    //         [node name="GrandChild" type="Type" parent="Child"]
    static const std::regex node_regex(R"re(\[node\s+name="([^"]+)"\s+type="([^"]+)"(?:\s+parent="([^"]*)")?.*?\])re");
    std::string line;
    json nodes = json::array();

    while (std::getline(file, line)) {
        try {
            std::smatch match;
            if (std::regex_search(line, match, node_regex)) {
                json node = {
                    {"name", match[1].str()},
                    {"type", match[2].str()},
                    {"parent", match[3].matched ? match[3].str() : ""}
                };
                nodes.push_back(node);
            }
        } catch (const std::regex_error &) {
            // Skip malformed lines
            continue;
        }
    }
    file.close();

    json result = {
        {"scenePath", scene_path},
        {"nodes", nodes},
        {"count", (int)nodes.size()}
    };
    r_result = result.dump();
}

// ===========================================================================
// add_node — adds a node entry
// ===========================================================================
//
// Smart hybrid:
//   1. If the target .tscn is the scene currently being edited in the editor,
//      we add the node to the LIVE scene tree through EditorUndoRedoManager
//      (so Ctrl+Z in the editor undoes it). The file on disk is left alone —
//      the user can save the scene via `save_scene` when they want it persisted.
//   2. Otherwise (no editor / different scene open), we append a [node] entry
//      to the .tscn text file directly. This is the legacy Phase 1 path and
//      is not undoable in the editor's UndoRedo stack.
// ===========================================================================

static void add_node_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (...) {
        r_error = "Invalid JSON in params";
        return;
    }

    std::string scene_path = params.value("scenePath", "");
    std::string parent_path = params.value("parentPath", ".");
    std::string node_name = params.value("nodeName", "");
    std::string node_type = params.value("nodeType", "Node");

    if (scene_path.empty()) {
        r_error = "Missing required parameter: scenePath";
        return;
    }
    if (node_name.empty()) {
        r_error = "Missing required parameter: nodeName";
        return;
    }

    // Resolve to absolute path (uses ProjectSettings.globalize_path, CWD-independent)
    std::string abs_path = GodotAPI::res_to_absolute(scene_path);

    // ---- Path 1: live editor scene + UndoRedo ----
    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (scene_root) {
        // Compare the edited scene's path to what we're being asked to modify.
        // EditorInterface::get_edited_scene_root() returns the root; its
        // scene_file_path property holds the res:// path of the on-disk file.
        std::string edited_path = GodotAPI::get_property_json(scene_root, "scene_file_path");
        // Strip surrounding quotes that come back from the JSON variant
        if (edited_path.size() >= 2 && edited_path.front() == '"' && edited_path.back() == '"') {
            edited_path = edited_path.substr(1, edited_path.size() - 2);
        }

        // Normalize the requested path the same way for comparison
        // If user passed res:// and we resolved to abs, also try the res form
        std::vector<std::string> candidates = { scene_path, abs_path };
        bool matches = false;
        for (const auto &c : candidates) {
            if (c == edited_path) { matches = true; break; }
        }
        // Also accept case where the editor's edited scene path matches the
        // absolute path of the file we were given
        if (!matches && !edited_path.empty()) {
            // Convert edited res:// to absolute for one more comparison
            std::string edited_abs = GodotAPI::res_to_absolute(edited_path);
            if (edited_abs == abs_path) matches = true;
        }

        if (matches) {
            // ---- Live scene + UndoRedo path ----
            GDExtensionObjectPtr new_node = GodotAPI::create_node(node_type);
            if (!new_node) {
                r_error = "Failed to create node of type: " + node_type + " (ClassDB may not exist)";
                return;
            }
            // Set the name via the property setter
            {
                uint8_t name_v[VARIANT_BUF_SIZE] = {0};
                GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)name_v, node_name);
                GodotAPI::set_property(new_node, "name", (GDExtensionConstVariantPtr)name_v);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)name_v);
            }

            // Resolve the parent
            GDExtensionObjectPtr parent = scene_root;
            std::string root_name = GodotAPI::get_node_name(scene_root);
            if (parent_path != "." && parent_path != root_name) {
                GDExtensionObjectPtr resolved = GodotAPI::get_node_child(scene_root, parent_path);
                if (!resolved) {
                    GodotAPI::queue_free(new_node);
                    r_error = "Parent node not found: " + parent_path;
                    return;
                }
                parent = resolved;
            }

            // Open UndoRedo action
            GodotAPI::undo_redo_create_action("Add node " + node_name + " to " + parent_path);

            // do: parent.add_child(node) ; node.set_owner(scene_root)
            {
                uint8_t p_v[VARIANT_BUF_SIZE] = {0}, n_v[VARIANT_BUF_SIZE] = {0};
                GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)p_v, parent);
                GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)n_v, new_node);
                GDExtensionConstVariantPtr a[] = { (GDExtensionConstVariantPtr)p_v, (GDExtensionConstVariantPtr)n_v };
                GodotAPI::undo_redo_add_do_method(parent, "add_child", a, 2);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)p_v);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)n_v);
            }
            {
                uint8_t n_v[VARIANT_BUF_SIZE] = {0}, sr_v[VARIANT_BUF_SIZE] = {0};
                GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)n_v, new_node);
                GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)sr_v, scene_root);
                GDExtensionConstVariantPtr a[] = { (GDExtensionConstVariantPtr)n_v, (GDExtensionConstVariantPtr)sr_v };
                GodotAPI::undo_redo_add_do_method(new_node, "set_owner", a, 2);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)n_v);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)sr_v);
            }

            // undo: parent.remove_child(node)
            {
                uint8_t p_v[VARIANT_BUF_SIZE] = {0}, n_v[VARIANT_BUF_SIZE] = {0};
                GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)p_v, parent);
                GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)n_v, new_node);
                GDExtensionConstVariantPtr a[] = { (GDExtensionConstVariantPtr)p_v, (GDExtensionConstVariantPtr)n_v };
                GodotAPI::undo_redo_add_undo_method(parent, "remove_child", a, 2);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)p_v);
                GodotAPI::destroy_variant((GDExtensionVariantPtr)n_v);
            }

            // Keep the new node alive across the undo boundary
            GodotAPI::undo_redo_add_do_reference(new_node);
            GodotAPI::undo_redo_add_undo_reference(new_node);

            GodotAPI::undo_redo_commit_action();

            // Apply the action for real
            GodotAPI::add_child(parent, new_node);
            GodotAPI::set_owner(new_node, scene_root);

            json result = {
                {"scenePath", scene_path},
                {"nodeName", node_name},
                {"nodeType", node_type},
                {"parentPath", parent_path},
                {"undoable", true},
                {"persisted", false}
            };
            result["hint"] = "Scene is open in the editor; node was added to the live tree only. "
                             "Use save_scene to persist to disk.";
            r_result = result.dump();
            return;
        }
    }

    // ---- Path 2: file I/O (no live editor scene / different scene open) ----

    // Read existing content
    std::ifstream in_file(abs_path);
    if (!in_file.is_open()) {
        r_error = "Cannot open scene file: " + abs_path;
        return;
    }
    std::stringstream ss;
    ss << in_file.rdbuf();
    std::string content = ss.str();
    in_file.close();

    // Append new node
    std::ofstream out_file(abs_path, std::ios::trunc);
    if (!out_file.is_open()) {
        r_error = "Cannot write to scene file: " + abs_path;
        return;
    }

    out_file << content;
    if (!content.empty() && content.back() != '\n') out_file << "\n";

    // Build the node entry
    out_file << "[node name=\"" << node_name << "\" type=\"" << node_type << "\"";
    if (parent_path == ".") {
        out_file << " parent=\".\"";
    } else {
        out_file << " parent=\"" << parent_path << "\"";
    }
    out_file << "]\n";
    out_file.close();

    json result = {
        {"scenePath", scene_path},
        {"nodeName", node_name},
        {"nodeType", node_type},
        {"parentPath", parent_path},
        {"undoable", false},
        {"persisted", true}
    };
    result["hint"] = "Scene is not open in the editor; node was appended to the .tscn file. "
                     "No editor UndoRedo entry was created.";
    r_result = result.dump();
}

// ===========================================================================
// attach_script_to_node — bind a .gd script to a node inside a .tscn file
// ===========================================================================
// Why: the rest of the tool set lets you build scenes and write scripts
// independently, but nothing wires the two together unless the scene is open
// in the editor. This pure-file-I/O tool stitches them up by:
//   1. adding an [ext_resource type="Script" ...] header entry, and
//   2. adding `script = ExtResource("N")` to the target [node] block.
// Then it refreshes the editor's file cache so the change is picked up.
static void attach_script_to_node_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    // The registry gives us a raw JSON string (see MCPMethods::handle, line 293
    // and 317: args_json is dumped from the JSON-RPC "arguments" object). We
    // parse it ourselves to match the codebase's tool-handler convention.
    json params;
    try {
        params = json::parse(p_params_json);
    } catch (const std::exception &e) {
        r_error = std::string("Invalid JSON in params: ") + e.what();
        return;
    }

    std::string scene_path  = params.value("scenePath", "");
    std::string node_path   = params.value("nodePath", "");    // root, "Player", "Player/Sprite", ...
    std::string script_path = params.value("scriptPath", ""); // res://scenes/star_catcher.gd

    if (scene_path.empty() || node_path.empty() || script_path.empty()) {
        r_error = "scenePath, nodePath and scriptPath are all required";
        return;
    }
    if (script_path.rfind("res://", 0) != 0 && script_path.rfind("user://", 0) != 0) {
        r_error = "scriptPath must be a res:// or user:// path (got: " + script_path + ")";
        return;
    }

    std::string abs_path = GodotAPI::res_to_absolute(scene_path);
    std::ifstream in_file(abs_path);
    if (!in_file.is_open()) {
        r_error = "Cannot open scene file: " + abs_path;
        return;
    }
    std::stringstream ss;
    ss << in_file.rdbuf();
    std::string content = ss.str();
    in_file.close();

    // Find the line that opens the target [node] block.
    // A node header looks like: [node name="X" type="Y" parent="..."]
    // For the root, parent="." or no parent= attribute.
    // We match the first [node] whose name attribute equals the LAST segment of nodePath
    // (i.e. "Player/Sprite" → look for name="Sprite" with parent="Player").
    // Special cases:
    //   nodePath == "."  → attach to the FIRST [node] (the root)
    //   nodePath == ""   → same as "."
    //   no '/' in path   → top-level node: parent must be "." (or absent)
    //   has '/'          → last segment is the name, prefix is the parent path
    std::string target_name = node_path;
    std::string parent_path = node_path;
    {
        size_t slash = node_path.find_last_of('/');
        if (slash != std::string::npos) {
            target_name = node_path.substr(slash + 1);
            parent_path = node_path.substr(0, slash);
        } else {
            // No slash: top-level (or root) node — both have parent="." in tscn
            parent_path = ".";
        }
    }

    // If user passed "." (or empty), they mean the root node. Find the first
    // [node ...] line in the file, which is always the root in a .tscn file.
    if (node_path.empty() || node_path == ".") {
        std::regex root_re(R"(\[node\s+[^\]]+\])");
        std::smatch root_match;
        if (!std::regex_search(content, root_match, root_re)) {
            r_error = "Scene file has no [node] entries.";
            return;
        }
        // Synthesize a match-shaped result so the rest of the function can
        // run unchanged.
        std::string matched_root = root_match.str(0);
        // Extract name attribute for the error message
        std::regex name_attr_re(R"RX(\bname="([^"]*)")RX");
        std::smatch name_m;
        std::string root_name = "";
        if (std::regex_search(matched_root, name_m, name_attr_re)) {
            root_name = name_m[1].str();
        }
        // Use the same path below by jumping to a "matched" state.
        // We do this by setting target_name/parent_path and re-running the
        // normal lookup with the actual root name.
        target_name = root_name;
        parent_path = ".";
    }

    // Build a regex to find the node entry: name="<target_name>"
    // We require the line to start with "[node" and the name attribute to match.
    // Then we also want the parent attribute (if any) to match the expected parent.
    std::string pattern = R"(\[node\s+name=")" + target_name + R"("[^\]]*\])";
    std::regex node_re(pattern);
    std::smatch node_match;
    if (!std::regex_search(content, node_match, node_re)) {
        r_error = "Node not found in scene: " + node_path + " (searched for name=\"" + target_name + "\")";
        return;
    }

    // If a parent_path is given, verify the matched line declares the same parent.
    // (Tscn format: parent="Foo" or parent="Foo/Bar". For the root, parent="." or omitted.)
    std::string matched_line = node_match.str(0);
    std::string expected_parent = parent_path;
    if (expected_parent.empty()) expected_parent = "."; // root
    {
        std::regex parent_re(R"RX(\bparent="([^"]*)")RX");
        std::smatch pm;
        std::string parent_attr;
        if (std::regex_search(matched_line, pm, parent_re)) {
            parent_attr = pm[1].str();
        } else {
            // No parent attribute → root. Accept if the user asked for "."
            // or for the root's own name (top-level with no parent attr).
            parent_attr = ".";
        }
        // Accept the match when either side resolves to "." and the other is
        // empty/".", OR they match exactly. This handles "root with no
        // parent attribute" vs "user passed the root's own name".
        bool match = (parent_attr == expected_parent)
                  || (parent_attr == "." && expected_parent == ".")
                  || (parent_attr == "." && expected_parent.empty())
                  || (parent_attr.empty() && expected_parent == ".");
        if (!match) {
            r_error = "Node " + target_name + " exists in scene but its parent is \"" +
                      parent_attr + "\", not \"" + expected_parent + "\" (full path: " + node_path + ")";
            return;
        }
    }

    // Reject if a script is already attached — refuse to silently overwrite.
    std::regex script_in_block_re(R"RX(\bscript\s*=\s*ExtResource\("([^"]+)"\))RX");
    {
        // search only inside the matched block, i.e. from node_match.position()
        // up to the next blank line / next [node ...] / EOF.
        size_t block_start = node_match.position();
        size_t block_end = content.find("\n\n", block_start);
        if (block_end == std::string::npos) block_end = content.size();
        std::string block = content.substr(block_start, block_end - block_start);
        if (std::regex_search(block, script_in_block_re)) {
            r_error = "Node " + node_path + " already has a script attached. "
                      "Edit the .tscn by hand or use set_node_properties.";
            return;
        }
    }

    // Choose an ext_resource id that does not collide with existing ones.
    std::regex id_re(R"RX(\bid="([^"]+)"\s*)RX");
    std::set<std::string> existing_ids;
    {
        auto begin = std::sregex_iterator(content.begin(), content.end(), id_re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            existing_ids.insert((*it)[1].str());
        }
    }
    std::string new_id = "1_attachscript";
    int suffix = 1;
    while (existing_ids.count(new_id)) {
        suffix += 1;
        new_id = "1_attachscript_" + std::to_string(suffix);
    }

    // Add the [ext_resource ...] header. Place it after any existing ext_resource
    // entries, but before the first [node] (Godot is lenient about this order
    // but the canonical layout is: gd_scene → ext_resource* → sub_resource* → node*).
    std::string ext_resource_line =
        "[ext_resource type=\"Script\" path=\"" + script_path + "\" id=\"" + new_id + "\"]\n";

    size_t first_node_pos = content.find("[node ");
    if (first_node_pos == std::string::npos) {
        // No [node] yet — append the ext_resource before EOF and the script
        // line will be appended in the block where the node lives (impossible
        // to find a block if no node exists, so error out).
        r_error = "Scene file has no [node] entries; cannot attach script.";
        return;
    }
    content.insert(first_node_pos, ext_resource_line);

    // Find the end of the MATCHED node header line (the "]") and insert a
    // `script = ExtResource("id")` line immediately after it. We must use
    // node_match.position() (the validated target), NOT first_node_pos, or
    // the script gets pinned to whichever [node] block happens to be
    // first in the file (the root) instead of the requested node.
    //
    // CRITICAL: we inserted ext_resource_line into `content` above, which
    // shifts every byte after first_node_pos by ext_resource_line.size().
    // node_match.position() was captured BEFORE the insert, so we must
    // add that shift to find the new position of the matched [node] line.
    size_t node_pos_after_insert = static_cast<size_t>(node_match.position()) + ext_resource_line.size();
    size_t header_end = content.find(']', node_pos_after_insert);
    if (header_end == std::string::npos) {
        r_error = "Malformed scene file: node header has no closing ']'";
        return;
    }
    size_t insert_pos = header_end + 1;
    // Skip the existing newline after the header so we land at start-of-line.
    if (insert_pos < content.size() && content[insert_pos] == '\n') insert_pos += 1;

    std::string script_line = "script = ExtResource(\"" + new_id + "\")\n";
    content.insert(insert_pos, script_line);

    // Write back to disk.
    std::ofstream out_file(abs_path, std::ios::trunc);
    if (!out_file.is_open()) {
        r_error = "Cannot write scene file: " + abs_path;
        return;
    }
    out_file << content;
    out_file.close();

    GodotAPI::refresh_filesystem();

    json result = {
        {"scenePath",  scene_path},
        {"nodePath",   node_path},
        {"scriptPath", script_path},
        {"ext_resource_id", new_id},
        {"persisted",  true}
    };
    r_result = result.dump();
}

// ===========================================================================
// attach_screenshot_helper — convenience wrapper around attach_script_to_node
// ===========================================================================
// Why: the user-facing screenshot flow needs the helper script attached to
// the scene they want to capture. They don't want to look up the helper's
// res:// path or remember the export names. This tool does all of that in
// one call, and also sets the two exports the user cares about
// (screenshot_path, wait_frames) so the next run_scene_with_timeout
// call doesn't need any sidecar or pre-config.
//
// The helper itself lives at res://addons/gear_mcp/screenshot_helper.gd
// (it ships with the plugin). This tool only writes a single ext_resource
// line + a script = ExtResource(...) line + the export values to the
// .tscn file. If the helper is already attached to the target node, only
// the export values are updated — no duplicate ext_resource is created.
static void attach_screenshot_helper_handler(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    static const std::string HELPER_PATH = "res://addons/gear_mcp/screenshot_helper.gd";

    json params;
    try {
        params = json::parse(p_params_json);
    } catch (const std::exception &e) {
        r_error = std::string("Invalid JSON in params: ") + e.what();
        return;
    }

    std::string scene_path  = params.value("scenePath", "");
    std::string node_path   = params.value("nodePath", "."); // default to root
    std::string screenshot_path = params.value("screenshotPath", "res://screenshot.png");
    int wait_frames = params.value("waitFrames", 60);

    if (scene_path.empty()) {
        r_error = "scenePath is required";
        return;
    }

    // Step 1: attach the helper script to the target node. attach_script_to_node
    // is idempotent in the sense that it always writes a fresh ext_resource id
    // and overwrites any existing script = ... line on the matched node, so
    // re-running this tool just refreshes the attachment. This is fine for the
    // helper since we control its path.
    {
        json attach_params = {
            {"scenePath",  scene_path},
            {"nodePath",   node_path},
            {"scriptPath", HELPER_PATH}
        };
        std::string inner_err;
        std::string inner_result;
        attach_script_to_node_handler(attach_params.dump(), inner_result, inner_err);
        if (!inner_err.empty()) {
            r_error = "attach_script_to_node failed: " + inner_err;
            return;
        }
    }

    // Step 2: set the export values on the target node. The helper exposes two
    // @export vars (screenshot_path, wait_frames). We update them in the .tscn
    // by inserting `key = value` lines under the matched [node] header.
    //
    // First, find the right [node] block: if nodePath is "." or empty we use
    // the first [node] (the root). Otherwise we walk all [node] lines and pick
    // the one whose name attribute matches the LAST segment of nodePath, with
    // the correct parent chain. This is the same matcher
    // attach_script_to_node uses.
    std::string abs_path = GodotAPI::res_to_absolute(scene_path);
    std::ifstream in_file(abs_path);
    if (!in_file.is_open()) {
        r_error = "Cannot reopen scene file: " + abs_path;
        return;
    }
    std::stringstream ss;
    ss << in_file.rdbuf();
    std::string content = ss.str();
    in_file.close();

    // Compute target_name and parent_path the same way attach_script_to_node does.
    std::string target_name = node_path;
    std::string parent_path = node_path;
    {
        size_t slash = node_path.find_last_of('/');
        if (slash != std::string::npos) {
            target_name = node_path.substr(slash + 1);
            parent_path = node_path.substr(0, slash);
        } else {
            parent_path = ".";
        }
    }
    if (node_path.empty() || node_path == ".") {
        std::regex root_re(R"(\[node\s+[^\]]+\])");
        std::smatch root_match;
        if (!std::regex_search(content, root_match, root_re)) {
            r_error = "Scene file has no [node] entries.";
            return;
        }
        std::string matched_root = root_match.str(0);
        std::regex name_attr_re("\\bname=\"([^\"]*)\"");
        std::smatch name_m;
        if (std::regex_search(matched_root, name_m, name_attr_re)) {
            target_name = name_m[1].str();
        }
    }

    // Now find the [node ...] line whose name="target_name" AND parent="parent_path"
    // (or, for top-level nodes, parent="."). If parent_path is ".", any parent
    // attribute that points to root (or is absent, since the first node is
    // always the root) is acceptable.
    std::regex node_re("\\[node\\s+[^\\]]+\\]");
    size_t target_node_pos = std::string::npos;
    {
        auto it = std::sregex_iterator(content.begin(), content.end(), node_re);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) {
            std::string hdr = it->str();
            std::smatch nm;
            std::string name;
            if (std::regex_search(hdr, nm, std::regex("\\bname=\"([^\"]*)\""))) {
                name = nm[1].str();
            }
            if (name != target_name) continue;
            std::smatch pm;
            std::string par;
            if (std::regex_search(hdr, pm, std::regex("\\bparent=\"([^\"]*)\""))) {
                par = pm[1].str();
            }
            // Root node: no parent attribute. Top-level: parent="."
            // Nested: parent="Parent" or parent="Parent/Child".
            bool match = false;
            if (par.empty()) {
                match = (parent_path == "." || parent_path.empty());
            } else if (par == ".") {
                match = (parent_path == "." || parent_path.empty());
            } else {
                match = (par == parent_path);
            }
            if (match) {
                target_node_pos = static_cast<size_t>(it->position());
                break;
            }
        }
    }
    if (target_node_pos == std::string::npos) {
        r_error = "Could not find node '" + node_path + "' in scene file (target_name='" + target_name + "', parent='" + parent_path + "').";
        return;
    }

    size_t header_end = content.find(']', target_node_pos);
    if (header_end == std::string::npos) {
        r_error = "Malformed scene file: node header has no closing ']'";
        return;
    }
    // Find the end of the node body: the next line starting with '[' (next
    // section header) or EOF.
    size_t body_search_start = header_end + 1;
    size_t body_end = content.size();
    std::regex next_section_re(R"(\n\[)");
    std::smatch m;
    std::string tail = content.substr(body_search_start);
    if (std::regex_search(tail, m, next_section_re)) {
        body_end = body_search_start + m.position(0) + 1; // +1 to keep the '\n'
    }

    // Within the node body, strip any existing helper export lines, then
    // append fresh ones at the end of the body.
    std::regex exp_path_re(R"(\n\s*screenshot_path\s*=\s*[^\n]*)");
    std::regex exp_wait_re(R"(\n\s*wait_frames\s*=\s*[^\n]*)");
    std::string body = content.substr(body_search_start, body_end - body_search_start);
    body = std::regex_replace(body, exp_path_re, std::string());
    body = std::regex_replace(body, exp_wait_re, std::string());
    // Trim trailing whitespace/newlines so our appended lines start cleanly.
    while (!body.empty() && (body.back() == '\n' || body.back() == ' ' || body.back() == '\r' || body.back() == '\t')) {
        body.pop_back();
    }
    body += "\nscreenshot_path = \"" + screenshot_path + "\"\n";
    body += "wait_frames = " + std::to_string(wait_frames) + "\n";

    content = content.substr(0, body_search_start) + body + content.substr(body_end);

    std::ofstream out_file(abs_path, std::ios::trunc);
    if (!out_file.is_open()) {
        r_error = "Cannot write scene file: " + abs_path;
        return;
    }
    out_file << content;
    out_file.close();

    GodotAPI::refresh_filesystem();

    json result = {
        {"scenePath",      scene_path},
        {"nodePath",       node_path},
        {"scriptPath",     HELPER_PATH},
        {"screenshotPath", screenshot_path},
        {"waitFrames",     wait_frames},
        {"persisted",      true}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

// ===========================================================================
// Registration
// ===========================================================================

void register_scene_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    static const char *CREATE_SCENE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scenePath": {"type": "string", "description": "Path for the new .tscn file"},
        "rootNodeType": {"type": "string", "description": "Root node class name", "default": "Node"},
        "rootName": {"type": "string", "description": "Root node name", "default": ""}
    },
    "required": ["scenePath"]
})schema";

    static const char *LIST_SCENE_NODES_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scenePath": {"type": "string", "description": "Path to the .tscn file"}
    },
    "required": ["scenePath"]
})schema";

    static const char *ADD_NODE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scenePath": {"type": "string", "description": "Path to the .tscn file"},
        "parentPath": {"type": "string", "description": "Parent node path in scene (default: root)", "default": "."},
        "nodeName": {"type": "string", "description": "Name for the new node"},
        "nodeType": {"type": "string", "description": "ClassDB type for the new node", "default": "Node"}
    },
    "required": ["scenePath", "nodeName"]
})schema";

    p_registry->register_tool("create_scene",
        "Create a new .tscn scene file with specified root node type",
        CREATE_SCENE_SCHEMA, gear_mcp::create_scene_handler, false);

    p_registry->register_tool("list_scene_nodes",
        "List all nodes in a .tscn scene file by parsing the text format",
        LIST_SCENE_NODES_SCHEMA, gear_mcp::list_scene_nodes_handler, false);

    p_registry->register_tool("add_node",
        "Add a new node entry — uses live editor scene + UndoRedo when the scene is open, otherwise writes the .tscn file directly",
        ADD_NODE_SCHEMA, gear_mcp::add_node_handler, true);

    static const char *ATTACH_SCRIPT_TO_NODE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scenePath":  {"type": "string", "description": "Path to the .tscn file (res:// or absolute)"},
        "nodePath":   {"type": "string", "description": "Node path inside the scene, e.g. '.' for root, 'Player', 'Player/Sprite'"},
        "scriptPath": {"type": "string", "description": "res:// path of the .gd script to attach"}
    },
    "required": ["scenePath", "nodePath", "scriptPath"]
})schema";

    p_registry->register_tool("attach_script_to_node",
        "Bind a .gd script to a node in a .tscn file (pure file I/O; does not require the scene to be open in the editor)",
        ATTACH_SCRIPT_TO_NODE_SCHEMA, gear_mcp::attach_script_to_node_handler, true);

    static const char *ATTACH_SCREENSHOT_HELPER_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "scenePath":      {"type": "string", "description": "Path to the .tscn file (res:// or absolute)"},
        "nodePath":       {"type": "string", "description": "Node path inside the scene, default '.' (root)", "default": "."},
        "screenshotPath": {"type": "string", "description": "Default res:// path the helper should write to. The MCP run_scene_with_timeout tool can still override this at launch time via the sidecar mechanism.", "default": "res://screenshot.png"},
        "waitFrames":     {"type": "integer", "description": "Number of frames to wait after _ready before capturing", "default": 60}
    },
    "required": ["scenePath"]
})schema";

    p_registry->register_tool("attach_screenshot_helper",
        "Persistently attach the bundled screenshot helper script to a node in a .tscn file, and write the screenshot_path / wait_frames exports. After this, run_scene_with_timeout will be able to capture the play window without any per-call setup.",
        ATTACH_SCREENSHOT_HELPER_SCHEMA, gear_mcp::attach_screenshot_helper_handler, true);

    std::printf("[Gear MCP] Registered scene tools: create_scene, list_scene_nodes, add_node, attach_script_to_node, attach_screenshot_helper\n");
}
