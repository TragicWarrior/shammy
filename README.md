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
- Agent skills (`SKILL.md` folders) and shell commands, each command approved first
- Markdown bubbles, code copy, regenerate / edit-resend, stop
- Text and image attachments (vision as `image_url` data URLs), plus Word, spreadsheet and PDF files when the tools to read them are installed
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

macOS (Homebrew; `poppler` is optional, for PDF files):

```bash
brew install cmake qt openssl@3 poppler
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # macOS: add -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix openssl@3)"
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/linux/shammy                           # macOS: ./build/mac/shammy
```

A Debian package is a release build. The `.deb`, `.changes`, and `.buildinfo` are written to `releases/` (gitignored; the folder's `.gitignore` is tracked):

```bash
cmake --build build --target deb
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

A backend that answers Ollama's `/api/version` is sent chats on Ollama's own
API (`/api/chat`), not its OpenAI-compatible one, which drops the context size.
The per-model context size set in Settings → Models is then what Ollama loads
the model with (up to what the model takes). Every other backend uses
`/v1/chat/completions`.

If the model list is empty, the backend is up but has nothing loaded — for
Ollama, `ollama pull <model>`.

Every model the backends list is offered in the model picker. To keep one out
of it, turn on **Hide from selection** on that model in Settings → Models. A
hidden model stays listed in Settings, where it can be shown again. Hiding the
model in use switches to the first one still offered, and opening a chat that
used a hidden model carries on with the selected model instead.

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

## Project files

Files added to a project are preloaded, as text, into every chat in it.

- **What can be added:** text files (source, markdown, CSV, logs...) are stored
  as they are. Word and OpenDocument files become `<name>.docx.txt`, each
  sheet of a spreadsheet becomes `<name>-<sheet>.csv`, and a PDF becomes
  `<name>.pdf.txt` (layout kept, so tables stay aligned). These use optional
  tools, found automatically or set in Settings → Advanced: LibreOffice or
  OpenOffice for Word and spreadsheets, and `pdftotext` (`poppler-utils`) for
  PDFs. A file type is supported only while its tool is found; otherwise the
  file is refused on the spot, with what to install. Conversion runs in the
  background. Images, archives and other non-text files are refused with a
  reason, rather than stored and pasted into the prompt as garbage. A scanned
  PDF has no text to read and is reported as needing OCR first.
- **How much is sent:** instructions and files together may use at most 40% of
  the model's context window (about 4 bytes to a token), never more than 256 KB,
  so the prompt always leaves room to talk. The window used is that of the model
  that is answering, so the same project sends less to a 16K model than to a
  256K one. No single file may take more than half of that budget. What does not
  fit is cut (or, if the budget is used up, left out and named); everything
  stays saved, and the usage bar in the project shows when something is cut.
- **Removing:** removing a file, or deleting a project, deletes its files from
  disk too. Only Shammy's own project folder is ever touched.

## Attachments

Files attached to a chat message go in as text by the same rules as project
files: text files as they are; Word, OpenDocument, spreadsheet and PDF files
converted with LibreOffice/OpenOffice or `pdftotext` while those are found, and
refused with what to install otherwise. Images go to models with vision.

- **Conversion** starts as soon as a file is attached and runs in the
  background. The chip shows "converting…", and the message can be sent once it
  is done. A file that cannot be read is dropped then, with the reason, before
  anything is sent.
- **Size:** the attachments of one message share the same budget as project
  files: 40% of the answering model's context window, never more than 256 KB,
  and one file at most half of that. Anything cut short or left out is marked
  in the message. A model with a big window therefore gets more of a large file
  than one with a small window.

## Web tools

With **Tools** on for a model, it can call `web_fetch` to read a page and,
with a search key set (Settings → General → Web search), `web_search`.

`web_fetch` reads a page as text and stops as soon as going on cannot help:

- **Size:** at most 1 MB of the page is read, then the download is cut off
  and the result is marked `[truncated]`. What the model receives is trimmed
  further, to about 24,000 characters of extracted text.
- **Type:** images, audio, video, PDFs, archives and other non-text responses
  are refused from their headers, before the body is read. A response that
  claims to be text but is binary is refused after its first few KB.
- **Time:** a fetch is abandoned when no data arrives for the *stall timeout*
  (Settings → Advanced, default 30 s). The timer restarts whenever more data
  arrives, so a slow but steady download is not cut off. Separately, one fetch
  may take at most the *page fetch time limit* (Settings → General → Web
  search, default 300 s) including redirects. Stop also cancels a fetch.
- **Where it may go:** only `http` and `https` addresses on the public
  internet. Loopback, private, link-local, multicast and other reserved
  ranges are refused (IPv4 and IPv6, including IPv4 addresses written as
  IPv6), as are single-label and local-network names such as `router`,
  `printer.local` and `*.lan`. A hostname is looked up first and refused if
  *any* address it resolves to is not public. Redirects are followed by hand
  and every hop gets the same checks. The check cannot stop a DNS server that
  answers differently the second time (DNS rebinding); it narrows that window
  but does not close it.

## Skills and commands

A skill is a folder holding a `SKILL.md`, in the same format other agent
harnesses use. Skills live in `~/.config/shammy/skills/` on Linux
(`$XDG_CONFIG_HOME/shammy/skills/` when that is set) and in
`~/Library/Application Support/Shammy/skills/` on macOS, next to `config.json`.
A Mac also reads `~/.config/shammy/skills/` after that, and when both hold a
skill of the same name the one in `Library` is used. Settings → Skills shows
the main folder and opens it:

```
~/.config/shammy/skills/
  sm-inventory/
    SKILL.md          name and description up top, then the instructions
    scripts/          anything else the instructions refer to
  weather/
    SKILL.md
```

```markdown
---
name: weather
description: Look up the forecast for a city. Use when asked about the weather.
---
Run `curl -s "wttr.in/<city>?format=3"` and report the result.
```

- **What the model sees:** only each skill's name and description, in the
  system prompt. When a request matches, it loads the instructions, or another
  file from the skill's folder, with the `load_skill` tool. Folders are read
  again for every reply, so a new or edited skill is picked up at once.
- **Needs Tools:** skills are offered only to a model with Tools on.
- **Running commands** (`run_command`) is off until turned on in
  Settings → Skills. Every command is shown before it runs; allow it once or
  for the rest of the chat. Nothing is allowed for good: each new chat asks
  again. Commands run in your shell (`$SHELL` when it is bash, zsh, sh, dash
  or ksh, else `/bin/sh`), in your home folder unless the model names another,
  with nothing on standard input, so a command that prompts for input fails.
  The model is told which system it is on, so it can write commands for the
  BSD tools on macOS.
- **Limits:** a command is stopped after 120 seconds, together with anything
  it started; its output is kept to 32 KB (the start and the end), less for a
  small context window. Stop ends a running command too.

## Contributing

Bug reports and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).
