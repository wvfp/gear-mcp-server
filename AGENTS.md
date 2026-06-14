# Gear MCP Server — AGENTS.md

## 这是什么项目

一个 C++ GDExtension，将 MCP 服务器直接嵌入 Godot 编辑器进程内部，目前已注册约 44 个工具（目标是 110+），供 AI 客户端操控 Godot 项目。附带一个极简 Node.js stdio 到 TCP 的代理（`gear-mcp-proxy`）。

## 架构一览

```
AI 客户端 (Claude Desktop / Cursor / Cline / OpenCode)
  │  MCP over stdio (JSON-RPC 2.0)
  ▼
gear-mcp-proxy (Node.js, ~50 行, `npx -y gear-mcp-proxy`)
  │  TCP 127.0.0.1:8510, \n 分隔 JSON-RPC
  ▼
gear-mcp-server (C++ GDExtension, 运行在 Godot 编辑器进程内)
  ├─ TCPServer — 每连接一个线程，读取 \n 分隔的 JSON-RPC
  ├─ MCPMethods — 分发 initialize/tools/list/tools/call/resources/*
  ├─ ToolRegistry — 线程安全 map<string, handler>，含 main_thread 标记
  ├─ MainThreadQueue — 将 Godot API 调用分发到主线程
  ├─ GodotAPI — 单例，约 30 个 GDExtension C API 函数指针
  ├─ tools/ — 各领域工具 handler（scene, file, script, classdb, ...）
  └─ shared/ — type_codec, path_utils, config_parser, logger
```

**两个产物：**

| 产物                             | 类型              | 路径                             |
| ------------------------------ | --------------- | ------------------------------ |
| `gear-mcp-server.dll/so/dylib` | GDExtension 动态库 | `project/addons/gear_mcp/bin/` |
| `gear-mcp-proxy`               | npm 包           | `proxy/`                       |

## 构建与运行命令

```powershell
# 配置（只需一次）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# 编译
cmake --build build --config Debug

# 输出到 project/addons/gear_mcp/bin/
# 文件名格式：libgear_mcp_server.{platform}.{variant}.{arch}.{ext}
```

项目用 `GODOTCPP_TARGET`（默认 `template_debug`）控制 GDExtension 后缀。Windows 上输出例如 `libgear_mcp_server.windows.template_debug.x86_64.dll`。

**端到端测试：**

1. 用 Godot 4.4+ 打开 `project/` 目录 — GDExtension 自动加载
2. 运行 `cd proxy && node index.js` — 代理连接 TCP :8510
3. 另开一个终端：`echo '{"jsonrpc":"2.0","method":"tools/list","id":1}' | node proxy/index.js`
4. Godot editor console 二进制在 G:\Godot\ 下

代理最多重试 60 秒等待 Godot 编辑器启动。环境变量：`GEAR_PORT`（8510）、`GEAR_HOST`（127.0.0.1）。

<br />

## 关键架构事实

- **仅编辑器模式**：`initialize_gear_mcp_module` 只在 `MODULE_INITIALIZATION_LEVEL_EDITOR` 执行。不支持运行时/模板模式。
- **线程安全**：标记 `main_thread=true` 的工具 handler 通过 `MainThreadQueue::invoke_on_main()` 运行，阻塞等待 Godot 主线程的 `_process` 处理。标记 `main_thread=false` 的工具（文件 I/O、配置解析）直接在 TCP 线程执行。
- **Tool handler 签名**：`void (*)(const std::string &params_json, std::string &result_out, std::string &error_out)`
- **所有 JSON-RPC 处理** 使用 `nlohmann/json`（v3.11.3，通过 FetchContent 自动下载）。没有手写 JSON 解析器。
- **入口符号**：`gear_mcp_server_init`（在 `register_types.cpp` 中通过 `extern "C"` 暴露）
- **TCP 协议**：`\n` 分隔的 JSON-RPC 消息。没有 Content-Length 头。每个 `\n` 是一条消息。
- **日志**：`src/shared/logger.h` 中的 `GEAR_LOG()` / `GEAR_WARN()` / `GEAR_ERR()` 宏 — 简单的 `printf`/`fprintf` 到 stderr。

## 添加一个新工具（3 个文件）

1. 在 `src/tools/{domain}/{tool_name}.cpp` 创建 handler 函数，匹配 ToolHandler 签名 — 引入 `"server/tool_registry.h"` 即可使用 schema 常量模式
2. 创建或更新 `src/tools/{domain}/register_{domain}.cpp` — 对每个工具调用 `p_reg->register_tool(name, description, schema, handler, main_thread)`
3. 在 `src/tools/register_all.cpp` 中添加注册调用和前向声明

如果新文件放在 `src/tools/{domain}/` 下，不需要改 CMakeLists.txt——所有工具文件已按名称列出（只有添加新子目录时需要改）。

## 当前工具清单（约 44 个，10 个领域）

| 领域       | 数量     | 注册文件                                                     | 注册函数                                                   |
| -------- | ------ | -------------------------------------------------------- | ------------------------------------------------------ |
| file     | 3      | `tools/file/file_tools.cpp`                              | `register_file_tools`                                  |
| scene    | 3+7=10 | `tools/scene/scene_tools.cpp` + `scene_tools_phase2.cpp` | `register_scene_tools` + `register_scene_tools_phase2` |
| script   | 3      | `tools/script/script_tools.cpp`                          | `register_script_tools`                                |
| classdb  | 3      | `tools/classdb/classdb_tools.cpp`                        | `register_classdb_tools`                               |
| project  | 5      | `tools/project/project_tools.cpp`                        | `register_project_tools`                               |
| editor   | 6      | `tools/editor/editor_tools.cpp`                          | `register_editor_tools`                                |
| resource | 5      | `tools/resource/resource_tools.cpp`                      | `register_resource_tools`                              |
| export   | 2      | `tools/export/export_tools.cpp`                          | `register_export_tools`                                |
| signal   | 3      | `tools/signal/signal_tools.cpp`                          | `register_signal_tools`                                |
| autoload | 4      | `tools/autoload/autoload_tools.cpp`                      | `register_autoload_tools`                              |

全部通过 `tools/register_all.cpp` 中的 `register_all_tools()` 汇总。Phase 2+ 规划的工具（animation、audio、runtime、debug、assets 等）要么有空的注册文件，要么尚未创建。

## 重要配置文件

| 文件                                             | 用途                                                      |
| ---------------------------------------------- | ------------------------------------------------------- |
| `CMakeLists.txt`                               | 构建配置、依赖下载、输出路径                                          |
| `project/addons/gear_mcp/gear_mcp.gdextension` | GDExtension 入口符号 + 各平台二进制路径                             |
| `project/addons/gear_mcp/plugin.cfg`           | Godot 插件元数据（0.1.0）                                      |
| `project/project.godot`                        | 测试项目配置（Godot 4.6, Jolt Physics, D3D12）                  |
| `proxy/package.json`                           | `gear-mcp-proxy` 的 npm 配置（尚未发布）                         |
| `.gitignore`                                   | 排除了 `build/` 和 `project/addons/gear_mcp/bin/`（二进制文件不提交） |

## 陷阱与注意事项

- **还没有 CI/CD** — 项目根目录没有 `.github/workflows/`。需要为跨平台构建创建一个。
- **`.gdextension`** **列出了 6 个平台变体**（debug/release × windows/linux/macos），但当前 CMake 默认只输出 `template_debug`。`GODOTCPP_TARGET` 变量控制这一点。
- **`plugin.cfg`** **中** **`script=""`** — GDScript 有意留空，因为 GDExtension 是自动加载的。不需要 GDScript 插件代码。
- **构建输出通过 CMake 生成器表达式设置**：`LIBRARY_OUTPUT_DIRECTORY "$<1:${OUTPUT_DIR}>"` — `$<1:...>` 强制表达式意味着它只适用于 Visual Studio 生成器（多配置）。
- **`build/`** **目录不在版本控制中**，包含自动下载的第三方依赖（通过 `FetchContent`）。不要提交它。
- **有两个场景工具注册文件**：`scene_tools.cpp`（Phase 1：3 个基本工具）和 `scene_tools_phase2.cpp`（7 个高级工具）。添加新场景工具前需要检查两者。
- **文档描述的是未来计划**，不是当前状态。`docs/Gear-Development-Plan.md` 说 "\~44 tools" 属于 Phase 2；这正是当前状态。架构文档描述的纯 C++ 方案与现状部分吻合，但夸大了进度。
- **代理尚未发布到 npm**。本地测试始终运行 `cd proxy && node index.js`。

## 依赖（自动下载，无需手动安装）

- `godot-cpp` — 仓库根目录的子模块
- `nlohmann/json` v3.11.3 — JSON 解析/序列化
- `cpp-httplib` v0.18.7 — HTTP 客户端（素材下载）
- `inih` r58 — INI 解析器（project.godot 文件）
- Windows 下：`ws2_32` 用于 TCP socket

## 资源（Resources）

- `godot://editor/context` — 在 `register_types.cpp` 中通过 `EditorContext::resource_read_handler` 注册
- 未来将添加：`godot://project/info`、`godot://scene/{path}`、`godot://script/{path}`、`godot://resource/{path}`

## 开发计划

见 `docs/` 目录下的路线图（Phase 1-5，约 10 周）。

查询可以使用codegraph mcp 记得经常 sync

代码参考：G:\Godot\godot-mcp\Doyunha-Gopeak，G:\Godot\godot-mcp\Gear-Godot-MCP。这个项目就是参考这两个项目的
