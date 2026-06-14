#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace gear_mcp {

class MCPServer;

// ---------------------------------------------------------------------------
// GearMCPAPI — the singleton class exposed to GDScript via
// `GDREGISTER_CLASS` + `bind_static_method("get_singleton", ...)`. The editor
// panel (project/addons/gear_mcp/panel.gd) calls these methods to read
// status, toggle tools, and stream the log.
//
// Lifetime: created in `register_types.cpp` at MODULE_INITIALIZATION_LEVEL_EDITOR
// right after MCPServer starts, destroyed in the corresponding uninit.
// ---------------------------------------------------------------------------
class GearMCPAPI : public godot::Object {
    GDCLASS(GearMCPAPI, godot::Object)

public:
    /// Returns the process-wide instance, or nullptr if not initialized yet.
    /// GDScript usage: `var api = Engine.get_singleton("GearMCP")` after the
    /// register_types code calls `Engine::register_singleton`, or use the
    /// static accessor `GearMCPAPI.get_singleton()`.
    static GearMCPAPI *get_singleton();

    /// Construct the singleton and register it as an Engine singleton under
    /// the name "GearMCPAPI" (must match the C++ class name). Must be called
    /// at SCENE init level so the editor plugin can see it. Pass nullptr
    /// for p_server; the actual server is wired in via set_server() at
    /// EDITOR level once MCPServer is constructed.
    static void create_and_register(MCPServer *p_server);

    /// Wire the live MCPServer pointer into the singleton. Called from
    /// register_types.cpp at EDITOR init after the server has been built.
    static void set_server(MCPServer *p_server);

    /// Unregister the engine singleton and free the instance. Called from
    /// register_types.cpp at EDITOR uninit.
    static void unregister_and_destroy();

    // ----- Status -----
    bool is_running() const;
    int  get_port() const;
    int  get_connected_clients() const;
    int  get_total_tools() const;
    int  get_enabled_tools() const;
    godot::String get_endpoint() const;
    int64_t get_total_call_count() const;

    // ----- Tools -----
    /// Returns: Array of Dictionary { name, description, domain, enabled, main_thread, call_count }
    godot::Array get_tools() const;
    bool set_tool_enabled(const godot::String &p_name, bool p_enabled);
    bool is_tool_enabled(const godot::String &p_name) const;
    int  enable_all_tools();
    int  disable_all_tools();

    // ----- Logs -----
    /// Returns: Array of Dictionary { timestamp_ms, level, method, tool, status, duration_ms, message }
    /// Most recent entries (up to p_max_count) at the back, oldest at the front.
    godot::Array get_log_entries(int p_max_count) const;
    int  get_log_count() const;
    void clear_logs();

    // ----- Signals (forwarded to GDScript panel for live updates) -----
    void emit_log_appended(int64_t p_timestamp_ms, const godot::String &p_level,
                           const godot::String &p_method, const godot::String &p_tool,
                           const godot::String &p_status, int p_duration_ms,
                           const godot::String &p_message);
    void emit_tool_state_changed(const godot::String &p_name, bool p_enabled);
    void emit_client_count_changed(int p_count);

protected:
    static void _bind_methods();

private:
    GearMCPAPI();
    ~GearMCPAPI();

    static GearMCPAPI *s_singleton;
    MCPServer *m_server = nullptr;
};

} // namespace gear_mcp
