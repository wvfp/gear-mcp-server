## Gear MCP Server — 开发计划

---

### 当前状态（Phase 0 已完成）

已完成的工作：

- [x] CMake 项目骨架（`CMakeLists.txt` + `add_subdirectory(godot-cpp)`）
- [x] godot-cpp 源码作为 CMake 子依赖（跨平台）
- [x] GDExtension 入口（`register_types.cpp/h`，EDITOR 级别初始化）
- [x] 插件目录结构（`project/addons/gear_mcp/`）
- [x] `.gdextension` + `plugin.cfg`
- [x] 第三方依赖集成（FetchContent 自动下载）：
  - **nlohmann/json** v3.11.3 — 替代手写 JSON 解析/构建
  - **cpp-httplib** v0.18.7 — HTTP 客户端（素材下载）
  - **inih** r58 — INI 文件解析（project.godot, export_presets.cfg）
- [x] Visual Studio 18 2026 构建验证通过，DLL 输出正确

---

### Phase 1：基础设施 — 跑通第一个工具调用

**目标**：从 AI 客户端发一条 `tools/call`，经过 proxy → TCP → GDExtension → 工具执行 → 响应返回，全链路打通。

#### 1.1 MCP 服务器核心（server/）

从现有 C++ 项目（`godot-cpp-template-main/src/server/`）迁移并增强：

| 文件 | 来源 | 工作内容 |
|------|------|---------|
| `tcp_server.h/cpp` | 迁移 | 复用 Winsock2/POSIX 实现，增加优雅关闭、连接超时 |
| `tool_registry.h` | 迁移+增强 | 增加 `main_thread` 标记、工具分组（group）机制 |
| `mcp_server.h/cpp` | 重写 | 整合以上组件，管理生命周期 |
| `mcp_methods.h/cpp` | 重写 | 使用 nlohmann/json 处理 JSON-RPC，完整分发 initialize/tools/resources |
| `main_thread_queue.h/cpp` | 新建 | 线程安全队列 + condition_variable 同步 |

> **JSON 处理全部使用 nlohmann/json**，不再手写解析器。JSON-RPC 消息直接用 `nlohmann::json::parse()` 解析，响应直接用 `.dump()` 序列化。

**关键验收标准**：
- TCP 服务器在 127.0.0.1:8510 监听
- 能解析 `{"jsonrpc":"2.0","method":"initialize","params":{},"id":1}` 并返回正确响应
- `tools/list` 返回已注册工具列表
- `tools/call` 能分发到具体 handler

#### 1.2 GodotAPI 基础层（godot_api/）

| 文件 | 工作内容 |
|------|---------|
| `godot_api.h/cpp` | 单例模式，init 时解析 ~30 个 GDExtension 函数指针 |
| `editor_context.h/cpp` | 获取 EditorInterface、当前场景根、选中节点 |

从现有 C++ 项目的 `editor_context.cpp` 迁移，扩展为更完整的 API 面。

#### 1.3 共享工具（shared/）

| 文件 | 工作内容 |
|------|---------|
| `type_codec.h/cpp` | Variant ↔ JSON 双向转换，使用 `nlohmann::json` 作为中间格式（先支持 10 种基础类型） |
| `path_utils.h/cpp` | `res://` ↔ 绝对路径转换，路径遍历保护 |
| `config_parser.h/cpp` | 使用 **inih** 解析 project.godot / export_presets.cfg（替代手写 INI 解析） |
| `logger.h/cpp` | 统一日志（`LOG_INFO`, `LOG_ERROR` → `godot::UtilityFunctions::print`） |

> **不再需要手写的组件**：`json_builder.h`（用 nlohmann/json）、`json_rpc_parser.h`（用 nlohmann/json）、`config_parser` 的 INI 解析部分（用 inih）

#### 1.4 第一批工具

| 域 | 工具数 | 工具 | 说明 |
|----|--------|------|------|
| `file/` | 3 | read_file, write_file, list_dir | 从现有 C++ 项目迁移，不需要 Godot API |
| `scene/` | 3 | create_scene, list_scene_nodes, add_node | 原型级别，验证主线程队列 |

#### 1.5 Node.js Proxy

| 文件 | 工作内容 |
|------|---------|
| `proxy/index.js` | ~40 行 stdio ↔ TCP 桥接（设计文档已有完整代码） |
| `proxy/package.json` | npm 包配置 |

#### 1.6 GDExtension 生命周期集成

修改 `register_types.cpp`：

```cpp
void initialize_gear_mcp_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) return;
    
    GodotAPI::init(...);              // 解析函数指针
    MCPServer::instance().start();     // 启动 TCP :8510
    register_all_tools(registry);      // 注册工具
    // 注册 _process 回调驱动 MainThreadQueue
}
```

#### 1.7 Phase 1 验收测试

```bash
# 1. 编译 GDExtension
cmake --build build --config Debug

# 2. 用 Godot 编辑器打开 project/ 目录

# 3. 启动 proxy
cd proxy && node index.js

# 4. 手动发送 JSON-RPC 测试
echo '{"jsonrpc":"2.0","method":"initialize","params":{},"id":1}' | node proxy/index.js

# 5. 验证 tools/call read_file 能读取文件
# 6. 验证 tools/call create_scene 能在 Godot 项目中创建场景
```

**里程碑**：Claude Desktop 配置 `npx -y gear-mcp-proxy`，成功调用 `create_scene` 创建一个场景文件。

**预计工期**：2 周

---

### Phase 2：核心工具域

**目标**：覆盖设计文档中的核心组（~30 个工具），实现 Gear 原有功能的 80%。

#### 2.1 场景操作完善（scene/）— 10 工具

| 工具 | 优先级 | 复杂度 | 依赖 |
|------|--------|--------|------|
| `create_scene` | P0 | 中 | GodotAPI::create_node, save_scene |
| `save_scene` | P0 | 低 | GodotAPI::call_method_void |
| `list_scene_nodes` | P0 | 中 | 递归遍历场景树 |
| `add_node` | P0 | 高 | UndoRedo 集成 |
| `get_node_properties` | P0 | 中 | type_codec 全类型支持 |
| `set_node_properties` | P0 | 高 | UndoRedo + type_codec |
| `delete_node` | P1 | 中 | UndoRedo（需要保存父节点引用） |
| `duplicate_node` | P1 | 中 | 深拷贝节点树 |
| `reparent_node` | P1 | 高 | remove + add + owner 设置 |
| `load_sprite` | P2 | 中 | FileSystem dock 刷新 |

**UndoRedo 实现要点**：
- 每个写操作注册 `undo_redo_create_action` → `add_do_method` → `add_undo_method` → `commit_action`
- 用户在 Godot 编辑器中 Ctrl+Z 可撤销

#### 2.2 脚本操作（script/）— 3 工具

| 工具 | 工作内容 |
|------|---------|
| `create_script` | 创建 .gd 文件，可选附加到节点 |
| `modify_script` | 全文替换或追加 GDScript 代码 |
| `get_script_info` | 读取脚本内容、方法列表、信号列表 |

#### 2.3 ClassDB 查询（classdb/）— 3 工具

| 工具 | 工作内容 |
|------|---------|
| `query_classes` | 列出所有可用类（支持过滤） |
| `query_class_info` | 获取类属性、方法、信号 |
| `inspect_inheritance` | 查询继承链 |

#### 2.4 项目管理（project/）— 5 工具

| 工具 | 工作内容 |
|------|---------|
| `list_projects` | 扫描指定目录下的 Godot 项目 |
| `get_project_info` | 读取 project.godot 元数据 |
| `search_project` | 全文搜索项目文件 |
| `get_project_setting` | 读取 ProjectSettings |
| `set_project_setting` | 修改 ProjectSettings |

#### 2.5 编辑器控制（editor/）— 6 工具

| 工具 | 工作内容 |
|------|---------|
| `run_project` | EditorInterface::play_main_scene() |
| `stop_project` | EditorInterface::stop_playing_scene() |
| `get_debug_output` | 读取编辑器输出日志 |
| `get_editor_status` | 当前场景、运行状态、选中节点 |
| `get_godot_version` | Engine::get_version_info() |
| `launch_editor` | 打开指定场景 |

#### 2.6 资源操作（resource/）— 5 工具

| 工具 | 工作内容 |
|------|---------|
| `create_resource` | 创建任意 Resource 类型 |
| `modify_resource` | 修改 Resource 属性 |
| `create_material` | 快捷创建 StandardMaterial3D |
| `create_shader` | 创建 Shader + ShaderMaterial |
| `get_dependencies` | 查询资源依赖关系 |

#### 2.7 导出（export/）— 2 工具

| 工具 | 工作内容 |
|------|---------|
| `export_project` | 调用 EditorExportPlatform::export_project() |
| `list_export_presets` | 读取 export_presets.cfg |

#### 2.8 信号（signal/）— 3 工具

| 工具 | 工作内容 |
|------|---------|
| `connect_signal` | 连接信号到方法 |
| `disconnect_signal` | 断开信号 |
| `list_connections` | 列出信号连接 |

#### 2.9 Autoload（autoload/）— 4 工具

| 工具 | 工作内容 |
|------|---------|
| `add_autoload` | 修改 project.godot [autoload] 段 |
| `remove_autoload` | 删除 autoload 条目 |
| `list_autoloads` | 列出所有 autoload |
| `set_main_scene` | 修改 project.godot run/main_scene |

**里程碑**：在 Claude Desktop 中完成一个完整的场景搭建工作流（创建场景 → 添加节点 → 设置属性 → 连接信号 → 保存）。

**预计工期**：3 周

---

### Phase 3：扩展工具域

**目标**：覆盖设计文档中的扩展组（~80 个工具），达到功能完整。

#### 3.1 动画系统（animation/）— 5 工具

- `create_animation` — 创建 Animation 资源
- `add_animation_track` — 添加轨道（value/method/track 等类型）
- `create_animation_tree` — 创建 AnimationTree 节点
- `add_animation_state` — AnimationNodeStateMachine 添加状态
- `connect_animation_states` — 状态机过渡连接

**复杂度**：高。AnimationPlayer/AnimationTree 的 API 层次深，需要处理轨道索引、关键帧插值等。

#### 3.2 音频（audio/）— 4 工具

- `create_audio_bus` — AudioServer::add_bus()
- `get_audio_buses` — 列出所有总线
- `set_audio_bus_effect` — 添加/替换 AudioEffect
- `set_audio_bus_volume` — 调整音量

#### 3.3 TileMap（tilemap/）— 2 工具

- `create_tileset` — 创建 TileSet + atlas source
- `set_tilemap_cells` — 批量设置 tile 坐标

**复杂度**：中。Godot 4.x 的 TileMap 系统比 3.x 复杂得多。

#### 3.4 导航（navigation/）— 2 工具

- `create_navigation_region` — NavigationRegion2D/3D
- `create_navigation_agent` — NavigationAgent2D/3D

#### 3.5 主题/UI（theme/）— 3 工具

- `set_theme_color` — Theme::set_color()
- `set_theme_font_size` — Theme::set_font_size()
- `apply_theme_shader` — 为主题控件应用 Shader

#### 3.6 插件管理（plugin/）— 3 工具

- `list_plugins` — 扫描 addons/ 目录
- `enable_plugin` — EditorInterface::set_plugin_enabled()
- `disable_plugin` — 同上

#### 3.7 输入映射（input/）— 1 工具

- `add_input_action` — 修改 project.godot [input] 段

#### 3.8 UID（uid/）— 2 工具

- `get_uid` — ResourceUID::get_id_for_path()
- `update_project_uids` — 扫描并刷新 UID

#### 3.9 导入管线（import/）— 5 工具

- `get_import_status` — 检查 .import 文件状态
- `get_import_options` — 读取导入选项
- `set_import_options` — 修改导入选项并重新导入
- `reimport_resource` — EditorFileSystem::reimport_files()
- `validate_project` — 检查丢失引用、缺失文件

#### 3.10 运行时检查（runtime/）— 4 工具

- `inspect_runtime_tree` — 通过 TCP:7777 获取运行时 SceneTree
- `set_runtime_property` — 修改运行时属性
- `call_runtime_method` — 调用运行时方法
- `get_runtime_metrics` — FPS、内存、节点数

**需要额外组件**：runtime autoload 脚本（注入到运行中的游戏进程）

**里程碑**：工具总数达到 80+，覆盖 Godot 编辑器大部分操作。

**预计工期**：2 周

---

### Phase 4：高级功能

**目标**：实现差异化功能，超越 Gear 原有能力。

#### 4.1 调试工具（debug/）— 9 工具

**LSP 客户端**（连接 Godot 内置语言服务器 :6005）：

| 工具 | 协议 |
|------|------|
| `lsp_get_diagnostics` | textDocument/publishDiagnostics |
| `lsp_get_completions` | textDocument/completion |
| `lsp_get_hover` | textDocument/hover |
| `lsp_get_symbols` | textDocument/documentSymbol |

**DAP 客户端**（连接 Godot 调试适配器 :6006）：

| 工具 | 协议 |
|------|------|
| `dap_set_breakpoint` | setBreakpoints |
| `dap_remove_breakpoint` | setBreakpoints (clear) |
| `dap_continue` | continue |
| `dap_pause` | pause |
| `dap_step_over` | next |
| `dap_get_stack_trace` | stackTrace |

**实现要点**：
- 新建 `shared/lsp_client.h/cpp`（HTTP-style LSP 帧：Content-Length 头）
- 新建 `shared/dap_client.h/cpp`（DAP 帧格式）
- 复用 TCP 基础设施

#### 4.2 CC0 素材（assets/）— 3 工具

- `search_assets` — 搜索 Poly Haven / AmbientCG / Kenney
- `fetch_asset` — 下载并导入到项目
- `list_asset_providers` — 列出可用素材源

**实现要点**：
- 使用 **cpp-httplib**（已集成），`httplib::Client cli("https://api.polyhaven.com")` 直接发起请求
- 下载后通过 `GodotAPI::refresh_filesystem()` 触发导入

#### 4.3 测试/截图（testing/）— 6 工具

- `capture_screenshot` — Viewport::get_texture() → Image::save_png()
- `capture_viewport` — 截取指定 Viewport
- `inject_action` — Input::action_press/release
- `inject_key` — InputEventKey
- `inject_mouse_click` — InputEventMouseButton
- `inject_mouse_motion` — InputEventMouseMotion

#### 4.4 开发者体验（dx/）— 4 工具

- `parse_error_log` — 解析编辑器输出中的错误/警告
- `get_project_health` — 项目健康检查（丢失引用、未使用资源、性能问题）
- `find_resource_usages` — 查找资源引用位置
- `scaffold_gameplay_prototype` — 快速搭建游戏原型（自动创建场景+脚本+信号连接）

#### 4.5 意图追踪（intent/）— 9 工具

纯数据工具，操作 JSON 文件存储，不需要 Godot API：

- `capture_intent_snapshot` — 保存当前意图
- `record_decision_log` — 记录决策
- `generate_handoff_brief` — 生成交接摘要
- `summarize_intent_context` — 总结上下文
- `record_work_step` — 记录工作步骤
- `record_execution_trace` — 记录执行轨迹
- `export_handoff_pack` — 导出交接包
- `set_recording_mode` / `get_recording_mode` — 切换录制模式

#### 4.6 元工具（meta/）— 2 工具

- `tool_catalog` — 搜索和激活工具组
- `manage_tool_groups` — 管理工具分组

#### 4.7 MCP Resources + Prompts

实现 MCP 协议的资源和建议功能：

**Resources**（5 个）：
- `godot://editor/context` — 编辑器上下文快照
- `godot://project/info` — 项目元数据
- `godot://scene/{path}` — 场景文件内容
- `godot://script/{path}` — 脚本文件内容
- `godot://resource/{path}` — 资源文件内容

**Prompts**（2 个）：
- `godot.scene_bootstrap` — 场景引导工作流模板
- `godot.debug_triage` — 调试分诊循环模板

**里程碑**：在 AI 客户端中使用 LSP 诊断修复 GDScript 错误，使用素材工具下载并导入 3D 模型。

**预计工期**：2 周

---

### Phase 5：发布准备

**目标**：跨平台发布，生产可用。

#### 5.1 跨平台 CI/CD

GitHub Actions 工作流：

| 平台 | 构建环境 | 输出 |
|------|---------|------|
| Windows x86_64 | windows-latest (MSVC) | `.dll` |
| Linux x86_64 | ubuntu-latest (GCC) | `.so` |
| macOS universal | macos-latest (Apple Clang) | `.dylib` (framework) |

```yaml
# .github/workflows/build.yml
name: Build GDExtension
on: [push, pull_request]
jobs:
  build:
    strategy:
      matrix:
        include:
          - os: windows-latest
            artifact: gear_mcp_server.windows.editor.x86_64.dll
          - os: ubuntu-latest
            artifact: gear_mcp_server.linux.editor.x86_64.so
          - os: macos-latest
            artifact: gear_mcp_server.macos.editor.universal.framework
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build --config Release
      - name: Upload
        uses: actions/upload-artifact@v4
        with:
          name: ${{ matrix.os }}
          path: project/addons/gear_mcp/bin/
```

#### 5.2 发布产物打包

```
gear-mcp-server-v2.0.0/
├── addons/
│   └── gear_mcp/
│       ├── plugin.cfg
│       ├── gear_mcp.gdextension
│       └── bin/
│           ├── gear_mcp_server.windows.editor.x86_64.dll
│           ├── gear_mcp_server.linux.editor.x86_64.so
│           └── gear_mcp_server.macos.editor.universal.framework
└── README.md
```

发布到 GitHub Releases + Godot Asset Library。

#### 5.3 npm 发布

```bash
cd proxy
npm publish  # gear-mcp-proxy
```

#### 5.4 文档

- README.md（安装步骤、配置说明、工具列表）
- CONTRIBUTING.md（构建指南、工具开发模板）
- 用户文档（MCP 客户端配置、工具使用示例）

#### 5.5 测试

- 单元测试：type_codec、json_builder、path_utils（纯函数，可用 Catch2/Google Test）
- 集成测试：启动 GDExtension → 连接 proxy → 调用每个工具验证基本功能
- 回归测试：用 AI 客户端完成典型工作流（创建项目 → 搭建场景 → 运行）

**里程碑**：GitHub Release 发布，用户可以在 5 分钟内完成安装和首次工具调用。

**预计工期**：1 周

---

### 文件数量与代码量估算

| 阶段 | 新增文件 | 新增代码行 | 累计代码行 |
|------|---------|-----------|-----------|
| Phase 0（已完成） | 5 + CMake 依赖配置 | ~120 | ~120 |
| Phase 1 | ~12 | ~2,000 | ~2,120 |
| Phase 2 | ~40 | ~5,000 | ~7,120 |
| Phase 3 | ~35 | ~4,000 | ~11,120 |
| Phase 4 | ~30 | ~3,000 | ~14,120 |
| Phase 5 | ~5 | ~200 | ~14,320 |
| **合计** | **~127** | **~14,320** | — |

> 引入 nlohmann/json + cpp-httplib + inih 后，手写代码量减少约 1,000 行（主要是 JSON 解析/构建、HTTP 客户端、INI 解析），同时代码质量和健壮性大幅提升。

---

### 依赖关系图

```
Phase 0 ──────────────────────────────────────────────────────────
  │  CMake 骨架（已完成）
  ▼
Phase 1 ──────────────────────────────────────────────────────────
  │
  ├── server/ (TCP + ToolRegistry)     ←── 无依赖，可独立开发（JSON 用 nlohmann/json）
  ├── godot_api/ (GodotAPI + EditorContext) ←── 无依赖
  ├── shared/ (type_codec + path_utils)    ←── 依赖 godot_api/ + nlohmann/json + inih
  ├── main_thread_queue                     ←── 无依赖
  ├── tools/file/ (3 工具)                  ←── 依赖 shared/
  ├── tools/scene/ (3 工具原型)             ←── 依赖 godot_api/ + shared/ + main_thread_queue
  ├── proxy/ (Node.js)                      ←── 无依赖，可与 C++ 并行开发
  │
  │  ★ 全链路验收：AI 客户端 → proxy → TCP → GDExtension → 工具 → 响应
  ▼
Phase 2 ──────────────────────────────────────────────────────────
  │
  ├── scene/ 完善 (10 工具)     ←── 依赖 Phase 1 基础
  ├── script/ (3 工具)          ←── 依赖 file/ 工具
  ├── classdb/ (3 工具)         ←── 依赖 godot_api/
  ├── project/ (5 工具)         ←── 依赖 shared/config_parser
  ├── editor/ (6 工具)          ←── 依赖 godot_api/
  ├── resource/ (5 工具)        ←── 依赖 godot_api/ + type_codec
  ├── export/ (2 工具)          ←── 依赖 godot_api/
  ├── signal/ (3 工具)          ←── 依赖 godot_api/
  ├── autoload/ (4 工具)        ←── 依赖 shared/config_parser
  │
  ▼
Phase 3 ──────────────────────────────────────────────────────────
  │
  ├── animation/ (5 工具)       ←── 依赖 Phase 2 scene/ + resource/
  ├── audio/ (4 工具)           ←── 依赖 godot_api/
  ├── tilemap/ (2 工具)         ←── 依赖 godot_api/
  ├── navigation/ (2 工具)      ←── 依赖 godot_api/
  ├── theme/ (3 工具)           ←── 依赖 godot_api/ + resource/
  ├── plugin/ (3 工具)          ←── 依赖 godot_api/
  ├── input/ (1 工具)           ←── 依赖 shared/config_parser
  ├── uid/ (2 工具)             ←── 依赖 godot_api/
  ├── import/ (5 工具)          ←── 依赖 godot_api/
  ├── runtime/ (4 工具)         ←── 需要额外 autoload 组件
  │
  ▼
Phase 4 ──────────────────────────────────────────────────────────
  │
  ├── debug/ (9 工具)           ←── 需要新建 lsp_client + dap_client（TCP 客户端）
  ├── assets/ (3 工具)          ←── 使用 cpp-httplib（已集成）
  ├── testing/ (6 工具)         ←── 依赖 godot_api/ + runtime/
  ├── dx/ (4 工具)              ←── 依赖 debug/ + import/
  ├── intent/ (9 工具)          ←── 纯数据，低依赖
  ├── meta/ (2 工具)            ←── 依赖 tool_registry 分组机制
  ├── MCP Resources + Prompts   ←── 依赖 mcp_methods/
  │
  ▼
Phase 5 ──────────────────────────────────────────────────────────
  │
  ├── CI/CD (GitHub Actions)
  ├── 发布打包
  ├── npm publish (gear-mcp-proxy)
  ├── 文档
  └── 测试
```

---

### 每个工具的开发模板

新增一个工具只需要 3 步：

**Step 1**：在 `tools/{domain}/` 下创建 handler 文件

```cpp
// tools/scene/my_new_tool.cpp
#include "godot_api/godot_api.h"
#include "shared/json_builder.h"

static const char *MY_TOOL_SCHEMA = R"schema({
    "type": "object",
    "properties": {
        "param1": {"type": "string", "description": "..."}
    },
    "required": ["param1"]
})schema";

static void handle_my_tool(const std::string &p_params, std::string &r_result, std::string &r_error) {
    // 1. 解析参数
    // 2. 调用 GodotAPI
    // 3. 构建 JSON 响应
}
```

**Step 2**：在 `register_{domain}.cpp` 中注册

```cpp
p_reg->register_tool("my_tool", "Description", MY_TOOL_SCHEMA, handle_my_tool, /*main_thread=*/true);
```

**Step 3**：编译测试

```bash
cmake --build build --config Debug
# 在 AI 客户端中调用 my_tool 验证
```

---

### 风险与应对

| 风险 | 概率 | 影响 | 应对 |
|------|------|------|------|
| GDExtension C API 某些编辑器功能不可用 | 中 | 部分工具无法实现 | Phase 1 先验证关键 API；不可行时回退到 GDScript autoload 辅助 |
| macOS 构建环境缺乏 | 低 | 延迟发布 | 使用 GitHub Actions macos-latest runner |
| 运行时通信 autoload 注入困难 | 中 | runtime 工具不可用 | 先跳过 runtime/，优先完成其他工具域 |
| LSP/DAP 协议实现复杂 | 高 | debug 工具延期 | 可先实现 LSP 只读功能（diagnostics/hover），DAP 后续补充 |
| type_codec 类型覆盖不全 | 中 | 某些属性无法序列化 | 逐步扩展，nlohmann/json 让类型映射更容易实现 |

---

### 时间线总览

```
Week 1-2   ████████░░░░░░░░░░░░  Phase 1: 基础框架 + 3 个工具跑通
Week 3-5   ░░░░░░░░████████████  Phase 2: 核心 41 个工具
Week 6-7   ░░░░░░░░░░░░░░░░████  Phase 3: 扩展 31 个工具
Week 8-9   ░░░░░░░░░░░░░░░░░░░░  Phase 4: 高级功能 33 个工具（可与 Phase 3 部分并行）
Week 10    ░░░░░░░░░░░░░░░░░░░░  Phase 5: CI/CD + 发布
```

总计约 **10 周**，产出 **~14,000 行 C++** + **~40 行 Node.js**，覆盖 **110+ 工具**。

第三方依赖（自动下载，无需手动管理）：nlohmann/json、cpp-httplib、inih、godot-cpp。
