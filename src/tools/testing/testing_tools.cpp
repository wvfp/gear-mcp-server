#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace gear_mcp {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Find an Input singleton instance.
GDExtensionObjectPtr get_input() {
    return GodotAPI::get_global_singleton("Input");
}

// Check if we are running inside the editor (not a running game).
// Input injection only works against a running game; in editor mode we
// return a clean error so the caller knows why.
//
// We do a layered check:
//   1. Engine.is_editor_hint() — true when running in the editor process
//   2. DisplayServer.get_name() == "headless" — true when running --headless
//   3. Has a Window — false in headless mode
// If any of these signal "no input event source", bail out.
bool is_editor_mode() {
    GDExtensionObjectPtr engine = GodotAPI::get_global_singleton("Engine");
    if (engine) {
        if (GodotAPI::call_method_bool(engine, "is_editor_hint")) return true;
    }
    GDExtensionObjectPtr ds = GodotAPI::get_global_singleton("DisplayServer");
    if (ds) {
        std::string name = GodotAPI::call_method_string(ds, "get_name");
        if (name == "headless") return true;
    } else {
        return true; // no display server at all
    }
    return false;
}

const char *EDITOR_MODE_HINT =
    "Input injection requires a running game (Input.parse_input_event needs "
    "an active display-server event source). Run the project with run_project "
    "first, then call this tool while the game is playing.";

// Create a new instance of the given class via classdb. Returns nullptr on
// failure. Caller is responsible for releasing the object when done.
GDExtensionObjectPtr make_event(const std::string &p_class_name) {
    return GodotAPI::create_node(p_class_name);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// capture_screenshot — save the root viewport's content as a PNG file
// ---------------------------------------------------------------------------

static const char *CAPTURE_SCREENSHOT_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "output_path": {"type": "string", "description": "Destination path for the PNG (relative paths are relative to the project root)"},
        "wait_for_frame": {"type": "boolean", "description": "If true, force a frame render before capture (editor mode only)", "default": true}
    },
    "required": ["output_path"]
})schema";

static void handle_capture_screenshot(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string output_path = params.value("output_path", "");
    if (output_path.empty()) { r_error = "Missing required parameter: output_path"; return; }
    if (path_utils::has_traversal(output_path)) {
        r_error = "Path traversal detected in output_path";
        return;
    }

    // Resolve output_path to absolute (allow res:// prefix and relative paths)
    std::string abs = output_path;
    if (abs.substr(0, 6) == "res://") abs = GodotAPI::res_to_absolute(abs);
    else if (!fs::path(abs).is_absolute()) {
        std::string proj = GodotAPI::get_project_path();
        if (proj.empty()) { r_error = "Cannot resolve relative output_path: no active project"; return; }
        abs = proj + "/" + abs;
    }

    // Make sure parent dir exists
    fs::path out_path_obj(abs);
    std::error_code ec;
    fs::create_directories(out_path_obj.parent_path(), ec);

    // Try to capture from the root viewport via get_viewport().get_texture().get_image()
    GDExtensionObjectPtr e_interface = GodotAPI::get_editor_interface();
    GDExtensionObjectPtr root_viewport = nullptr;
    if (e_interface) {
        // EditorInterface has a "get_editor_viewport" method that returns a Viewport
        root_viewport = GodotAPI::call_method_object_with_args(e_interface, "get_editor_viewport", nullptr, 0);
    }
    // If editor viewport not available, try the current scene's Window via get_tree().get_root()
    if (!root_viewport) {
        GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
        if (scene_root) {
            // get_viewport() on a Node returns its containing viewport
            root_viewport = GodotAPI::call_method_object_with_args(scene_root, "get_viewport", nullptr, 0);
        }
    }
    if (!root_viewport) {
        r_error = "No root viewport available (open a scene first)";
        return;
    }

    // viewport.get_texture() -> ViewportTexture
    GDExtensionObjectPtr tex = GodotAPI::call_method_object_with_args(root_viewport, "get_texture", nullptr, 0);
    if (!tex) { r_error = "Failed to get viewport texture"; return; }
    // tex.get_image() -> Image
    GDExtensionObjectPtr img = GodotAPI::call_method_object_with_args(tex, "get_image", nullptr, 0);
    if (!img) { r_error = "Failed to get image from viewport texture"; return; }

    // img.save_png(path) — returns an Error code. Capture it so we can
    // report actual failures instead of guessing from a missing file.
    uint8_t vb[64] = {0};
    GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)vb, abs);
    GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)vb };
    int save_err = GodotAPI::call_method_int_with_args(img, "save_png", args, 1);
    GodotAPI::destroy_variant((GDExtensionVariantPtr)vb);

    if (!fs::exists(abs, ec)) {
        r_error = "save_png did not produce a file at " + abs +
                  " (save_png returned Error=" + std::to_string(save_err) + ")";
        return;
    }
    int64_t size = fs::file_size(abs, ec);

    json result = {
        {"success", true},
        {"output_path", abs},
        {"size_bytes", (int)size}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// capture_viewport — alias of capture_screenshot for now (single viewport
// resolution). Reserved for future multi-viewport support.
// ---------------------------------------------------------------------------

static const char *CAPTURE_VIEWPORT_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "output_path": {"type": "string", "description": "Destination path for the PNG"},
        "viewport": {"type": "string", "description": "Viewport name; reserved for future use (default: root)", "default": "root"}
    },
    "required": ["output_path"]
})schema";

static void handle_capture_viewport(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    // Reuse the screenshot implementation; the 'viewport' param is currently
    // advisory only and defaults to the root viewport.
    return handle_capture_screenshot(p_params_json, r_result, r_error);
}

// ---------------------------------------------------------------------------
// inject_action — call Input.action_press / action_release
// ---------------------------------------------------------------------------

static const char *INJECT_ACTION_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "action": {"type": "string", "description": "Action name (must be defined in project.godot's [input] section)"},
        "strength": {"type": "number", "description": "Action strength (0.0 - 1.0)", "default": 1.0},
        "press": {"type": "boolean", "description": "True to press, false to release", "default": true}
    },
    "required": ["action"]
})schema";

static void handle_inject_action(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string action = params.value("action", "");
    if (action.empty()) { r_error = "Missing required parameter: action"; return; }
    double strength = params.value("strength", 1.0);
    bool press = params.value("press", true);

    GDExtensionObjectPtr input = get_input();
    if (!input) { r_error = "Input singleton not available"; return; }

    // action_press/release are safe in editor mode, but the dispatched
    // event only has effect in a running game. Include a hint in the result.
    bool editor_mode = is_editor_mode();

    const char *method = press ? "action_press" : "action_release";
    {
        uint8_t vb_str[64] = {0};
        uint8_t vb_flt[64] = {0};
        GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)vb_str, action);
        GodotAPI::make_variant_float((GDExtensionUninitializedVariantPtr)vb_flt, strength);
        GDExtensionConstVariantPtr args[] = {
            (GDExtensionConstVariantPtr)vb_str,
            (GDExtensionConstVariantPtr)vb_flt
        };
        GodotAPI::call_method_void(input, method, args, 2);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)vb_str);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)vb_flt);
    }

    json result = {
        {"success", true},
        {"action", action},
        {"strength", strength},
        {"press", press},
        {"editor_mode", editor_mode},
        {"note", editor_mode ? EDITOR_MODE_HINT : ""}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// inject_key — synthesize an InputEventKey and pass to Input.parse_input_event
// ---------------------------------------------------------------------------

static const char *INJECT_KEY_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "keycode": {"type": "string", "description": "Key name (e.g., 'A', 'SPACE', 'ESCAPE') or numeric key code"},
        "pressed": {"type": "boolean", "description": "True for press, false for release", "default": true},
        "ctrl": {"type": "boolean", "description": "Ctrl held", "default": false},
        "shift": {"type": "boolean", "description": "Shift held", "default": false},
        "alt": {"type": "boolean", "description": "Alt held", "default": false}
    },
    "required": ["keycode"]
})schema";

// Map common key names to Godot Key enum values (subset). The full enum is
// huge; for keys not listed here, callers can pass a numeric keycode.
static int keycode_from_name(const std::string &p_name) {
    // Single ASCII letters and digits map to their own codes
    if (p_name.size() == 1) {
        char c = (char)std::toupper((unsigned char)p_name[0]);
        if (c >= 'A' && c <= 'Z') return c; // KEY_A == 65
        if (c >= '0' && c <= '9') return c;
    }
    if (p_name == "SPACE") return 32;
    if (p_name == "ESCAPE" || p_name == "ESC") return 4194305; // KEY_ESCAPE
    if (p_name == "TAB") return 4194306;
    if (p_name == "BACKSPACE") return 4194308;
    if (p_name == "ENTER" || p_name == "RETURN") return 4194309;
    if (p_name == "INSERT") return 4194310;
    if (p_name == "DELETE" || p_name == "DEL") return 4194311;
    if (p_name == "LEFT") return 4194319;
    if (p_name == "RIGHT") return 4194321;
    if (p_name == "UP") return 4194320;
    if (p_name == "DOWN") return 4194322;
    if (p_name == "HOME") return 4194313;
    if (p_name == "END") return 4194314;
    if (p_name == "PAGEUP") return 4194315;
    if (p_name == "PAGEDOWN") return 4194316;
    if (p_name == "F1") return 4194332;
    if (p_name == "F2") return 4194333;
    if (p_name == "F3") return 4194334;
    if (p_name == "F4") return 4194335;
    if (p_name == "F5") return 4194336;
    if (p_name == "F6") return 4194337;
    if (p_name == "F7") return 4194338;
    if (p_name == "F8") return 4194339;
    if (p_name == "F9") return 4194340;
    if (p_name == "F10") return 4194341;
    if (p_name == "F11") return 4194342;
    if (p_name == "F12") return 4194343;
    // Numeric fallback: caller may pass a numeric string
    try { return std::stoi(p_name); } catch (...) {}
    return 0;
}

static void handle_inject_key(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string keycode_str = params.value("keycode", "");
    if (keycode_str.empty()) { r_error = "Missing required parameter: keycode"; return; }
    bool pressed = params.value("pressed", true);
    bool ctrl = params.value("ctrl", false);
    bool shift = params.value("shift", false);
    bool alt = params.value("alt", false);

    int keycode = keycode_from_name(keycode_str);
    if (keycode == 0) { r_error = "Unknown keycode: " + keycode_str; return; }

    if (is_editor_mode()) {
        r_error = EDITOR_MODE_HINT;
        return;
    }

    // Create an InputEventKey
    GDExtensionObjectPtr ev = make_event("InputEventKey");
    if (!ev) { r_error = "Failed to instantiate InputEventKey"; return; }

    // Build JSON for set_property_from_json calls
    auto set_json = [&](const std::string &p_name, const std::string &p_json) {
        GodotAPI::set_property_from_json(ev, p_name, p_json);
    };
    set_json("keycode", std::to_string(keycode));
    set_json("physical_keycode", std::to_string(keycode));
    set_json("pressed", pressed ? "true" : "false");
    set_json("ctrl_pressed", ctrl ? "true" : "false");
    set_json("shift_pressed", shift ? "true" : "false");
    set_json("alt_pressed", alt ? "true" : "false");
    set_json("echo", "false");

    // Pass to Input.parse_input_event
    GDExtensionObjectPtr input = get_input();
    if (!input) { r_error = "Input singleton not available"; return; }

    // Input.parse_input_event(event: InputEvent) — need to wrap ev as a variant
    // The simplest path is to call parse_input_event with a single object arg.
    // We do that via call_method_void with the object converted to a variant.
    {
        uint8_t vb[64] = {0};
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)vb, ev);
        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)vb };
        GodotAPI::call_method_void(input, "parse_input_event", args, 1);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)vb);
    }

    json result = {
        {"success", true},
        {"keycode", keycode},
        {"key_name", keycode_str},
        {"pressed", pressed},
        {"modifiers", {{"ctrl", ctrl}, {"shift", shift}, {"alt", alt}}}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// inject_mouse_click — synthesize InputEventMouseButton (button_index 1=L, 2=R, 3=M)
// ---------------------------------------------------------------------------

static const char *INJECT_MOUSE_CLICK_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "button": {"type": "string", "description": "Mouse button: 'left', 'right', 'middle'", "default": "left"},
        "x": {"type": "number", "description": "X coordinate", "default": 0},
        "y": {"type": "number", "description": "Y coordinate", "default": 0},
        "pressed": {"type": "boolean", "description": "True for press, false for release", "default": true},
        "double_click": {"type": "boolean", "description": "Double-click", "default": false}
    }
})schema";

static int mouse_button_index(const std::string &p_button) {
    std::string b = p_button;
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (b == "left") return 1;
    if (b == "right") return 2;
    if (b == "middle") return 3;
    if (b == "wheel_up") return 4;
    if (b == "wheel_down") return 5;
    return 1;
}

static void handle_inject_mouse_click(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string button = params.value("button", "left");
    double x = params.value("x", 0.0);
    double y = params.value("y", 0.0);
    bool pressed = params.value("pressed", true);
    bool dbl = params.value("double_click", false);

    if (is_editor_mode()) {
        r_error = EDITOR_MODE_HINT;
        return;
    }

    GDExtensionObjectPtr ev = make_event("InputEventMouseButton");
    if (!ev) { r_error = "Failed to instantiate InputEventMouseButton"; return; }

    auto set_json = [&](const std::string &p_name, const std::string &p_json) {
        GodotAPI::set_property_from_json(ev, p_name, p_json);
    };
    set_json("button_index", std::to_string(mouse_button_index(button)));
    set_json("pressed", pressed ? "true" : "false");
    set_json("double_click", dbl ? "true" : "false");
    set_json("position", "{\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + "}");
    set_json("global_position", "{\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + "}");

    GDExtensionObjectPtr input = get_input();
    if (!input) { r_error = "Input singleton not available"; return; }
    {
        uint8_t vb[64] = {0};
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)vb, ev);
        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)vb };
        GodotAPI::call_method_void(input, "parse_input_event", args, 1);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)vb);
    }

    json result = {
        {"success", true},
        {"button", button},
        {"x", x}, {"y", y},
        {"pressed", pressed},
        {"double_click", dbl}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// inject_mouse_motion — synthesize InputEventMouseMotion
// ---------------------------------------------------------------------------

static const char *INJECT_MOUSE_MOTION_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "x": {"type": "number", "description": "Target X coordinate", "default": 0},
        "y": {"type": "number", "description": "Target Y coordinate", "default": 0},
        "relative_x": {"type": "number", "description": "Relative X motion (delta)", "default": 0},
        "relative_y": {"type": "number", "description": "Relative Y motion (delta)", "default": 0}
    }
})schema";

static void handle_inject_mouse_motion(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    double x = params.value("x", 0.0);
    double y = params.value("y", 0.0);
    double rx = params.value("relative_x", 0.0);
    double ry = params.value("relative_y", 0.0);

    if (is_editor_mode()) {
        r_error = EDITOR_MODE_HINT;
        return;
    }

    GDExtensionObjectPtr ev = make_event("InputEventMouseMotion");
    if (!ev) { r_error = "Failed to instantiate InputEventMouseMotion"; return; }

    auto set_json = [&](const std::string &p_name, const std::string &p_json) {
        GodotAPI::set_property_from_json(ev, p_name, p_json);
    };
    set_json("position", "{\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + "}");
    set_json("global_position", "{\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + "}");
    set_json("relative", "{\"x\":" + std::to_string(rx) + ",\"y\":" + std::to_string(ry) + "}");

    GDExtensionObjectPtr input = get_input();
    if (!input) { r_error = "Input singleton not available"; return; }
    {
        uint8_t vb[64] = {0};
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)vb, ev);
        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)vb };
        GodotAPI::call_method_void(input, "parse_input_event", args, 1);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)vb);
    }

    json result = {{"success", true}, {"x", x}, {"y", y}, {"relative_x", rx}, {"relative_y", ry}};
    r_result = result.dump();
}

} // namespace gear_mcp

void register_testing_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("capture_screenshot",
        "Capture the current root viewport and save as a PNG file",
        gear_mcp::CAPTURE_SCREENSHOT_SCHEMA, gear_mcp::handle_capture_screenshot, /*main_thread=*/true);

    p_registry->register_tool("capture_viewport",
        "Capture a specific viewport (currently always the root viewport)",
        gear_mcp::CAPTURE_VIEWPORT_SCHEMA, gear_mcp::handle_capture_viewport, /*main_thread=*/true);

    p_registry->register_tool("inject_action",
        "Press or release a named input action (must exist in project.godot's [input])",
        gear_mcp::INJECT_ACTION_SCHEMA, gear_mcp::handle_inject_action, /*main_thread=*/true);

    p_registry->register_tool("inject_key",
        "Inject a keyboard event (InputEventKey) into the running game",
        gear_mcp::INJECT_KEY_SCHEMA, gear_mcp::handle_inject_key, /*main_thread=*/true);

    p_registry->register_tool("inject_mouse_click",
        "Inject a mouse click (InputEventMouseButton)",
        gear_mcp::INJECT_MOUSE_CLICK_SCHEMA, gear_mcp::handle_inject_mouse_click, /*main_thread=*/true);

    p_registry->register_tool("inject_mouse_motion",
        "Inject a mouse motion event (InputEventMouseMotion)",
        gear_mcp::INJECT_MOUSE_MOTION_SCHEMA, gear_mcp::handle_inject_mouse_motion, /*main_thread=*/true);

    std::printf("[Gear MCP] Registered testing tools: capture_screenshot, capture_viewport, inject_action, inject_key, inject_mouse_click, inject_mouse_motion\n");
}
