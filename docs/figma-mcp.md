# Figma MCP handoff

## Prepared, not connected

The project includes [figma-mcp.json](../config/figma-mcp.json):

```json
{
  "mcpServers": {
    "figma": {
      "type": "http",
      "url": "https://mcp.figma.com/mcp"
    }
  }
}
```

This is a common HTTP MCP server-entry format. Client schemas differ; use the target client's documented import or connector UI. Do not blindly overwrite an existing global MCP configuration.

The current Freebuff session exposes **no MCP connector manager or Figma tools**. Consequently no server connection, OAuth flow, account provisioning, credential storage, or Figma file access has occurred. Preparing JSON cannot enable a runtime connector. No API keys or tokens belong in this file.

## What remains for an MCP-capable client

1. Check that your coding client supports HTTP/streamable-HTTP MCP and is supported by Figma's current client policy/catalog.
2. Add a remote HTTP MCP server named `figma` with URL `https://mcp.figma.com/mcp`, using that client's settings. Import the provided JSON only if its schema matches.
3. Complete the client's **Figma OAuth sign-in** yourself. Authentication and file access are under your Figma account; do not paste tokens in chat.
4. Confirm the connection can list Figma tools. Copy a link to the specific frame/layer you want recreated and give it to the coding client.
5. Request a native C++ Dear ImGui implementation using the frame's spacing, colors, typography, and assets. Figma is a design-context provider, not a guaranteed C++/ImGui generator.

Remote-server access/rate limits depend on Figma's current plan/seat policy and your file permissions. The desktop MCP server is a different workflow: it requires Figma desktop, Dev Mode, and the appropriate paid-plan seat. Do not assume connecting a desktop server activates the remote one.

## Alternative without MCP

Provide an accessible frame link or export a PNG/SVG plus font names, color values, and spacing/token specs. Native ImGui layouts are hand-coded; React/CSS and Figma frames cannot be embedded as native controls without translating them.

## Official references

- [Figma MCP server introduction](https://developers.figma.com/docs/figma-mcp-server/)
- [Tools and prompts](https://developers.figma.com/docs/figma-mcp-server/tools-and-prompts/)
- [Access and rate limits](https://developers.figma.com/docs/figma-mcp-server/rate-limits-access/)
- [Desktop server setup](https://developers.figma.com/docs/figma-mcp-server/local-server-installation/)
- [Figma server vs. coding agent](https://developers.figma.com/docs/figma-mcp-server/mcp-vs-agent/)
- [Export static designs](https://help.figma.com/hc/en-us/articles/360040028114-Export-static-designs-from-Figma)

Research performed during implementation. A real connection test remains blocked by missing client connector support in this session.
