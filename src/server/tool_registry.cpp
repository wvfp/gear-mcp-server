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

} // namespace gear_mcp
