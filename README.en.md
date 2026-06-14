<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/logo-dark.svg">
    <img alt="Gear MCP Server" src="assets/logo-light.svg" width="520">
  </picture>
</p>

<p align="center">
  <a href="README.md">中文</a> · <strong>English</strong>
</p>

<p align="center">
  <strong>Embed an MCP server directly into the Godot editor process.</strong><br>
  109 tools · 5 resources · 2 prompts — for Claude Desktop, Cursor, Cline, OpenCode and any MCP client.
</p>

<p align="center">
  <a href="#build"><img alt="CMake" src="https://img.shields.io/badge/build-cmake-064F8C?logo=cmake&logoColor=white"></a>
  <a href="#build"><img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B&logoColor=white"></a>
  <a href="#build"><img alt="Godot 4.4+" src="https://img.shields.io/badge/Godot-4.4%2B-478CBF?logo=godotengine&logoColor=white"></a>
  <a href="LICENSE"><img alt="License MIT" src="https://img.shields.io/badge/license-MIT-green"></a>
</p>

---

## What is this

**Gear MCP Server** is a C++ GDExtension that runs an MCP server **inside the Godot editor process** itself. No separate daemon, no socket juggling — load the addon, open the editor, and your AI client can read, write, and refactor the project through 109 well-typed tools.

The repo ships two artifacts:

| Artifact                  | Type                  | Path                              |
| ------------------------- | --------------------- | --------------------------------- |
| `gear-mcp-server`         | GDExtension dynamic library | `project/addons/gear_mcp/bin/` |
| `gear-mcp-proxy`          | npm package           | `proxy/`                          |

## Architecture

<p align="center">
  <img alt="Architecture" src="assets/architecture.svg" width="760">
</p>

- The GDExtension opens a `TCPServer` on `127.0.0.1:8510` (overridable via `GEAR_PORT`).
- Each client connection is handled on its own thread; JSON-RPC 2.0 messages are `\n`-delimited (no `Content-Length` header).
- Tool handlers are registered in a thread-safe `ToolRegistry`. Handlers that touch Godot objects are marked `main_thread=true` and dispatched through `MainThreadQueue` into the editor's `_process` loop.
- `gear-mcp-proxy` is a ~50-line Node.js shim that bridges an MCP client's stdio JSON-RPC to the TCP socket. Run it as `npx -y gear-mcp-proxy` (local path: `cd proxy && node index.js`).

## Quick start

### 1. Build the GDExtension

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

The output goes to `project/addons/gear_mcp/bin/`. Filename format:
`libgear_mcp_server.{platform}.{variant}.{arch}.{ext}` — e.g.
`libgear_mcp_server.windows.template_debug.x86_64.dll` on Windows.

`GODOTCPP_TARGET` (default `template_debug`) controls the suffix. The `.gdextension` file
already lists the six variants (`debug`/`release` × `windows`/`linux`/`macos`).

### 2. Open the project in Godot 4.4+

Open the `project/` directory. The GDExtension auto-loads and starts the TCP server.

### 3. Run the proxy and call a tool

```powershell
# Terminal 1
cd proxy && node index.js

# Terminal 2
echo '{"jsonrpc":"2.0","method":"tools/list","id":1}' | node proxy/index.js
```

The proxy retries for up to 60 s waiting for the editor to come up.
Override with `GEAR_HOST` / `GEAR_PORT`.

### 4. Wire it into your MCP client

```json
{
  "mcpServers": {
    "gear": {
      "command": "npx",
      "args": ["-y", "gear-mcp-proxy"]
    }
  }
}
```

## Tools (109 across 26 domains)

<p align="center">
  <img alt="Tool domains" src="assets/tools.svg" width="760">
</p>

### Files & project (16 tools)

| Domain     | Count | Highlights                                                   |
| ---------- | :---: | ------------------------------------------------------------ |
| `file`     |   3   | read, write, list                                            |
| `project`  |   5   | settings, autoloads (read), project info, version            |
| `classdb`  |   3   | class & property & method introspection                      |
| `import`   |   5   | reimport, list, settings, presets                            |

### Scene & world (18 tools)

| Domain       | Count | Highlights                                                   |
| ------------ | :---: | ------------------------------------------------------------ |
| `scene`      |  10   | open, save, instancing, runtime attach, plus 7 advanced (nodes, properties, signals, groups) |
| `runtime`    |   4   | spawn scene, set var, get var, await signal                  |
| `tilemap`    |   2   | set / get cell                                                |
| `navigation` |   2   | map regions, agent path                                      |

### Code & debug (13 tools)

| Domain   | Count | Highlights                                                   |
| -------- | :---: | ------------------------------------------------------------ |
| `script` |   3   | read, write, attach                                          |
| `debug`  |  10   | LSP: diagnostics, hover, completion, definition · DAP: breakpoint, step, continue, variables, stack, evaluate |

### Editor & plugin (19 tools)

| Domain     | Count | Highlights                                                   |
| ---------- | :---: | ------------------------------------------------------------ |
| `editor`   |   6   | selection, undo/redo, screenshots, play, stop, reload         |
| `autoload` |   4   | add, remove, list, register                                  |
| `plugin`   |   3   | enable, disable, list                                        |
| `signal`   |   3   | list, connect, emit                                          |
| `theme`    |   3   | create, apply, list                                          |

### Media (14 tools)

| Domain      | Count | Highlights                                                   |
| ----------- | :---: | ------------------------------------------------------------ |
| `resource`  |   5   | load, save, list, inspect, create                            |
| `audio`     |   4   | bus, stream player, generate tone, listener                  |
| `animation` |   5   | create track, add key, set value, play, stop                 |

### Input & identity (3 tools)

| Domain  | Count | Highlights      |
| ------- | :---: | --------------- |
| `input` |   1   | action map      |
| `uid`   |   2   | generate, resolve |

### Build & export (2 tools)

| Domain   | Count | Highlights                                          |
| -------- | :---: | --------------------------------------------------- |
| `export` |   2   | presets, run (cross-platform process spawn)        |

### AI workflow (24 tools)

| Domain    | Count | Highlights                                                   |
| --------- | :---: | ------------------------------------------------------------ |
| `intent`  |   9   | snapshots, decision log, work steps, traces, recording toggle, summarize, handoff |
| `meta`    |   2   | tool catalog, schema lookup                                  |
| `assets`  |   3   | CC0 downloads: Poly Haven, AmbientCG, Kenney                |
| `testing` |   6   | inject input, capture screenshot, headless run, log assertion |
| `dx`      |   4   | project health, lint, file index, symbol index              |

## Resources & Prompts

### Resources (5)

| URI                                | Description                              |
| ---------------------------------- | ---------------------------------------- |
| `godot://editor/context`           | current selection, edited scene, FPS     |
| `godot://project/info`             | project name, version, autoloads, settings digest |
| `godot://scene/{path}`             | live scene tree                          |
| `godot://script/{path}`            | script text + parse diagnostics          |
| `godot://resource/{path}`          | resource text dump (.tres / .res metadata) |

Resource URIs use template prefixes — the server resolves `godot://script/res://player.gd` to
the matching handler.

### Prompts (2)

- `gear:save-intent-snapshot` — generate a structured intent snapshot
- `gear:summarize-session` — summarize the current editor session from intent state

## Project layout

```
gear-mcp-server/
├── CMakeLists.txt              # top-level build (godot-cpp, nlohmann/json, cpp-httplib, inih via FetchContent)
├── project/                    # Godot project (auto-loads the GDExtension)
│   └── addons/gear_mcp/
│       ├── gear_mcp.gdextension
│       └── plugin.cfg
├── proxy/                      # Node.js stdio↔TCP bridge
│   ├── index.js
│   └── package.json
├── src/
│   ├── register_types.cpp      # GDExtension entry point (editor-mode only)
│   ├── gear_main_thread_node.* # EditorProcessFrame bridge
│   ├── godot_api/              # 30+ GDExtension C API function pointers
│   ├── server/                 # TCPServer, MCPMethods, ToolRegistry, MainThreadQueue
│   ├── shared/                 # type_codec, path_utils, config_parser, json_rpc_client, logger
│   └── tools/                  # 26 domains, one folder each
└── tests/                      # Node.js E2E suites (e2e_test, e2e_live_editor, e2e_phase4)
```

## Threading model

- `main_thread=true` handlers run inside the editor's `_process` via `MainThreadQueue::invoke_on_main`,
  blocking the TCP thread until the editor frame completes them.
- `main_thread=false` handlers (file I/O, INI parsing, asset downloads) execute directly on the TCP thread.
- The intent state file is rewritten atomically on every mutation; access is protected by a recursive mutex.

## Dependencies

Pulled automatically through CMake `FetchContent` — no manual install.

| Library        | Version | Purpose                                    |
| -------------- | ------- | ------------------------------------------ |
| `godot-cpp`    | git submodule | GDExtension C++ bindings             |
| `nlohmann/json`| 3.11.3  | JSON parse / serialize (the only JSON lib) |
| `cpp-httplib`  | 0.18.7  | HTTP client for CC0 asset downloads       |
| `inih`         | r58     | `project.godot` INI parser                 |
| `ws2_32`       | —       | TCP socket on Windows                      |

## License

MIT — see [LICENSE](LICENSE).
