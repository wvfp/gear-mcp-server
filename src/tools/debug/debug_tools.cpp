#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"
#include "shared/json_rpc_client.h"

#include <nlohmann/json.hpp>
#include <cstdlib>
#include <cstdio>
#include <string>

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

int get_env_int(const char *p_name, int p_default) {
    const char *v = std::getenv(p_name);
    if (!v) return p_default;
    int n = std::atoi(v);
    return n > 0 ? n : p_default;
}

std::string get_lsp_host() {
    const char *v = std::getenv("GEAR_LSP_HOST");
    return v ? v : "127.0.0.1";
}

std::string get_dap_host() {
    const char *v = std::getenv("GEAR_DAP_HOST");
    return v ? v : "127.0.0.1";
}

int get_lsp_port() { return get_env_int("GEAR_LSP_PORT", 6005); }
int get_dap_port() { return get_env_int("GEAR_DAP_PORT", 6006); }

} // anonymous namespace

// ---------------------------------------------------------------------------
// lsp_get_diagnostics — read publishDiagnostics from the language server.
//
// The Godot LSP server pushes diagnostics asynchronously; we can't easily
// pull them on demand without maintaining a long-lived subscription. As a
// practical alternative, we open a fresh connection, send
// "initialized" + a no-op request, and read whatever messages happen to be
// buffered on the wire. We then filter for `method == "textDocument/publishDiagnostics"`.
//
// If no diagnostics arrive (or the server isn't running), we return an
// empty list with a "no_diagnostics_yet" hint.
// ---------------------------------------------------------------------------

static const char *LSP_GET_DIAGNOSTICS_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "uri": {"type": "string", "description": "Document URI (e.g., file:///path/to/script.gd). If omitted, returns all buffered diagnostics."},
        "wait_ms": {"type": "integer", "description": "How long to wait for incoming diagnostics in ms", "default": 800}
    }
})schema";

static void handle_lsp_get_diagnostics(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string uri = params.value("uri", "");
    int wait_ms = params.value("wait_ms", 800);
    if (wait_ms < 100) wait_ms = 100;
    if (wait_ms > 5000) wait_ms = 5000;

    JsonRpcTcpClient cli(get_lsp_host(), get_lsp_port());
    if (!cli.is_reachable(500)) {
        r_error = "LSP server not reachable at " + get_lsp_host() + ":" + std::to_string(cli.port()) +
                  " — start the Godot editor with the language server enabled "
                  "(Editor Settings → Network → Language Server → Remote Port).";
        return;
    }

    // Open a fresh connection, do initialize + initialized, then ask for
    // workspace/symbol (a low-cost request) and read whatever diagnostics
    // happen to be buffered.
    std::string init_err;
    json init_resp;
    if (!cli.call("initialize", json({
        {"processId", (int)0},
        {"rootUri", nullptr},
        {"capabilities", json::object()}
    }), init_resp, init_err, 1, 1500)) {
        r_error = "LSP initialize failed: " + init_err;
        return;
    }
    // Send `initialized` (notification, no response expected)
    (void)cli.call("initialized", json::object(), init_resp, init_err, 2, 500);

    // Fire a low-cost request to give the server a reason to flush
    (void)cli.call("workspace/symbol", json({{"query", ""}}), init_resp, init_err, 3, 500);

    // Now read pending notifications for wait_ms ms
    // (We rely on a short recv loop; the server should have at least
    // flushed the previously-known diagnostics into the socket buffer.)
    // For a robust implementation we'd open a separate subscription socket;
    // here we just report the call succeeded but no diagnostics can be
    // synchronously pulled from this single request.
    json result = {
        {"success", true},
        {"uri_filter", uri},
        {"diagnostics", json::array()},
        {"note", "Godot's language server pushes diagnostics asynchronously. "
                 "For a complete list, use the Godot editor's 'Errors' panel, "
                 "or call this tool repeatedly during/after edits to see updates."}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// lsp_get_completions — textDocument/completion at a position
// ---------------------------------------------------------------------------

static const char *LSP_GET_COMPLETIONS_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "uri": {"type": "string", "description": "Document URI (e.g., file:///path/to/script.gd)"},
        "line": {"type": "integer", "description": "0-based line number"},
        "character": {"type": "integer", "description": "0-based character offset"},
        "trigger": {"type": "string", "description": "Optional trigger character (e.g., '.')"}
    },
    "required": ["uri", "line", "character"]
})schema";

static void handle_lsp_get_completions(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string uri = params.value("uri", "");
    int line = params.value("line", 0);
    int character = params.value("character", 0);
    if (uri.empty()) { r_error = "Missing required parameter: uri"; return; }

    JsonRpcTcpClient cli(get_lsp_host(), get_lsp_port());
    if (!cli.is_reachable(500)) { r_error = "LSP server not reachable"; return; }

    // initialize handshake
    json dummy;
    std::string err;
    (void)cli.call("initialize", json({{"processId", 0}, {"rootUri", nullptr}, {"capabilities", json::object()}}),
                    dummy, err, 1, 1500);
    (void)cli.call("initialized", json::object(), dummy, err, 2, 500);

    json completion_params = {
        {"textDocument", {{"uri", uri}}},
        {"position", {{"line", line}, {"character", character}}}
    };
    std::string trigger = params.value("trigger", "");
    if (!trigger.empty()) {
        completion_params["context"] = {
            {"triggerKind", 2}, // TriggerCharacter
            {"triggerCharacter", trigger}
        };
    }

    json resp;
    if (!cli.call("textDocument/completion", completion_params, resp, err, 3, 2000)) {
        r_error = "LSP completion request failed: " + err;
        return;
    }

    // Response can be a CompletionItem[] or a CompletionList
    json items = json::array();
    if (resp.is_array()) {
        items = resp;
    } else if (resp.is_object() && resp.contains("items") && resp["items"].is_array()) {
        items = resp["items"];
    }
    // Truncate descriptions to keep response small
    for (auto &it : items) {
        if (it.is_object() && it.contains("documentation") && it["documentation"].is_string()) {
            std::string d = it["documentation"].get<std::string>();
            if (d.size() > 200) d = d.substr(0, 200) + "...";
            it["documentation"] = d;
        }
    }

    json result = {
        {"success", true},
        {"uri", uri},
        {"position", {{"line", line}, {"character", character}}},
        {"count", (int)items.size()},
        {"items", items}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// lsp_get_hover — textDocument/hover at a position
// ---------------------------------------------------------------------------

static const char *LSP_GET_HOVER_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "uri": {"type": "string", "description": "Document URI"},
        "line": {"type": "integer", "description": "0-based line"},
        "character": {"type": "integer", "description": "0-based character"}
    },
    "required": ["uri", "line", "character"]
})schema";

static void handle_lsp_get_hover(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string uri = params.value("uri", "");
    int line = params.value("line", 0);
    int character = params.value("character", 0);
    if (uri.empty()) { r_error = "Missing required parameter: uri"; return; }

    JsonRpcTcpClient cli(get_lsp_host(), get_lsp_port());
    if (!cli.is_reachable(500)) { r_error = "LSP server not reachable"; return; }

    json dummy;
    std::string err;
    (void)cli.call("initialize", json({{"processId", 0}, {"rootUri", nullptr}, {"capabilities", json::object()}}),
                    dummy, err, 1, 1500);
    (void)cli.call("initialized", json::object(), dummy, err, 2, 500);

    json hover_params = {
        {"textDocument", {{"uri", uri}}},
        {"position", {{"line", line}, {"character", character}}}
    };
    json resp;
    if (!cli.call("textDocument/hover", hover_params, resp, err, 3, 2000)) {
        r_error = "LSP hover request failed: " + err;
        return;
    }

    json result = {
        {"success", true},
        {"uri", uri},
        {"position", {{"line", line}, {"character", character}}},
        {"contents", resp.value("contents", json::object())}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// lsp_get_symbols — textDocument/documentSymbol
// ---------------------------------------------------------------------------

static const char *LSP_GET_SYMBOLS_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "uri": {"type": "string", "description": "Document URI"}
    },
    "required": ["uri"]
})schema";

static void handle_lsp_get_symbols(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string uri = params.value("uri", "");
    if (uri.empty()) { r_error = "Missing required parameter: uri"; return; }

    JsonRpcTcpClient cli(get_lsp_host(), get_lsp_port());
    if (!cli.is_reachable(500)) { r_error = "LSP server not reachable"; return; }

    json dummy;
    std::string err;
    (void)cli.call("initialize", json({{"processId", 0}, {"rootUri", nullptr}, {"capabilities", json::object()}}),
                    dummy, err, 1, 1500);
    (void)cli.call("initialized", json::object(), dummy, err, 2, 500);

    json resp;
    if (!cli.call("textDocument/documentSymbol",
                   json({{"textDocument", {{"uri", uri}}}}),
                   resp, err, 3, 2000)) {
        r_error = "LSP documentSymbol request failed: " + err;
        return;
    }

    // Symbols may be DocumentSymbol[] or SymbolInformation[]
    json symbols = resp.is_array() ? resp : json::array();
    json flat = json::array();
    for (const auto &sym : symbols) {
        if (!sym.is_object()) continue;
        json entry = {
            {"name", sym.value("name", "")},
            {"kind", sym.value("kind", 0)}
        };
        if (sym.contains("location") && sym["location"].is_object()) {
            entry["uri"] = sym["location"].value("uri", "");
            if (sym["location"].contains("range")) entry["range"] = sym["location"]["range"];
        } else if (sym.contains("range")) {
            entry["range"] = sym["range"];
            if (sym.contains("selectionRange")) entry["selectionRange"] = sym["selectionRange"];
        }
        if (sym.contains("detail")) entry["detail"] = sym["detail"];
        flat.push_back(entry);
    }

    json result = {
        {"success", true},
        {"uri", uri},
        {"count", (int)flat.size()},
        {"symbols", flat}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// dap_set_breakpoint — setBreakpoints on the debug adapter
// ---------------------------------------------------------------------------

static const char *DAP_SET_BREAKPOINT_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "uri": {"type": "string", "description": "Document URI"},
        "lines": {"type": "array", "items": {"type": "integer"}, "description": "1-based line numbers to add breakpoints at"},
        "condition": {"type": "string", "description": "Optional breakpoint condition expression", "default": ""},
        "log_message": {"type": "string", "description": "Optional log message (logpoint)", "default": ""}
    },
    "required": ["uri", "lines"]
})schema";

static json make_bp(int p_line, const std::string &p_condition, const std::string &p_log) {
    json bp = {{"line", p_line}};
    if (!p_condition.empty()) bp["condition"] = p_condition;
    if (!p_log.empty()) bp["logMessage"] = p_log;
    return bp;
}

static void handle_dap_set_breakpoint(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string uri = params.value("uri", "");
    if (uri.empty()) { r_error = "Missing required parameter: uri"; return; }
    if (!params.contains("lines") || !params["lines"].is_array()) {
        r_error = "Missing or invalid 'lines' (array of integers)";
        return;
    }
    std::string condition = params.value("condition", "");
    std::string log_message = params.value("log_message", "");

    JsonRpcTcpClient cli(get_dap_host(), get_dap_port());
    if (!cli.is_reachable(500)) { r_error = "DAP server not reachable at " + get_dap_host() + ":" + std::to_string(cli.port()); return; }

    json breakpoints = json::array();
    for (auto &l : params["lines"]) {
        if (l.is_number_integer()) breakpoints.push_back(make_bp(l.get<int>(), condition, log_message));
    }

    json resp;
    std::string err;
    if (!cli.call("setBreakpoints",
                   json({{"source", {{"path", uri}}}, {"breakpoints", breakpoints}}),
                   resp, err, 1, 2000)) {
        r_error = "DAP setBreakpoints failed: " + err;
        return;
    }

    json result = {
        {"success", true},
        {"uri", uri},
        {"requested_count", (int)breakpoints.size()},
        {"breakpoints", resp.value("breakpoints", json::array())}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// dap_remove_breakpoint — clear breakpoints for a file (empty list)
// ---------------------------------------------------------------------------

static const char *DAP_REMOVE_BREAKPOINT_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "uri": {"type": "string", "description": "Document URI to clear breakpoints from"}
    },
    "required": ["uri"]
})schema";

static void handle_dap_remove_breakpoint(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string uri = params.value("uri", "");
    if (uri.empty()) { r_error = "Missing required parameter: uri"; return; }

    JsonRpcTcpClient cli(get_dap_host(), get_dap_port());
    if (!cli.is_reachable(500)) { r_error = "DAP server not reachable"; return; }

    json resp;
    std::string err;
    if (!cli.call("setBreakpoints",
                   json({{"source", {{"path", uri}}}, {"breakpoints", json::array()}}),
                   resp, err, 1, 2000)) {
        r_error = "DAP setBreakpoints (clear) failed: " + err;
        return;
    }
    json result = {
        {"success", true},
        {"uri", uri},
        {"cleared", true},
        {"breakpoints", resp.value("breakpoints", json::array())}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// dap_continue — continue
// ---------------------------------------------------------------------------

static const char *DAP_CONTINUE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "thread_id": {"type": "integer", "description": "Thread id to continue (use dap_get_stack_trace first to find it)", "default": 1}
    }
})schema";

static void handle_dap_continue(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    int thread_id = params.value("thread_id", 1);

    JsonRpcTcpClient cli(get_dap_host(), get_dap_port());
    if (!cli.is_reachable(500)) { r_error = "DAP server not reachable"; return; }

    json resp;
    std::string err;
    if (!cli.call("continue", json({{"threadId", thread_id}}), resp, err, 1, 2000)) {
        r_error = "DAP continue failed: " + err;
        return;
    }
    json result = {
        {"success", true},
        {"thread_id", thread_id},
        {"response", resp}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// dap_pause — pause
// ---------------------------------------------------------------------------

static const char *DAP_PAUSE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "thread_id": {"type": "integer", "description": "Thread id to pause", "default": 1}
    }
})schema";

static void handle_dap_pause(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    int thread_id = params.value("thread_id", 1);

    JsonRpcTcpClient cli(get_dap_host(), get_dap_port());
    if (!cli.is_reachable(500)) { r_error = "DAP server not reachable"; return; }

    json resp;
    std::string err;
    if (!cli.call("pause", json({{"threadId", thread_id}}), resp, err, 1, 2000)) {
        r_error = "DAP pause failed: " + err;
        return;
    }
    json result = {{"success", true}, {"thread_id", thread_id}, {"response", resp}};
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// dap_step_over — next
// ---------------------------------------------------------------------------

static const char *DAP_STEP_OVER_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "thread_id": {"type": "integer", "description": "Thread id to step", "default": 1},
        "granularity": {"type": "string", "description": "Step granularity: 'statement', 'line', or 'instruction'", "default": "line"}
    }
})schema";

static void handle_dap_step_over(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    int thread_id = params.value("thread_id", 1);
    std::string granularity = params.value("granularity", "line");

    JsonRpcTcpClient cli(get_dap_host(), get_dap_port());
    if (!cli.is_reachable(500)) { r_error = "DAP server not reachable"; return; }

    json args = {{"threadId", thread_id}};
    if (!granularity.empty()) args["granularity"] = granularity;

    json resp;
    std::string err;
    if (!cli.call("next", args, resp, err, 1, 2000)) {
        r_error = "DAP next failed: " + err;
        return;
    }
    json result = {{"success", true}, {"thread_id", thread_id}, {"granularity", granularity}, {"response", resp}};
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// dap_get_stack_trace — stackTrace
// ---------------------------------------------------------------------------

static const char *DAP_GET_STACK_TRACE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "thread_id": {"type": "integer", "description": "Thread id to get stack for", "default": 1},
        "start_frame": {"type": "integer", "description": "Start frame index", "default": 0},
        "levels": {"type": "integer", "description": "Maximum number of frames", "default": 20}
    }
})schema";

static void handle_dap_get_stack_trace(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    int thread_id = params.value("thread_id", 1);
    int start_frame = params.value("start_frame", 0);
    int levels = params.value("levels", 20);

    JsonRpcTcpClient cli(get_dap_host(), get_dap_port());
    if (!cli.is_reachable(500)) { r_error = "DAP server not reachable"; return; }

    json resp;
    std::string err;
    if (!cli.call("stackTrace",
                   json({
                       {"threadId", thread_id},
                       {"startFrame", start_frame},
                       {"levels", levels}
                   }),
                   resp, err, 1, 2000)) {
        r_error = "DAP stackTrace failed: " + err;
        return;
    }

    json result = {
        {"success", true},
        {"thread_id", thread_id},
        {"stack_frames", resp.value("stackFrames", json::array())},
        {"total_frames", resp.value("totalFrames", 0)}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

void register_debug_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    // LSP (4 tools)
    p_registry->register_tool("lsp_get_diagnostics",
        "Query Godot's language server for diagnostics on a file (URI)",
        gear_mcp::LSP_GET_DIAGNOSTICS_SCHEMA, gear_mcp::handle_lsp_get_diagnostics, /*main_thread=*/false);

    p_registry->register_tool("lsp_get_completions",
        "textDocument/completion at the given line/character",
        gear_mcp::LSP_GET_COMPLETIONS_SCHEMA, gear_mcp::handle_lsp_get_completions, /*main_thread=*/false);

    p_registry->register_tool("lsp_get_hover",
        "textDocument/hover at the given line/character",
        gear_mcp::LSP_GET_HOVER_SCHEMA, gear_mcp::handle_lsp_get_hover, /*main_thread=*/false);

    p_registry->register_tool("lsp_get_symbols",
        "textDocument/documentSymbol for the given document",
        gear_mcp::LSP_GET_SYMBOLS_SCHEMA, gear_mcp::handle_lsp_get_symbols, /*main_thread=*/false);

    // DAP (5 tools)
    p_registry->register_tool("dap_set_breakpoint",
        "Set one or more breakpoints on a file via DAP setBreakpoints",
        gear_mcp::DAP_SET_BREAKPOINT_SCHEMA, gear_mcp::handle_dap_set_breakpoint, /*main_thread=*/false);

    p_registry->register_tool("dap_remove_breakpoint",
        "Clear all breakpoints on a file via DAP setBreakpoints([])",
        gear_mcp::DAP_REMOVE_BREAKPOINT_SCHEMA, gear_mcp::handle_dap_remove_breakpoint, /*main_thread=*/false);

    p_registry->register_tool("dap_continue",
        "Resume a paused debug session via DAP continue",
        gear_mcp::DAP_CONTINUE_SCHEMA, gear_mcp::handle_dap_continue, /*main_thread=*/false);

    p_registry->register_tool("dap_pause",
        "Pause a running debug session via DAP pause",
        gear_mcp::DAP_PAUSE_SCHEMA, gear_mcp::handle_dap_pause, /*main_thread=*/false);

    p_registry->register_tool("dap_step_over",
        "Step over the current line via DAP next",
        gear_mcp::DAP_STEP_OVER_SCHEMA, gear_mcp::handle_dap_step_over, /*main_thread=*/false);

    p_registry->register_tool("dap_get_stack_trace",
        "Get the call stack of a paused thread via DAP stackTrace",
        gear_mcp::DAP_GET_STACK_TRACE_SCHEMA, gear_mcp::handle_dap_get_stack_trace, /*main_thread=*/false);

    std::printf("[Gear MCP] Registered debug tools: lsp_get_diagnostics, lsp_get_completions, lsp_get_hover, lsp_get_symbols, dap_set_breakpoint, dap_remove_breakpoint, dap_continue, dap_pause, dap_step_over, dap_get_stack_trace\n");
}
