#include "register_all.h"
#include "server/tool_registry.h"

#include <cstdio>

// Forward declarations for tool registration functions
void register_file_tools(gear_mcp::ToolRegistry *p_registry);
void register_scene_tools(gear_mcp::ToolRegistry *p_registry);
void register_scene_tools_phase2(gear_mcp::ToolRegistry *p_registry);
void register_script_tools(gear_mcp::ToolRegistry *p_registry);
void register_classdb_tools(gear_mcp::ToolRegistry *p_registry);
void register_project_tools(gear_mcp::ToolRegistry *p_registry);
void register_editor_tools(gear_mcp::ToolRegistry *p_registry);
void register_resource_tools(gear_mcp::ToolRegistry *p_registry);
void register_export_tools(gear_mcp::ToolRegistry *p_registry);
void register_signal_tools(gear_mcp::ToolRegistry *p_registry);
void register_autoload_tools(gear_mcp::ToolRegistry *p_registry);
// Phase 3 domains
void register_animation_tools(gear_mcp::ToolRegistry *p_registry);
void register_audio_tools(gear_mcp::ToolRegistry *p_registry);
void register_tilemap_tools(gear_mcp::ToolRegistry *p_registry);
void register_navigation_tools(gear_mcp::ToolRegistry *p_registry);
void register_theme_tools(gear_mcp::ToolRegistry *p_registry);
void register_plugin_tools(gear_mcp::ToolRegistry *p_registry);
void register_input_tools(gear_mcp::ToolRegistry *p_registry);
void register_uid_tools(gear_mcp::ToolRegistry *p_registry);
void register_import_tools(gear_mcp::ToolRegistry *p_registry);
void register_runtime_tools(gear_mcp::ToolRegistry *p_registry);
// Phase 4 domains
void register_intent_tools(gear_mcp::ToolRegistry *p_registry);
void register_meta_tools(gear_mcp::ToolRegistry *p_registry);
void register_assets_tools(gear_mcp::ToolRegistry *p_registry);
void register_debug_tools(gear_mcp::ToolRegistry *p_registry);
void register_testing_tools(gear_mcp::ToolRegistry *p_registry);
void register_dx_tools(gear_mcp::ToolRegistry *p_registry);

namespace gear_mcp {

void register_all_tools(ToolRegistry *p_registry) {
    if (!p_registry) {
        std::fprintf(stderr, "[Gear MCP] register_all_tools: ToolRegistry is null\n");
        return;
    }

    register_file_tools(p_registry);          // 3 tools
    register_scene_tools(p_registry);         // 3 tools (Phase 1)
    register_scene_tools_phase2(p_registry);  // 7 tools
    register_script_tools(p_registry);        // 3 tools
    register_classdb_tools(p_registry);       // 3 tools
    register_project_tools(p_registry);       // 5 tools
    register_editor_tools(p_registry);        // 6 tools
    register_resource_tools(p_registry);      // 5 tools
    register_export_tools(p_registry);        // 2 tools
    register_signal_tools(p_registry);        // 3 tools
    register_autoload_tools(p_registry);      // 4 tools
    // Phase 3
    register_animation_tools(p_registry);    // 5 tools
    register_audio_tools(p_registry);         // 4 tools
    register_tilemap_tools(p_registry);       // 2 tools
    register_navigation_tools(p_registry);    // 2 tools
    register_theme_tools(p_registry);         // 3 tools
    register_plugin_tools(p_registry);        // 3 tools
    register_input_tools(p_registry);         // 1 tool
    register_uid_tools(p_registry);           // 2 tools
    register_import_tools(p_registry);        // 5 tools
    register_runtime_tools(p_registry);       // 4 tools
    // Phase 4
    register_intent_tools(p_registry);        // 9 tools
    register_meta_tools(p_registry);          // 2 tools
    register_assets_tools(p_registry);        // 3 tools
    register_debug_tools(p_registry);         // 10 tools (4 LSP + 6 DAP)
    register_testing_tools(p_registry);       // 6 tools
    register_dx_tools(p_registry);            // 4 tools

    std::printf("[Gear MCP] All tools registered: %zu total\n", p_registry->tool_count());
}

} // namespace gear_mcp
