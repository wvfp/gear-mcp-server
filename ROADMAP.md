# Gear 路线图

> 最新规划请查看 [`docs/platform-roadmap.md`](docs/platform-roadmap.md)。

---

## 当前版本 (2026年4月)

- **包版本**: `2.3.6`
- **分发**: npm 包 `gear-godot-mcp`
- **默认工具暴露**: `compact` 模式
- **工具数量**: 33 核心工具 + 22 动态组 (总计 110+)
- **MCP 能力**: tools、resources、prompts 已实现
- **传输协议**: stdio (默认)

---

## 当前优先级

| 优先级 | 任务 | 状态 |
|--------|------|------|
| P0 | 模块化重构 `src/index.ts` | ✅ |
| P0 | 保持向后兼容 | ✅ |
| P1 | 工具缓存和性能优化 | ✅ |
| P1 | 结构化日志和容错机制 | ✅ |
| P2 | CLI 增强 (doctor/profile) | ✅ |
| P2 | AI 语义工具搜索 | ✅ |

---

## 工作原则

1. **增量优先**: 优先选择可验证的增量变更，而非大规模重写
2. **文档同步**: README、package.json、server.json 保持与代码一致
3. **向后兼容**: compact/full/legacy 配置和别名保持不变

---

## 相关文档

- [平台路线图](docs/platform-roadmap.md)
- [架构文档](docs/architecture.md)
- [发布流程](docs/release-process.md)
- [更新日志](CHANGELOG.md)
