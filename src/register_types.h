#pragma once

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

void initialize_gear_mcp_module(godot::ModuleInitializationLevel p_level);
void uninitialize_gear_mcp_module(godot::ModuleInitializationLevel p_level);

extern "C" {
    GDExtensionBool GDE_EXPORT gear_mcp_server_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization *r_initialization);
}
