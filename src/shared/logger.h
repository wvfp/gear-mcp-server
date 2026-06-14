#pragma once

#include <cstdio>

// ---------------------------------------------------------------------------
// Simple logging macros for the Gear MCP server.
// Usage: GEAR_LOG("message %s", args...);
// ---------------------------------------------------------------------------

#define GEAR_LOG(fmt, ...) \
    std::printf("[Gear MCP] " fmt "\n", ##__VA_ARGS__)

#define GEAR_WARN(fmt, ...) \
    std::fprintf(stderr, "[Gear MCP WARN] " fmt "\n", ##__VA_ARGS__)

#define GEAR_ERR(fmt, ...) \
    std::fprintf(stderr, "[Gear MCP ERR] " fmt "\n", ##__VA_ARGS__)
