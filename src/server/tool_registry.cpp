#include "tool_registry.h"

#include <cstdio>

namespace gear_mcp {

void ToolRegistry::register_tool(const char *p_name, const char *p_description,
                                  const char *p_input_schema, ToolHandler p_handler,
                                  bool p_main_thread) {
    std::lock_guard<std::mutex> lock(m_mutex);

    Entry entry;
    entry.info.name = p_name ? p_name : "";
    entry.info.description = p_description ? p_description : "";
    entry.info.input_schema = p_input_schema ? p_input_schema : "{}";
    entry.info.main_thread = p_main_thread;
    entry.handler = p_handler;

    m_tools[entry.info.name] = entry;
}

bool ToolRegistry::get_tool(const char *p_name, ToolInfo &r_info, ToolHandler &r_handler) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_tools.find(p_name ? p_name : "");
    if (it == m_tools.end()) return false;

    r_info = it->second.info;
    r_handler = it->second.handler;
    return true;
}

void ToolRegistry::list_tools(std::vector<ToolInfo> &r_tools) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    r_tools.clear();
    r_tools.reserve(m_tools.size());
    for (const auto &pair : m_tools) {
        r_tools.push_back(pair.second.info);
    }
}

size_t ToolRegistry::tool_count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tools.size();
}

size_t ToolRegistry::enabled_count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tools.size() - m_disabled.size();
}

bool ToolRegistry::set_tool_enabled(const std::string &p_name, bool p_enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_tools.find(p_name) == m_tools.end()) return false;
    if (p_enabled) {
        m_disabled.erase(p_name);
    } else {
        m_disabled.insert(p_name);
    }
    return true;
}

bool ToolRegistry::is_tool_enabled(const std::string &p_name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_disabled.find(p_name) == m_disabled.end();
}

size_t ToolRegistry::enable_all_tools() {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t n = m_disabled.size();
    m_disabled.clear();
    return n;
}

size_t ToolRegistry::disable_all_tools() {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t n = 0;
    for (const auto &pair : m_tools) {
        if (m_disabled.insert(pair.first).second) ++n;
    }
    return n;
}

void ToolRegistry::increment_call_count(const std::string &p_name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_call_counts[p_name]; // operator[] inserts 0 if missing
}

int64_t ToolRegistry::total_call_count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    int64_t sum = 0;
    for (const auto &pair : m_call_counts) sum += pair.second;
    return sum;
}

int64_t ToolRegistry::get_call_count(const std::string &p_name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_call_counts.find(p_name);
    return it == m_call_counts.end() ? 0 : it->second;
}

} // namespace gear_mcp
