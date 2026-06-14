# gear-mcp-proxy

stdio ↔ TCP proxy for [Gear MCP Server](https://github.com/wvfp/gear-mcp-server) (a Godot GDExtension that runs an MCP server inside the Godot editor process).

## What it does

Gear MCP Server listens on `127.0.0.1:8510` inside the Godot editor. MCP clients (Claude Desktop, Cursor, Cline, OpenCode, …) talk JSON-RPC over stdio. This ~50-line shim bridges the two.

```
AI client (stdio JSON-RPC)  ──▶  gear-mcp-proxy  ──▶  Godot editor (TCP 127.0.0.1:8510)
```

## Install & run

```bash
npx -y gear-mcp-proxy
```

Or install globally:

```bash
npm i -g gear-mcp-proxy
gear-mcp-proxy
```

## Environment

| Variable     | Default       | Description                          |
| ------------ | ------------- | ------------------------------------ |
| `GEAR_HOST`  | `127.0.0.1`   | Godot editor host                    |
| `GEAR_PORT`  | `8510`        | Godot editor MCP port                |

The proxy retries on `ECONNREFUSED` for up to 60 seconds, so it's safe to launch before the editor finishes loading.

## Wire it into an MCP client

```json
{
  "mcpServers": {
    "gear": {
      "command": "npx",
      "args": ["-y", "gear-mcp-proxy"]
    }
  }
}
```

## License

MIT — see [LICENSE](https://github.com/wvfp/gear-mcp-server/blob/main/LICENSE).
