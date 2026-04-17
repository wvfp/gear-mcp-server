# Gear 实现规范

## 概述

本文档定义了 Gear 项目的实现计划，排除文档和国际化的优化。

---

## 范围

### 已包含

- **架构**：index.ts 模块化、分层工具组
- **工具**：实现 IMPROVEMENT_SPECS.md 中的缺失工具，增强现有工具
- **GDScript 分析**：语义分析、代码度量、重构支持
- **集成**：LSP/DAP 深度集成、运行时插件增强、第三方服务
- **开发者体验**：AI 语义工具搜索、调试改进、CLI 增强
- **性能**：缓存、Worker 线程、连接池
- **稳定性**：重试、熔断器、结构化日志
- **平台**：MCP 协议演进、资源订阅、提示增强

### 已排除

- **7.1 文档**：自动生成 API 文档、视频教程
- **6.2 国际化**：多语言支持

---

## 实现阶段

### Phase 1: 架构基础

#### 1.1 模块化 index.ts

将单体 `src/index.ts` 拆分为：

```
src/
├── server/
│   ├── entry.ts              # 主入口
│   ├── tool-dispatcher.ts    # 工具路由
│   ├── lifecycle.ts          # 生命周期
│   └── param-mapper.ts       # 参数映射
├── capabilities/
│   ├── tools/
│   │   ├── index.ts          # 工具注册
│   │   └── executor.ts       # 工具执行
│   ├── resources/
│   │   └── index.ts          # 资源处理
│   └── prompts/
│       └── index.ts          # 提示模板
└── integrations/
    ├── godot-bridge.ts
    ├── lsp-client.ts
    └── dap-client.ts
```

#### 1.2 分层工具组管理

```
tool-groups.ts
├── 核心组 (始终可用)
│   ├── project
│   ├── editor
│   └── scene
├── 领域组 (懒加载)
│   ├── scene_advanced
│   ├── runtime
│   ├── animation
│   └── ...
└── 功能组 (按需)
    ├── testing
    ├── dx_tools
    └── intent_tracking
```

---

### Phase 2: 新工具

#### 2.1 优先级 1

| 工具 | 说明 |
|------|------|
| `create_export_preset` | 创建导出预设 |
| `get_node_info` | 详细节点信息 |
| `list_plugins` | 列出编辑器插件 |
| `enable_plugin` / `disable_plugin` | 插件管理 |
| `install_asset_lib_plugin` | 从资源库安装插件 |

#### 2.2 优先级 2

| 工具 | 说明 |
|------|------|
| `bulk_update_nodes` | 批量节点更新 |
| `incremental_scene_parse` | 增量场景解析 |

---

### Phase 3: 工具增强

#### 3.1 脚本工具

- `modify_script` 增强 (AST 支持)
- 函数替换和删除
- 重构操作 (符号重命名)

#### 3.2 场景工具

- 批量节点更新
- 条件节点检索

#### 3.3 运行时工具

- 性能剖析
- 对象引用追踪

---

### Phase 4: GDScript 分析

#### 4.1 语义分析

- 函数调用图
- 依赖分析
- 跨脚本符号解析

#### 4.2 代码度量

- 圈复杂度
- 重复代码检测

---

### Phase 5: 集成增强

#### 5.1 LSP 增强

| 功能 | 说明 |
|------|------|
| 跳转定义 | 跨脚本导航 |
| 查找引用 | 完整引用链 |
| 安全重命名 | 预览后确认 |

#### 5.2 DAP 增强

| 功能 | 说明 |
|------|------|
| 条件断点 | 命中计数 |
| 表达式求值 | Watch 窗口 |
| 断点组 | 分组管理 |

#### 5.3 运行时增强

- 性能剖析 (FPS、内存)
- 场景快照 (保存/恢复)
- 多人支持基础

#### 5.4 第三方服务

- itch.io 资源搜索
- Godot 资源库集成

---

### Phase 6: 开发者体验

#### 6.1 AI 语义工具搜索

- 基于嵌入的相似度搜索
- 自然语言工具描述匹配

#### 6.2 调试改进

- 工具执行计时
- 调用链追踪
- 健康检查
- 自动修复建议

#### 6.3 CLI 增强

| 命令 | 说明 |
|------|------|
| `gear doctor` | 环境诊断 |
| `gear profile` | 性能剖析 |
| `gear debug` | 调试模式 |

---

### Phase 7: 性能

#### 7.1 缓存策略

- 工具定义缓存 (失效机制)
- 项目元数据缓存 (TTL)

#### 7.2 Worker 线程

- 场景解析 Worker
- 大文件处理 Worker

#### 7.3 连接管理

- LSP 连接复用
- 懒初始化优化

---

### Phase 8: 稳定性

#### 8.1 容错

- Bridge/LSP/DAP 自动重连
- 熔断器模式
- 操作超时配置

#### 8.2 日志

- 结构化 JSON 日志
- 日志级别: ERROR/WARN/INFO/DEBUG
- 请求关联 ID

---

### Phase 9: 平台演进

#### 9.1 MCP 协议

- 资源订阅 (推送更新)
- 参数化提示
- 条件提示
- HTTP Streamable 传输 (未来)

---

## 向后兼容

所有更改必须保持：

- compact/full/legacy 工具配置
- 旧工具别名
- CLI 启动行为
- MCP 能力声明

---

## 验收标准

1. `npm run ci` 通过
2. 现有 compact/full 配置行为不变
3. 新工具已记录
4. 无破坏性更改
