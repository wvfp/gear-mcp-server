## Gear Godot MCP — 架构重设计方案

---

### 一、现状诊断

对现有代码做了逐文件、逐方法的深入分析后，梳理出以下核心问题。

#### 1.1 Godot 插件层（GDScript，~2900 行）

**三插件割裂**：`godot_mcp_editor`、`godot_mcp_runtime`、`auto_reload` 各自独立，安装时需分别启用，但三者之间存在隐式行为耦合——editor 插件写文件到磁盘，auto_reload 轮询检测变化后刷新编辑器，runtime 插件只管注册 autoload。三个插件没有共享任何工具模块，导致大量代码重复。

**严重的代码重复**：`_ensure_res_path` 在 scene_tools、resource_tools、animation_tools 中有 3 种不同实现（最简单的只加 `res://` 前缀，最复杂的还处理绝对路径）。`_parse_value`（JSON 到 Godot 类型转换）同样有 3 份拷贝，分别支持 10 种、7 种、3 种类型。这意味着同一个 Vector2i 值传给动画工具会解析失败，传给场景工具却正常。

**类型序列化不一致**：editor 侧用 `"type"` 作为类型标签键名，runtime 侧用 `"_type"`。editor 侧兼容两种输入，但 runtime 侧只认 `"_type"`，跨通道传递数据时可能出错。

**EditorInterface 访问方式混乱**：scene_tools 通过 `_editor_plugin.get_editor_interface()` 获取，resource_tools 和 animation_tools 直接用 `EditorInterface` 静态单例。前一种在 plugin 引用为 null 时静默失败。

**保存模式不一致**：scene_tools 的 `_save_scene` 返回 Dictionary（空 = 成功），resource_tools 的 `_save_scene_root` 返回 int 错误码。

**无 Undo/Redo 支持**：所有工具操作都是"加载 → 修改 → 保存"的直接操作，Ctrl+Z 无法撤销任何 MCP 操作。

**auto_reload 只监视当前场景**：如果 MCP 修改了非当前编辑的 `.tres` 或 `.tscn` 文件，不会触发刷新。

#### 1.2 通信层（TypeScript，4 个通道）

**Runtime TCP 每次请求新建连接**：`handleRuntimeCommand` 每次调用都新建一个 TCP socket 到 7777 端口，发完命令立刻断开。无法接收推送事件，且有 TCP 握手延迟。

**Bridge 保活检测缺失**：WebSocket 桥接每 10 秒发 ping，记录了 `lastPongAt` 但从不检查是否超时。编辑器卡死时，工具调用会一直挂到 30 秒超时。

**LSP/DAP 无重连机制**：连接断开后只在下次调用时尝试一次连接，没有重试逻辑。编辑器重启后第一次 LSP/DAP 调用必然失败。

**LSP/DAP 帧解析代码重复**：两个客户端的 `frameMessage()` 和 `parseMessages()` 几乎一模一样。

**POST /mcp 端点是空壳**：HTTP 端点只实现了 `initialize` 方法，其他请求返回 400。这是无用的复杂度。

#### 1.3 TypeScript 服务器层

**249KB 巨型单体 index.ts**：包含 130 个 case 的 switch 分发、~90 个 handle 方法、参数标准化、分组管理、进程生命周期、意图追踪、配置解析等所有职责。添加一个工具需要改 3 处。

**半途而废的重构**：`server/` 目录下有 7 个提取出来的模块（lifecycle、param-mapper、resilience、semantic-search 等），但大部分从未被 index.ts 实际使用。参数标准化在 3 个地方存在，进程管理在 2 个地方存在，`createErrorResponse` 在 2 个地方存在。这比没有提取更糟——让人困惑哪份是权威的。

**死代码**：`tools.ts`（Priority 1 处理器）从未被引用。`gdscript_utils.ts` 和 `gdscript_parser.ts` 功能重叠。`server/lifecycle.ts`、`server/resilience.ts`、`server/param-mapper.ts` 都是写了没用上的。

---

### 二、新架构总览

#### 2.1 设计原则

针对 Godot 4.4+ 单一目标版本，遵循以下原则重新设计：

- **单一插件**：所有 Godot 侧功能合并为一个 EditorPlugin，包括 autoload 注册和文件监视
- **统一通信**：WebSocket 单通道替代 4 个独立端口，LSP/DAP 走 Godot 内置接口而非独立 TCP
- **领域驱动拆分**：TypeScript 侧按领域拆分为独立模块，消灭 249KB 单体
- **注册制分发**：用 Handler Registry 替代 130-case switch
- **消除重复**：GDScript 侧提取共享基础类，TypeScript 侧完成 server/ 模块的实际接入

#### 2.2 架构对比图

```
旧架构                                    新架构
─────────────────────────                 ─────────────────────────

MCP Client (stdio)                        MCP Client (stdio)
    │                                         │
    ▼                                         ▼
┌──────────────────────┐               ┌──────────────────────┐
│ index.ts (249KB 单体) │               │ Server (薄入口)       │
│  130-case switch      │               │  ├─ handlers/        │
│  ~90 handle 方法      │               │  │  ├─ project.ts    │
│  参数标准化 ×3        │               │  │  ├─ scene.ts      │
│  进程管理 (内联)      │               │  │  ├─ script.ts     │
└──────────┬───────────┘               │  │  ├─ debug.ts      │
           │                            │  │  ├─ runtime.ts    │
     ┌─────┼─────┬──────┐              │  │  ├─ assets.ts     │
     │     │     │      │              │  │  └─ ...           │
     ▼     ▼     ▼      ▼              │  ├─ bridge/          │
  Bridge  LSP   DAP  Runtime           │  │  └─ connection.ts │
  :6505  :6005 :6006  :7777            │  ├─ registry.ts      │
     │     │     │      │              │  └─ config/          │
     ▼     ▼     ▼      ▼              └──────────┬───────────┘
  4个独立TCP通道                                    │
     │                                         单一 WebSocket
     ▼                                         (统一协议)
┌──────────────────────┐               ┌──────────────────────┐
│ Godot Editor          │               │ Godot Editor          │
│                       │               │                       │
│ ┌─editor (plugin)───┐│               │ ┌─gear_mcp (plugin)─┐│
│ │  mcp_client (WS)  ││               │ │  connection (WS)  ││
│ │  tool_executor    ││               │ │  dispatcher       ││
│ │  scene_tools      ││               │ │  ┌─shared_utils──┐││
│ │  resource_tools   ││               │ │  │ type_codec    │││
│ │  animation_tools  ││               │ │  │ path_utils    │││
│ └───────────────────┘│               │ │  │ scene_io      │││
│ ┌─runtime (plugin)──┐│               │ │  │ value_parser  │││
│ │  autoload注册      ││               │ │  └───────────────┘││
│ │  (死代码)         ││               │ │  scene_tools      ││
│ └───────────────────┘│               │ │  resource_tools   ││
│ ┌─auto_reload───────┐│               │ │  animation_tools  ││
│ │  1s轮询           ││               │ │  runtime_server   ││
│ │  只看当前场景     ││               │ │  file_watcher     ││
│ └───────────────────┘│               │ └───────────────────┘│
└──────────────────────┘               └──────────────────────┘
```

---

### 三、Godot 插件层重设计

#### 3.1 合并为单一插件 `gear_mcp`

将 3 个插件合并为 1 个 `EditorPlugin`，目录结构如下：

```
addons/gear_mcp/
├── plugin.cfg
├── plugin.gd                  # 唯一入口：注册 autoload、启动 file_watcher、建立 WS 连接
├── connection.gd              # WebSocket 客户端（统一协议）
├── dispatcher.gd              # 工具分发（替代 tool_executor.gd）
├── runtime_server.gd          # 运行时 TCP 服务器（作为 autoload，由 plugin.gd 注册）
├── file_watcher.gd            # 文件监视器（替代 auto_reload）
├── tools/
│   ├── scene_tools.gd         # 场景操作
│   ├── resource_tools.gd      # 资源操作
│   ├── animation_tools.gd     # 动画 + 导航
│   └── runtime_tools.gd       # 运行时操作（截图、输入注入、树检查）
└── shared/
    ├── type_codec.gd          # 统一的类型序列化/反序列化（替代 3 份 _parse_value）
    ├── path_utils.gd          # 统一的路径处理（替代 3 份 _ensure_res_path）
    ├── scene_io.gd            # 统一的场景加载/保存（替代 3 份 _load_scene/_save_scene）
    └── value_parser.gd        # Godot 值类型解析（统一 type/_type 标签）
```

#### 3.2 plugin.gd 职责

```gdscript
@tool
extends EditorPlugin

const AUTOLOAD_SCRIPT = "res://addons/gear_mcp/runtime_server.gd"
const AUTOLOAD_NAME = "GearMCPRuntime"

var _connection: Node    # connection.gd
var _dispatcher: Node    # dispatcher.gd
var _file_watcher: Node  # file_watcher.gd
var _status_label: Label

func _enter_tree():
    # 1. 注册 autoload（原来 godot_mcp_runtime.gd 的唯一职责）
    if not ProjectSettings.has_setting("autoload/" + AUTOLOAD_NAME):
        add_autoload_singleton(AUTOLOAD_NAME, AUTOLOAD_SCRIPT)

    # 2. 创建连接、分发器、文件监视器
    _connection = preload("connection.gd").new()
    _dispatcher = preload("dispatcher.gd").new()
    _file_watcher = preload("file_watcher.gd").new()
    
    _dispatcher.set_editor_plugin(self)
    _file_watcher.set_editor_plugin(self)
    
    add_child(_connection)
    add_child(_dispatcher)
    add_child(_file_watcher)
    
    # 3. 连接信号
    _connection.tool_requested.connect(_dispatcher.execute_tool)
    _dispatcher.tool_completed.connect(_connection.send_tool_result)
    
    # 4. 状态指示
    _setup_status_indicator()
    _connection.connected.connect(_on_connected)
    _connection.disconnected.connect(_on_disconnected)
    
    # 5. 启动连接
    _connection.connect_to_server()

func _exit_tree():
    # 清理 autoload、子节点、状态标签
    remove_autoload_singleton(AUTOLOAD_NAME)
    # ... 其余清理
```

核心变化：原来 3 个 plugin.cfg + 3 个 EditorPlugin 的职责，现在由一个 plugin.gd 统一管理。用户只需安装并启用一个插件。

#### 3.3 shared/type_codec.gd — 统一类型系统

这是消除代码重复最关键的一步。将 3 份 `_parse_value` + 3 份 `_serialize_value` 合并为一个类，统一使用 `"type"` 作为标签键名（同时兼容 `"_type"` 输入）：

```gdscript
class_name GearTypeCodec

# 支持所有 Godot 值类型：
# Vector2, Vector3, Vector2i, Vector3i, Color, Rect2, Rect2i,
# Transform2D, Transform3D, NodePath, Resource, Basis, Quaternion,
# Plane, AABB, Projection

static func parse(value, expected_type: int = TYPE_NIL) -> Variant:
    if value is Dictionary:
        var type_tag = value.get("type", value.get("_type", ""))
        match type_tag:
            "Vector2": return Vector2(value.x, value.y)
            "Vector3": return Vector3(value.x, value.y, value.z)
            "Vector2i": return Vector2i(value.x, value.y)
            "Vector3i": return Vector3i(value.x, value.y, value.z)
            "Color": return Color(value.r, value.g, value.b, value.get("a", 1.0))
            "Rect2": return Rect2(value.position, value.size)
            "Transform2D": # ...
            "Transform3D": # ...
            "NodePath": return NodePath(value.path)
            # ... 其余类型
    if expected_type != TYPE_NIL:
        return _coerce_to_type(value, expected_type)
    return value

static func serialize(value: Variant) -> Variant:
    if value is Vector2:
        return {"type": "Vector2", "x": value.x, "y": value.y}
    if value is Vector3:
        return {"type": "Vector3", "x": value.x, "y": value.y, "z": value.z}
    # ... 其余类型
    return value
```

所有 tools/ 下的文件统一调用 `GearTypeCodec.parse()` 和 `GearTypeCodec.serialize()`，不再各自实现。

#### 3.4 shared/scene_io.gd — 统一场景 I/O

```gdscript
class_name GearSceneIO

static func load_scene(scene_path: String) -> Array:
    # 返回 [root_node, error_dict]
    var res_path = GearPathUtils.to_res_path(scene_path)
    var packed = load(res_path) as PackedScene
    if packed == null:
        return [null, {"error": "Failed to load scene: " + res_path}]
    return [packed.instantiate(), {}]

static func save_scene(root: Node, scene_path: String) -> Dictionary:
    var res_path = GearPathUtils.to_res_path(scene_path)
    GearPathUtils.ensure_parent_dir(res_path)
    var packed = PackedScene.new()
    var err = packed.pack(root)
    if err != OK:
        root.queue_free()
        return {"error": "Failed to pack scene: " + str(err)}
    err = ResourceSaver.save(packed, res_path)
    root.queue_free()
    if err != OK:
        return {"error": "Failed to save scene: " + str(err)}
    EditorInterface.get_resource_filesystem().scan()
    return {}

static func refresh_and_reload(scene_path: String):
    EditorInterface.get_resource_filesystem().scan()
    var current = EditorInterface.get_edited_scene_root()
    if current and current.scene_file_path == scene_path:
        EditorInterface.reload_scene_from_path(scene_path)
```

统一返回 Dictionary 格式（空字典 = 成功），消除 int vs Dictionary 的不一致。

#### 3.5 file_watcher.gd — 增强版文件监视

```gdscript
@tool
extends Node

# 替代 auto_reload，改进点：
# 1. 监视整个项目的 .tscn/.gd/.tres/.res/.gdshader 文件（不只是当前场景）
# 2. 使用 EditorFileSystem 的信号而非轮询（Godot 4.4+）
# 3. 区分"由 MCP 自己触发的修改"和"用户手动修改"，避免循环刷新

var _editor_plugin: EditorPlugin
var _watched_files: Dictionary = {}  # {res_path: last_modified_time}
var _mcp_modified_files: Dictionary = {}  # 由 MCP 工具标记的文件
var _check_interval: float = 1.0
var _timer: Timer

func mark_mcp_modified(file_path: String):
    """由 dispatcher 在工具执行后调用，标记此文件为 MCP 修改"""
    _mcp_modified_files[file_path] = Time.get_ticks_msec()

func _check_for_changes():
    # 扫描所有项目文件（不只是当前场景）
    var extensions = ["tscn", "scn", "gd", "tres", "res", "gdshader"]
    # ... 对比 modified time
    # 对于 MCP 修改的文件，直接刷新不弹窗
    # 对于用户修改的文件，提示或自动重载
```

#### 3.6 connection.gd — 统一通信

合并 WebSocket 客户端和运行时通信为一个连接层。保留 WebSocket 作为主通道（端口 6505），同时在同一个类中管理 runtime autoload 的生命周期：

```gdscript
@tool
extends Node

signal connected
signal disconnected
signal tool_requested(request_id: String, tool_name: String, args: Dictionary)

const DEFAULT_URL = "ws://127.0.0.1:6505/godot"
const RECONNECT_DELAY = 3.0
const MAX_RECONNECT_DELAY = 30.0

var socket: WebSocketPeer
var _is_connected: bool = false
var _reconnect_timer: Timer
var _current_reconnect_delay: float = RECONNECT_DELAY
var _should_reconnect: bool = true
var _project_path: String

# 新增：死连接检测
const PONG_TIMEOUT = 30.0
var _last_pong_time: float = 0.0
var _pong_check_timer: Timer

func _process(delta):
    if not _initialized:
        return
    # 检查 pong 超时
    if _is_connected and _last_pong_time > 0:
        if Time.get_ticks_msec() / 1000.0 - _last_pong_time > PONG_TIMEOUT:
            push_warning("Gear MCP: Pong timeout, reconnecting...")
            _handle_disconnect()
            return
    # ... 原有的 WebSocket poll 逻辑
```

#### 3.7 dispatcher.gd — 自动注册式分发

```gdscript
@tool
extends Node

var _editor_plugin: EditorPlugin
var _tool_map: Dictionary = {}
var _tool_modules: Array[Node] = []

func set_editor_plugin(plugin: EditorPlugin):
    _editor_plugin = plugin
    _register_all_tools()

func _register_all_tools():
    # 自动扫描 tools/ 目录下的所有脚本
    var tools_dir = get_script().resource_path.get_base_dir() + "/tools"
    var dir = DirAccess.open(tools_dir)
    if dir == null:
        return
    dir.list_dir_begin()
    var file_name = dir.get_next()
    while file_name != "":
        if file_name.ends_with(".gd"):
            var script = load(tools_dir + "/" + file_name)
            if script and script.has_method("get_tool_names"):
                var module = script.new()
                module.set_editor_plugin(_editor_plugin)
                add_child(module)
                _tool_modules.append(module)
                # 模块自注册
                for tool_info in module.get_tool_names():
                    _tool_map[tool_info.name] = {"module": module, "method": tool_info.method}
        file_name = dir.get_next()

func execute_tool(request_id: String, tool_name: String, args: Dictionary):
    var entry = _tool_map.get(tool_name)
    if entry == null:
        # 发送错误结果
        tool_completed.emit(request_id, false, {}, "Unknown tool: " + tool_name)
        return
    
    var result = entry.module.call(entry.method, args)
    # 通知 file_watcher 哪些文件被修改了
    if result.has("modified_files"):
        for f in result.modified_files:
            get_parent().get_node("FileWatcher").mark_mcp_modified(f)
    
    if result.has("ok") and result.ok:
        var clean = result.duplicate()
        clean.erase("ok")
        clean.erase("modified_files")
        tool_completed.emit(request_id, true, clean, "")
    else:
        tool_completed.emit(request_id, false, {}, result.get("error", "Unknown error"))
```

每个工具模块通过 `get_tool_names()` 方法自注册，无需维护集中的 tool_map 字典。

#### 3.8 工具模块的标准化模式

每个 tools/ 文件遵循统一模板：

```gdscript
@tool
extends Node

var _editor_plugin: EditorPlugin

func set_editor_plugin(plugin: EditorPlugin):
    _editor_plugin = plugin

# 自注册接口
static func get_tool_names() -> Array[Dictionary]:
    return [
        {"name": "create_scene", "method": "create_scene"},
        {"name": "add_node", "method": "add_node"},
        # ...
    ]

# 统一的工具方法签名
func create_scene(args: Dictionary) -> Dictionary:
    var scene_path = GearPathUtils.to_res_path(args.get("scenePath", ""))
    var root_type = args.get("rootNodeType", "Node")
    
    # 使用共享工具类
    var root = ClassDB.instantiate(root_type)
    if root == null:
        return {"ok": false, "error": "Unknown type: " + root_type}
    
    # ... 操作逻辑
    
    var save_result = GearSceneIO.save_scene(root, scene_path)
    if save_result.has("error"):
        return {"ok": false, "error": save_result.error}
    
    return {"ok": true, "scenePath": scene_path, "modified_files": [scene_path]}
```

#### 3.9 Undo/Redo 支持（可选，优先级中）

对于场景操作，通过 `UndoRedo` 系统注册操作：

```gdscript
func add_node(args: Dictionary) -> Dictionary:
    # ... 创建节点 ...
    var undo_redo = _editor_plugin.get_undo_redo()
    undo_redo.create_action("MCP: Add " + node_name)
    undo_redo.add_do_method(parent, "add_child", new_node)
    undo_redo.add_do_method(new_node, "set_owner", scene_root)
    undo_redo.add_do_reference(new_node)
    undo_redo.add_undo_method(parent, "remove_child", new_node)
    undo_redo.commit_action()
    # ... 保存场景 ...
```

这样用户可以用 Ctrl+Z 撤销 MCP 的操作。

---

### 四、通信层重设计

#### 4.1 统一协议

将 4 个独立通道简化为 2 个：

| 通道 | 端口 | 用途 | 说明 |
|------|------|------|------|
| WebSocket 桥接 | 6505 | 编辑器操作 + LSP/DAP 代理 | 主通道，所有设计时操作 |
| Runtime TCP | 7777 | 运行时检查 | 游戏运行时操作（保持独立，因为是不同进程） |

**关键变化**：LSP 和 DAP 不再由 Node.js 侧直接 TCP 连接 Godot 的 6005/6006 端口。改为通过 WebSocket 桥接发送 LSP/DAP 请求，由 Godot 侧的插件转发给内置语言服务器。这样：

- 减少 2 个 TCP 连接
- 消除 LSP/DAP 帧解析代码重复
- 消除连接管理的复杂度
- Godot 4.4+ 的 EditorPlugin 可以访问 `EditorInterface.get_script_editor()` 等方法获取诊断信息

```
旧：Node.js --TCP:6005--> Godot LSP
    Node.js --TCP:6006--> Godot DAP
    Node.js --WS:6505--> Godot Editor Plugin
    Node.js --TCP:7777--> Godot Runtime

新：Node.js --WS:6505--> Godot Editor Plugin (统一入口)
                          ├── 场景/资源/动画操作
                          ├── LSP 代理（调用 EditorScript API）
                          ├── DAP 代理（调用 Debugger API）
                          └── autoload 管理
    Node.js --TCP:7777--> Godot Runtime (保持，因为是运行时进程)
```

但如果 Godot 4.4+ EditorPlugin 的 API 不足以覆盖所有 LSP/DAP 功能（比如代码补全需要访问 Language Server 内部状态），可以保留 LSP/DAP 直连作为降级方案。在插件代码中增加两个工具方法 `lsp_request` 和 `dap_request`，由 Godot 侧发起 TCP 连接到自己的 6005/6006 端口（本地回环），将结果通过 WebSocket 返回给 Node.js。

#### 4.2 Runtime TCP 改为持久连接

```typescript
// 旧的 handleRuntimeCommand：每次新建连接
async handleRuntimeCommand(command, params) {
    return new Promise((resolve) => {
        const socket = new net.Socket();
        socket.connect(7777, '127.0.0.1', () => { ... });
        // ... 用完即弃
    });
}

// 新的 RuntimeConnection 类：持久连接 + 消息队列
class RuntimeConnection {
    private socket: net.Socket | null = null;
    private pendingRequests: Map<number, PendingRequest> = new Map();
    private buffer: string = '';
    
    async connect(): Promise<void> {
        // 持久连接，带重连逻辑
    }
    
    async sendCommand(command: string, params: any): Promise<any> {
        const id = Date.now();
        const promise = new Promise((resolve, reject) => {
            this.pendingRequests.set(id, { resolve, reject, timeout: ... });
        });
        this.socket!.write(JSON.stringify({ command, params, id }) + '\n');
        return promise;
    }
    
    private onData(data: Buffer): void {
        this.buffer += data.toString();
        const lines = this.buffer.split('\n');
        // 解析完整消息，匹配 request id，resolve 对应 promise
    }
}
```

#### 4.3 Bridge 增加死连接检测

```typescript
// godot-bridge.ts 新增
private startPongWatchdog(): void {
    this.pongWatchdogInterval = setInterval(() => {
        if (this.godotSocket && this.lastPongAt > 0) {
            const elapsed = Date.now() - this.lastPongAt;
            if (elapsed > PONG_TIMEOUT_MS) {
                this.logger.warn(`Pong timeout (${elapsed}ms), closing stale connection`);
                this.godotSocket.close(4001, 'Pong timeout');
            }
        }
    }, 10_000);
}
```

#### 4.4 统一消息协议

WebSocket 上统一使用以下消息格式（合并 editor 和 runtime 的风格）：

```typescript
// 请求（Node.js -> Godot）
interface ToolInvokeMessage {
    type: 'tool_invoke';
    id: string;
    tool: string;
    args: Record<string, any>;
}

// 响应（Godot -> Node.js）
interface ToolResultMessage {
    type: 'tool_result';
    id: string;
    success: boolean;
    result?: Record<string, any>;
    error?: string;
    modified_files?: string[];  // 新增：告知 file_watcher
}

// 心跳
interface PingMessage { type: 'ping'; timestamp: number; }
interface PongMessage { type: 'pong'; timestamp: number; }

// 握手
interface ReadyMessage { 
    type: 'godot_ready'; 
    project_path: string; 
    godot_version: string;  // 新增
    plugin_version: string; // 新增
}

// 推送通知（Godot -> Node.js，新增）
interface EventMessage {
    type: 'event';
    event: 'file_changed' | 'scene_saved' | 'project_modified';
    data: Record<string, any>;
}
```

---

### 五、TypeScript 服务器层重设计

#### 5.1 目录结构

```
src/
├── index.ts                    # 薄入口（<200行）：创建 Server、注册 handlers、启动
├── server.ts                   # Server 类：生命周期、MCP 协议处理
├── registry.ts                 # ToolRegistry：handler 注册 + 分发
├── config.ts                   # 全局配置（环境变量、常量）
│
├── handlers/                   # 领域处理器（每个 <1000 行）
│   ├── project.ts              # list_projects, get_project_info, search_project, settings
│   ├── scene.ts                # 场景操作（桥接转发）
│   ├── script.ts               # create_script, modify_script, get_script_info
│   ├── editor.ts               # launch_editor, run_project, stop_project, process 管理
│   ├── classdb.ts              # query_classes, query_class_info, inspect_inheritance
│   ├── export.ts               # export_project, list_export_presets
│   ├── debug.ts                # LSP + DAP 相关工具
│   ├── runtime.ts              # 运行时工具（通过 RuntimeConnection）
│   ├── assets.ts               # CC0 素材搜索/下载
│   ├── animation.ts            # 动画操作（桥接转发）
│   ├── resource.ts             # 资源操作（桥接转发）
│   ├── navigation.ts           # 导航系统
│   ├── audio.ts                # 音频总线
│   ├── tilemap.ts              # TileMap
│   ├── theme.ts                # 主题/UI
│   ├── plugin.ts               # 插件管理
│   ├── input.ts                # 输入映射
│   ├── autoload.ts             # Autoload 管理
│   ├── intent.ts               # 意图追踪/交接系统
│   ├── testing.ts              # 截图、输入注入
│   ├── dx.ts                   # 开发者体验工具
│   └── meta.ts                 # tool_catalog, manage_tool_groups
│
├── bridge/                     # 通信层
│   ├── connection.ts           # WebSocket 桥接服务器
│   ├── runtime-connection.ts   # 运行时持久 TCP 连接
│   └── protocol.ts             # 消息类型定义
│
├── providers/                  # CC0 素材提供者（保持不变）
│   ├── types.ts
│   ├── manager.ts
│   ├── polyhaven.ts
│   ├── ambientcg.ts
│   └── kenney.ts
│
├── tools/                      # 工具元数据
│   ├── definitions.ts          # 工具 schema 定义
│   └── groups.ts               # 分组定义
│
├── utils/                      # 共享工具
│   ├── godot-path.ts           # Godot 路径检测
│   ├── config-parser.ts        # project.godot / export_presets.cfg 解析
│   ├── logger.ts               # 统一日志
│   └── gdscript.ts             # GDScript 解析
│
├── visualizer/                 # 项目可视化（保持）
│   └── ...
│
├── resources.ts                # MCP Resources
├── prompts.ts                  # MCP Prompts
├── cli.ts                      # CLI 入口
└── cli/                        # CLI 子命令（保持）
    └── ...
```

#### 5.2 ToolRegistry — 替代 130-case switch

```typescript
// registry.ts
export interface ToolHandler {
    name: string;
    handle(args: Record<string, any>, context: ServerContext): Promise<ToolResult>;
}

export interface ServerContext {
    bridge: BridgeConnection;
    runtime: RuntimeConnection;
    godotPath: string;
    config: AppConfig;
    logger: Logger;
}

export class ToolRegistry {
    private handlers = new Map<string, ToolHandler>();
    private groups = new Map<string, string[]>();  // group -> tool names

    register(handler: ToolHandler, group: string): void {
        this.handlers.set(handler.name, handler);
        if (!this.groups.has(group)) this.groups.set(group, []);
        this.groups.get(group)!.push(handler.name);
    }

    async dispatch(toolName: string, args: Record<string, any>, context: ServerContext): Promise<ToolResult> {
        const handler = this.handlers.get(toolName);
        if (!handler) throw new Error(`Unknown tool: ${toolName}`);
        return handler.handle(args, context);
    }

    getToolsForGroups(activeGroups: Set<string>): ToolHandler[] {
        // 返回核心组 + 已激活的动态组的工具
    }
}
```

#### 5.3 Handler 示例 — 领域模块自注册

```typescript
// handlers/scene.ts
import { ToolHandler, ServerContext, ToolResult } from '../registry.js';

export function registerSceneHandlers(registry: ToolRegistry): void {
    registry.register(new CreateSceneHandler(), 'core_scene');
    registry.register(new AddNodeHandler(), 'core_scene');
    registry.register(new DeleteNodeHandler(), 'core_scene');
    // ...
}

class CreateSceneHandler implements ToolHandler {
    name = 'create_scene';
    
    async handle(args: Record<string, any>, ctx: ServerContext): Promise<ToolResult> {
        if (!ctx.bridge.isConnected()) {
            return { error: 'Godot editor not connected' };
        }
        return ctx.bridge.invokeTool('create_scene', args);
    }
}
```

#### 5.4 薄入口 index.ts

```typescript
// index.ts — 目标 <200 行
import { Server } from './server.js';
import { ToolRegistry } from './registry.js';
import { BridgeConnection } from './bridge/connection.js';
import { RuntimeConnection } from './bridge/runtime-connection.js';

// 导入所有 handler 注册函数
import { registerProjectHandlers } from './handlers/project.js';
import { registerSceneHandlers } from './handlers/scene.js';
import { registerScriptHandlers } from './handlers/script.js';
// ... 其余 handlers

async function main() {
    const registry = new ToolRegistry();
    
    // 注册所有 handlers
    registerProjectHandlers(registry);
    registerSceneHandlers(registry);
    registerScriptHandlers(registry);
    // ...
    
    const bridge = new BridgeConnection();
    const runtime = new RuntimeConnection();
    
    const server = new Server(registry, bridge, runtime);
    await server.start();
}

main().catch(console.error);
```

#### 5.5 工具分组简化

当前的 compact/full/legacy 三模式 + 22 个动态分组过于复杂。简化为：

```
核心组（始终可见，~30 个工具）：
  project, editor, scene, script, classdb, export, meta, diagnostics

扩展组（按需激活，~80 个工具）：
  animation, audio, autoload, navigation, tilemap, theme, plugin,
  input, resource, runtime, debug, assets, testing, dx, intent,
  scene_advanced, import, signal, uid, version_gate
```

去掉 legacy profile（不再有新旧名称映射），只保留 compact（默认）和 full。去掉 `GOPEAK_TOOLS_PAGE_SIZE` 分页——如果 AI 客户端需要某个工具，通过 `tool_catalog` 搜索激活即可。

---

### 六、迁移路径

建议按以下阶段逐步迁移，每个阶段都可独立测试和发布：

**阶段 1：Godot 插件合并（1-2 周）**
- 创建 `shared/` 基础模块（type_codec, path_utils, scene_io, value_parser）
- 将 3 个工具文件的重复代码迁移到 shared/
- 合并 3 个 plugin.cfg 为 1 个 gear_mcp
- 将 auto_reload 逻辑整合进 plugin.gd
- 添加死连接检测到 connection.gd
- 验证所有 29 个编辑器工具正常工作

**阶段 2：TypeScript 侧模块化（1-2 周）**
- 创建 handlers/ 目录和 ToolRegistry
- 逐个领域从 index.ts 提取 handler（从最简单的开始：project, classdb, export）
- 将 server/ 下已提取但未使用的模块要么接入要么删除
- 删除死代码（tools.ts、gdscript_utils.ts 等）
- 每个提取的 handler 确保功能对等后删除 index.ts 中的对应代码

**阶段 3：通信层优化（1 周）**
- 实现 RuntimeConnection 持久连接替代逐次新建
- 评估 LSP/DAP 代理方案的可行性（Godot 4.4 API 是否足够）
- 如果可行，将 LSP/DAP 请求走 WebSocket 桥接
- 如果不可行，提取 LSP/DAP 共享的帧解析为公共模块
- 统一消息协议（type 标签键名、modified_files 字段）

**阶段 4：收尾与增强（1 周）**
- 简化工具分组（去掉 legacy profile，精简分组数量）
- 为场景操作添加 Undo/Redo 支持
- 增强 file_watcher 支持全项目监视
- 更新文档和安装脚本
- 全面回归测试

---

### 七、关键决策点

以下决策需要根据实际测试结果确定：

**LSP/DAP 是否走 WebSocket 代理**：Godot 4.4 的 `EditorPlugin` API 是否提供了足够的接口来获取代码诊断、补全、调试信息？如果不行（很可能），则需要保留 LSP/DAP 直连。这种情况下，至少应将帧解析代码提取为共享模块 `base-protocol.ts`。

**Runtime TCP 是否保留独立端口**：Runtime autoload 运行在游戏进程中（不是编辑器进程），与编辑器插件不在同一个进程。因此 7777 端口在架构上是合理的，无法合并到 WebSocket 桥接中。改为持久连接是最佳优化。

**是否引入 MCP Streamable HTTP 传输**：当前只有 stdio 传输。如果未来需要支持远程 MCP 客户端（如 Web 版 AI 助手），可以添加 HTTP+SSE 传输作为 stdio 的替代。但这是锦上添花，不应阻塞核心重构。

---

### 八、预期收益

| 维度 | 现状 | 重构后 |
|------|------|--------|
| 插件安装 | 3 个插件分别启用 | 1 个插件一键启用 |
| GDScript 代码重复 | 3 份 _parse_value、3 份 _ensure_res_path | 各 1 份共享实现 |
| TypeScript 最大文件 | 249 KB (index.ts) | ~15 KB (各 handler) |
| 工具分发 | 130-case switch | Map 查找 + 领域注册 |
| TCP 连接数 | 4 个独立端口 | 2 个（WS + Runtime TCP） |
| Runtime 连接模型 | 每次请求新建 TCP | 持久连接 + 消息队列 |
| 死代码 | ~5 个未使用模块 | 清理或接入 |
| Undo 支持 | 无 | 场景操作支持 Ctrl+Z |
| 文件监视 | 只看当前场景 | 全项目监视 |
