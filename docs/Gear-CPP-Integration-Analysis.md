## Gear + C++ GDExtension 集成分析

---

### 一、两个项目的架构差异

Gear 和这个 C++ GDExtension 项目代表了两种完全不同的思路。

**Gear** 的架构是"外部进程 + 脚本桥接"：一个 Node.js 进程运行 MCP 服务器，通过 WebSocket（端口 6505）连接 Godot 编辑器里的 GDScript 插件，再由插件调用 Godot API 执行操作。中间还有 LSP（端口 6005）、DAP（端口 6006）、Runtime TCP（端口 7777）三个额外通道。安装需要 Node.js 18+、npm 配置、3 个 GDScript 插件分别启用。

**C++ GDExtension** 的架构是"进程内嵌"：MCP 服务器直接编译为 C++ 动态库，作为 GDExtension 加载进 Godot 编辑器进程内部。通过 GDExtension C API 直接调用 EditorInterface、场景树、Variant 系统等所有编辑器能力，对外暴露一个 TCP 端口（8510）接收 JSON-RPC 请求。安装只需放一个 .gdextension 文件 + 编译好的 DLL/SO/DYLIB，零配置。

核心差异一览：

| 维度 | Gear | C++ GDExtension |
|------|------|-----------------|
| MCP 服务器位置 | 外部 Node.js 进程 | Godot 进程内部 |
| 与编辑器的通信 | WebSocket + 3 个 TCP 通道 | 无需通信（同进程） |
| 编辑器 API 访问 | 间接（GDScript 插件转发） | 直接（GDExtension C API） |
| Godot 侧插件 | 3 个 GDScript EditorPlugin | 1 个 GDExtension（自动加载） |
| 外部依赖 | Node.js 18+, npm, ws, axios | 无（纯 C++ 标准库 + 平台 socket） |
| 类型序列化 | JSON 往返 + 3 份 _parse_value | 可直接操作 Variant（无需序列化） |
| Undo/Redo | 不支持 | 可直接调用 UndoRedo API |
| 安装步骤 | npm install + 配置 MCP + 启用 3 插件 | 放文件到 addons/ 目录 |
| 内置 UI | 无 | 完整聊天面板 + Markdown 渲染 + 会话管理 |

---

### 二、C++ GDExtension 的关键优势

#### 2.1 零桥接延迟

Gear 的每个工具调用都要经历：Node.js → JSON 序列化 → WebSocket 发送 → GDScript 接收 → JSON 反序列化 → 执行操作 → 结果 JSON 序列化 → WebSocket 返回 → Node.js 反序列化。C++ GDExtension 没有这层通信开销，工具函数直接调用 Godot API，零序列化开销。

#### 2.2 直接访问 EditorInterface

editor_context.cpp 已经演示了通过 GDExtension C API 直接调用 `EditorInterface.get_edited_scene_root()`、`EditorSelection.get_selected_nodes()` 等方法。这意味着所有 Gear 的 scene_tools.gd（762 行）、resource_tools.gd（426 行）、animation_tools.gd（503 行）都可以用 C++ 重写，并且可以直接操作 Node 对象、Resource 对象，不需要任何"加载场景 → 修改 → 保存"的间接模式。

#### 2.3 原生 UndoRedo 支持

GDExtension 可以直接获取 `EditorInterface.get_editor_undo_redo()`，为每个操作注册 Undo/Redo 动作。这是 GDScript 插件同样可以做到但目前没有做的——而 C++ 方案中实现它更自然，因为可以直接操作 UndoRedo 的 C API。

#### 2.4 安装体验

用户只需要：
1. 把 `addons/godot-mcp-server/` 目录复制到 Godot 项目
2. 打开编辑器，GDExtension 自动加载
3. MCP 服务器自动在 8510 端口启动

不需要安装 Node.js，不需要 `npm install`，不需要在编辑器里手动启用 3 个插件，不需要配置环境变量。

#### 2.5 内置聊天 UI

C++ 项目有一套完整的编辑器内聊天界面（ChatPanel 停靠面板、消息气泡、Markdown 渲染器、流式文本标签、会话侧边栏、OpenCode 启动器）。Gear 没有任何内嵌 UI，用户只能通过外部 MCP 客户端交互。

#### 2.6 ToolRegistry 设计

C++ 的 ToolRegistry 非常干净：`register_tool(name, description, schema, handler)` 一行注册，handler 是简单函数指针 `void(const string&, string&, string&)`。比 Gear 的 130-case switch + 动态分组 + compact 别名系统简单一个数量级。

---

### 三、C++ GDExtension 的局限

#### 3.1 缺少 MCP stdio 传输

目前 C++ 项目只有 TCP 传输（端口 8510）。但主流 MCP 客户端（Claude Desktop、Cursor、Cline）期望通过 stdio 与 MCP 服务器通信。不解决这个问题，AI 客户端无法直接连接。

#### 3.2 工具数量

目前只有 3 个工具（read_file、write_file、list_directory）+ 1 个资源（editor context）。Gear 有 110+ 个工具。要追平 Gear 的功能集需要大量 C++ 开发工作。

#### 3.3 跨平台编译

C++ GDExtension 需要为 Windows（DLL）、Linux（SO）、macOS（DYLIB）分别编译。Gear 的 Node.js 方案天然跨平台。每次发布需要 CI/CD 流水线产出多平台二进制。

#### 3.4 开发迭代速度

TypeScript 改完即生效，C++ 需要重新编译。对于工具定义调整、参数 schema 修改等频繁变动的部分，C++ 的开发体验不如 TypeScript。

#### 3.5 JSON 手写解析

C++ 项目使用手写 JSON 解析器（无第三方依赖），目前能工作但扩展性受限。随着工具数量增加和参数复杂度提升，手写解析器的维护成本会上升。

---

### 四、集成方案

有三种可行的集成路径，从保守到激进排列。

#### 方案 A：C++ 插件 + 瘦 Node.js 代理（推荐）

这是最务实的方案，结合了两者优势。

```
AI 客户端 (Claude/Cursor/Cline)
    │ MCP over stdio
    ▼
Node.js 瘦代理 (~200 行)
    │ TCP JSON-RPC
    ▼
C++ GDExtension (运行在 Godot 进程内)
    │ GDExtension C API
    ▼
Godot Editor
```

**Node.js 瘦代理** 只做一件事：把 MCP stdio 协议翻译成 TCP JSON-RPC 发给 C++ 扩展。大约 200 行代码，无业务逻辑，无工具定义，无 handler。

**C++ GDExtension** 承担所有实际工作：MCP 方法处理、工具注册与执行、编辑器 API 调用、资源管理。所有 110+ 工具都在 C++ 中实现，直接调用 Godot API。

用户配置：
```json
{
  "mcpServers": {
    "godot": {
      "command": "npx",
      "args": ["-y", "gear-bridge"]
    }
  }
}
```

**优势**：
- 保留 MCP stdio 生态兼容性（所有 AI 客户端都能用）
- 消除 Gear 的 4 通道通信架构（只剩 1 个 TCP 连接）
- 消除 3 个 GDScript 插件（换成 1 个 GDExtension）
- 消除所有类型序列化问题（C++ 直接操作 Variant）
- Node.js 从 249KB 单体缩减为 200 行代理
- 保留 TypeScript 开发体验（仅用于瘦代理）

**风险**：
- 需要跨平台编译 C++ GDExtension
- 110+ 工具的 C++ 移植工作量大
- 手写 JSON 解析器需要增强

#### 方案 B：C++ 插件 + Node.js 混合分工

保持 Node.js 处理部分工具（文件操作、进程管理、素材下载等不需要编辑器 API 的工具），C++ 处理编辑器相关工具（场景、资源、动画、调试等）。

```
AI 客户端
    │ MCP over stdio
    ▼
Node.js 服务器 (保留部分工具)
    │ TCP JSON-RPC (仅编辑器工具)
    ▼
C++ GDExtension (编辑器工具 + 编辑器 API)
    │ GDExtension C API
    ▼
Godot Editor
```

**优势**：
- 文件/进程/素材工具保留 TypeScript（开发快、跨平台无忧）
- 编辑器工具用 C++（直接 API 访问、无序列化）
- 渐进式迁移，不需要一次性移植所有工具

**劣势**：
- Node.js 仍然是"胖服务器"，保留了大部分复杂度
- 两个代码库需要维护
- 工具分发逻辑分散在两端

#### 方案 C：纯 C++ 方案（长期目标）

C++ GDExtension 直接实现 MCP stdio 传输，完全消除 Node.js 依赖。

```
AI 客户端
    │ MCP over stdio
    ▼
C++ GDExtension (MCP stdio + 所有工具)
    │ GDExtension C API
    ▼
Godot Editor
```

**实现方式**：GDExtension 在初始化时 fork 一个子进程（或创建命名管道），子进程负责 stdio 读写，通过管道/共享内存与主进程通信。或者让 MCP 客户端直接通过 TCP 连接（需要客户端支持 TCP 传输，目前只有少数支持）。

**优势**：
- 最简单的部署（零外部依赖）
- 最快的执行速度
- 单一代码库

**劣势**：
- stdio 传输实现工作量大
- 跨平台编译和进程管理复杂度
- 目前 MCP 生态以 stdio 为主，纯 TCP 方案兼容性受限

---

### 五、推荐迁移路径（基于方案 A）

#### 阶段 1：基础设施搭建（1-2 周）

1. **扩展 C++ ToolRegistry**：添加场景操作工具的原型实现
   - 实现 `create_scene`、`add_node`、`get_node_properties`、`set_node_properties` 4 个核心场景工具
   - 通过 GDExtension C API 直接操作 PackedScene、Node、Resource
   - 验证"直接 API 访问"模式的可行性

2. **实现 stdio 代理**：编写 Node.js 瘦代理
   - 连接 MCP stdio ↔ TCP 8510
   - 处理 initialize、tools/list、tools/call、resources/* 方法
   - 约 200 行代码

3. **增强 JSON 解析器**：为 C++ 项目添加嵌套对象/数组提取能力
   - 当前 extract_param_string 只支持顶层键
   - 工具参数常有嵌套结构（如 properties: { "position": { "type": "Vector2", ... } }）

#### 阶段 2：编辑器工具移植（2-3 周）

按领域分批移植 Gear 的 GDScript 工具到 C++：

**批次 1 — 场景操作**（替代 scene_tools.gd 的 762 行）：
- create_scene, list_scene_nodes, add_node, delete_node
- duplicate_node, reparent_node, get/set_node_properties
- save_scene, load_sprite, connect/disconnect_signal, list_connections
- 关键改进：直接操作 Node 对象而非"加载 → 修改 → 保存"模式

**批次 2 — 资源操作**（替代 resource_tools.gd 的 426 行）：
- create_resource, modify_resource, create_material, create_shader
- create_tileset, set_tilemap_cells, set_theme_color/font_size/shader
- 关键改进：直接操作 Resource 对象，无需 JSON 类型转换

**批次 3 — 动画/导航**（替代 animation_tools.gd 的 503 行）：
- create_animation, add_animation_track, create_animation_tree
- add_animation_state, connect_animation_states
- create_navigation_region, create_navigation_agent

**批次 4 — 编辑器控制**：
- launch_editor, run_project, stop_project（通过 OS.execute / CreateProcess）
- get_project_info, get/set_project_setting（直接读 project.godot 或通过 ProjectSettings API）
- get_godot_version, get_editor_status

#### 阶段 3：非编辑器工具保留在 Node.js（1 周）

以下工具不需要编辑器 API，保留在 Node.js 瘦代理中直接处理（不转发给 C++）：

- **文件系统工具**：search_project, list_projects（已有 C++ 实现可复用）
- **GDScript 工具**：create_script, modify_script, get_script_info（纯文件操作）
- **ClassDB 查询**：query_classes, query_class_info（解析 extension_api.json）
- **导出工具**：export_project, list_export_presets（调用 Godot CLI）
- **素材工具**：search_assets, fetch_asset（HTTP 请求 Poly Haven / AmbientCG / Kenney）
- **意图追踪**：capture_intent_snapshot, record_decision_log 等（纯数据管理）
- **项目可视化**：map_project（HTTP 服务器 + WebSocket）

这样 Node.js 代理不是纯粹的"透明代理"，而是一个"混合代理"：编辑器工具转发给 C++，非编辑器工具自己处理。代码量从 249KB 缩减到大约 30-40KB。

#### 阶段 4：收尾（1 周）

- 添加 UndoRedo 支持到场景操作工具
- 集成 C++ 项目的聊天 UI 到 Gear
- 更新安装脚本和文档
- 跨平台 CI/CD 流水线
- 回归测试

---

### 六、从 C++ 项目中可直接复用的组件

| 组件 | 文件 | 复用方式 |
|------|------|---------|
| TCP 服务器 | tcp_server.cpp/h | 直接使用，平台抽象已完善 |
| JSON-RPC 解析器 | json_rpc_parser.cpp/h | 直接使用，需增强嵌套支持 |
| ToolRegistry | tool_registry.cpp/h | 直接使用，线程安全 |
| MCP 方法分发 | mcp_methods.cpp/h | 直接使用，已实现 tools/list、tools/call、resources/* |
| EditorContext | editor_context.cpp/h | 直接使用，GDExtension API 解析模式是最佳实践 |
| 聊天 UI | ui/ 整个目录 | 直接使用或作为参考 |
| 工具注册模式 | register_tools.cpp | 参考其模式添加新工具领域 |
| 文件工具 | tools/file/ | 直接使用（read_file, write_file, list_dir） |

---

### 七、最终架构对比

```
Gear 现有架构（改造前）             Gear + C++ GDExtension（改造后）
─────────────────────────          ─────────────────────────

AI Client                          AI Client
    │ stdio                            │ stdio
    ▼                                  ▼
Node.js (249KB 单体)               Node.js (~30-40KB)
    │                                  │
    ├── WebSocket :6505 ──┐             ├── TCP :8510 ──────────┐
    ├── TCP :6005 (LSP) ──┤             │                       │
    ├── TCP :6006 (DAP) ──┤             │   C++ GDExtension     │
    ├── TCP :7777 (RT) ───┤             │   (Godot 进程内)      │
    │                     │             │                       │
    ▼                     ▼             │   ┌─ ToolRegistry ──┐ │
~90 handle 方法      3 个 GDScript     │   │ 110+ 工具        │ │
(130-case switch)    插件              │   │ 直接 API 访问    │ │
                     (2900 行)         │   │ UndoRedo 支持    │ │
                                       │   └──────────────────┘ │
                                       │   ┌─ 聊天 UI ────────┐ │
                                       │   │ ChatPanel         │ │
                                       │   │ Markdown 渲染     │ │
                                       │   │ 会话管理          │ │
                                       │   └──────────────────┘ │
                                       │                        │
                                       └────────────────────────┘

端口数: 4                           端口数: 1
插件数: 3 (手动启用)                插件数: 1 (自动加载)
外部依赖: Node.js + npm             外部依赖: Node.js (瘦代理)
最大文件: 249KB                     最大文件: ~15KB (各 handler)
类型序列化: 3 份重复实现             类型序列化: 无 (直接 Variant)
```

---

### 八、决策建议

**如果目标是快速改善 Gear 的安装体验和编辑器集成**，方案 A（C++ 插件 + 瘦 Node.js 代理）是最佳路径。它用最小的架构变动解决了 Gear 最大的三个痛点（3 插件安装、WebSocket 桥接复杂度、类型序列化混乱），同时保留了 MCP 生态兼容性。

**如果目标是做一个全新的、更简洁的 MCP 方案**，可以考虑方案 C（纯 C++），但需要投入更多时间实现 stdio 传输和跨平台 CI。

**无论选哪条路**，C++ 项目中的 EditorContext（GDExtension API 解析模式）、ToolRegistry（注册制分发）、TCPServer（平台抽象）都是可以直接复用的优质基础设施代码。
