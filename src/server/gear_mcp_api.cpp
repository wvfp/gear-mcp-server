#include "gear_mcp_api.h"
#include "mcp_server.h"
#include "mcp_methods.h"
#include "tcp_server.h"
#include "tool_registry.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace gear_mcp {

GearMCPAPI *GearMCPAPI::s_singleton = nullptr;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GearMCPAPI::GearMCPAPI() = default;

GearMCPAPI::~GearMCPAPI() {
    s_singleton = nullptr;
}

GearMCPAPI *GearMCPAPI::get_singleton() {
    return s_singleton;
}

void GearMCPAPI::create_and_register(MCPServer *p_server) {
    if (s_singleton) return;
    // memnew is required (not raw new) so godot-cpp's binding callbacks
    // (RefCounted-style instance binding, ClassDB lookups) get attached.
    s_singleton = memnew(GearMCPAPI);
    s_singleton->m_server = p_server;
    // Must use the class name "GearMCPAPI" as the lookup key — GDScript-side
    // Engine.get_singleton() walks the same table the C++ Engine singleton
    // uses, and Engine.has_singleton("GearMCP") would otherwise return false.
    auto engine = godot::Engine::get_singleton();
    engine->register_singleton("GearMCPAPI", s_singleton);
}

void GearMCPAPI::set_server(MCPServer *p_server) {
    // Called from EDITOR level after the server is constructed. The singleton
    // is registered at SCENE level (above) with a null server pointer.
    s_singleton->m_server = p_server;
}

void GearMCPAPI::unregister_and_destroy() {
    if (!s_singleton) return;
    godot::Engine::get_singleton()->unregister_singleton("GearMCPAPI");
    memdelete(s_singleton);
    s_singleton = nullptr;
}

// ---------------------------------------------------------------------------
// Method bindings
// ---------------------------------------------------------------------------

void GearMCPAPI::_bind_methods() {
    using namespace godot;

    // Singleton accessor (so GDScript can also do GearMCPAPI.get_singleton())
    ClassDB::bind_static_method("GearMCPAPI", D_METHOD("get_singleton"),
                                &GearMCPAPI::get_singleton);

    // Status
    ClassDB::bind_method(D_METHOD("is_running"), &GearMCPAPI::is_running);
    ClassDB::bind_method(D_METHOD("get_port"), &GearMCPAPI::get_port);
    ClassDB::bind_method(D_METHOD("get_connected_clients"), &GearMCPAPI::get_connected_clients);
    ClassDB::bind_method(D_METHOD("get_total_tools"), &GearMCPAPI::get_total_tools);
    ClassDB::bind_method(D_METHOD("get_enabled_tools"), &GearMCPAPI::get_enabled_tools);
    ClassDB::bind_method(D_METHOD("get_endpoint"), &GearMCPAPI::get_endpoint);
    ClassDB::bind_method(D_METHOD("get_total_call_count"), &GearMCPAPI::get_total_call_count);

    // Tools
    ClassDB::bind_method(D_METHOD("get_tools"), &GearMCPAPI::get_tools);
    ClassDB::bind_method(D_METHOD("set_tool_enabled", "name", "enabled"),
                         &GearMCPAPI::set_tool_enabled);
    ClassDB::bind_method(D_METHOD("is_tool_enabled", "name"), &GearMCPAPI::is_tool_enabled);
    ClassDB::bind_method(D_METHOD("enable_all_tools"), &GearMCPAPI::enable_all_tools);
    ClassDB::bind_method(D_METHOD("disable_all_tools"), &GearMCPAPI::disable_all_tools);

    // Logs
    ClassDB::bind_method(D_METHOD("get_log_entries", "max_count"),
                         &GearMCPAPI::get_log_entries);
    ClassDB::bind_method(D_METHOD("get_log_count"), &GearMCPAPI::get_log_count);
    ClassDB::bind_method(D_METHOD("clear_logs"), &GearMCPAPI::clear_logs);

    // Internal emitters (used by the C++ server threads to push signals to
    // the GDScript panel without round-tripping through MCPMethods).
    ClassDB::bind_method(D_METHOD("_emit_log_appended", "timestamp_ms", "level",
                                  "method", "tool", "status", "duration_ms", "message"),
                         &GearMCPAPI::emit_log_appended);
    ClassDB::bind_method(D_METHOD("_emit_tool_state_changed", "name", "enabled"),
                         &GearMCPAPI::emit_tool_state_changed);
    ClassDB::bind_method(D_METHOD("_emit_client_count_changed", "count"),
                         &GearMCPAPI::emit_client_count_changed);

    // Signals
    ADD_SIGNAL(MethodInfo("log_appended",
                          PropertyInfo(Variant::INT, "timestamp_ms"),
                          PropertyInfo(Variant::STRING, "level"),
                          PropertyInfo(Variant::STRING, "method"),
                          PropertyInfo(Variant::STRING, "tool"),
                          PropertyInfo(Variant::STRING, "status"),
                          PropertyInfo(Variant::INT, "duration_ms"),
                          PropertyInfo(Variant::STRING, "message")));
    ADD_SIGNAL(MethodInfo("tool_state_changed",
                          PropertyInfo(Variant::STRING, "name"),
                          PropertyInfo(Variant::BOOL, "enabled")));
    ADD_SIGNAL(MethodInfo("client_count_changed",
                          PropertyInfo(Variant::INT, "count")));
}

// ---------------------------------------------------------------------------
// Status accessors
// ---------------------------------------------------------------------------

bool GearMCPAPI::is_running() const {
    return m_server && m_server->is_running();
}

int GearMCPAPI::get_port() const {
    if (!m_server) return 0;
    return m_server->get_port();
}

int GearMCPAPI::get_connected_clients() const {
    if (!m_server) return 0;
    return m_server->get_connected_clients();
}

int GearMCPAPI::get_total_tools() const {
    if (!m_server) return 0;
    return (int)m_server->get_tool_registry()->tool_count();
}

int GearMCPAPI::get_enabled_tools() const {
    if (!m_server) return 0;
    return (int)m_server->get_tool_registry()->enabled_count();
}

godot::String GearMCPAPI::get_endpoint() const {
    if (!m_server) return godot::String();
    return godot::String("127.0.0.1:") + godot::String::num_int64(m_server->get_port());
}

int64_t GearMCPAPI::get_total_call_count() const {
    if (!m_server) return 0;
    return m_server->get_tool_registry()->total_call_count();
}

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

godot::Array GearMCPAPI::get_tools() const {
    godot::Array out;
    if (!m_server) return out;
    std::vector<ToolInfo> tools;
    m_server->get_tool_registry()->list_tools(tools);
    out.resize((int)tools.size());
    for (const auto &t : tools) {
        godot::Dictionary d;
        d["name"] = godot::String(t.name.c_str());
        d["description"] = godot::String(t.description.c_str());
        // Derive domain from "domain/action" naming convention.
        godot::String full(t.name.c_str());
        int slash = full.find("/");
        d["domain"] = (slash < 0) ? full : full.substr(0, slash);
        d["enabled"] = m_server->get_tool_registry()->is_tool_enabled(t.name);
        d["main_thread"] = t.main_thread;
        d["call_count"] = (int64_t)m_server->get_tool_registry()->get_call_count(t.name);
        out.push_back(d);
    }
    return out;
}

bool GearMCPAPI::set_tool_enabled(const godot::String &p_name, bool p_enabled) {
    if (!m_server) return false;
    std::string n(p_name.utf8().get_data());
    bool ok = m_server->get_tool_registry()->set_tool_enabled(n, p_enabled);
    if (ok) {
        emit_tool_state_changed(p_name, p_enabled);
    }
    return ok;
}

bool GearMCPAPI::is_tool_enabled(const godot::String &p_name) const {
    if (!m_server) return false;
    std::string n(p_name.utf8().get_data());
    return m_server->get_tool_registry()->is_tool_enabled(n);
}

int GearMCPAPI::enable_all_tools() {
    if (!m_server) return 0;
    int n = (int)m_server->get_tool_registry()->enable_all_tools();
    // Fire a single summary signal by re-emitting the tools list shape.
    // The panel will re-read on its next polling tick anyway.
    return n;
}

int GearMCPAPI::disable_all_tools() {
    if (!m_server) return 0;
    return (int)m_server->get_tool_registry()->disable_all_tools();
}

// ---------------------------------------------------------------------------
// Logs
// ---------------------------------------------------------------------------

godot::Array GearMCPAPI::get_log_entries(int p_max_count) const {
    godot::Array out;
    if (!m_server) return out;
    std::vector<LogEntry> entries;
    m_server->get_methods()->get_recent_logs(entries, (size_t)(p_max_count > 0 ? p_max_count : 200));
    int n = (int)entries.size();
    out.resize(n);
    for (const auto &e : entries) {
        godot::Dictionary d;
        d["timestamp_ms"] = (int64_t)e.timestamp_ms;
        d["level"] = godot::String(e.level.c_str());
        d["method"] = godot::String(e.method.c_str());
        d["tool"] = godot::String(e.tool.c_str());
        d["status"] = godot::String(e.status.c_str());
        d["duration_ms"] = (int64_t)e.duration_ms;
        d["message"] = godot::String(e.message.c_str());
        out.push_back(d);
    }
    return out;
}

int GearMCPAPI::get_log_count() const {
    if (!m_server) return 0;
    return (int)m_server->get_methods()->log_count();
}

void GearMCPAPI::clear_logs() {
    if (!m_server) return;
    m_server->get_methods()->clear_logs();
}

// ---------------------------------------------------------------------------
// Signal emitters (called from C++ server threads; signal dispatch is queued
// onto the main thread by godot-cpp automatically).
// ---------------------------------------------------------------------------

void GearMCPAPI::emit_log_appended(int64_t p_timestamp_ms, const godot::String &p_level,
                                   const godot::String &p_method, const godot::String &p_tool,
                                   const godot::String &p_status, int p_duration_ms,
                                   const godot::String &p_message) {
    emit_signal("log_appended", p_timestamp_ms, p_level, p_method, p_tool, p_status,
                p_duration_ms, p_message);
}

void GearMCPAPI::emit_tool_state_changed(const godot::String &p_name, bool p_enabled) {
    emit_signal("tool_state_changed", p_name, p_enabled);
}

void GearMCPAPI::emit_client_count_changed(int p_count) {
    emit_signal("client_count_changed", p_count);
}

} // namespace gear_mcp
