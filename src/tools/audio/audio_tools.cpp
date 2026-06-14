#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/path_utils.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace gear_mcp {

// ---------------------------------------------------------------------------
// Helper: get the AudioServer singleton
// ---------------------------------------------------------------------------

static GDExtensionObjectPtr get_audio_server() {
    return GodotAPI::get_global_singleton("AudioServer");
}

// ---------------------------------------------------------------------------
// create_audio_bus — add a new audio bus
// ---------------------------------------------------------------------------

static const char *CREATE_AUDIO_BUS_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "name": {"type": "string", "description": "Name for the new bus"},
        "bus_index": {"type": "integer", "description": "Position to insert the bus at (-1 = append)", "default": -1}
    },
    "required": ["name"]
})schema";

static void handle_create_audio_bus(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    std::string name = params.value("name", "");
    if (name.empty()) { r_error = "Missing required parameter: name"; return; }

    int bus_index = params.value("bus_index", -1);

    GDExtensionObjectPtr audio_server = get_audio_server();
    if (!audio_server) { r_error = "AudioServer singleton not available"; return; }

    int new_idx = -1;
    if (bus_index < 0) {
        // bus_count -> new index
        new_idx = GodotAPI::call_method_int(audio_server, "get_bus_count");
    } else {
        new_idx = bus_index;
    }

    {
        uint8_t vbuf[64] = {0};
        GodotAPI::make_variant_int((GDExtensionUninitializedVariantPtr)vbuf, new_idx);
        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)vbuf };
        GodotAPI::call_method_void(audio_server, "add_bus", args, 1);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)vbuf);
    }

    {
        uint8_t ibuf[64] = {0}, nbuf[64] = {0};
        GodotAPI::make_variant_int((GDExtensionUninitializedVariantPtr)ibuf, new_idx);
        GodotAPI::make_variant_string((GDExtensionUninitializedVariantPtr)nbuf, name);
        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)ibuf, (GDExtensionConstVariantPtr)nbuf };
        GodotAPI::call_method_void(audio_server, "set_bus_name", args, 2);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)ibuf);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)nbuf);
    }

    json result = {
        {"success", true},
        {"name", name},
        {"bus_index", new_idx}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// get_audio_buses — list all audio buses
// ---------------------------------------------------------------------------

static const char *GET_AUDIO_BUSES_SCHEMA = R"schema({
    "type": "object",
    "properties": {}
})schema";

static void handle_get_audio_buses(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    (void)p_params_json;
    GDExtensionObjectPtr audio_server = get_audio_server();
    if (!audio_server) { r_error = "AudioServer singleton not available"; return; }

    int bus_count = GodotAPI::call_method_int(audio_server, "get_bus_count");

    json buses = json::array();
    for (int i = 0; i < bus_count; i++) {
        uint8_t ibuf[64] = {0};
        GodotAPI::make_variant_int((GDExtensionUninitializedVariantPtr)ibuf, i);
        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)ibuf };
        std::string bus_name = GodotAPI::call_method_with_args(audio_server, "get_bus_name", args, 1);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)ibuf);

        // Get volume in dB
        uint8_t ibuf2[64] = {0};
        GodotAPI::make_variant_int((GDExtensionUninitializedVariantPtr)ibuf2, i);
        GDExtensionConstVariantPtr args2[] = { (GDExtensionConstVariantPtr)ibuf2 };
        double volume = GodotAPI::call_method_float_with_args(audio_server, "get_bus_volume_db", args2, 1);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)ibuf2);

        // Get mute state
        uint8_t ibuf3[64] = {0};
        GodotAPI::make_variant_int((GDExtensionUninitializedVariantPtr)ibuf3, i);
        GDExtensionConstVariantPtr args3[] = { (GDExtensionConstVariantPtr)ibuf3 };
        bool muted = GodotAPI::call_method_bool_with_args(audio_server, "is_bus_mute", args3, 1);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)ibuf3);

        buses.push_back({
            {"index", i},
            {"name", bus_name},
            {"volume_db", volume},
            {"muted", muted}
        });
    }

    json result = {
        {"success", true},
        {"bus_count", bus_count},
        {"buses", buses}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// set_audio_bus_effect — add/replace an audio effect on a bus
// ---------------------------------------------------------------------------

static const char *SET_AUDIO_BUS_EFFECT_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "bus_index": {"type": "integer", "description": "Index of the bus"},
        "effect_index": {"type": "integer", "description": "Slot for the effect (-1 = append)", "default": -1},
        "effect_type": {"type": "string", "description": "Class name of the effect (e.g., AudioEffectReverb, AudioEffectDistortion)"}
    },
    "required": ["bus_index", "effect_type"]
})schema";

static void handle_set_audio_bus_effect(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    int bus_index = params.value("bus_index", -1);
    int effect_index = params.value("effect_index", -1);
    std::string effect_type = params.value("effect_type", "");

    if (bus_index < 0) { r_error = "Missing or invalid 'bus_index'"; return; }
    if (effect_type.empty()) { r_error = "Missing required parameter: effect_type"; return; }

    GDExtensionObjectPtr audio_server = get_audio_server();
    if (!audio_server) { r_error = "AudioServer singleton not available"; return; }

    // Create the effect instance
    GDExtensionObjectPtr effect = GodotAPI::create_node(effect_type);
    if (!effect) { r_error = "Failed to create effect of type: " + effect_type + " (ClassDB may not exist)"; return; }

    if (effect_index < 0) {
        // Determine current effect count
        uint8_t ibuf[64] = {0};
        GodotAPI::make_variant_int((GDExtensionUninitializedVariantPtr)ibuf, bus_index);
        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)ibuf };
        effect_index = GodotAPI::call_method_int_with_args(audio_server, "get_bus_effect_count", args, 1);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)ibuf);
    }

    // add_bus_effect(bus_idx, effect, at_pos=-1)
    {
        uint8_t bibuf[64] = {0}, ebuf[64] = {0}, pibuf[64] = {0};
        GodotAPI::make_variant_int((GDExtensionUninitializedVariantPtr)bibuf, bus_index);
        GodotAPI::make_variant_object((GDExtensionUninitializedVariantPtr)ebuf, effect);
        GodotAPI::make_variant_int((GDExtensionUninitializedVariantPtr)pibuf, effect_index);
        GDExtensionConstVariantPtr args[] = {
            (GDExtensionConstVariantPtr)bibuf,
            (GDExtensionConstVariantPtr)ebuf,
            (GDExtensionConstVariantPtr)pibuf
        };
        GodotAPI::call_method_void(audio_server, "add_bus_effect", args, 3);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)bibuf);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)ebuf);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)pibuf);
    }

    json result = {
        {"success", true},
        {"bus_index", bus_index},
        {"effect_index", effect_index},
        {"effect_type", effect_type}
    };
    r_result = result.dump();
}

// ---------------------------------------------------------------------------
// set_audio_bus_volume — adjust bus volume (in dB)
// ---------------------------------------------------------------------------

static const char *SET_AUDIO_BUS_VOLUME_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "bus_index": {"type": "integer", "description": "Index of the bus"},
        "volume_db": {"type": "number", "description": "Volume in dB (0 = unity gain)"},
        "muted": {"type": "boolean", "description": "Mute the bus", "default": false}
    },
    "required": ["bus_index", "volume_db"]
})schema";

static void handle_set_audio_bus_volume(const std::string &p_params_json, std::string &r_result, std::string &r_error) {
    json params = json::parse(p_params_json, nullptr, false);
    if (params.is_discarded()) { r_error = "Invalid JSON parameters."; return; }

    int bus_index = params.value("bus_index", -1);
    if (bus_index < 0) { r_error = "Missing or invalid 'bus_index'"; return; }

    double volume_db = params.value("volume_db", 0.0);
    bool muted = params.value("muted", false);

    GDExtensionObjectPtr audio_server = get_audio_server();
    if (!audio_server) { r_error = "AudioServer singleton not available"; return; }

    {
        uint8_t ibuf[64] = {0}, vbuf[64] = {0};
        GodotAPI::make_variant_int((GDExtensionUninitializedVariantPtr)ibuf, bus_index);
        GodotAPI::make_variant_float((GDExtensionUninitializedVariantPtr)vbuf, volume_db);
        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)ibuf, (GDExtensionConstVariantPtr)vbuf };
        GodotAPI::call_method_void(audio_server, "set_bus_volume_db", args, 2);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)ibuf);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)vbuf);
    }

    {
        uint8_t ibuf[64] = {0}, mbuf[64] = {0};
        GodotAPI::make_variant_int((GDExtensionUninitializedVariantPtr)ibuf, bus_index);
        GodotAPI::make_variant_bool((GDExtensionUninitializedVariantPtr)mbuf, muted);
        GDExtensionConstVariantPtr args[] = { (GDExtensionConstVariantPtr)ibuf, (GDExtensionConstVariantPtr)mbuf };
        GodotAPI::call_method_void(audio_server, "set_bus_mute", args, 2);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)ibuf);
        GodotAPI::destroy_variant((GDExtensionVariantPtr)mbuf);
    }

    json result = {
        {"success", true},
        {"bus_index", bus_index},
        {"volume_db", volume_db},
        {"muted", muted}
    };
    r_result = result.dump();
}

} // namespace gear_mcp

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_audio_tools(gear_mcp::ToolRegistry *p_registry) {
    if (!p_registry) return;

    p_registry->register_tool("create_audio_bus",
        "Add a new audio bus with the given name",
        gear_mcp::CREATE_AUDIO_BUS_SCHEMA, gear_mcp::handle_create_audio_bus, /*main_thread=*/true);

    p_registry->register_tool("get_audio_buses",
        "List all audio buses with name, volume, and mute state",
        gear_mcp::GET_AUDIO_BUSES_SCHEMA, gear_mcp::handle_get_audio_buses, /*main_thread=*/true);

    p_registry->register_tool("set_audio_bus_effect",
        "Add an audio effect to a bus (e.g., AudioEffectReverb)",
        gear_mcp::SET_AUDIO_BUS_EFFECT_SCHEMA, gear_mcp::handle_set_audio_bus_effect, /*main_thread=*/true);

    p_registry->register_tool("set_audio_bus_volume",
        "Set the volume (dB) and mute state of an audio bus",
        gear_mcp::SET_AUDIO_BUS_VOLUME_SCHEMA, gear_mcp::handle_set_audio_bus_volume, /*main_thread=*/true);

    std::printf("[Gear MCP] Registered audio tools: create_audio_bus, get_audio_buses, set_audio_bus_effect, set_audio_bus_volume\n");
}
