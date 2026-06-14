## Gear MCP — 纯 C++ 架构设计文档

---

### 一、设计目标

用 C++ GDExtension 重写 Gear MCP 服务器，嵌入 Godot 编辑器进程内部，消除 GDScript 插件层。对外通过一个极简 Node.js 代理（gear-mcp-proxy）提供 MCP stdio 传输，`npx -y gear-mcp-proxy` 即可启动，无需配置路径，兼容所有主流 AI 客户端。

不内嵌任何聊天 UI。不包含 ChatPanel、Markdown 渲染器、会话侧边栏等组件。Godot 侧只运行纯粹的 MCP 服务器。

**核心指标**：

- GDExtension 自动加载（不需要手动启用插件）
- `npx` 一行配置，任何项目通用（不需要每个项目配路径）
- MCP stdio 协议兼容（Claude Desktop、Cursor、Cline、OpenCode 均可连接）
- 110+ 工具全覆盖
- Godot 4.4+ 单一目标版本

---

### 二、整体架构

```
AI 客户端 (Claude Desktop / Cursor / Cline / OpenCode)
    │
    │  MCP over stdio (JSON-RPC 2.0)
    │  stdin/stdout 管道
    ▼
┌─────────────────────────────────────────┐
│  gear-mcp-proxy (Node.js, ~40 行)       │
│  npx -y gear-mcp-proxy                  │
│                                         │
│  • 从 stdin 读取 MCP JSON-RPC 消息      │
│  • 通过 TCP 转发到 GDExtension           │
│  • 将 TCP 响应写回 stdout                │
│  • npm 全局分发，任何项目通用            │
└──────────────┬──────────────────────────┘
               │  TCP 127.0.0.1:8510
               │  \n 分隔 JSON-RPC
               ▼
┌─────────────────────────────────────────┐
│  gear-mcp-server (C++ GDExtension)      │
│  运行在 Godot 编辑器进程内部             │
│                                         │
│  ┌─ MCPServer ────────────────────────┐ │
│  │  TCPServer (多线程, 每连接一线程)   │ │
│  │  JSONRPCParser (手写, 零依赖)      │ │
│  │  ToolRegistry (线程安全 Map)       │ │
│  │  MCPMethods (方法分发)             │ │
│  └────────────────────────────────────┘ │
│                                         │
│  ┌─ GodotAPI ────────────────────────┐  │
│  │  GDExtension C API 函数指针管理    │  │
│  │  EditorInterface 访问              │  │
│  │  场景/资源/脚本 直接操作           │  │
│  │  UndoRedo 集成                    │  │
│  │  Variant ↔ JSON 类型编解码         │  │
│  └────────────────────────────────────┘ │
│                                         │
│  ┌─ 工具域 (tools/) ─────────────────┐  │
│  │  project/ scene/ script/ classdb/  │  │
│  │  resource/ export/ runtime/        │  │
│  │  animation/ audio/ tilemap/        │  │
│  │  navigation/ theme/ autoload/      │  │
│  │  signal/ plugin/ input/            │  │
│  │  assets/ testing/ dx/ intent/      │  │
│  │  debug/ file/                      │  │
│  └────────────────────────────────────┘ │
│                                         │
│  ┌─ 共享层 (shared/) ────────────────┐  │
│  │  type_codec  path_utils  config/   │  │
│  │  json_builder  logger              │  │
│  └────────────────────────────────────┘ │
└─────────────────────────────────────────┘
```

**两个产物**：

| 产物 | 类型 | 职责 | 代码量 |
|------|------|------|--------|
| `gear-mcp-server.dll/so/dylib` | GDExtension 动态库 | MCP 服务器 + 所有工具 + Godot API 访问 | ~15,000 行 C++ |
| `gear-mcp-proxy` (npm 包) | Node.js 脚本 | MCP stdio ↔ TCP 桥接 | ~40 行 JS |

用户安装流程：
1. 把 `addons/gear-mcp-server/` 目录放入 Godot 项目（包含 .gdextension + 编译好的动态库）
2. 在 MCP 客户端配置中添加：
```json
{
  "mcpServers": {
    "godot": {
      "command": "npx",
      "args": ["-y", "gear-mcp-proxy"]
    }
  }
}
```
3. 打开 Godot 编辑器，GDExtension 自动加载，MCP 服务器自动启动
4. 配置全局通用——任何项目都用同一行 `npx` 命令，无需改路径

---

### 三、gear-mcp-proxy — Node.js stdio 代理

#### 3.1 职责

gear-mcp-proxy 是一个极简的 Node.js 包，只做 stdio 和 TCP 之间的字节流转发。它不包含任何 MCP 协议逻辑、工具定义或 Godot 相关代码。通过 `npm publish` 发布后，用户用 `npx -y gear-mcp-proxy` 即可在任何项目中启动。

#### 3.2 为什么用 Node.js 而不是 C++

| 维度 | C++ 二进制 | Node.js npx |
|------|-----------|-------------|
| 配置方式 | 每个项目写绝对路径 | `npx -y gear-mcp-proxy`，全局通用 |
| 分发方式 | 需要 CI 编译 3 平台二进制 | `npm publish` 一次 |
| 跨平台 | 需要 Windows/Linux/macOS 各编译一份 | Node.js 天然跨平台 |
| 更新方式 | 用户需要重新下载二进制 | `npx` 自动拉最新版本 |
| 代码量 | ~300 行 + CMake | ~40 行 + package.json |

#### 3.3 完整源码

```js
#!/usr/bin/env node
// gear-mcp-proxy/index.js

const net = require('net');

const PORT = parseInt(process.env.GEAR_PORT || '8510');
const HOST = process.env.GEAR_HOST || '127.0.0.1';

const socket = new net.Socket();

// TCP → stdout（GDExtension 的响应写回 AI 客户端）
socket.on('data', (chunk) => {
    process.stdout.write(chunk);
});

socket.on('close', () => process.exit(0));
socket.on('error', (err) => {
    if (err.code !== 'ECONNREFUSED') {
        process.stderr.write(`gear-mcp-proxy: ${err.message}\n`);
        process.exit(1);
    }
});

// stdin → TCP（AI 客户端的请求转发给 GDExtension）
process.stdin.on('data', (chunk) => {
    socket.write(chunk);
});

process.stdin.on('close', () => socket.end());

// 连接 GDExtension TCP 服务器（带重试，等 Godot 编辑器启动）
let retries = 0;
const MAX_RETRIES = 60; // 最多等 60 秒

function connect() {
    socket.connect(PORT, HOST);
}

socket.on('connect', () => {
    retries = 0;
});

socket.on('error', (err) => {
    if (err.code === 'ECONNREFUSED' && retries < MAX_RETRIES) {
        retries++;
        setTimeout(connect, 1000);
    } else if (err.code === 'ECONNREFUSED') {
        process.stderr.write('gear-mcp-proxy: Godot editor not running or MCP server not started\n');
        process.exit(1);
    }
});

connect();
```

#### 3.4 package.json

```json
{
  "name": "gear-mcp-proxy",
  "version": "1.0.0",
  "description": "stdio-to-TCP proxy for Gear MCP Server (Godot GDExtension)",
  "main": "index.js",
  "bin": {
    "gear-mcp-proxy": "./index.js"
  },
  "keywords": ["godot", "mcp", "proxy"],
  "license": "MIT"
}
```

#### 3.5 用户配置

MCP 客户端只需一行配置，所有项目通用：

```json
{
  "mcpServers": {
    "godot": {
      "command": "npx",
      "args": ["-y", "gear-mcp-proxy"]
    }
  }
}
```

可选环境变量：

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `GEAR_PORT` | `8510` | GDExtension TCP 端口 |
| `GEAR_HOST` | `127.0.0.1` | GDExtension TCP 地址 |

#### 3.6 通信协议

```
AI 客户端 ←→ gear-mcp-proxy (stdio) ←→ GDExtension (TCP :8510)
                   │                        │
            原始字节流透传              \n 分隔 JSON-RPC
            (stdin/stdout pipe)        (现有 TCPServer 协议)
```

gear-mcp-proxy 不做任何协议解析，纯粹透传字节流。GDExtension 的 TCPServer 使用 `\n` 分隔 JSON-RPC（tcp_server.cpp 第 375 行），MCP 客户端发的 stdio 数据原封不动到达 TCPServer，TCPServer 的响应也原封不动回到 MCP 客户端的 stdout。

---

### 四、GDExtension 目录结构

```
src/
├── register_types.cpp/h          # GDExtension 入口，init/deinit
├── types.h                       # MCPRequest, MCPResponse, MCPToolInfo
│
├── server/                       # MCP 服务器基础设施（复用现有 C++ 项目）
│   ├── mcp_server.cpp/h          # 顶层编排：TCP + JSON-RPC + ToolRegistry
│   ├── tcp_server.cpp/h          # 平台 TCP 服务器（Winsock/POSIX）
│   ├── json_rpc_parser.cpp/h     # JSON-RPC 2.0 解析器（手写，零依赖）
│   ├── tool_registry.cpp/h       # 线程安全工具注册表
│   └── mcp_methods.cpp/h        # MCP 方法分发（tools/list, tools/call, resources/*）
│
├── godot_api/                    # GDExtension C API 封装层
│   ├── godot_api.cpp/h           # 单例：所有 GDExtension 函数指针解析
│   ├── editor_context.cpp/h      # EditorInterface 访问（当前场景、选中节点等）
│   ├── scene_ops.cpp/h           # 场景操作 API（加载/保存/遍历/修改场景树）
│   ├── resource_ops.cpp/h        # 资源操作 API（创建/修改/保存 Resource）
│   ├── script_ops.cpp/h          # GDScript 文件操作
│   ├── classdb_ops.cpp/h         # ClassDB 查询
│   ├── undo_redo.cpp/h           # UndoRedo 集成
│   ├── project_settings.cpp/h    # ProjectSettings 读写
│   └── process_ops.cpp/h         # 进程管理（启动/停止项目）
│
├── shared/                       # 共享工具库
│   ├── type_codec.cpp/h          # Variant ↔ JSON 双向编解码（统一所有类型）
│   ├── path_utils.cpp/h          # 路径处理（res://, 绝对路径, 遍历保护）
│   ├── config_parser.cpp/h       # project.godot / export_presets.cfg 解析
│   ├── json_builder.cpp/h        # JSON 构建辅助（escape, string, object, array）
│   ├── http_client.cpp/h         # 轻量 HTTP 客户端（素材下载使用）
│   └── logger.cpp/h              # 统一日志
│
├── tools/                        # 工具域实现
│   ├── register_all.cpp/h        # 统一注册入口
│   │
│   ├── project/                  # 项目管理 (5 工具)
│   │   ├── register_project.cpp
│   │   ├── list_projects.cpp
│   │   ├── get_project_info.cpp
│   │   ├── search_project.cpp
│   │   └── project_settings.cpp  # get/set_project_setting
│   │
│   ├── editor/                   # 编辑器控制 (6 工具)
│   │   ├── register_editor.cpp
│   │   ├── launch_editor.cpp
│   │   ├── run_project.cpp
│   │   ├── stop_project.cpp
│   │   ├── get_debug_output.cpp
│   │   ├── get_editor_status.cpp
│   │   └── get_godot_version.cpp
│   │
│   ├── scene/                    # 场景操作 (10 工具)
│   │   ├── register_scene.cpp
│   │   ├── create_scene.cpp
│   │   ├── save_scene.cpp
│   │   ├── list_scene_nodes.cpp
│   │   ├── add_node.cpp
│   │   ├── get_node_properties.cpp
│   │   ├── set_node_properties.cpp
│   │   ├── delete_node.cpp
│   │   ├── duplicate_node.cpp
│   │   ├── reparent_node.cpp
│   │   └── load_sprite.cpp
│   │
│   ├── script/                   # GDScript (3 工具)
│   │   ├── register_script.cpp
│   │   ├── create_script.cpp
│   │   ├── modify_script.cpp
│   │   └── get_script_info.cpp
│   │
│   ├── classdb/                  # ClassDB (3 工具)
│   │   ├── register_classdb.cpp
│   │   ├── query_classes.cpp
│   │   ├── query_class_info.cpp
│   │   └── inspect_inheritance.cpp
│   │
│   ├── signal/                   # 信号管理 (3 工具)
│   │   ├── register_signal.cpp
│   │   ├── connect_signal.cpp
│   │   ├── disconnect_signal.cpp
│   │   └── list_connections.cpp
│   │
│   ├── resource/                 # 资源操作 (5 工具)
│   │   ├── register_resource.cpp
│   │   ├── create_resource.cpp
│   │   ├── modify_resource.cpp
│   │   ├── create_material.cpp
│   │   ├── create_shader.cpp
│   │   └── get_dependencies.cpp
│   │
│   ├── export/                   # 导出 (2 工具)
│   │   ├── register_export.cpp
│   │   ├── export_project.cpp
│   │   └── list_export_presets.cpp
│   │
│   ├── animation/                # 动画 (5 工具)
│   │   ├── register_animation.cpp
│   │   ├── create_animation.cpp
│   │   ├── add_animation_track.cpp
│   │   ├── create_animation_tree.cpp
│   │   ├── add_animation_state.cpp
│   │   └── connect_animation_states.cpp
│   │
│   ├── runtime/                  # 运行时检查 (4 工具)
│   │   ├── register_runtime.cpp
│   │   ├── inspect_runtime_tree.cpp
│   │   ├── set_runtime_property.cpp
│   │   ├── call_runtime_method.cpp
│   │   └── get_runtime_metrics.cpp
│   │
│   ├── autoload/                 # Autoload (4 工具)
│   │   ├── register_autoload.cpp
│   │   ├── add_autoload.cpp
│   │   ├── remove_autoload.cpp
│   │   ├── list_autoloads.cpp
│   │   └── set_main_scene.cpp
│   │
│   ├── plugin/                   # 插件管理 (3 工具)
│   │   ├── register_plugin.cpp
│   │   ├── list_plugins.cpp
│   │   ├── enable_plugin.cpp
│   │   └── disable_plugin.cpp
│   │
│   ├── input/                    # 输入映射 (1 工具)
│   │   ├── register_input.cpp
│   │   └── add_input_action.cpp
│   │
│   ├── tilemap/                  # TileMap (2 工具)
│   │   ├── register_tilemap.cpp
│   │   ├── create_tileset.cpp
│   │   └── set_tilemap_cells.cpp
│   │
│   ├── audio/                    # 音频 (4 工具)
│   │   ├── register_audio.cpp
│   │   ├── create_audio_bus.cpp
│   │   ├── get_audio_buses.cpp
│   │   ├── set_audio_bus_effect.cpp
│   │   └── set_audio_bus_volume.cpp
│   │
│   ├── navigation/               # 导航 (2 工具)
│   │   ├── register_navigation.cpp
│   │   ├── create_navigation_region.cpp
│   │   └── create_navigation_agent.cpp
│   │
│   ├── theme/                    # 主题/UI (3 工具)
│   │   ├── register_theme.cpp
│   │   ├── set_theme_color.cpp
│   │   ├── set_theme_font_size.cpp
│   │   └── apply_theme_shader.cpp
│   │
│   ├── uid/                      # UID 管理 (2 工具)
│   │   ├── register_uid.cpp
│   │   ├── get_uid.cpp
│   │   └── update_project_uids.cpp
│   │
│   ├── import/                   # 导入管线 (5 工具)
│   │   ├── register_import.cpp
│   │   ├── get_import_status.cpp
│   │   ├── get_import_options.cpp
│   │   ├── set_import_options.cpp
│   │   ├── reimport_resource.cpp
│   │   └── validate_project.cpp
│   │
│   ├── assets/                   # CC0 素材 (3 工具)
│   │   ├── register_assets.cpp
│   │   ├── search_assets.cpp     # Poly Haven + AmbientCG + Kenney
│   │   ├── fetch_asset.cpp
│   │   └── list_asset_providers.cpp
│   │
│   ├── testing/                  # 测试/截图/输入注入 (6 工具)
│   │   ├── register_testing.cpp
│   │   ├── capture_screenshot.cpp
│   │   ├── capture_viewport.cpp
│   │   ├── inject_action.cpp
│   │   ├── inject_key.cpp
│   │   ├── inject_mouse_click.cpp
│   │   └── inject_mouse_motion.cpp
│   │
│   ├── dx/                       # 开发者体验 (4 工具)
│   │   ├── register_dx.cpp
│   │   ├── parse_error_log.cpp
│   │   ├── get_project_health.cpp
│   │   ├── find_resource_usages.cpp
│   │   └── scaffold_gameplay_prototype.cpp
│   │
│   ├── intent/                   # 意图追踪 (9 工具)
│   │   ├── register_intent.cpp
│   │   ├── capture_intent_snapshot.cpp
│   │   ├── record_decision_log.cpp
│   │   ├── generate_handoff_brief.cpp
│   │   ├── summarize_intent_context.cpp
│   │   ├── record_work_step.cpp
│   │   ├── record_execution_trace.cpp
│   │   ├── export_handoff_pack.cpp
│   │   ├── set_recording_mode.cpp
│   │   └── get_recording_mode.cpp
│   │
│   ├── debug/                    # LSP + DAP (9 工具)
│   │   ├── register_debug.cpp
│   │   ├── lsp_get_diagnostics.cpp
│   │   ├── lsp_get_completions.cpp
│   │   ├── lsp_get_hover.cpp
│   │   ├── lsp_get_symbols.cpp
│   │   ├── dap_set_breakpoint.cpp
│   │   ├── dap_remove_breakpoint.cpp
│   │   ├── dap_continue.cpp
│   │   ├── dap_pause.cpp
│   │   ├── dap_step_over.cpp
│   │   └── dap_get_stack_trace.cpp
│   │
│   ├── file/                     # 文件操作 (3 工具，复用现有实现)
│   │   ├── register_file.cpp
│   │   ├── read_file_tool.cpp/h
│   │   ├── write_file_tool.cpp/h
│   │   └── list_dir_tool.cpp/h
│   │
│   └── meta/                     # 元工具 (2 工具)
│       ├── register_meta.cpp
│       ├── tool_catalog.cpp
│       └── manage_tool_groups.cpp
│
└── proxy/                        # gear-mcp-proxy (独立 npm 包，不在 C++ 项目中)
    ├── package.json
    └── index.js                  # stdio ↔ TCP 桥接 (~40 行 Node.js)
```

---

### 五、核心层设计

#### 5.1 GodotAPI 单例

所有 GDExtension C API 函数指针在 init 阶段一次性解析，存入静态变量。这是从现有 C++ 项目的 EditorContext 模式扩展而来。

```cpp
// godot_api/godot_api.h

class GodotAPI {
public:
    static void init(GDExtensionInterfaceGetProcAddress p_get_proc_address);

    // ---- EditorInterface 访问 ----
    static GDExtensionObjectPtr get_editor_interface();
    static GDExtensionObjectPtr get_edited_scene_root();
    static GDExtensionObjectPtr get_selection();
    static std::vector<GDExtensionObjectPtr> get_selected_nodes();
    static GDExtensionObjectPtr get_editor_undo_redo();

    // ---- 场景操作 ----
    static GDExtensionObjectPtr load_scene(const std::string &p_res_path);
    static bool save_scene(GDExtensionObjectPtr p_root, const std::string &p_res_path);
    static void refresh_filesystem();
    static void reload_scene(const std::string &p_res_path);

    // ---- 节点操作 ----
    static GDExtensionObjectPtr create_node(const std::string &p_class_name);
    static void add_child(GDExtensionObjectPtr p_parent, GDExtensionObjectPtr p_child);
    static void remove_child(GDExtensionObjectPtr p_parent, GDExtensionObjectPtr p_child);
    static void set_owner_recursive(GDExtensionObjectPtr p_node, GDExtensionObjectPtr p_owner);
    static std::string get_node_name(GDExtensionObjectPtr p_node);
    static std::string get_node_class(GDExtensionObjectPtr p_node);
    static GDExtensionObjectPtr get_node_child(GDExtensionObjectPtr p_node, const std::string &p_path);

    // ---- 属性操作 ----
    static void set_property(GDExtensionObjectPtr p_obj, const std::string &p_name, const std::string &p_json_value);
    static std::string get_property_json(GDExtensionObjectPtr p_obj, const std::string &p_name);
    static std::vector<std::pair<std::string, std::string>> get_all_properties_json(GDExtensionObjectPtr p_obj);

    // ---- Variant 工具 ----
    static std::string variant_to_json(GDExtensionConstVariantPtr p_variant);
    static void json_to_variant(const std::string &p_json, GDExtensionUninitializedVariantPtr r_variant);

    // ---- 通用方法调用 ----
    static std::string call_method_string(GDExtensionObjectPtr p_obj, const char *p_method);
    static GDExtensionObjectPtr call_method_object(GDExtensionObjectPtr p_obj, const char *p_method);
    static void call_method_void(GDExtensionObjectPtr p_obj, const char *p_method,
        const GDExtensionConstVariantPtr *p_args, int p_arg_count);

    // ---- 进程管理 ----
    static int spawn_process(const std::string &p_executable, const std::vector<std::string> &p_args);
    static bool kill_process(int p_pid);
    static bool is_process_running(int p_pid);

    // ---- 路径工具 ----
    static std::string get_project_path();
    static std::string res_to_absolute(const std::string &p_res_path);
    static std::string absolute_to_res(const std::string &p_abs_path);

private:
    // 所有 GDExtension 函数指针（约 30 个）
    static GDExtensionInterfaceGlobalGetSingleton s_global_get_singleton;
    static GDExtensionInterfaceObjectCallScriptMethod s_object_call_script;
    static GDExtensionInterfaceVariantNewNil s_variant_new_nil;
    // ... 其余同 EditorContext 模式
};
```

#### 5.2 type_codec — 统一类型编解码

消除 Gear 中 3 份 `_parse_value` 的重复问题。所有 Variant ↔ JSON 转换通过一个函数处理。

```cpp
// shared/type_codec.h

namespace type_codec {

/// JSON → Variant
/// 支持的类型标签: Vector2, Vector3, Vector2i, Vector3i, Color,
/// Rect2, Transform2D, Transform3D, NodePath, Basis, Quaternion,
/// Plane, AABB, Projection, String, int, float, bool
///
/// 输入格式: {"type": "Vector2", "x": 1.0, "y": 2.0}
/// 兼容 "type" 和 "_type" 两种键名
void parse(const std::string &p_json, int p_expected_type,
           GDExtensionUninitializedVariantPtr r_variant);

/// Variant → JSON
/// 输出统一使用 "type" 作为键名
std::string serialize(GDExtensionConstVariantPtr p_variant);

/// 将 Godot 属性值序列化为 JSON 安全格式
/// 自动处理 Resource、Object、Callable 等不可序列化类型
std::string property_to_json(GDExtensionConstVariantPtr p_variant);

} // namespace type_codec
```

#### 5.3 工具注册模式

每个工具域有一个 `register_xxx.cpp`，负责注册该域的所有工具。统一的注册入口 `register_all.cpp` 调用各域的注册函数。

```cpp
// tools/scene/register_scene.cpp

#include "server/tool_registry.h"
#include "godot_api/godot_api.h"
#include "shared/type_codec.h"
#include "shared/json_builder.h"
#include "shared/path_utils.h"

// ---- 工具 Schema ----
static const char *CREATE_SCENE_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "projectPath": {"type": "string", "description": "Godot project root path"},
        "scenePath":   {"type": "string", "description": "Path for the new .tscn file"},
        "rootNodeType":{"type": "string", "description": "Root node class name", "default": "Node"},
        "scriptPath":  {"type": "string", "description": "Optional script to attach"}
    },
    "required": ["projectPath", "scenePath"]
})schema";

// ---- 工具 Handler ----

static void handle_create_scene(const std::string &p_params, std::string &r_result, std::string &r_error) {
    std::string scene_path = json::extract_string(p_params, "scenePath");
    std::string root_type  = json::extract_string(p_params, "rootNodeType");
    if (root_type.empty()) root_type = "Node";

    if (scene_path.empty()) {
        r_error = "Missing required parameter: scenePath";
        return;
    }

    // 1. 验证类型
    if (!GodotAPI::classdb_class_exists(root_type)) {
        r_error = "Unknown node type: " + root_type;
        return;
    }

    // 2. 创建根节点
    GDExtensionObjectPtr root = GodotAPI::create_node(root_type);
    if (!root) {
        r_error = "Failed to instantiate: " + root_type;
        return;
    }

    // 3. 保存场景
    std::string res_path = path_utils::to_res_path(scene_path);
    if (!GodotAPI::save_scene(root, res_path)) {
        r_error = "Failed to save scene: " + res_path;
        return;
    }

    // 4. 返回结果
    r_result = json::object({
        {"scenePath", json::string(res_path)},
        {"rootNode",  json::string(root_type)}
    });
}

// ---- 注册 ----

void register_scene_tools(godot_mcp::ToolRegistry *p_reg) {
    p_reg->register_tool("create_scene",
        "Create a new .tscn scene file with specified root node type",
        CREATE_SCENE_SCHEMA, handle_create_scene);

    p_reg->register_tool("list_scene_nodes",
        "List all nodes in a scene tree",
        LIST_SCENE_NODES_SCHEMA, handle_list_scene_nodes);

    p_reg->register_tool("add_node",
        "Add a node of any ClassDB type to a scene",
        ADD_NODE_SCHEMA, handle_add_node);

    // ... 其余场景工具
}
```

#### 5.4 统一注册入口

```cpp
// tools/register_all.cpp

void register_all_tools(godot_mcp::ToolRegistry *p_reg) {
    register_file_tools(p_reg);       // 3 工具
    register_project_tools(p_reg);    // 5 工具
    register_editor_tools(p_reg);     // 6 工具
    register_scene_tools(p_reg);      // 10 工具
    register_script_tools(p_reg);     // 3 工具
    register_classdb_tools(p_reg);    // 3 工具
    register_signal_tools(p_reg);     // 3 工具
    register_resource_tools(p_reg);   // 5 工具
    register_export_tools(p_reg);     // 2 工具
    register_animation_tools(p_reg);  // 5 工具
    register_runtime_tools(p_reg);    // 4 工具
    register_autoload_tools(p_reg);   // 4 工具
    register_plugin_tools(p_reg);     // 3 工具
    register_input_tools(p_reg);      // 1 工具
    register_tilemap_tools(p_reg);    // 2 工具
    register_audio_tools(p_reg);      // 4 工具
    register_navigation_tools(p_reg); // 2 工具
    register_theme_tools(p_reg);      // 3 工具
    register_uid_tools(p_reg);        // 2 工具
    register_import_tools(p_reg);     // 5 工具
    register_asset_tools(p_reg);      // 3 工具
    register_testing_tools(p_reg);    // 6 工具
    register_dx_tools(p_reg);         // 4 工具
    register_intent_tools(p_reg);     // 9 工具
    register_debug_tools(p_reg);      // 9 工具
    register_meta_tools(p_reg);       // 2 工具
}
```

---

### 六、线程安全模型

#### 6.1 问题

TCPServer 为每个客户端连接创建独立线程（tcp_server.cpp 第 351 行 `CreateThread`）。工具 handler 在这些线程中被调用。但 Godot API（场景操作、资源访问、EditorInterface）**必须在主线程调用**。

#### 6.2 解决方案：主线程队列

```cpp
// server/main_thread_queue.h

#include <functional>
#include <mutex>
#include <queue>
#include <condition_variable>

class MainThreadQueue {
public:
    /// 从任意线程提交一个任务，等待执行完成后返回结果
    static std::string invoke_on_main(std::function<std::string()> p_task);

    /// 由 GDExtension 的 _process 回调驱动（每帧处理队列中的任务）
    static void process_pending();

private:
    struct Task {
        std::function<std::string()> func;
        std::string *result_ptr;
        std::mutex *done_mutex;
        std::condition_variable *done_cv;
        bool *done_flag;
    };

    static std::mutex s_queue_mutex;
    static std::queue<Task> s_queue;
};
```

**工作流**：

```
TCP 客户端线程                        Godot 主线程
    │                                     │
    │  invoke_on_main(task)               │
    │  → 锁队列，入队                      │
    │  → 等待 condition_variable           │
    │                                     │  _process() 回调
    │                                     │  → process_pending()
    │                                     │  → 从队列取任务
    │                                     │  → 执行 task()（调用 Godot API）
    │                                     │  → 写入结果，通知 cv
    │  ← 收到通知，取回结果               │
    │  → 返回给 TCP 客户端                │
```

#### 6.3 集成方式

在 GDExtension init 时注册一个 `_process` 通知回调（通过 EditorPlugin 的 NOTIFICATION_PROCESS），每帧调用 `MainThreadQueue::process_pending()`。

对于不访问 Godot API 的工具（file 读写、project.godot 解析、素材 HTTP 下载、意图追踪），可以直接在 TCP 线程中执行，无需走主线程队列。工具注册时可以标记：

```cpp
// 标记工具是否需要在主线程执行
p_reg->register_tool("create_scene", ..., handler, /*main_thread=*/true);
p_reg->register_tool("read_file",    ..., handler, /*main_thread=*/false);
p_reg->register_tool("search_assets",..., handler, /*main_thread=*/false);
```

MCPMethods 分发时检查此标记：
- `main_thread=true` → 通过 `MainThreadQueue::invoke_on_main()` 调度
- `main_thread=false` → 直接在当前 TCP 线程执行

---

### 七、MCP 方法处理

#### 7.1 initialize 响应

```json
{
    "protocolVersion": "2024-11-05",
    "capabilities": {
        "tools": { "listChanged": true },
        "resources": { "subscribe": false, "listChanged": false }
    },
    "serverInfo": {
        "name": "gear-mcp-server",
        "version": "2.0.0"
    }
}
```

#### 7.2 工具分组（精简版）

去掉 Gear 原有的 compact/full/legacy 三模式和 22 个动态分组。简化为两层：

**核心组（始终可见，约 30 个工具）**：
project(5) + editor(6) + scene(10) + script(3) + classdb(3) + signal(3) + export(2) + meta(2) + file(3) + resource(5) + editor_context(1)

**扩展组（通过 tool_catalog 搜索激活，约 80 个工具）**：
animation, runtime, autoload, plugin, input, tilemap, audio, navigation, theme, uid, import, assets, testing, dx, intent, debug

分组管理逻辑保留在 `tools/meta/` 中，通过 ToolRegistry 的 group 机制实现。

#### 7.3 MCP Resources

| URI | 说明 | 实现方式 |
|-----|------|---------|
| `godot://editor/context` | 编辑器上下文 | GodotAPI::get_context_snapshot() |
| `godot://project/info` | 项目元数据 | config_parser 读取 project.godot |
| `godot://scene/{path}` | 场景文件内容 | 读取 .tscn 文件 |
| `godot://script/{path}` | 脚本文件内容 | 读取 .gd 文件 |
| `godot://resource/{path}` | 资源文件内容 | 读取 .tres/.tscn/.gd |

#### 7.4 MCP Prompts

| 名称 | 说明 |
|------|------|
| `godot.scene_bootstrap` | 场景引导工作流 |
| `godot.debug_triage` | 调试分诊循环 |

---

### 八、工具域详细说明

#### 8.1 场景操作 (scene/) — 10 工具

这是 Gear 中代码量最大的域（scene_tools.gd 762 行），也是 C++ 方案收益最大的域。

**关键改进**：GDScript 版本必须走"加载 PackedScene → 实例化 → 修改 → 打包 → 保存"的间接模式，因为 GDScript 插件运行在编辑器进程中但没有直接的场景树写权限（需要通过文件系统触发刷新）。C++ 版本通过 GDExtension C API 可以直接操作编辑器的场景树：

```cpp
// 直接获取当前编辑的场景根节点
GDExtensionObjectPtr root = GodotAPI::get_edited_scene_root();

// 直接创建节点并加入场景树
GDExtensionObjectPtr new_node = GodotAPI::create_node("CharacterBody2D");
GodotAPI::add_child(root, new_node);
GodotAPI::set_owner_recursive(new_node, root);

// 通过 EditorInterface 保存
GodotAPI::call_method_void(editor_interface, "save_scene", ...);
```

无需 load → instantiate → modify → pack → save 的五步循环，也无需 queue_free 临时实例。

**UndoRedo 集成**：

```cpp
static void handle_add_node(const std::string &p_params, std::string &r_result, std::string &r_error) {
    // ... 解析参数 ...

    GDExtensionObjectPtr undo_redo = GodotAPI::get_editor_undo_redo();
    GDExtensionObjectPtr parent = GodotAPI::get_node_child(root, parent_path);
    GDExtensionObjectPtr new_node = GodotAPI::create_node(node_type);

    // 注册 Undo/Redo 动作
    GodotAPI::undo_redo_create_action("MCP: Add " + node_name);
    GodotAPI::undo_redo_add_do_method(parent, "add_child", {new_node});
    GodotAPI::undo_redo_add_do_method(new_node, "set_owner", {root});
    GodotAPI::undo_redo_add_do_reference(new_node);
    GodotAPI::undo_redo_add_undo_method(parent, "remove_child", {new_node});
    GodotAPI::undo_redo_commit_action();

    // ... 返回结果 ...
}
```

用户在编辑器中按 Ctrl+Z 即可撤销 MCP 操作。

#### 8.2 运行时工具 (runtime/) — 4 工具

运行时工具需要与正在运行的游戏进程通信。在 Gear 中通过端口 7777 的 TCP autoload 实现。

C++ 方案保留此模式：GDExtension 注册一个 autoload 脚本（或在 C++ 中直接注入一个 Node 到运行时的 SceneTree），启动 TCP 服务器监听 7777 端口。但通信改为**持久连接**而非每次新建。

```cpp
// tools/runtime/runtime_connection.h

class RuntimeConnection {
public:
    static RuntimeConnection &instance();

    bool connect();      // 持久连接到 127.0.0.1:7777
    void disconnect();
    bool is_connected() const;

    std::string send_command(const std::string &p_command, const std::string &p_params_json);

private:
    int m_socket_fd = -1;
    std::mutex m_send_mutex;  // 串行化发送
};
```

#### 8.3 调试工具 (debug/) — 9 工具

LSP 和 DAP 通过 Godot 内置的语言服务器和调试适配器（端口 6005/6006）访问。

C++ 方案直接用 TCP 客户端连接这两个端口（复用现有 C++ 项目的 TCP 基础设施），发送标准 LSP/DAP 帧（Content-Length 头格式）。

```cpp
// tools/debug/lsp_client.h

class LSPClient {
public:
    static LSPClient &instance();

    bool connect();  // TCP 127.0.0.1:6005
    std::string get_diagnostics(const std::string &p_file_path);
    std::string get_completions(const std::string &p_file_path, int p_line, int p_col);
    std::string get_hover(const std::string &p_file_path, int p_line, int p_col);
    std::string get_symbols(const std::string &p_file_path);

private:
    int m_socket_fd = -1;
    std::string send_request(const std::string &p_method, const std::string &p_params_json);
    void send_document(const std::string &p_file_path);  // didOpen/didChange
};
```

#### 8.4 素材工具 (assets/) — 3 工具

CC0 素材搜索和下载（Poly Haven、AmbientCG、Kenney）。不需要 Godot API，直接在 TCP 线程中用 `shared/http_client.cpp` 发 HTTP 请求。

#### 8.5 意图追踪 (intent/) — 9 工具

纯数据管理工具，操作 JSON 文件存储。不需要 Godot API，直接在 TCP 线程执行。

---

### 九、构建系统

#### 9.1 CMake 结构

```
CMakeLists.txt                    # 顶层
├── src/server/                   # MCP 服务器基础设施（静态库）
├── src/godot_api/                # Godot API 封装层（静态库）
├── src/shared/                   # 共享工具（静态库）
├── src/tools/                    # 工具域（静态库）
├── godot-cpp/                    # godot-cpp 子模块
├── gear-mcp-server (target)      # GDExtension 动态库
└── proxy/CMakeLists.txt          # gear-proxy 可执行文件
```

```cmake
# CMakeLists.txt (顶层)
cmake_minimum_required(VERSION 3.16)
project(gear-mcp-server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

# godot-cpp
add_subdirectory(godot-cpp)

# 静态库
add_library(gear-server-lib STATIC
    src/server/mcp_server.cpp
    src/server/tcp_server.cpp
    src/server/json_rpc_parser.cpp
    src/server/tool_registry.cpp
    src/server/mcp_methods.cpp
    src/server/main_thread_queue.cpp
    src/godot_api/godot_api.cpp
    src/godot_api/editor_context.cpp
    src/godot_api/scene_ops.cpp
    # ... 其余源文件
    src/shared/type_codec.cpp
    src/shared/path_utils.cpp
    # ... 其余源文件
    src/tools/register_all.cpp
    src/tools/file/read_file_tool.cpp
    # ... 其余工具源文件
)
target_link_libraries(gear-server-lib PRIVATE godot::cpp)
target_include_directories(gear-server-lib PUBLIC src/)

# GDExtension 动态库
add_library(gear-mcp-server SHARED src/register_types.cpp)
target_link_libraries(gear-mcp-server PRIVATE gear-server-lib)
if(WIN32)
    target_link_libraries(gear-mcp-server PRIVATE ws2_32)
endif()

# 注意：gear-mcp-proxy 是独立的 npm 包，不在此 CMake 中构建
# 见 proxy/ 目录的 package.json
```

#### 9.2 多平台构建

| 平台 | 输出文件 | 编译器 |
|------|---------|--------|
| Windows x86_64 | gear-mcp-server.windows.editor.x86_64.dll + gear-proxy.exe | MSVC / MinGW |
| Linux x86_64 | gear-mcp-server.linux.editor.x86_64.so + gear-proxy | GCC / Clang |
| macOS x86_64 + arm64 | gear-mcp-server.macos.editor.universal.dylib + gear-proxy | Apple Clang |

#### 9.3 gdextension 配置

```ini
[configuration]
entry_symbol = "gear_mcp_server_init"
compatibility_minimum = 4.4

[libraries]
windows.editor.x86_64 = "res://addons/gear-mcp-server/gear-mcp-server.windows.editor.x86_64.dll"
linux.editor.x86_64   = "res://addons/gear-mcp-server/gear-mcp-server.linux.editor.x86_64.so"
macos.editor           = "res://addons/gear-mcp-server/gear-mcp-server.macos.editor.universal.dylib"
```

注意：只需要 `editor` 变体。`template_debug` 和 `template_release` 不需要，因为 MCP 服务器只在编辑器中运行。

---

### 十、运行时流程

#### 10.1 启动序列

```
1. 用户打开 Godot 编辑器（加载项目）
2. Godot 加载 GDExtension → 调用 gear_mcp_server_init()
3. GDEXTENSION_INITIALIZATION_EDITOR 级别：
   a. GodotAPI::init() — 解析所有 GDExtension 函数指针
   b. MCPServer 创建并启动 → TCP 监听 127.0.0.1:8510
   c. register_all_tools() — 注册 110+ 工具到 ToolRegistry
   d. 注册 MCP Resources (godot://editor/context 等)
   e. 注册 _process 通知回调（驱动 MainThreadQueue）
4. 控制台输出: "gear-mcp-server: listening on 127.0.0.1:8510, 112 tools registered"
```

#### 10.2 工具调用流程

```
AI 客户端发送 tools/call { name: "add_node", arguments: {...} }
    │
    │ stdin (JSON-RPC + \n)
    ▼
gear-mcp-proxy 转发到 TCP 127.0.0.1:8510
    │
    │ TCP recv
    ▼
TCPServer 客户端线程接收完整消息
    │
    │ JSONRPCParser::parse_request()
    ▼
MCPMethods::handle()
    │
    │ 匹配 "tools/call"
    ▼
MCPMethods::_handle_tools_call()
    │
    │ ToolRegistry::get_tool("add_node")
    ▼
检查 main_thread 标记
    │
    ├── main_thread=true (场景操作)
    │   │
    │   ▼
    │   MainThreadQueue::invoke_on_main(task)
    │   │
    │   │ 等待 Godot 主线程 _process() 处理
    │   │
    │   ▼
    │   主线程执行:
    │     GodotAPI::create_node("CharacterBody2D")
    │     GodotAPI::add_child(parent, new_node)
    │     GodotAPI::undo_redo_commit_action()
    │   │
    │   ▼ 结果通过 condition_variable 返回
    │
    ├── main_thread=false (文件/素材/意图等)
    │   │
    │   ▼ 直接在当前 TCP 线程执行
    │
    ▼
JSONRPCParser::build_response()
    │
    │ TCP send + \n
    ▼
gear-proxy 转发到 stdout
    │
    │ stdout
    ▼
AI 客户端收到结果
```

---

### 十一、与 Gear 现有架构的对比

| 维度 | Gear 现有 | C++ GDExtension + Node.js proxy |
|------|----------|------------|
| 外部依赖 | Node.js 18+, npm, ws, axios, fs-extra | Node.js（仅 proxy，40 行） |
| Godot 侧组件 | 3 个 GDScript EditorPlugin | 1 个 GDExtension（自动加载） |
| MCP 服务器 | Node.js 进程 (249KB index.ts) | C++ GDExtension (进程内) |
| stdio 代理 | npx gear-godot-mcp (Node.js 全量服务器) | npx gear-mcp-proxy (40 行纯转发) |
| 通信通道 | 4 个 (WS:6505 + LSP:6005 + DAP:6006 + RT:7777) | 3 个 (TCP:8510 + LSP:6005 + DAP:6006) + RT:7777 |
| 与编辑器通信 | WebSocket JSON 消息 | 直接 GDExtension C API |
| 类型序列化 | 3 份 _parse_value (共 ~130 行重复) | 1 份 type_codec (统一) |
| Undo/Redo | 不支持 | 完整支持 |
| 工具分发 | 130-case switch | Map<string, Handler> 查找 |
| 最大单文件 | 249 KB (index.ts) | ~15 KB (各工具域) |
| 安装步骤 | npm + 配置 + 启用 3 插件 | 放 GDExtension + npx 一行配置 |
| MCP 配置 | 每个项目可能不同 | `npx -y gear-mcp-proxy`，全局通用 |
| 跨平台 | Node.js 天然跨平台 | GDExtension 需要 CI 编译 3 平台，proxy 天然跨平台 |

---

### 十二、开发阶段

#### 阶段 1：基础框架（2 周）

目标：能跑通一个完整的工具调用。

- 搭建 CMake 项目结构
- 复用/增强现有 TCPServer、JSONRPCParser、ToolRegistry
- 实现 GodotAPI 单例（扩展 EditorContext 模式）
- 实现 type_codec、path_utils、json_builder
- 实现 MainThreadQueue
- 实现 3 个 file 工具（复用现有 read_file/write_file/list_dir）
- 实现 3 个 scene 工具原型（create_scene、add_node、list_scene_nodes）
- 实现 gear-mcp-proxy npm 包（40 行 Node.js）
- 验证：在 Claude Desktop 中通过 `npx -y gear-mcp-proxy` 调用 create_scene 成功

#### 阶段 2：核心工具域（3 周）

目标：覆盖 Gear 的核心组 + 最常用的扩展组。

- 完成 scene/ 全部 10 个工具 + UndoRedo
- 完成 script/ 3 个工具
- 完成 classdb/ 3 个工具
- 完成 project/ 5 个工具
- 完成 editor/ 6 个工具
- 完成 resource/ 5 个工具
- 完成 export/ 2 个工具
- 完成 signal/ 3 个工具
- 完成 autoload/ 4 个工具

#### 阶段 3：扩展工具域（2 周）

- animation/ 5 工具
- audio/ 4 工具
- tilemap/ 2 工具
- navigation/ 2 工具
- theme/ 3 工具
- plugin/ 3 工具
- input/ 1 工具
- uid/ 2 工具
- import/ 5 工具
- runtime/ 4 工具 + RuntimeConnection 持久连接

#### 阶段 4：高级功能（2 周）

- debug/ 9 工具（LSP + DAP 客户端）
- assets/ 3 工具（HTTP 客户端 + Poly Haven/AmbientCG/Kenney API）
- testing/ 6 工具（截图、输入注入）
- dx/ 4 工具
- intent/ 9 工具
- meta/ 2 工具（工具分组管理）
- MCP Resources + Prompts

#### 阶段 5：发布准备（1 周）

- 跨平台 CI/CD（Windows/Linux/macOS）
- 安装脚本和文档
- 回归测试
- 性能基准测试

总计约 10 周。

---

### 十三、风险和缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| GDExtension C API 不足以覆盖某些编辑器操作 | 部分工具无法实现 | 阶段 1 先验证关键 API（场景保存、UndoRedo、ClassDB 实例化），不可行时回退到 GDScript autoload 辅助 |
| 跨平台编译维护成本 | 每次发布需要 3 平台二进制 | GitHub Actions CI 自动化；提供编译好的二进制发布，用户无需自行编译 |
| 手写 JSON 解析器复杂度随工具增长 | 嵌套参数解析容易出错 | 阶段 1 增强 JSONRPCParser 支持嵌套对象/数组提取；考虑引入 nlohmann/json 作为可选依赖 |
| 主线程队列增加延迟 | 每个编辑器操作多一帧延迟 | 可接受的延迟（~16ms），比 Gear 的 WebSocket 往返 + JSON 序列化开销更小 |
| C++ 开发迭代速度 | 修改后需重新编译 | 工具域模块化，单个文件编译快；CMake 增量编译 |
| gear-proxy 需要随 GDExtension 一起分发 | 多一个二进制文件 | 已改为 npm 包，`npx` 自动分发，无需手动管理 |
