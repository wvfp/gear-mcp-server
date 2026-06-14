#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Intent state — pure data, no Godot API required.
//
// All artifacts are stored in a single JSON file: `<project>/.gear_mcp/intent_state.json`.
// The file contains:
//   - snapshots:   array of intent snapshots
//   - decisions:   array of decision-log entries
//   - work_steps:  array of work-step entries
//   - traces:      array of execution-trace entries
//   - recording:   bool (on/off) — when off, all append-ops become no-ops
//
// A process-local mutex protects the in-memory state; the file is rewritten
// atomically on every mutation.
// ---------------------------------------------------------------------------

namespace fs = std::filesystem;

namespace {

// Recursive mutex because some handlers (summarize, handoff) need to read
// while another thread may be writing.
std::mutex g_state_mutex;
json g_state = json::object({
    {"snapshots", json::array()},
    {"decisions", json::array()},
    {"work_steps", json::array()},
    {"traces", json::array()},
    {"recording", true},
});
bool g_loaded = false;
std::string g_state_path; // resolved lazily on first access

std::string iso_now() {
    auto t = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count() % 1000;
    std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    std::ostringstream os;
    os << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S") << '.'
       << std::setw(3) << std::setfill('0') << ms << 'Z';
    return os.str();
}

std::string resolve_state_path() {
    if (!g_state_path.empty()) return g_state_path;
    std::string project = GodotAPI::get_project_path();
    if (project.empty()) {
        // Fall back to current working directory
        std::error_code ec;
        project = fs::current_path(ec).string();
    }
    std::string dir = project + "/.gear_mcp";
    std::error_code ec;
    fs::create_directories(dir, ec);
    g_state_path = dir + "/intent_state.json";
    return g_state_path;
}

void load_state() {
    if (g_loaded) return;
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (g_loaded) return;
    std::string path = resolve_state_path();
    std::ifstream f(path);
    if (f.is_open()) {
        try {
            json loaded = json::parse(f);
            for (auto &el : loaded.items()) {
                if (g_state.contains(el.key())) {
                    g_state[el.key()] = el.value();
                }
            }
        } catch (...) {
            // Corrupt file — start fresh (don't overwrite until next write)
        }
        f.close();
    }
    g_loaded = true;
}

void persist_state() {
    std::string path = resolve_state_path();
    std::string tmp = path + ".tmp";
    std::ofstream f(tmp, std::ios::trunc | std::ios::binary);
    if (!f.is_open()) return;
    f << g_state.dump(2);
    f.close();
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        // Best-effort fallback: copy then unlink
        std::ifstream src(tmp, std::ios::binary);
        std::ofstream dst(path, std::ios::trunc | std::ios::binary);
        dst << src.rdbuf();
        src.close();
        dst.close();
        fs::remove(tmp, ec);
    }
}

bool is_recording() {
    return g_state.value("recording", true);
}

void append_array(const std::string &p_key, const json &p_entry) {
    g_state[p_key].push_back(p_entry);
}

// Trim array to last N entries to keep file size bounded
void trim_array(const std::string &p_key, size_t p_max) {
    auto &arr = g_state[p_key];
    if (arr.size() > p_max) {
        arr.erase(arr.begin(), arr.begin() + (arr.size() - p_max));
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// capture_intent_snapshot — save a named snapshot of the current intent
// (typically called at the start of a session or major milestone).
// ---------------------------------------------------------------------------

static const char *CAPTURE_INTENT_SNAPSHOT_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "label": {"type": "string", "description": "Human-readable label for this snapshot"},
        "intent": {"type": "string", "description": "What the user/agent is trying to accomplish"},
        "context": {"type": "object", "description": "Free-form context payload (key/value)"}
    },
    "required": ["label", "intent"]
})schema";

static void handle_capture_intent_snapshot(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string label = params.value("label", "");
    std::string intent = params.value("intent", "");
    if (label.empty() || intent.empty()) {
        r_error = "Missing required parameters: label, intent";
        return;
    }
    json context = params.value("context", json::object());

    load_state();
    std::lock_guard<std::mutex> lock(g_state_mutex);

    json entry = {
        {"label", label},
        {"intent", intent},
        {"context", context},
        {"timestamp", iso_now()}
    };
    append_array("snapshots", entry);
    trim_array("snapshots", 200);
    persist_state();

    json result = {{"success", true}, {"entry", entry}, {"total_snapshots", g_state["snapshots"].size()}};
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// record_decision_log — record a discrete decision with rationale
// ---------------------------------------------------------------------------

static const char *RECORD_DECISION_LOG_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "decision": {"type": "string", "description": "What was decided"},
        "rationale": {"type": "string", "description": "Why this decision was made"},
        "alternatives": {"type": "array", "items": {"type": "string"}, "description": "Other options considered"},
        "tags": {"type": "array", "items": {"type": "string"}, "description": "Tags for retrieval"}
    },
    "required": ["decision", "rationale"]
})schema";

static void handle_record_decision_log(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string decision = params.value("decision", "");
    std::string rationale = params.value("rationale", "");
    if (decision.empty() || rationale.empty()) {
        r_error = "Missing required parameters: decision, rationale";
        return;
    }

    load_state();
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (!is_recording()) {
        r_result = json({{"success", true}, {"recorded", false}, {"reason", "recording off"}}).dump();
        return;
    }

    json entry = {
        {"decision", decision},
        {"rationale", rationale},
        {"alternatives", params.value("alternatives", json::array())},
        {"tags", params.value("tags", json::array())},
        {"timestamp", iso_now()}
    };
    append_array("decisions", entry);
    trim_array("decisions", 500);
    persist_state();

    json result = {{"success", true}, {"entry", entry}, {"total_decisions", g_state["decisions"].size()}};
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// record_work_step — record a discrete work step (action + outcome)
// ---------------------------------------------------------------------------

static const char *RECORD_WORK_STEP_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "action": {"type": "string", "description": "What action was taken (e.g., 'create_scene')"},
        "tool": {"type": "string", "description": "Tool name if applicable"},
        "args": {"type": "object", "description": "Tool arguments"},
        "outcome": {"type": "string", "description": "Result or status of the step"}
    },
    "required": ["action"]
})schema";

static void handle_record_work_step(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string action = params.value("action", "");
    if (action.empty()) { r_error = "Missing required parameter: action"; return; }

    load_state();
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (!is_recording()) {
        r_result = json({{"success", true}, {"recorded", false}, {"reason", "recording off"}}).dump();
        return;
    }

    json entry = {
        {"action", action},
        {"tool", params.value("tool", "")},
        {"args", params.value("args", json::object())},
        {"outcome", params.value("outcome", "")},
        {"timestamp", iso_now()}
    };
    append_array("work_steps", entry);
    trim_array("work_steps", 1000);
    persist_state();

    json result = {{"success", true}, {"entry", entry}, {"total_work_steps", g_state["work_steps"].size()}};
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// record_execution_trace — record a low-level execution trace event
// (typically called for performance-sensitive paths or errors).
// ---------------------------------------------------------------------------

static const char *RECORD_EXECUTION_TRACE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "event": {"type": "string", "description": "Trace event name"},
        "level": {"type": "string", "description": "Severity: info, warn, error", "enum": ["info", "warn", "error"], "default": "info"},
        "duration_ms": {"type": "number", "description": "Duration in milliseconds (optional)"},
        "details": {"type": "object", "description": "Free-form details"}
    },
    "required": ["event"]
})schema";

static void handle_record_execution_trace(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string event = params.value("event", "");
    if (event.empty()) { r_error = "Missing required parameter: event"; return; }
    std::string level = params.value("level", "info");

    load_state();
    std::lock_guard<std::mutex> lock(g_state_mutex);
    if (!is_recording()) {
        r_result = json({{"success", true}, {"recorded", false}, {"reason", "recording off"}}).dump();
        return;
    }

    json entry = {
        {"event", event},
        {"level", level},
        {"duration_ms", params.value("duration_ms", 0.0)},
        {"details", params.value("details", json::object())},
        {"timestamp", iso_now()}
    };
    append_array("traces", entry);
    // Keep traces larger — they're more granular
    trim_array("traces", 5000);
    persist_state();

    json result = {{"success", true}, {"entry", entry}, {"total_traces", g_state["traces"].size()}};
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// summarize_intent_context — return a compact summary of all collected data
// ---------------------------------------------------------------------------

static const char *SUMMARIZE_INTENT_CONTEXT_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "max_recent": {"type": "integer", "description": "Max number of recent entries per category to include", "default": 5}
    }
})schema";

static void handle_summarize_intent_context(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    int max_recent = params.value("max_recent", 5);
    if (max_recent < 1) max_recent = 5;
    if (max_recent > 100) max_recent = 100;

    load_state();
    std::lock_guard<std::mutex> lock(g_state_mutex);

    auto recent_tail = [&](const std::string &p_key) {
        auto &arr = g_state[p_key];
        if (arr.size() <= (size_t)max_recent) return arr;
        return json(arr.end() - max_recent, arr.end());
    };

    json result = {
        {"recording", g_state.value("recording", true)},
        {"counts", {
            {"snapshots", g_state["snapshots"].size()},
            {"decisions", g_state["decisions"].size()},
            {"work_steps", g_state["work_steps"].size()},
            {"traces", g_state["traces"].size()}
        }},
        {"recent_snapshots", recent_tail("snapshots")},
        {"recent_decisions", recent_tail("decisions")},
        {"recent_work_steps", recent_tail("work_steps")},
        {"recent_traces", recent_tail("traces")}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// generate_handoff_brief — build a markdown/text handoff brief from state
// ---------------------------------------------------------------------------

static const char *GENERATE_HANDOFF_BRIEF_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "title": {"type": "string", "description": "Title for the brief", "default": "Gear MCP Handoff Brief"},
        "max_recent": {"type": "integer", "description": "Max recent items per category", "default": 10}
    }
})schema";

static void handle_generate_handoff_brief(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string title = params.value("title", "Gear MCP Handoff Brief");
    int max_recent = params.value("max_recent", 10);
    if (max_recent < 1) max_recent = 10;
    if (max_recent > 200) max_recent = 200;

    load_state();
    std::lock_guard<std::mutex> lock(g_state_mutex);

    std::ostringstream brief;
    brief << "# " << title << "\n\n";
    brief << "Generated: " << iso_now() << "\n\n";

    auto write_section = [&](const char *p_heading, const std::string &p_key,
                              const std::string &p_item_label) {
        brief << "## " << p_heading << " (" << g_state[p_key].size() << ")\n\n";
        auto &arr = g_state[p_key];
        if (arr.empty()) {
            brief << "_(none)_\n\n";
            return;
        }
        size_t start = arr.size() > (size_t)max_recent ? arr.size() - max_recent : 0;
        for (size_t i = start; i < arr.size(); i++) {
            const auto &e = arr[i];
            brief << "- **" << p_item_label << "** ";
            if (e.contains("label")) brief << "[" << e.value("label", "") << "] ";
            if (e.contains("decision")) brief << e.value("decision", "");
            else if (e.contains("intent")) brief << e.value("intent", "");
            else if (e.contains("action")) brief << e.value("action", "");
            else if (e.contains("event")) brief << e.value("event", "");
            brief << "\n";
            if (e.contains("rationale")) brief << "  - Why: " << e.value("rationale", "") << "\n";
            if (e.contains("outcome")) {
                std::string out = e.value("outcome", "");
                if (!out.empty()) brief << "  - Outcome: " << out << "\n";
            }
            if (e.contains("timestamp")) brief << "  - At: " << e.value("timestamp", "") << "\n";
        }
        brief << "\n";
    };

    write_section("Snapshots", "snapshots", "snapshot");
    write_section("Decisions", "decisions", "decision");
    write_section("Work Steps", "work_steps", "step");
    write_section("Traces", "traces", "trace");

    json result = {
        {"success", true},
        {"brief", brief.str()},
        {"counts", {
            {"snapshots", g_state["snapshots"].size()},
            {"decisions", g_state["decisions"].size()},
            {"work_steps", g_state["work_steps"].size()},
            {"traces", g_state["traces"].size()}
        }}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// export_handoff_pack — write the current state (plus the brief) to a single
// exportable JSON file that can be shared.
// ---------------------------------------------------------------------------

static const char *EXPORT_HANDOFF_PACK_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "output_path": {"type": "string", "description": "Destination path for the handoff pack JSON"},
        "title": {"type": "string", "description": "Title for the embedded brief", "default": "Gear MCP Handoff Pack"},
        "max_recent": {"type": "integer", "description": "Max recent items per category in brief", "default": 20}
    },
    "required": ["output_path"]
})schema";

static void handle_export_handoff_pack(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    std::string output_path = params.value("output_path", "");
    if (output_path.empty()) { r_error = "Missing required parameter: output_path"; return; }
    if (path_utils::has_traversal(output_path)) {
        r_error = "Path traversal detected in output_path";
        return;
    }
    std::string title = params.value("title", "Gear MCP Handoff Pack");
    int max_recent = params.value("max_recent", 20);
    if (max_recent < 1) max_recent = 20;
    if (max_recent > 1000) max_recent = 1000;

    load_state();
    // We snapshot the state under the mutex, then build the brief outside it
    // to avoid holding the lock during file I/O.
    json snapshot;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        snapshot = g_state;
    }

    // Build brief from snapshot
    std::ostringstream brief;
    brief << "# " << title << "\n\nGenerated: " << iso_now() << "\n\n";

    auto write_section = [&](const char *p_heading, const std::string &p_key,
                              const std::string &p_item_label) {
        brief << "## " << p_heading << " (" << snapshot[p_key].size() << ")\n\n";
        auto &arr = snapshot[p_key];
        if (arr.empty()) { brief << "_(none)_\n\n"; return; }
        size_t start = arr.size() > (size_t)max_recent ? arr.size() - max_recent : 0;
        for (size_t i = start; i < arr.size(); i++) {
            const auto &e = arr[i];
            brief << "- **" << p_item_label << "** ";
            if (e.contains("label")) brief << "[" << e.value("label", "") << "] ";
            if (e.contains("decision")) brief << e.value("decision", "");
            else if (e.contains("intent")) brief << e.value("intent", "");
            else if (e.contains("action")) brief << e.value("action", "");
            else if (e.contains("event")) brief << e.value("event", "");
            brief << "\n";
            if (e.contains("rationale")) brief << "  - Why: " << e.value("rationale", "") << "\n";
            if (e.contains("outcome")) {
                std::string out = e.value("outcome", "");
                if (!out.empty()) brief << "  - Outcome: " << out << "\n";
            }
        }
        brief << "\n";
    };
    write_section("Snapshots", "snapshots", "snapshot");
    write_section("Decisions", "decisions", "decision");
    write_section("Work Steps", "work_steps", "step");
    write_section("Traces", "traces", "trace");

    path_utils::ensure_parent_dirs(output_path);
    json pack = {
        {"version", 1},
        {"generated_at", iso_now()},
        {"title", title},
        {"brief_markdown", brief.str()},
        {"state", snapshot}
    };
    std::ofstream f(output_path, std::ios::trunc | std::ios::binary);
    if (!f.is_open()) { r_error = "Cannot open output_path for writing: " + output_path; return; }
    f << pack.dump(2);
    f.close();

    json result = {
        {"success", true},
        {"output_path", output_path},
        {"bytes_written", (int)fs::file_size(output_path)},
        {"title", title}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// set_recording_mode — toggle whether append operations are recorded
// ---------------------------------------------------------------------------

static const char *SET_RECORDING_MODE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "enabled": {"type": "boolean", "description": "True to enable recording, false to pause"}
    },
    "required": ["enabled"]
})schema";

static void handle_set_recording_mode(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }
    if (!params.contains("enabled") || !params["enabled"].is_boolean()) {
        r_error = "Missing or invalid 'enabled' (boolean)";
        return;
    }
    bool enabled = params["enabled"].get<bool>();

    load_state();
    std::lock_guard<std::mutex> lock(g_state_mutex);
    g_state["recording"] = enabled;
    persist_state();

    json result = {{"success", true}, {"recording", enabled}};
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// get_recording_mode — return current recording state
// ---------------------------------------------------------------------------

static const char *GET_RECORDING_MODE_SCHEMA = R"schema({
    "type": "object",
    "properties": {}
})schema";

static void handle_get_recording_mode(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    (void)p_params_json;
    load_state();
    std::lock_guard<std::mutex> lock(g_state_mutex);

    json result = {
        {"recording", g_state.value("recording", true)},
        {"counts", {
            {"snapshots", g_state["snapshots"].size()},
            {"decisions", g_state["decisions"].size()},
            {"work_steps", g_state["work_steps"].size()},
            {"traces", g_state["traces"].size()}
        }},
        {"state_path", resolve_state_path()}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

void register_intent_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("capture_intent_snapshot",
        "Capture an intent snapshot (label + intent text + context)",
        gear_mcp::CAPTURE_INTENT_SNAPSHOT_SCHEMA, gear_mcp::handle_capture_intent_snapshot, /*main_thread=*/false);

    p_registry->register_tool("record_decision_log",
        "Append a decision entry with rationale and optional alternatives/tags",
        gear_mcp::RECORD_DECISION_LOG_SCHEMA, gear_mcp::handle_record_decision_log, /*main_thread=*/false);

    p_registry->register_tool("record_work_step",
        "Append a work-step entry (action, tool, args, outcome)",
        gear_mcp::RECORD_WORK_STEP_SCHEMA, gear_mcp::handle_record_work_step, /*main_thread=*/false);

    p_registry->register_tool("record_execution_trace",
        "Append a low-level execution-trace event (level, duration, details)",
        gear_mcp::RECORD_EXECUTION_TRACE_SCHEMA, gear_mcp::handle_record_execution_trace, /*main_thread=*/false);

    p_registry->register_tool("summarize_intent_context",
        "Return a JSON summary of recorded snapshots/decisions/steps/traces",
        gear_mcp::SUMMARIZE_INTENT_CONTEXT_SCHEMA, gear_mcp::handle_summarize_intent_context, /*main_thread=*/false);

    p_registry->register_tool("generate_handoff_brief",
        "Generate a markdown handoff brief summarizing recorded intent state",
        gear_mcp::GENERATE_HANDOFF_BRIEF_SCHEMA, gear_mcp::handle_generate_handoff_brief, /*main_thread=*/false);

    p_registry->register_tool("export_handoff_pack",
        "Export a full handoff pack (brief + state) to a JSON file",
        gear_mcp::EXPORT_HANDOFF_PACK_SCHEMA, gear_mcp::handle_export_handoff_pack, /*main_thread=*/false);

    p_registry->register_tool("set_recording_mode",
        "Enable or disable recording of new intent entries",
        gear_mcp::SET_RECORDING_MODE_SCHEMA, gear_mcp::handle_set_recording_mode, /*main_thread=*/false);

    p_registry->register_tool("get_recording_mode",
        "Return current recording mode and recorded entry counts",
        gear_mcp::GET_RECORDING_MODE_SCHEMA, gear_mcp::handle_get_recording_mode, /*main_thread=*/false);

    std::printf("[Gear MCP] Registered intent tools: capture_intent_snapshot, record_decision_log, record_work_step, record_execution_trace, summarize_intent_context, generate_handoff_brief, export_handoff_pack, set_recording_mode, get_recording_mode\n");
}
