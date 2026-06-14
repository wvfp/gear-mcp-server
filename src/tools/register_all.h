#pragma once

namespace gear_mcp {

class ToolRegistry;

/// Register all tools (file/ + scene/ + script/ + classdb/ + project/ +
/// editor/ + resource/ + export/ + signal/ + autoload/).
void register_all_tools(ToolRegistry *p_registry);

} // namespace gear_mcp
