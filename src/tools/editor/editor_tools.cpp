#include "server/tool_registry.h"
#include "server/main_thread_queue.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Handler: run_project
// ---------------------------------------------------------------------------
static void handle_run_project(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) {
        r_error = "Invalid JSON parameters.";
        return;
    }

    GDExtensionObjectPtr editor = GodotAPI::get_editor_interface();
    if (!editor) {
        r_error = "Editor interface not available.";
        return;
    }

    std::string scene;
    if (params.contains("scene") && params["scene"].is_string()) {
        scene = params["scene"].get<std::string>();
    }

    if (!scene.empty()) {
        uint8_t var_buf[64] = {0};
        GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)var_buf, scene);
        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)var_buf };
        GodotAPI::call_method_void(editor, "play_custom_scene", args, 1);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)var_buf);
        json res;
        res["status"] = "running";
        res["scene"] = scene;
        r_result = res.dump();
    } else {
        GodotAPI::call_method_void(editor, "play_main_scene", nullptr, 0);
        json res;
        res["status"] = "running";
        res["scene"] = "main";
        r_result = res.dump();
    }
}

// ---------------------------------------------------------------------------
// Handler: run_scene_with_timeout
// ---------------------------------------------------------------------------
//
// Composite tool: launches a scene via play_custom_scene, waits for the
// requested number of milliseconds, captures a viewport screenshot to
// <output_path>, then stops the running scene and returns the screenshot
// path. Lets a caller exercise "open + play + observe + stop" end-to-end
// through MCP without touching the editor UI.
//
// IMPORTANT: this handler is registered as main_thread=false. The Sleep
// between play and capture must NOT block the Godot main thread — the
// editor's main thread is what drives the play window's rendering loop.
// Blocking it would mean the play window never gets a single frame to
// render, and the screenshot would be empty. Instead we run the
// Godot API calls (play_custom_scene, get_viewport chain, save_png,
// stop_playing_scene) via MainThreadQueue, while the worker thread
// itself does the Sleep.
// ---------------------------------------------------------------------------
static void handle_run_scene_with_timeout(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string scene = params.value("scene", "");
    int timeout_ms = params.value("timeout_ms", 3000);
    if (timeout_ms < 100) timeout_ms = 100;
    if (timeout_ms > 60000) timeout_ms = 60000;
    std::string output_path = params.value("output_path", "res://screenshot.png");

    // Outer-scope state for the capture step. Declared up here so the
    // file-watch loop and the C++-side fallback both update the same
    // variables reported in the final response.
    std::string absolute = GodotAPI::res_to_absolute(output_path);
    bool captured = false;
    std::string capture_source;
    int save_png_error = -1;
    int tried_sources = 0;
    std::string capture_log;

    // 1. Launch the scene on the main thread.
    //
    //    Before launching, write a sidecar file with the caller's
    //    requested output_path. The screenshot helper script attached
    //    to the scene reads this file in _ready and redirects its
    //    save_png() call to whatever the caller asked for. This is how
    //    the C++ tool and the GDScript helper stay in agreement about
    //    the final file location, even though they run in different
    //    processes (the editor and the played scene).
    {
        namespace fs = std::filesystem;
        std::string sidecar_abs = GodotAPI::res_to_absolute("res://mcp_screenshot_target.txt");
        std::error_code ec;
        // Make sure parent dir exists
        fs::create_directories(fs::path(sidecar_abs).parent_path(), ec);
        std::ofstream sf(sidecar_abs, std::ios::trunc);
        if (sf.is_open()) {
            sf << output_path;
            sf.close();
        }

        std::string scene_copy = scene;
        std::string launch_err = MainThreadQueue::invoke_on_main([scene_copy]() -> std::string {
            GDExtensionObjectPtr editor = GodotAPI::get_editor_interface();
            if (!editor) return std::string("Editor interface not available.");
            uint8_t var_buf[64] = {0};
            GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)var_buf, scene_copy);
            GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)var_buf };
            GodotAPI::call_method_void(editor, "play_custom_scene", args, 1);
            GodotAPI::destroy_variant((GDExtensionVariantPtr)var_buf);
            return std::string();
        });
        if (!launch_err.empty()) {
            r_error = launch_err;
            return;
        }
    }

    // 2. Poll is_playing_scene on the main thread until it reports true,
    //    with a 6 s budget. If play never starts (e.g. invalid display
    //    driver, missing main scene, syntax error in the scene's script),
    //    we abort immediately instead of sleeping the full timeout. This
    //    prevents the editor from sitting in a zombie state and the
    //    worker thread from blocking on a 60 s Sleep for no reason.
    //
    //    The 6 s budget (up from 2 s) is needed because the play
    //    subprocess takes ~1-2 s to load the project, GDExtension, and
    //    scripts before the editor's is_playing_scene flag flips.
    bool started_playing = false;
    std::string poll_diag;
    {
        const int poll_total_ms = 6000;
        const int poll_step_ms = 100;
        int waited = 0;
        while (waited < poll_total_ms) {
            std::string playing_flag = MainThreadQueue::invoke_on_main([]() -> std::string {
                GDExtensionObjectPtr ed = GodotAPI::get_editor_interface();
                if (!ed) return std::string("false");
                return GodotAPI::call_method_string(ed, "is_playing_scene");
            });
            poll_diag += "[" + std::to_string(waited) + "ms]=" + playing_flag + ";";
            if (playing_flag == "true" || playing_flag == "1") {
                started_playing = true;
                break;
            }
#ifdef _WIN32
            Sleep((DWORD)poll_step_ms);
#else
            usleep((useconds_t)poll_step_ms * 1000);
#endif
            waited += poll_step_ms;
        }
        if (!started_playing) {
            // Best-effort stop in case the editor half-started.
            MainThreadQueue::invoke_on_main([]() -> std::string {
                GDExtensionObjectPtr ed = GodotAPI::get_editor_interface();
                if (ed) GodotAPI::call_method_void(ed, "stop_playing_scene", nullptr, 0);
                return std::string();
            });
            json err;
            err["error"] = "Scene failed to start playing within 6 s. is_playing_scene stayed false the whole time. Poll log: " + poll_diag;
            err["hint"] = "Check that the scene file exists and that the play subprocess can launch (no missing imports, no syntax errors, valid graphics driver).";
            r_error = err.dump();
            return;
        }
    }

    // 3. Wait for the helper script (if attached) to write the screenshot
    //    file. The helper script attached to the scene calls
    //    `img.save_png(screenshot_path)` from its own _process loop, so
    //    all this step needs to do is poll the filesystem for the
    //    file to exist and grow past 0 bytes. We then take whatever the
    //    helper wrote as the source of truth.
    bool file_exists = false;
    size_t file_size = 0;
    {
        const int wait_total_ms = timeout_ms; // budget = total timeout
        const int wait_step_ms = 50;
        int waited = 0;
        namespace fs = std::filesystem;
        std::error_code ec;
        while (waited < wait_total_ms) {
            if (fs::exists(absolute, ec) && fs::file_size(absolute, ec) > 0) {
                file_exists = true;
                file_size = fs::file_size(absolute, ec);
                break;
            }
#ifdef _WIN32
            Sleep((DWORD)wait_step_ms);
#else
            usleep((useconds_t)wait_step_ms * 1000);
#endif
            waited += wait_step_ms;
        }
    }

    // 4. If the helper script wrote the file, we're done. Otherwise fall
    //    back to C++-side viewport capture (which usually returns empty
    //    for 2D scenes, but may work for 3D / embedded viewports).
    if (file_exists) {
        captured = true;
        capture_source = "helper_script";
    } else {
        auto try_capture = [&](GDExtensionObjectPtr vp, const char *source_name) -> bool {
            if (!vp) {
                capture_log += std::string(source_name) + ":null;";
                return false;
            }
            GDExtensionObjectPtr tex = GodotAPI::call_method_object_with_args(vp, "get_texture", nullptr, 0);
            if (!tex) { capture_log += std::string(source_name) + ":no-tex;"; return false; }
            GDExtensionObjectPtr img = GodotAPI::call_method_object_with_args(tex, "get_image", nullptr, 0);
            if (!img) { capture_log += std::string(source_name) + ":no-img;"; return false; }
            int img_w = GodotAPI::call_method_int_with_args(img, "get_width", nullptr, 0);
            int img_h = GodotAPI::call_method_int_with_args(img, "get_height", nullptr, 0);
            capture_log += std::string(source_name) + ":";
            capture_log += std::to_string(img_w) + "x" + std::to_string(img_h) + ";";
            if (img_w <= 0 || img_h <= 0) return false;
            uint8_t path_buf[512] = {0};
            GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)path_buf, absolute);
            GDExtensionConstVariantPtr save_args[] = { (GDExtensionConstVariantPtr)path_buf };
            int err = GodotAPI::call_method_int_with_args(img, "save_png", save_args, 1);
            GodotAPI::destroy_variant((GDExtensionVariantPtr)path_buf);
            save_png_error = err;
            if (err == 0) {
                namespace fs = std::filesystem;
                std::error_code ec;
                if (fs::exists(absolute, ec) && fs::file_size(absolute, ec) > 0) {
                    capture_source = source_name;
                    return true;
                }
            }
            return false;
        };

        // Run the fallback ladder on the main thread (Godot API calls
        // must happen on the main thread).
        MainThreadQueue::invoke_on_main([&]() -> std::string {
            // Source 1: editor's 3D viewport
            GDExtensionObjectPtr e_interface = GodotAPI::get_editor_interface();
            GDExtensionObjectPtr vp = e_interface
                ? GodotAPI::call_method_object_with_args(e_interface, "get_editor_viewport", nullptr, 0)
                : nullptr;
            tried_sources++;
            captured = try_capture(vp, "editor_viewport");
            // Source 2: OS.get_root() — main editor window viewport
            if (!captured) {
                GDExtensionObjectPtr os = GodotAPI::get_global_singleton("OS");
                GDExtensionObjectPtr root = os
                    ? GodotAPI::call_method_object_with_args(os, "get_root", nullptr, 0)
                    : nullptr;
                tried_sources++;
                captured = try_capture(root, "os_root_window");
            }
            // Source 3: Engine.get_main_loop().get_root() — SceneTree root
            if (!captured) {
                GDExtensionObjectPtr engine = GodotAPI::get_global_singleton("Engine");
                GDExtensionObjectPtr main_loop = engine
                    ? GodotAPI::call_method_object_with_args(engine, "get_main_loop", nullptr, 0)
                    : nullptr;
                GDExtensionObjectPtr st_root = main_loop
                    ? GodotAPI::call_method_object_with_args(main_loop, "get_root", nullptr, 0)
                    : nullptr;
                tried_sources++;
                captured = try_capture(st_root, "scenetree_root");
            }
            return std::string();
        });
    }

    // 5. Stop the running scene (best-effort).
    MainThreadQueue::invoke_on_main([]() -> std::string {
        GDExtensionObjectPtr ed = GodotAPI::get_editor_interface();
        if (ed) GodotAPI::call_method_void(ed, "stop_playing_scene", nullptr, 0);
        return std::string();
    });

    json res;
    res["status"] = captured ? "captured" : "capture_failed";
    res["scene"] = scene;
    res["timeout_ms"] = timeout_ms;
    res["output_path"] = output_path;
    res["absolute_path"] = absolute;
    res["captured"] = captured;
    res["capture_source"] = capture_source;
    res["file_size"] = file_size;
    res["save_png_error"] = save_png_error;
    res["tried_sources"] = tried_sources;
    res["capture_log"] = capture_log;
    res["poll_diag"] = poll_diag;
    if (!captured) {
        res["hint"] = "Neither the helper script nor any editor viewport produced a screenshot. "
                      "If the scene has a script that overwrites the file, make sure it does not call "
                      "get_tree().quit() (the MCP tool owns the play loop). If you don't have a helper "
                      "script, attach scripts/screenshot_helper.gd to the root node and ensure the "
                      "screenshot_path export matches output_path.";
    }
    r_result = res.dump();
}

// ---------------------------------------------------------------------------
// Handler: stop_project
// ---------------------------------------------------------------------------
static void handle_stop_project(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    GDExtensionObjectPtr editor = GodotAPI::get_editor_interface();
    if (!editor) {
        r_error = "Editor interface not available.";
        return;
    }

    GodotAPI::call_method_void(editor, "stop_playing_scene", nullptr, 0);

    json res;
    res["status"] = "stopped";
    r_result = res.dump();
}

// ---------------------------------------------------------------------------
// Handler: get_debug_output
// ---------------------------------------------------------------------------
static void handle_get_debug_output(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) {
        r_error = "Invalid JSON parameters.";
        return;
    }

    int max_lines = 100;
    if (params.contains("max_lines") && params["max_lines"].is_number_integer()) {
        max_lines = params["max_lines"].get<int>();
    }

    GDExtensionObjectPtr editor = GodotAPI::get_editor_interface();
    if (!editor) {
        r_error = "Editor interface not available.";
        return;
    }

    std::string log_output = GodotAPI::call_method_string(editor, "get_editor_log");

    if (log_output.empty()) {
        json res;
        res["output"] = "";
        res["message"] = "Editor log not available or empty.";
        r_result = res.dump();
        return;
    }

    // Split into lines and apply max_lines limit.
    std::vector<std::string> lines;
    std::string::size_type start = 0;
    std::string::size_type pos = 0;
    while ((pos = log_output.find('\n', start)) != std::string::npos) {
        lines.push_back(log_output.substr(start, pos - start));
        start = pos + 1;
    }
    if (start < log_output.size()) {
        lines.push_back(log_output.substr(start));
    }

    int total_lines = (int)lines.size();
    int begin_idx = total_lines > max_lines ? total_lines - max_lines : 0;

    std::string trimmed;
    for (int i = begin_idx; i < total_lines; ++i) {
        if (!trimmed.empty()) {
            trimmed += "\n";
        }
        trimmed += lines[i];
    }

    json res;
    res["output"] = trimmed;
    res["total_lines"] = total_lines;
    res["returned_lines"] = total_lines - begin_idx;
    r_result = res.dump();
}

// ---------------------------------------------------------------------------
// Handler: get_editor_status
// ---------------------------------------------------------------------------
static void handle_get_editor_status(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    GDExtensionObjectPtr editor = GodotAPI::get_editor_interface();
    if (!editor) {
        r_error = "Editor interface not available.";
        return;
    }

    json res;

    // Scene open
    GDExtensionObjectPtr scene_root = GodotAPI::get_edited_scene_root();
    if (scene_root) {
        std::string scene_name = GodotAPI::call_method_string(scene_root, "get_scene_file_path");
        if (scene_name.empty()) {
            scene_name = GodotAPI::call_method_string(scene_root, "get_name");
        }
        res["scene_open"] = scene_name.empty() ? json(nullptr) : json(scene_name);
    } else {
        res["scene_open"] = false;
    }

    // Playing state
    std::string playing_str = GodotAPI::call_method_string(editor, "is_playing_scene");
    res["is_playing"] = (playing_str == "true" || playing_str == "1");

    // Selected nodes
    std::vector<GDExtensionObjectPtr> selected = GodotAPI::get_selected_nodes();
    json selected_arr = json::array();
    for (GDExtensionObjectPtr node : selected) {
        std::string node_name = GodotAPI::call_method_string(node, "get_name");
        if (!node_name.empty()) {
            selected_arr.push_back(node_name);
        }
    }
    res["selected_nodes"] = selected_arr;

    // Project path
    res["project_path"] = GodotAPI::get_project_path();

    r_result = res.dump();
}

// ---------------------------------------------------------------------------
// Handler: get_godot_version
// ---------------------------------------------------------------------------
static void handle_get_godot_version(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    std::string version = GodotAPI::get_godot_version();

    if (version.empty()) {
        r_error = "Unable to retrieve Godot version.";
        return;
    }

    json res;
    res["version"] = version;
    r_result = res.dump();
}

// ---------------------------------------------------------------------------
// Handler: launch_editor
// ---------------------------------------------------------------------------
static void handle_launch_editor(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) {
        r_error = "Invalid JSON parameters.";
        return;
    }

    if (!params.contains("scene_path") || !params["scene_path"].is_string()) {
        r_error = "Missing required parameter: scene_path";
        return;
    }

    std::string scene_path = params["scene_path"].get<std::string>();

    GDExtensionObjectPtr loaded = GodotAPI::load_scene(scene_path);
    if (!loaded) {
        r_error = "Failed to load scene: " + scene_path;
        return;
    }

    json res;
    res["status"] = "loaded";
    res["scene_path"] = scene_path;
    r_result = res.dump();
}

// ---------------------------------------------------------------------------
// Handler: is_playing
// ---------------------------------------------------------------------------
// Lightweight poll used by callers that want to gate a follow-up tool on
// the play window actually being up. Cheaper than get_editor_status, which
// walks the edited scene tree and collects selected nodes.
static void handle_is_playing(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    GDExtensionObjectPtr editor = GodotAPI::get_editor_interface();
    if (!editor) {
        r_error = "Editor interface not available.";
        return;
    }
    std::string flag = GodotAPI::call_method_string(editor, "is_playing_scene");
    json res;
    res["is_playing"] = (flag == "true" || flag == "1");
    r_result = res.dump();
}

// ---------------------------------------------------------------------------
// Handler: restart_editor
// ---------------------------------------------------------------------------
// Asks the engine to restart the current editor process. The official
// hook is EditorInterface.restart_editor(bool save_before). It saves
// the project if requested and re-execs the same binary with the same
// command-line arguments, so the new editor comes back pointing at the
// same project (and MCP comes back on the same port).
//
// We pass save_before=false to avoid clobbering an in-progress edit the
// user might be making out-of-band. The tool is meant to be called after
// set_project_setting has already persisted the new value to project.godot.
//
// Note: restart_editor takes a bool, not no-args. We need to pass false.
static void handle_restart_editor(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    (void)p_params_json;
    GDExtensionObjectPtr editor = GodotAPI::get_editor_interface();
    if (!editor) {
        r_error = "Editor interface not available.";
        return;
    }

    uint8_t save_buf[16] = {0};
    GodotAPI::make_variant_bool((GDExtensionUninitializedVariantPtr)save_buf, false);
    GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)save_buf };
    GodotAPI::call_method_void(editor, "restart_editor", args, 1);
    GodotAPI::destroy_variant((GDExtensionVariantPtr)save_buf);

    json res;
    res["status"] = "restarting";
    res["method"] = "EditorInterface.restart_editor(false)";
    res["note"] = "The current editor will exit shortly and a new one will open. MCP will come back on the same port once the new editor finishes loading. If it does not, close this editor and rerun it manually: <executable> --editor --path <project>";
    r_result = res.dump();
}

} // namespace gear_mcp

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
void register_editor_tools(gear_mcp::ToolRegistry *p_registry) {

    // run_project
    p_registry->register_tool("run_project",
        "Run the current Godot project or a specific scene.",
        R"({"type":"object","properties":{"scene":{"type":"string","description":"Optional specific scene to run"}}})",
        gear_mcp::handle_run_project, /*main_thread=*/true);

    // run_scene_with_timeout
    // main_thread=false: the handler does its own main-thread dispatch via
    // MainThreadQueue::invoke_on_main(), which is the only way to keep the
    // editor's main thread responsive while we sleep. If we marked this
    // main_thread=true, the Sleep inside the handler would freeze the
    // editor — and with it, the play window's rendering loop.
    p_registry->register_tool("run_scene_with_timeout",
        "Composite: run a scene, wait N ms, capture a screenshot, then stop. Returns the screenshot path.",
        R"JSON({"type":"object","properties":{"scene":{"type":"string","description":"res:// path of the scene to play"},"timeout_ms":{"type":"integer","description":"How long to let the scene run before capture (ms)","default":3000},"output_path":{"type":"string","description":"res:// path for the PNG screenshot","default":"res://screenshot.png"}},"required":["scene"]})JSON",
        gear_mcp::handle_run_scene_with_timeout, /*main_thread=*/false);

    // stop_project
    p_registry->register_tool("stop_project",
        "Stop the currently running project.",
        R"({"type":"object","properties":{}})",
        gear_mcp::handle_stop_project, /*main_thread=*/true);

    // get_debug_output
    p_registry->register_tool("get_debug_output",
        "Get the editor output/debug log.",
        R"({"type":"object","properties":{"max_lines":{"type":"integer","description":"Max lines to return","default":100}}})",
        gear_mcp::handle_get_debug_output, /*main_thread=*/true);

    // get_editor_status
    p_registry->register_tool("get_editor_status",
        "Get current editor status including open scene, running state, and selected nodes.",
        R"({"type":"object","properties":{}})",
        gear_mcp::handle_get_editor_status, /*main_thread=*/true);

    // get_godot_version
    p_registry->register_tool("get_godot_version",
        "Get the Godot engine version information.",
        R"({"type":"object","properties":{}})",
        gear_mcp::handle_get_godot_version, /*main_thread=*/true);

    // launch_editor
    p_registry->register_tool("launch_editor",
        "Open a specific scene in the Godot editor.",
        R"({"type":"object","properties":{"scene_path":{"type":"string","description":"res:// path to scene file"}},"required":["scene_path"]})",
        gear_mcp::handle_launch_editor, /*main_thread=*/true);

    // is_playing
    p_registry->register_tool("is_playing",
        "Return whether the editor is currently playing a scene.",
        R"({"type":"object","properties":{}})",
        gear_mcp::handle_is_playing, /*main_thread=*/true);

    // restart_editor
    p_registry->register_tool("restart_editor",
        "Restart the Godot editor process. Use after set_project_setting changes that need a reload (e.g. rendering_driver). The new editor comes back on the same port.",
        R"({"type":"object","properties":{}})",
        gear_mcp::handle_restart_editor, /*main_thread=*/true);
}
