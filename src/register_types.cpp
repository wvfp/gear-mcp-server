#include "register_types.h"

#include "godot_api/godot_api.h"
#include "godot_api/editor_context.h"
#include "gear_main_thread_node.h"
#include "server/gear_mcp_api.h"
#include "server/main_thread_queue.h"
#include "server/mcp_methods.h"
#include "server/mcp_server.h"
#include "server/tool_registry.h"
#include "server/phase4_resources.h"
#include "tools/register_all.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdio>
#include <thread>
#include <chrono>

using namespace godot;

namespace {

gear_mcp::MCPServer *g_mcp_server = nullptr;

// Single instance of the dispatch node, kept alive for the lifetime of the
// editor. Created on a background thread once the SceneTree is available,
// then added to the root Window via call_deferred() so the actual
// add_child happens on the main thread.
gear_mcp::GearMainThreadNode *g_main_thread_node = nullptr;

void schedule_main_thread_dispatch_node() {
    std::thread([]() {
        for (int i = 0; i < 120; ++i) { // up to ~60s at 500ms
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            MainLoop *ml = Engine::get_singleton()->get_main_loop();
            SceneTree *tree = Object::cast_to<SceneTree>(ml);
            Window *root = tree ? tree->get_root() : nullptr;
            if (root) {
                g_main_thread_node = memnew(gear_mcp::GearMainThreadNode);
                g_main_thread_node->set_name("GearMainThreadNode");
                // Queue add_child on the main thread to avoid the documented
                // thread-safety issue with modifying the scene tree off-thread.
                root->call_deferred("add_child", g_main_thread_node);
                g_main_thread_node->set_process(true);

                // The node's _process will start firing on the next frame,
                // so flip the dispatch flag right away. (If a task is queued
                // in the same frame, the worst case is one extra frame of
                // latency, which the 10s timeout in invoke_on_main covers.)
                gear_mcp::MainThreadQueue::set_dispatch_available(true);
                std::fprintf(stderr, "[Gear MCP] Main-thread dispatch ENABLED.\n");
                return;
            }
        }
        std::fprintf(stderr,
                     "[Gear MCP] Main-thread dispatch: SceneTree never available, "
                     "tools will run on caller thread (fallback).\n");
    }).detach();
}

} // namespace

void initialize_gear_mcp_module(ModuleInitializationLevel p_level) {
    // Class registration must happen at SCENE level (or later) so the engine
    // accepts a Node subclass. Doing it here avoids the manual
    // classdb_register_extension_class2 / classdb_construct_object dance
    // (those C API versions are deprecated/removed in 4.7-dev3).
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(gear_mcp::GearMainThreadNode);
        // GearMCPAPI extends Object and only needs registration so that
        // _bind_methods is invoked. The singleton instance is created at
        // SCENE level (per Godot 4.4+ requirement that engine singletons
        // be registered at SCENE init, not EDITOR) with a null server
        // pointer; the live server is wired in at EDITOR level via
        // set_server() below.
        GDREGISTER_CLASS(gear_mcp::GearMCPAPI);
        gear_mcp::GearMCPAPI::create_and_register(nullptr);
        return;
    }

    if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) {
        return;
    }

    gear_mcp::GodotAPI::init(godot::gdextension_interface::get_proc_address);

    g_mcp_server = new gear_mcp::MCPServer();
    gear_mcp::register_all_tools(g_mcp_server->get_tool_registry());

    gear_mcp::ResourceInfo ctx;
    ctx.uri = "godot://editor/context";
    ctx.name = "Editor Context";
    ctx.description = "Current editor context information";
    ctx.mime_type = "application/json";
    ctx.read_handler = gear_mcp::EditorContext::resource_read_handler;
    g_mcp_server->get_methods()->register_resource(ctx);

    // Phase 4.7: register additional resources and prompts
    gear_mcp::register_phase4_resources(g_mcp_server->get_methods());

    if (g_mcp_server->start()) {
        std::fprintf(stderr, "[Gear MCP] Server started on 127.0.0.1:8510, %zu tools registered\n",
                     g_mcp_server->get_tool_registry()->tool_count());
    } else {
        std::fprintf(stderr, "[Gear MCP] Failed to start server\n");
    }

    // The C++ Node subclass is registered at SCENE level above; here we
    // just wait for the SceneTree to be ready and add it. This is the
    // only piece of "magic" needed to bridge worker threads -> main thread.
    schedule_main_thread_dispatch_node();
}

void uninitialize_gear_mcp_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) {
        return;
    }
    gear_mcp::MainThreadQueue::set_dispatch_available(false);

    // Tear down the API singleton before MCPServer so the engine lookup
    // table doesn't dangle.
    gear_mcp::GearMCPAPI::unregister_and_destroy();

    if (g_mcp_server) {
        delete g_mcp_server;
        g_mcp_server = nullptr;
    }
    // The node is owned by the SceneTree and will be freed when the editor
    // tears down; we only drop our pointer here.
    g_main_thread_node = nullptr;
}

extern "C" {
GDExtensionBool GDE_EXPORT gear_mcp_server_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization) {
    GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_gear_mcp_module);
    init_obj.register_terminator(uninitialize_gear_mcp_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}
}
