# 贡献指南

感谢您对 Gear 的贡献！

---

## 开发环境设置

```bash
git clone https://github.com/wvfp/Gear-godot-mcp.git
cd Gear-godot-mcp
npm install
npm run build
```

常用命令：

```bash
npm run watch        # TypeScript 监视模式
npm run inspector   # MCP 检查器
npm run ci          # 运行所有检查
```

---

## 提交流程

1. **创建分支**: `git checkout -b feature/your-feature`
2. **编写代码**: 保持更改最小化
3. **运行检查**: `npm run ci`
4. **提交 PR**: 描述更改内容和原因

---

## 代码规范

- 优先复用现有代码和命名模式
- 保持用户可见行为稳定
- 更新相关文档
- 保持 package.json、server.json、README 一致

---

## PR 检查清单

提交 PR 前请运行：

```bash
npm run ci                    # 构建和类型检查
npm run test:dynamic-groups   # 动态工具组测试
npm run test:integration      # 集成测试
```

---

## 文档更新

- `README.md` - 用户面向的文档
- `docs/architecture.md` - 架构决策
- `docs/platform-roadmap.md` - 路线图和规划

---

## 好的 PR 描述应该包含

- 做了什么更改
- 为什么要更改
- 验证命令和结果
- 任何已知问题或兼容性说明
