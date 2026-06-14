#include "editor_context.h"
#include "godot_api.h"

namespace gear_mcp {

void EditorContext::resource_read_handler(const std::string &p_uri, std::string &r_text_out) {
    (void)p_uri;
    r_text_out = GodotAPI::get_context_snapshot();
}

} // namespace gear_mcp
