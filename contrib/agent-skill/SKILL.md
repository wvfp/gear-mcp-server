---
name: "gear-mcp-godot"
description: "Drive a Godot project through the gear-mcp-server MCP tools (write_file, create_scene, add_node, attach_script_to_node, attach_screenshot_helper, run_scene_with_timeout, plus ~110 more). Invoke when the user opens the gear-mcp-server project, mentions the in-editor MCP, or asks to build, debug, or visually test a Godot scene/script via MCP."
---

# gear-mcp-godot

End-to-end guide for working with the Godot project under
`g:\Godot\godot-mcp\gear-mcp-server\project\`. The MCP server runs
inside the Godot editor process (a GDExtension). A 50-line Node.js
proxy at `proxy/index.js` bridges stdio ↔ TCP `127.0.0.1:8510`. End
users of the npm package just run the editor; AI agents in this IDE
talk to the editor directly.

## Project layout

```
gear-mcp-server/
├── src/                      ← C++ GDExtension source
│   ├── server/                ← TCP server (port 8510)
│   ├── godot_api/             ← Wrappers for EditorInterface / ProjectSettings
│   └── tools/
│       ├── editor/            ← run_scene_with_timeout, screenshot, restart_editor
│       ├── scene/             ← create_scene, add_node, attach_script, ...
│       ├── script/            ← write_file (gd), parse helpers
│       ├── project/           ← ProjectSettings getters/setters
│       ├── resource/          ← load_resource, save_resource
│       └── config/            ← set_project_setting, get_project_setting
├── project/                   ← The Godot project being controlled
│   ├── project.godot
│   ├── scenes/                ← .tscn files
│   ├── scripts/               ← .gd files
│   └── addons/gear_mcp/       ← The plugin (panel UI, helper scripts)
└── proxy/
    ├── index.js               ← npm package entry (50 lines, the stdio↔TCP proxy)
    ├── package.json
    ├── README.md
    └── dev/
        └── send.js            ← One-shot test request sender (NOT in npm)
```

## How to talk to the editor

The Godot editor (when started on the project directory) listens on
`127.0.0.1:8510`. The MCP server is a GDExtension, so it's loaded at
editor startup. **You must restart the editor whenever the DLL is
rebuilt** — `cmake --build` does not hot-reload GDExtensions.

A request looks like:

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"<tool>","arguments":{...}},"id":1}
```

For dev/testing, save it to a file and send it with `proxy/dev/send.js`:

```bash
node proxy/dev/send.js path/to/req.json
```

For production, the stdio↔TCP proxy at `proxy/index.js` is what
Claude/Cursor/etc. invoke. Don't touch it.

## Standard workflow: create a runnable scene with a script and screenshot

```bash
# 1. Write the script
node proxy/dev/send.js <<EOF
{"jsonrpc":"2.0","method":"tools/call",
 "params":{"name":"write_file",
           "arguments":{"path":"G:/Godot/godot-mcp/gear-mcp-server/project/scripts/my_game.gd",
                        "content":"extends Node2D\nfunc _ready(): ..."}},
 "id":1}
EOF

# 2. Create the scene
# 3. Add a Timer or other child nodes
# 4. Attach the script to the root
# 5. Add a ScreenshotHelper child node
# 6. Attach the helper script to that child
# 7. Run + capture
```

In practice, save each request to its own .json file and chain them
with `&&` in PowerShell. The .json files are throwaway — don't
commit them.

## Screenshot pipeline (the tricky part)

**You cannot capture a play-window screenshot from C++**. The editor's
3D viewport / OS root / SceneTree root all return blank images for
play scenes because the play SubWindow is a separate Viewport tree.

**Solution**: a GDScript helper runs *inside* the play process and
calls `get_viewport().get_texture().get_image().save_png(...)`.

**The helper is part of the plugin** at
`project/addons/gear_mcp/screenshot_helper.gd`. The
`attach_screenshot_helper` MCP tool does three things in one call:

1. Adds the helper to a scene as an ext_resource
2. Attaches it to the specified node (defaults to root, but you
   usually want a child node — see pitfall #1)
3. Writes the `screenshot_path` and `wait_frames` exports into the
   .tscn block

**The flow** for `run_scene_with_timeout`:

1. C++ writes a sidecar file `res://mcp_screenshot_target.txt` with
   the requested output path
2. C++ calls `EditorInterface.play_custom_scene`
3. C++ polls `is_playing_scene` (6s budget)
4. The play scene starts; helper script's `_ready` runs:
   - reads the sidecar, overrides `screenshot_path` to the requested path
   - waits `wait_frames` frames (default 60)
   - calls `save_png(screenshot_path)`
5. C++ file watcher detects the PNG, stops the play scene, returns
   `{captured: true, file_size: N, capture_source: "helper_script"}`

## Tool reference (essentials)

Discover the full list with `tools/list` (~110 tools). Categories:

| Tool | Purpose |
|---|---|
| `write_file` | Write a file (path is absolute; use `G:/Godot/.../project/...`) |
| `read_file` | Read a file by absolute path |
| `create_scene` | New .tscn with a root node (`scenePath`, `rootNodeType`, `rootName`) |
| `add_node` | Add child node to an existing .tscn (`scenePath`, `parentPath`, `nodeName`, `nodeType`) |
| `attach_script_to_node` | Bind a .gd to a node in a .tscn (`scenePath`, `nodePath`, `scriptPath`) |
| `attach_screenshot_helper` | Convenience: attach helper + set export vars (see above) |
| `set_project_setting` | Modify `project.godot`; **be aware**: it wrote unquoted strings in v1 — the version in this repo auto-quotes |
| `run_scene_with_timeout` | Play a scene, capture, stop. Returns `{captured, file_size, capture_source, poll_diag, ...}` |
| `is_playing` | Lightweight check |
| `restart_editor` | Calls `EditorInterface.restart_editor` (current impl kills the editor without auto-restarting) |
| `launch_editor` | Open a scene in the editor's tab |
| `tools/list` | List all tools |
| `get_debug_output` | Editor log (often null) |

## Common pitfalls

### 1. Helper must go on a CHILD node, not the root
If your root already has a script (e.g. `click_catch.gd`), you can't
attach the helper to the same node — `attach_script_to_node` refuses
to overwrite. Always:

```bash
add_node        nodeName=ScreenshotHelper nodeType=Node2D
attach_screenshot_helper nodePath=ScreenshotHelper
```

### 2. `is_playing_scene` can return cached `true`
When the play subprocess fails to launch (bad driver, missing
import), the editor may report `is_playing_scene: true` briefly and
then silently go false. The C++ poll in this repo waits 6s and
records `poll_diag` (e.g. `[0ms]=true;[100ms]=true;...`). If a run
fails, **always read `poll_diag`**.

### 3. ext_resource insert shifts byte offsets
If you write code that walks a .tscn and inserts ext_resources, then
later searches for `[node ...]` lines, your captured positions are
now wrong by `ext_resource_line.size()`. This was an actual bug in
`attach_script_to_node` that was fixed in the version in this repo.

### 4. MSBuild / CMake caching
CMake's "Checking File Globs" step can decide a .cpp is up-to-date
when it isn't. If your edit doesn't seem to take effect, **delete the
.obj** and rebuild:

```bash
rm build/gear-mcp-server.dir/Debug/scene_tools.obj
cmake --build build --config Debug --target gear-mcp-server
```

### 5. Render driver breaks play launch
`rendering_device/driver.windows="d3d12"` will abort the play scene
on machines that only have a software/opengl display driver. The
`set_project_setting` tool was being used to set this to `vulkan` —
works on this hardware, YMMV. If `run_scene_with_timeout` returns
`capture_failed` with a `save_png_error` and no `helper_script`
source, suspect the driver.

### 6. The editor process may be owned by a different user
On Windows, if the editor was launched from a different user
context, the AI agent cannot `Stop-Process` or `taskkill /F` it
("Access Denied"). **Ask the user to restart Godot manually** if
the editor is wedged. Don't waste turns trying to kill it.

### 7. Pretty-printed JSON gets split by the TCP server
The Godot TCP server splits messages on `\n`. A multi-line
pretty-printed request becomes N broken messages. `proxy/dev/send.js`
strips all internal newlines before sending. If you write your own
sender, do the same — or upgrade the server to use Content-Length
(the server in this repo supports both, but `\n`-delimited is still
the default).

## Verifying that something actually worked

Always combine the MCP call with **two side-channel checks**:

1. **`Get-Item` on the resulting file** — file exists, byte count > 0
2. **Read the helper's `screenshot_helper_diag.txt`** — proves
   `_ready` actually ran, captured frame index, final screenshot
   path. If this file doesn't exist, the play scene never started,
   period.

```powershell
Get-Content "g:/Godot/.../project/screenshot_helper_diag.txt" -ErrorAction SilentlyContinue
```

PNG file size tells you whether the capture is real:
- ~3000-4000 bytes = blank editor viewport (NOT a play capture)
- 8000+ bytes = actual game content

## When the editor is wedged / MCP is down

```powershell
# Check if anything is listening
netstat -ano | findstr :8510

# Get the editor PID
Get-Process -Name "Godot_v4.7*"

# Try to kill (may fail with Access Denied if launched by another user)
Get-Process -Name "Godot_v4.7*" | Stop-Process -Force

# Start fresh
Start-Process -FilePath "G:\Godot\Godot_v4.7-dev3_win64_console.exe" `
  -ArgumentList "--editor","--path","g:\Godot\godot-mcp\gear-mcp-server\project" `
  -RedirectStandardOutput "g:\Godot\godot-mcp\gear-mcp-server\project\editor_out.log" `
  -RedirectStandardError "g:\Godot\godot-mcp\gear-mcp-server\project\editor_err.log" `
  -WorkingDirectory "g:\Godot\godot-mcp\gear-mcp-server\project"
```

Wait 5-6 seconds for MCP to come up before sending requests.

## Quick test recipe

```bash
# Ping
echo '{"jsonrpc":"2.0","method":"tools/list","id":1}' > req.json
node proxy/dev/send.js req.json
```

## What NOT to do

- **Don't write files directly with the file system tools** when the
  goal is to test the MCP. The user wants to drive the project
  *through* the MCP, not bypass it.
- **Don't read the .gd.uid files**. Godot generates them, you
  shouldn't.
- **Don't trust `get_debug_output`** for capturing play subprocess
  output — it usually returns `null`. The helper's diag file is
  the reliable channel.
- **Don't kill the editor from the agent if the user is watching it
  in the IDE** — they'll be confused. Always ask first.
