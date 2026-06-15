# agent-skill/

A Trae / Claude / Cursor skill that teaches an AI agent how to drive
this project via the MCP tools. Drop the `SKILL.md` into your IDE's
skill directory (e.g. `~/.trae-cn/builtin/global/skills/`) to get
end-to-end guidance on:

- Workflows (write_file → create_scene → add_node → attach_script → run + screenshot)
- The screenshot pipeline and the helper-script trick
- 7 common pitfalls (driver crashes, byte-offset shifts, etc.)
- Recovery when the editor is wedged

The MCP tool count listed in `SKILL.md` may drift from the actual
count — if you add/remove tools, update the frontmatter description
and the "Tool reference" section.
