# Gear 实现任务清单

本文档追踪 Gear 项目的实现进度。

---

## 实现完成状态

### ✅ Phase 1: 架构重构

| 任务 | 状态 |
|------|------|
| 创建 `src/server/` 目录结构 | ✅ |
| 提取 `param-mapper.ts` | ✅ |
| 提取 `lifecycle.ts` | ✅ |
| 创建结构化日志 `structured-logger.ts` | ✅ |
| 创建容错机制 `resilience.ts` | ✅ |
| 创建缓存系统 `cache.ts` | ✅ |
| 创建工具处理器 `tool-handlers.ts` | ✅ |
| 创建语义搜索 `semantic-search.ts` | ✅ |
| 实现分层工具组管理 | ✅ |

### ✅ Phase 2: 新工具

| 工具 | 状态 |
|------|------|
| `install_asset_lib_plugin` | ✅ |
| `get_node_info` | ✅ |
| `bulk_update_nodes` | ✅ |
| `lsp_goto_definition` | ✅ |
| `lsp_find_references` | ✅ |
| `lsp_rename_symbol` | ✅ |
| `dap_set_condition_breakpoint` | ✅ |
| `dap_evaluate_expression` | ✅ |
| `dap_add_to_watch` | ✅ |
| `runtime_profile` | ✅ |
| `runtime_snapshot_save` | ✅ |
| `runtime_snapshot_restore` | ✅ |
| `tool_semantic_search` | ✅ |
| `get_project_health_detailed` | ✅ |
| `asset_library_search` | ✅ |
| `asset_library_install` | ✅ |
| `analyze_error` | ✅ |
| `scene_diff` | ✅ |
| `list_project_templates` | ✅ |
| `create_from_template` | ✅ |

### ✅ Phase 3: 工具增强

| 功能 | 状态 |
|------|------|
| 批量节点更新 | ✅ |
| 场景对比 | ✅ |
| 符号重命名 | ✅ |

### ✅ Phase 4: 性能优化

| 功能 | 状态 |
|------|------|
| 工具定义缓存 | ✅ |
| 项目元数据缓存 | ✅ |
| 缓存失效机制 | ✅ |

### ✅ Phase 5: 稳定性

| 功能 | 状态 |
|------|------|
| 结构化日志 (JSON) | ✅ |
| 日志级别 (ERROR/WARN/INFO/DEBUG) | ✅ |
| 请求关联 ID | ✅ |
| 熔断器 | ✅ |
| 重试机制 | ✅ |

### ✅ Phase 6: CLI 增强

| 命令 | 状态 |
|------|------|
| `gear doctor` - 环境诊断 | ✅ |
| `gear profile` - 性能分析 | ✅ |

---

## 新增文件清单

### 服务端模块 (`src/server/`)

| 文件 | 说明 |
|------|------|
| `param-mapper.ts` | 参数映射 (snake_case ↔ camelCase) |
| `lifecycle.ts` | 生命周期管理 |
| `structured-logger.ts` | 结构化日志 |
| `resilience.ts` | 熔断器和重试 |
| `cache.ts` | 工具和元数据缓存 |
| `tool-handlers.ts` | 新工具处理器 |
| `semantic-search.ts` | 语义工具搜索 |

### CLI 模块 (`src/cli/`)

| 文件 | 说明 |
|------|------|
| `doctor.ts` | 环境诊断 |
| `profile.ts` | 性能分析 |

---

## 构建验证

| 检查项 | 状态 |
|--------|------|
| `npm run build` | ✅ |
| `npm run typecheck` | ✅ |
| `npm run test:smoke` | ✅ |
| `npm run test:regressions` | ✅ |

---

## 项目重命名

项目已从 **GoPeak** 重命名为 **Gear**。

- CLI 命令: `gopeak` → `gear`
- npm 包: `gopeak` → `gear-godot-mcp`
- 环境变量: `GOPEAK_*` → `GEAR_*`
