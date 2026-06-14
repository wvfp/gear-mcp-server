#pragma once

#include <string>

namespace gear_mcp {

// ---------------------------------------------------------------------------
// EditorContext — provides the MCP resource handler for godot://editor/context.
// Delegates to GodotAPI for the actual data.
// ---------------------------------------------------------------------------
class EditorContext {
public:
    /// MCP resources/read callback for godot://editor/context.
    static void resource_read_handler(const std::string &p_uri, std::string &r_text_out);
};

} // namespace gear_mcp
