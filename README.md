# Shammy

Cross-platform desktop chat client in the shape of ChatGPT / Claude Desktop,
aimed at **Ollama**, **llama.cpp server**, LM Studio, vLLM, and any other
OpenAI-compatible `/v1` backend. Formerly LlamaChat.

C++20, CMake, Qt 6.4+ Quick. MIT licensed.

## Features (v1)

- Streaming `POST /v1/chat/completions` (not the Ollama-only `/v1/responses` API)
- Multiple named backends with a model picker from `GET /v1/models`
- Conversation history, search, pin, rename, delete
- Projects: instructions, file attachments, scoped chats
- Artifacts: tagged blocks plus large HTML/SVG/JS fences, versioned side pane
- Local MCP over stdio (Claude Desktop `mcpServers` schema), tool loop, permission prompts
- Markdown bubbles, code copy, regenerate / edit-resend, stop
- Text and image attachments (vision as `image_url` data URLs)
- Thinking/reasoning deltas when the backend streams them
- Dark / light theme

## Build

Needs Qt 6.4 or newer: Core, Gui, Quick, QuickControls2, Network, Sql, Svg.
WebEngine is optional (`-DWITH_WEBENGINE=OFF` if you skip HTML preview).

Debian / Ubuntu 24.04:

```bash
sudo apt-get install -y cmake g++ \
  qt6-base-dev qt6-declarative-dev qt6-svg-dev qt6-tools-dev \
  qt6-webengine-dev qml6-module-qtwebengine
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/shammy
```

Data lives under `~/.local/share/shammy/` (SQLite). On first launch, an
existing LlamaChat database and MCP config are copied if Shammy’s paths
are empty. MCP config uses the
Claude Desktop `mcpServers` schema:

| OS | Path |
|---|---|
| Linux | `~/.shammy/config.json` |
| macOS | `~/Library/Application Support/Shammy/config.json` |
| Windows | `%APPDATA%\Shammy\config.json` |

On first launch, if that file is missing, Shammy imports `mcpServers` from
Claude Desktop (`~/.config/Claude/claude_desktop_config.json` on Linux) or from
a leftover `mcp.json`.

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/you"]
    }
  }
}
```

## Backends

| Preset | Default URL |
|---|---|
| Ollama | `http://127.0.0.1:11434/v1` |
| llama.cpp | `http://127.0.0.1:8080/v1` |

Paste either `http://host:port` or `http://host:port/v1`. Empty API keys are
sent as `ollama` (ignored by Ollama, often ignored by llama.cpp).

If the model list is empty, the backend is up but has nothing loaded — for
Ollama, `ollama pull <model>`.

## Artifacts

Ask the model for a standalone file, or instruct it via the built-in system
prompt to wrap output in:

```xml
<artifact identifier="slug" type="text/html" title="Title">
...
</artifact>
```

HTML and SVG artifacts preview in the side pane with JavaScript enabled
(CDN scripts allowed). Fragments are wrapped into a complete document for
preview and Save; a full `<!DOCTYPE html>` page is left as-is. Large
`html` / `svg` / `javascript` fences (15+ lines) are promoted even without
tags. Needs Qt WebEngine (`qt6-webengine-dev`).

**Export to Word** writes a `.docx` from the current HTML or markdown
preview using LibreOffice or OpenOffice (`soffice`) when a binary is
detected on `PATH` or in a common install location. The action is hidden
until that is true. Set a custom path in Settings → Advanced if the
auto-detect misses your install. Interactive pages (JavaScript
dashboards) flatten to a static document. Install with
`sudo apt-get install libreoffice`. The artifact in Shammy stays HTML
or markdown; Word is a download, not a second source of truth.

## Contributing

Bug reports and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).
