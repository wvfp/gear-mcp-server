#pragma once

namespace gear_mcp {

class MCPMethods;

/// Register Phase 4.7 MCP resources (project/info, scene, script, resource)
/// and prompts (scene_bootstrap, debug_triage).
void register_phase4_resources(MCPMethods *p_methods);

} // namespace gear_mcp
