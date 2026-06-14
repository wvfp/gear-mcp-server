cmake_minimum_required(VERSION 3.17)

project(gear-mcp-server
    VERSION 0.1.0
    DESCRIPTION "Gear MCP Server - C++ GDExtension for Godot Editor"
    LANGUAGES C CXX
)

# ============================================================================
# godot-cpp Configuration
# ============================================================================
set(GODOTCPP_TARGET "template_debug" CACHE STRING "godot-cpp build target")
add_subdirectory(godot-cpp)

# ============================================================================
# Third-Party Dependencies (auto-fetched via FetchContent)
# ============================================================================
include(FetchContent)

# nlohmann/json — JSON parsing & serialization
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# cpp-httplib — HTTP/HTTPS client (for future asset downloads)
FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.7
    GIT_SHALLOW    TRUE
)

# inih — INI file parser (for project.godot, export_presets.cfg)
FetchContent_Declare(
    inih
    GIT_REPOSITORY https://github.com/benhoyt/inih.git
    GIT_TAG        r58
    GIT_SHALLOW    TRUE
)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(HTTPLIB_COMPILE OFF CACHE INTERNAL "")

FetchContent_MakeAvailable(nlohmann_json cpp_httplib)

# inih has no CMakeLists.txt — manually download and create target
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(inih)
if(NOT inih_POPULATED)
    FetchContent_Populate(inih)
endif()

add_library(inih STATIC
    ${inih_SOURCE_DIR}/ini.c
    ${inih_SOURCE_DIR}/cpp/INIReader.cpp
)
target_include_directories(inih PUBLIC
    ${inih_SOURCE_DIR}
    ${inih_SOURCE_DIR}/cpp
)
# Override INI_API to avoid __declspec(dllimport) when building as static lib
target_compile_definitions(inih PRIVATE INI_API=)

# ============================================================================
# Extension Library
# ============================================================================
set(TARGET_NAME "gear-mcp-server")

add_library(${TARGET_NAME} SHARED)

# All source files for the GDExtension
target_sources(${TARGET_NAME} PRIVATE
    # Entry point
    src/register_types.cpp
    src/register_types.h

    # Main-thread dispatch node (Godot C++ Node subclass)
    src/gear_main_thread_node.h
    src/gear_main_thread_node.cpp

    # Server infrastructure
    src/server/tcp_server.h
    src/server/tcp_server.cpp
    src/server/gear_mcp_api.h
    src/server/gear_mcp_api.cpp
    src/server/tool_registry.h
    src/server/tool_registry.cpp
    src/server/main_thread_queue.h
    src/server/main_thread_queue.cpp
    src/server/mcp_server.h
    src/server/mcp_server.cpp
    src/server/mcp_methods.h
    src/server/mcp_methods.cpp
    src/server/phase4_resources.h
    src/server/phase4_resources.cpp

    # Godot API layer
    src/godot_api/godot_api.h
    src/godot_api/godot_api.cpp
    src/godot_api/editor_context.h
    src/godot_api/editor_context.cpp

    # Shared utilities
    src/shared/type_codec.h
    src/shared/type_codec.cpp
    src/shared/path_utils.h
    src/shared/path_utils.cpp
    src/shared/config_parser.h
    src/shared/config_parser.cpp
    src/shared/json_rpc_client.h
    src/shared/json_rpc_client.cpp
    src/shared/logger.h

    # Tool implementations
    src/tools/register_all.h
    src/tools/register_all.cpp
    src/tools/file/file_tools.cpp
    src/tools/scene/scene_tools.cpp
    src/tools/scene/scene_tools_phase2.cpp
    src/tools/script/script_tools.cpp
    src/tools/classdb/classdb_tools.cpp
    src/tools/project/project_tools.cpp
    src/tools/editor/editor_tools.cpp
    src/tools/resource/resource_tools.cpp
    src/tools/export/export_tools.cpp
    src/tools/signal/signal_tools.cpp
    src/tools/autoload/autoload_tools.cpp
    # Phase 3 tool domains
    src/tools/animation/animation_tools.cpp
    src/tools/audio/audio_tools.cpp
    src/tools/tilemap/tilemap_tools.cpp
    src/tools/navigation/navigation_tools.cpp
    src/tools/theme/theme_tools.cpp
    src/tools/plugin/plugin_tools.cpp
    src/tools/input/input_tools.cpp
    src/tools/uid/uid_tools.cpp
    src/tools/import/import_tools.cpp
    src/tools/runtime/runtime_tools.cpp
    # Phase 4 tool domains
    src/tools/intent/intent_tools.cpp
    src/tools/meta/meta_tools.cpp
    src/tools/assets/assets_tools.cpp
    src/tools/debug/debug_tools.cpp
    src/tools/testing/testing_tools.cpp
    src/tools/dx/dx_tools.cpp
)

# Source root is an include directory so we can use #include "server/xxx.h" etc.
target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Link all dependencies
target_link_libraries(${TARGET_NAME} PRIVATE
    godot-cpp
    nlohmann_json::nlohmann_json
    httplib::httplib
    inih
)

# Winsock2 for TCP server on Windows
if(WIN32)
    target_link_libraries(${TARGET_NAME} PRIVATE ws2_32)
endif()

# ============================================================================
# Target Properties
# ============================================================================
get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)

set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/project/addons/gear_mcp/bin")

set_target_properties(
    ${TARGET_NAME}
    PROPERTIES
        CXX_STANDARD 17
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET ${GODOTCPP_SYMBOL_VISIBILITY}

        POSITION_INDEPENDENT_CODE ON
        BUILD_RPATH_USE_ORIGIN ON

        LINK_SEARCH_START_STATIC ON
        LINK_SEARCH_END_STATIC ON

        LIBRARY_OUTPUT_DIRECTORY "$<1:${OUTPUT_DIR}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${OUTPUT_DIR}>"
        PDB_OUTPUT_DIRECTORY     "$<1:${OUTPUT_DIR}>"

        PREFIX "lib"
        OUTPUT_NAME "gear_mcp_server${GODOTCPP_SUFFIX}"

        FOLDER "gear-mcp-server"
)

# macOS framework handling
if(CMAKE_SYSTEM_NAME STREQUAL Darwin)
    set_target_properties(
        ${TARGET_NAME}
        PROPERTIES
            SUFFIX ""
            OUTPUT_DIR "${OUTPUT_DIR}/libgear_mcp_server.macos.${GODOTCPP_TARGET}.framework"
    )
endif()
