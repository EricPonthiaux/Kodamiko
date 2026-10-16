# Claude Code integration surfaces for third-party editors and GUIs (state as of Oct 2026)

Researcher notes. Version numbers below come from the official docs as fetched on 2026-10-15 (Claude Code is in the v2.1.2xx range at this point). Integration points: (1) the IDE MCP-over-WebSocket protocol (lock file in `~/.claude/ide`), (2) hooks, (3) headless `claude -p` / Agent SDK / ACP, (4) statusline JSON, checkpoints, session files, (5) terminal-level signals (OSC notifications, OSC 7501 status, rendering modes).

## How does the official IDE integration work, and can a third-party editor implement it?

### Takeaway
The official VS Code/JetBrains integration is a local MCP server over WebSocket. The IDE hosts it and writes a lock file to `~/.claude/ide/<port>.lock`. The CLI connects to it, sends selection and open-file context with every prompt, opens diffs in the editor (`openDiff` blocks until the user accepts or rejects), and exposes `getDiagnostics` to the model. The protocol is not officially specified. Still, coder/claudecode.nvim (Neovim) and claude-code-ide.el (Emacs) have reimplemented it from the minified VS Code extension. A Qt editor like Enlumine can do the same with QWebSocketServer.

### Cited Findings
- **Discovery:** the IDE starts a WebSocket server on a random port between 10000 and 65535. It writes a lock file `~/.claude/ide/[port].lock` containing JSON `{pid, workspaceFolders[], ideName, transport:"ws", authToken}`. When the IDE launches Claude itself, it sets `CLAUDE_CODE_SSE_PORT=<port>` and `ENABLE_IDE_INTEGRATION=true`. Claude matches that port against the lock files and connects. — [claudecode.nvim PROTOCOL.md](https://github.com/coder/claudecode.nvim/blob/main/PROTOCOL.md)
- **Official transport and security details:** the server binds to `127.0.0.1` only, on a random port in 10000–65535 that is not configurable, using unencrypted `ws://`. Each activation generates a fresh token. The CLI must send it in the `X-Claude-Code-Ide-Authorization` header. The lock file is mode `0600` inside a `0700` directory. If `CLAUDE_CONFIG_DIR` is set, the lock file goes to `$CLAUDE_CONFIG_DIR/ide/` instead. — [VS Code docs, "The built-in IDE MCP server"](https://code.claude.com/docs/en/vs-code)
- **Wire format:** JSON-RPC 2.0 over WebSocket, based on MCP spec 2025-03-26 with WebSocket replacing stdio/HTTP. The token is 32 lowercase hex characters (128 bits) from a CSPRNG. — [PROTOCOL.md](https://github.com/coder/claudecode.nvim/blob/main/PROTOCOL.md)
- **Notifications from IDE to Claude:**
  - `selection_changed`: `{text, filePath, fileUrl, selection:{start:{line,character}, end:{...}, isEmpty}}`.
  - `at_mentioned`: `{filePath, lineStart, lineEnd}`, sent when the user explicitly adds context.
  — [PROTOCOL.md](https://github.com/coder/claudecode.nvim/blob/main/PROTOCOL.md)
- **The 12 MCP tools the IDE implements:**
  - `openFile(filePath, preview, startText, endText, selectToEndOfLine, makeFrontmost)`
  - `openDiff(old_file_path, new_file_path, new_file_contents, tab_name)`: blocking, returns `FILE_SAVED` or `DIFF_REJECTED`
  - `getCurrentSelection`, `getLatestSelection`
  - `getOpenEditors`: returns `uri, isActive, label, languageId, isDirty`
  - `getWorkspaceFolders`
  - `getDiagnostics(uri?)`
  - `checkDocumentDirty`, `saveDocument`
  - `close_tab`: returns `TAB_CLOSED`
  - `closeAllDiffTabs`: returns `CLOSED_<n>_DIFF_TABS`
  - `executeCode`: Jupyter only
  — [PROTOCOL.md](https://github.com/coder/claudecode.nvim/blob/main/PROTOCOL.md)
- **What the model sees:** only two tools are exposed to the model, `mcp__ide__getDiagnostics` and `mcp__ide__executeCode`. The rest are internal RPC the CLI uses for its own UI (diffs, selection, saves). The server is named `ide` and is hidden from `/mcp`. Organizations that allowlist MCP tools through a `PreToolUse` hook need to account for it. — [VS Code docs](https://code.claude.com/docs/en/vs-code)
- **Selection context:** while connected, the CLI attaches the current selection and the active file path to every prompt. The transcript shows `⧉ Selected N lines from <file>`. A queued message keeps the selection that existed when Enter was pressed. — [VS Code docs](https://code.claude.com/docs/en/vs-code)
- **Diagnostics in the VS Code chat panel:** from v2.1.285, the graphical panel uses a separate built-in server, `claude-vscode`, which exposes `mcp__claude-vscode__getDiagnostics`. — [VS Code docs](https://code.claude.com/docs/en/vs-code)
- **`executeCode` safety:** in VS Code the code is always inserted as a new cell and the user confirms through a Quick Pick (Execute/Cancel). Nothing runs silently. — [VS Code docs](https://code.claude.com/docs/en/vs-code)
- **Connection modes:** running `claude` in the IDE's integrated terminal auto-connects. From an external terminal, use `claude --ide` or the `/ide` command to pick a running IDE. `--bare` skips the automatic IDE connection unless `--ide` is passed. — [claudecode.nvim README](https://github.com/coder/claudecode.nvim); [headless docs](https://code.claude.com/docs/en/headless)
- **claudecode.nvim UX:**
  - Diffs open as native Neovim diffs. `:w` accepts, `:q` rejects, and the proposal can be edited before accepting.
  - Diffs close when the session disconnects. Autocmds `ClaudeCodeDiffOpened` / `ClaudeCodeDiffClosed` fire on the lifecycle.
  - Terminal providers: `snacks`, `native`, `external`, `none`. With `none`, the server runs without a terminal and the user connects with `/ide`.
  - Debugging: `:checkhealth claudecode`.
  — [claudecode.nvim](https://github.com/coder/claudecode.nvim)
- **claude-code-ide.el (Emacs):**
  - Diffs use ediff. Buffer B (Claude's proposal) is editable before accepting, and the edits are sent back to Claude.
  - Diagnostics come from Flycheck or Flymake.
  - Context: the active file and region are shared, not file contents.
  - Extra MCP tools for Claude: `xref-find-references`, `xref-find-apropos`, `treesit-info`, `imenu-list-symbols`, `project-info`, plus `claude-code-ide-make-tool` to expose any Elisp function. An Elisp `executeCode` tool is enabled by default.
  - Terminal backends: vterm, eat, ghostel.
  — [claude-code-ide.el](https://github.com/manzaltu/claude-code-ide.el)
- **VS Code extension features worth knowing:**
  - The default is now a graphical chat panel. The `useTerminal` setting switches back to the CLI in a terminal.
  - Plan review/editing, `@file#5-10` mentions (Alt+K), multiple conversations in tabs, history.
  - Checkpoint actions in the chat: "Rewind code to here" and "Fork conversation and rewind code".
  - Context indicator and a prompt-cache countdown clock.
  — [VS Code docs](https://code.claude.com/docs/en/vs-code)

### Inferences
- **Recommended for Enlumine:** implement the lock-file + WebSocket MCP server with Qt's `QWebSocketServer` bound to `QHostAddress::LocalHost`, and start Claude in the embedded terminal with `CLAUDE_CODE_SSE_PORT` and `ENABLE_IDE_INTEGRATION=true` set. The minimal useful subset is:
  - `selection_changed` and `at_mentioned` notifications
  - `openDiff` (show Enlumine's diff view, reply `FILE_SAVED` / `DIFF_REJECTED`)
  - `getDiagnostics` (if Enlumine has tree-sitter or LSP diagnostics)
  - `getOpenEditors`, `getWorkspaceFolders`, `openFile`, `checkDocumentDirty`, `saveDocument`, `close_tab`, `closeAllDiffTabs`

  With this, Claude's edits flow through Enlumine's own diff UI before they hit disk, instead of the editor only reacting after the fact.
- `openDiff` blocks the CLI's tool call. The diff UI must therefore be non-modal and keep the request ID pending. It must also resolve the request (for example as rejected) when the tab is closed or the socket drops.
- Delete the lock file on exit and handle stale lock files. Because the protocol is reverse-engineered, pin the behavior to tested Claude Code versions and degrade gracefully.

### Gaps
- No official Anthropic specification of the IDE protocol exists. All tool schemas come from reverse engineering (claudecode.nvim says so explicitly), so they may change without notice.
- Not confirmed: whether JetBrains uses exactly the same protocol (it is widely assumed, but I did not fetch the JetBrains docs).
- Not found in sources: the exact semantics of the `openDiff` response when the user edits the proposal before accepting (claude-code-ide.el says edits are sent back, but the wire format was not documented).

## Hooks: which events exist and how do GUIs use them?

### Takeaway
As of late 2026 Claude Code has 33 hook events and 5 handler types (`command`, `http`, `mcp_tool`, `prompt`, `agent`). For a GUI, the most useful events are:
- `Stop` (turn finished, includes `last_assistant_message`)
- `PostToolUse` (file edited; for Bash, `tool_response.bashEditDiff` lists changed files)
- `Notification` (needs input / permission prompt)
- `PermissionRequest` (can allow or deny, so a GUI can show its own approval dialog)
- `SessionStart` / `SessionEnd`, `UserPromptSubmit`, `CwdChanged`, `FileChanged`

The `http` hook type lets Enlumine's HookServer receive POSTs directly, without a shell shim.

### Cited Findings
- **All 33 events:**
  - Session and setup: SessionStart, Setup, SessionEnd
  - Prompts: UserPromptSubmit, UserPromptExpansion
  - Tools and permissions: PreToolUse, PermissionRequest, PermissionDenied, PostToolUse, PostToolUseFailure, PostToolBatch
  - Display and notification: Notification, MessageDisplay
  - Subagents, tasks, teammates: SubagentStart, SubagentStop, TaskCreated, TaskCompleted, TeammateIdle
  - End of turn: Stop, StopFailure
  - Configuration and environment: InstructionsLoaded, ConfigChange, CwdChanged, DirectoryAdded, FileChanged
  - Worktrees: WorktreeCreate, WorktreeRemove
  - Compaction: PreCompact, PostCompact
  - Model switching: PreModelSwitch, PostModelSwitch
  - MCP elicitation: Elicitation, ElicitationResult
  — [Hooks reference](https://code.claude.com/docs/en/hooks)
- **Handler types:**
  - `command`: JSON on stdin; results via exit code and stdout.
  - `http`: POSTs the same JSON to a URL; the response body is the result. Fields: `url`, `headers`, `allowedEnvVars`, `onFailure`.
  - `mcp_tool`, `prompt`, `agent` (agent is experimental).
  - Common fields: `if` (permission-rule syntax such as `Edit(*.ts)`), `timeout`, `statusMessage`, `async`, `asyncRewake`.
  - `"onFailure":"block"` requires v2.1.295+.
  — [Hooks reference](https://code.claude.com/docs/en/hooks)
- **Timeouts:**
  - 600 s by default for command, http, and mcp_tool.
  - 30 s on UserPromptSubmit and the model-switch events; 10 s on MessageDisplay.
  - SessionEnd shares a budget of 1.5 s by default (can be raised to 60 s).
  - `async: true` runs the hook in the background without blocking.
  — [Hooks reference](https://code.claude.com/docs/en/hooks)
- **Common input fields:** `session_id`, `prompt_id`, `transcript_path`, `cwd`, `scratchpad_dir`, `permission_mode`, `effort`, `hook_event_name`. Subagents add `agent_id` and `agent_type`. — [Hooks reference](https://code.claude.com/docs/en/hooks)
- **Exit codes:** 0 means success, and stdout is parsed as JSON if it is a single object. 2 means a blocking error, with stderr as the reason. Other codes are non-blocking errors. — [Hooks reference](https://code.claude.com/docs/en/hooks)
- **SessionStart:** the matcher is `startup|resume|clear|compact|fork`. Output can include `additionalContext`, `initialUserMessage`, `sessionTitle`, `watchPaths`, `reloadSkills`. — [Hooks reference](https://code.claude.com/docs/en/hooks)
- **Stop:**
  - Input: `stop_hook_active`, `last_assistant_message`, `background_tasks`, `session_crons`. The docs recommend `last_assistant_message` over reading the transcript, which may not contain the final message yet at Stop time.
  - Stop does **not** fire on user interrupts. API errors fire `StopFailure` instead.
  - `decision:"block"` keeps Claude working, capped at 8 consecutive continuations.
  — [Hooks reference](https://code.claude.com/docs/en/hooks)
- **Notification:**
  - Types: `permission_prompt`, `idle_prompt`, `auth_success`, `elicitation_dialog`, `elicitation_url_dialog`, `elicitation_complete`, `elicitation_response`, `agent_needs_input`, `agent_completed`, `quota_auto_resume_*`.
  - Fields: `message`, `title`, `notification_type`. It cannot block.
  - In terminal sessions, `permission_prompt` fires only after about 6 s without typing, and `idle_prompt` about 60 s after Claude finishes. Treat them as "user is away" signals, not real-time state.
  — [Hooks reference](https://code.claude.com/docs/en/hooks)
- **PermissionRequest:**
  - Input: `tool_name`, `tool_input`, `permission_suggestions`. There is no `tool_use_id`.
  - Output: `hookSpecificOutput.decision.behavior` = `allow` / `deny`, with `updatedInput`, `updatedPermissions` (e.g. `addRules`, `setMode`), `message`, `interrupt`.
  - In headless sessions with no prompt, the call is denied if no hook decides.
  — [Hooks reference](https://code.claude.com/docs/en/hooks)
- **PostToolUse:**
  - Input: `tool_name`, `tool_input`, `tool_response`, `tool_use_id`, `duration_ms`. File paths are always absolute.
  - For Bash, `tool_response.bashEditDiff` can list the files a command changed in a git repo (best effort, public beta, v2.1.269+).
  - `updatedToolOutput` changes only what Claude sees.
  — [Hooks reference](https://code.claude.com/docs/en/hooks)
- **Hooks in other integrations:**
  - Notification hooks are the documented way to get alerts in terminals without native desktop notifications (VS Code integrated terminal, Warp). Alternatively, set `preferredNotifChannel: "terminal_bell"`. — [Terminal config](https://code.claude.com/docs/en/terminal-config)
  - Hooks run in `-p` sessions too, including the project's `.claude/settings.json` hooks, even in untrusted folders unless `--bare` is used. — [Headless docs](https://code.claude.com/docs/en/headless)

### Inferences
- Enlumine can declare `"type":"http"` hooks pointing at `http://127.0.0.1:<port>/hook` (with a secret header) instead of a shell script.
- Enlumine can pass them via `--settings <json>` at launch rather than writing them into the user's `~/.claude/settings.json`. The `--settings <file-or-json>` flag is documented in the headless page; that it also works in interactive mode is an inference to verify.
- Refreshing the git diff on `Stop` alone misses interrupted turns (Stop doesn't fire) and failed turns (`StopFailure`). Also refreshing on `PostToolUse` with matcher `Edit|Write|MultiEdit|NotebookEdit|Bash` (debounced) gives live updates and covers Bash-driven changes.
- `PermissionRequest` via HTTP lets a GUI show its own approval cards. If it does, the hook must answer quickly or be scoped carefully, since the TUI prompt is also on screen.

### Gaps
- Not confirmed: the exact HTTP hook response semantics (status codes, timeouts, behavior when the server is down). The page section past character 200k was not read.

## Headless mode / Agent SDK / ACP vs. embedding the TUI

### Takeaway
`claude -p --output-format stream-json` and the Agent SDK give structured events: init metadata, assistant and tool messages, partial tokens, cost, permission callbacks, and retry events. They suit a fully custom GUI. ACP (Zed, JetBrains AIR) wraps the Agent SDK via `@agentclientprotocol/claude-agent-acp`. Embedding the real TUI keeps full feature parity (slash commands, `/rewind`, plan mode, every new feature on day one) and stays clearly on the "first-party CLI" side of Anthropic's subscription billing. That billing split was announced for 15 June 2026 and then postponed. Zed added "Terminal Threads" in May 2026 for this reason.

### Cited Findings
- **Headless basics:**
  - `claude -p` is now documented as "the Agent SDK via the CLI".
  - `--output-format text|json|stream-json`. `stream-json` with `--verbose --include-partial-messages` gives token deltas (`stream_event`). The last line is a `result` message with text, cost, and session metadata.
  - `--json-schema` gives structured output.
  — [Headless docs](https://code.claude.com/docs/en/headless)
- **Useful stream events:**
  - `system/init`: model, tools, MCP servers, plugins, `plugin_errors`, `mcp_server_errors`, and a `capabilities` array for feature detection.
  - `system/api_retry`: `attempt`, `max_retries`, `retry_delay_ms`, `error` category.
  - `permission_denied` system messages.
  - `parent_tool_use_id` for subagent messages.
  - JSON output includes `total_cost_usd` plus a per-model breakdown. These are client-side estimates.
  — [Headless docs](https://code.claude.com/docs/en/headless)
- **Session control:**
  - `--continue`, `--resume <id>` (or the path to a `.jsonl` transcript), and forking (`--fork-session`).
  - Permission hosts: `canUseTool` in the SDK, or `--permission-prompt-tool` (an MCP tool).
  - `--permission-prompts none` (v2.1.259+) for unattended runs.
  - `--permission-mode auto|dontAsk|acceptEdits|...`.
  — [Headless docs](https://code.claude.com/docs/en/headless)
- **`--bare`:** skips hooks, skills, plugins, MCP, CLAUDE.md, and OAuth/keychain, so it requires `ANTHROPIC_API_KEY`. It is recommended for scripts and will become the default for `-p` in a future release. Without `--bare`, `-p` runs project hooks and `.mcp.json` servers with no trust dialog. — [Headless docs](https://code.claude.com/docs/en/headless)
- **Signals:** SIGTERM exits with code 143 and leaves the turn unfinished. Send SIGINT or the SDK's `interrupt()` first to end the turn cleanly. — [Headless docs](https://code.claude.com/docs/en/headless)
- **ACP adapter:** `claude-agent-acp` (npm `@agentclientprotocol/claude-agent-acp`) implements an ACP agent on top of the official Claude Agent SDK.
  - Supported: edit review, diff patch extension, interactive and background terminals, permission requests, TODO lists, custom slash commands, images, @-mentions, client MCP servers, subagent transcripts.
  - ACP v2 is experimental (`CLAUDE_AGENT_ACP_EXPERIMENTAL_V2=1`).
  — [claude-agent-acp repo](https://github.com/zed-industries/claude-code-acp)
- **Claude Code in Zed:** Claude Code runs through Zed's bridge adapter because Anthropic has not adopted ACP natively. Permission requests go through `session/request_permission`. — [search summary of Zed/ACP sources](https://zed.dev/blog/codex-is-live-in-zed)
- **Billing:**
  - Anthropic announced that, from 15 June 2026, Agent SDK / `claude -p` / ACP / third-party SDK use on subscriptions would draw from a separate monthly "Agent SDK credit" ($20 Pro, $100 Max 5x, $200 Max 20x). The official CLI would keep normal subscription limits.
  - Update of 16 June 2026: the change "is not taking effect yet". Anthropic promised advance notice before any future change.
  — [Zed blog](https://zed.dev/blog/anthropic-subscription-changes)
- **Zed Terminal Threads (May 2026):** they run `claude` (or `codex`, `amp`) as managed threads in the Agent Panel sidebar, with title updates and attention notifications. Zed called them "the only way to keep using Claude Code in Zed with your existing Claude subscription" under the planned billing. Zed's framing: ACP gives tighter edit/review integration, while terminal threads give flexibility. — [Zed blog: Terminal Threads](https://zed.dev/blog/terminal-threads)
- **Reported ACP limits for external agents in Zed:** no PTY mode, no thread history resumption, no checkpointing. This comes from a third-party blog about Codex, not verified for Claude Code. — [Codex in Zed blog](https://codex.danielvaughan.com/2026/05/05/codex-cli-in-zed-parallel-agents-acp-integration-ide-workflows/)

### Inferences
- Enlumine's current architecture (TUI in a terminal plus hooks) is the lowest-risk, highest-parity choice and is safe under the billing uncertainty.
- The best upgrade path is TUI + IDE protocol (lock file) + hooks + statusline, rather than switching to stream-json.
- A stream-json or ACP mode could be an optional secondary backend for a "native chat panel". It costs maintenance against fast-moving message schemas and loses TUI-only features.

### Gaps
- No independent benchmark or user survey found comparing satisfaction with ACP vs. TUI integrations.
- The current (Oct 2026) status of the billing change after June 2026 is not verified beyond the 16 June update.

## Checkpoints, statusline, permission modes, sessions, usage/cost: what GUIs expose

### Takeaway
- **Checkpoints:** captured per user prompt, for file-tool edits only. Bash and subagent edits are not covered. Snapshots are stored with the session and swept after about 30 days.
- **Statusline:** a command receives a rich JSON on stdin (cost, context window %, rate limits, model, worktree, PR). It is the cleanest documented feed of live session metrics.
- **GUIs:** the Claude desktop app, Nimbalyst (successor of Crystal), Conductor and crystl converge on four features: parallel sessions in git worktrees, a diff review pane with line comments, a usage/context ring, and OS notifications when a session needs attention.

### Cited Findings
- **Checkpoint basics:**
  - A checkpoint is taken before each prompt that starts a turn. File snapshots are kept for the last 100 checkpoints, are saved with the conversation (so `/rewind` works after resume), and are deleted by the retention sweep about 30 days later (`cleanupPeriodDays`).
  - `/rewind` or Esc Esc offers: restore code and conversation, restore conversation, restore code, summarize from here, summarize up to here.
  — [Checkpointing](https://code.claude.com/docs/en/checkpointing)
- **Checkpoint limits:**
  - Not tracked: Bash file changes, edits by background subagents, external or manual edits, and messages typed mid-turn.
  - Symlinked and hard-linked files are skipped on restore.
  - "Not a replacement for version control."
  — [Checkpointing](https://code.claude.com/docs/en/checkpointing)
- **VS Code session-diff baseline:** each file's first snapshot is kept as the baseline for the extension's session diffs. — [Checkpointing](https://code.claude.com/docs/en/checkpointing)
- **Statusline JSON fields:**
  - Session: `session_id`, `session_name`, `prompt_id`, `transcript_path`, `version`, `output_style`.
  - Model and workspace: `model.{id,display_name}`, `workspace.{current_dir,project_dir,added_dirs,git_worktree,repo}`.
  - Cost: `cost.{total_cost_usd,total_duration_ms,total_api_duration_ms,total_lines_added,total_lines_removed}`.
  - Context: `context_window.{total_input_tokens,total_output_tokens,context_window_size,used_percentage,remaining_percentage,current_usage}`, plus `prompt_cache.{warm,ttl,expires_at,hit_ratio,...}`.
  - Settings state: `fast_mode`, `effort.level`, `thinking.enabled`, `vim.mode`.
  - Rate limits: `rate_limits.five_hour/seven_day.{used_percentage,resets_at}`. Pro/Max subscribers only, and only after the first API response. `spend_limit` behind a gateway requires v2.1.251+.
  - Other: `agent.name`, `pr.{number,url,review_state}`, `worktree.{name,path,branch,original_cwd,original_branch}`.
  — [Statusline docs](https://code.claude.com/docs/en/statusline)
- **Statusline update triggers:** session start or resume, each new assistant message, end of `/compact`, permission-mode change, vim toggle, `refreshInterval` timer, rate-limit reset, and cache expiry. Updates are debounced at 300 ms, and an in-flight script is cancelled when a new update arrives. — [Statusline docs](https://code.claude.com/docs/en/statusline)
- **Permission modes:** Manual (default), Accept edits, Plan, Auto (classifier), Bypass permissions. The desktop app has a mode selector (Cmd+Shift+M). `permissions.defaultMode` sets the default. — [Desktop docs](https://code.claude.com/docs/en/desktop); [Headless docs](https://code.claude.com/docs/en/headless)
- **Claude Code desktop app:**
  - Diff stats indicator (`+12 -1`) opening a file-list diff viewer, with line comments batched back to Claude.
  - `/code-review` card ("Walk through in diff", "Apply fixes").
  - Worktree per session (`.claude/worktrees/`), split view.
  - Usage ring (context plus plan usage), OS notifications when an unfocused session finishes.
  - Integrated terminal, file pane with external-change warnings, browser/preview pane with auto-verify, CI status and auto-fix.
  — [Desktop docs](https://code.claude.com/docs/en/desktop)
- **Third-party GUIs:**
  - opcode (Tauri): checkpoints with a session timeline, usage dashboard (tokens and cost), searchable session history, per-agent permissions. Single session. Reported unmaintained (vendor-authored claim).
  - crystl (macOS, closed source): worktree per agent, floating approval cards, an activity panel with all sessions' states.
  - CloudCLI: a web UI.
  — [crystl comparison (biased: published by crystl)](https://crystl.dev/compare/claude-code-guis/)
- **Crystal → Nimbalyst:** Crystal's final release was in Feb 2026 and pointed to Nimbalyst. Nimbalyst keeps parallel sessions in worktrees and adds a session kanban, WYSIWYG editors, and an iOS app. It was open-sourced under MIT on 30 Apr 2026 and runs on macOS, Windows, and Linux (Electron). Conductor is a proprietary, free, macOS-only app for parallel Claude Code/Codex agents in worktrees. — [search summary: rywalker.com, superset.sh, implicator.ai](https://rywalker.com/research/nimbalyst); [superset.sh comparison](https://superset.sh/compare/conductor-vs-crystal)

### Inferences
- Enlumine can get cost, context and rate-limit data without parsing the TUI by installing its own `statusLine` command. For example, a small script or `curl` that forwards stdin JSON to the HookServer and also prints a short line, ideally injected via `--settings`.
- Because native checkpoints don't cover Bash edits, Enlumine's own git-based Snapshots (seen in `src/git/Snapshots.*`) complement `/rewind`. This matches how the desktop app and the worktree GUIs rely on git.
- Valued features that recur across GUIs:
  - a diff review of the whole turn or session with line comments sent back to Claude
  - an attention or approval indicator per session
  - a context/usage meter
  - worktree-based parallel sessions
  - session history with resume

### Gaps
- No quantitative user data (surveys, download counts) found ranking which features users value most. The evidence is convergence across products plus vendor marketing.
- Conductor's specific features (checkpoints, cost display) were not verified from primary sources.

## Pitfalls reported by integrators

### Takeaway
The main pitfalls are:
- terminal rendering (flicker, resize reflow, Shift+Enter, GPU glitches)
- the blocking `openDiff` and editor auto-save interactions
- stale or insecure lock files
- an undocumented, shifting IDE protocol
- hooks that fire late (Notification) or not at all (Stop on interrupt)
- untrusted project hooks running under `-p`

### Cited Findings
- **Protocol pitfalls:** bind to 127.0.0.1 only, use a CSPRNG token, and fail rather than fall back to a weak token. Naming is inconsistent: `close_tab` and the `openDiff` parameters are snake_case, everything else is camelCase. `openFile`'s output differs depending on `makeFrontmost`. The protocol is reverse-engineered and may change. — [PROTOCOL.md](https://github.com/coder/claudecode.nvim/blob/main/PROTOCOL.md)
- **Auto-save:** auto-save plugins can accept diffs without user input. — [claudecode.nvim](https://github.com/coder/claudecode.nvim)
- **Large pastes:** large pastes can be truncated in some terminals (Neovim < 0.12.2; the VS Code integrated terminal drops characters). — [claudecode.nvim](https://github.com/coder/claudecode.nvim); [Terminal config](https://code.claude.com/docs/en/terminal-config)
- **Emacs terminal issues:** resizing the window causes uncontrollable scrolling during reflow (Claude Code bug #1422, workaround on by default). An init delay of about 0.1 s is needed to avoid misaligned prompts. vterm needs anti-flicker batching. Fullscreen mode (`CLAUDE_CODE_NO_FLICKER`) limits scrolling and search to the visible screen. Several instances share one working tree, so use worktrees. — [claude-code-ide.el](https://github.com/manzaltu/claude-code-ide.el)
- **Rendering:** for flicker, use fullscreen rendering (`/tui fullscreen` or `CLAUDE_CODE_NO_FLICKER=1`). If the terminal supports synchronized output but isn't detected (e.g. Emacs eat), use `CLAUDE_CODE_FORCE_SYNC_OUTPUT=1`. In VS Code, `/terminal-setup` turns off GPU acceleration to avoid garbled text. — [Terminal config](https://code.claude.com/docs/en/terminal-config)
- **Shift+Enter:** works out of the box on terminals with the kitty keyboard protocol (v2.1.269+). Otherwise users need `/terminal-setup` or Ctrl+J. — [Terminal config](https://code.claude.com/docs/en/terminal-config)
- **Notifications and status:** native desktop notifications are only sent in Ghostty, Kitty and iTerm2 by default (OSC escape sequences); other terminals need `preferredNotifChannel:"terminal_bell"` or a hook. Since v2.1.295, Claude Code reports working/waiting/done through the OSC 7501 Program Status Protocol if the terminal supports it. `CLAUDE_CODE_DISABLE_TERMINAL_TITLE=1` disables both terminal titles and status reporting. — [Terminal config](https://code.claude.com/docs/en/terminal-config)
- **Hook timing:** Notification `permission_prompt` / `idle_prompt` fire only after about 6 s / 60 s of inactivity. Stop doesn't fire on interrupt. The transcript may lack the final message at Stop time, so use `last_assistant_message`. — [Hooks reference](https://code.claude.com/docs/en/hooks)
- **`-p` security:** without `--bare`, `-p` runs project hooks and MCP servers with no trust dialog. — [Headless docs](https://code.claude.com/docs/en/headless)
- **Auto-install:** running `claude` in the VS Code terminal auto-reinstalls the extension (`autoInstallIdeExtension`, `CLAUDE_CODE_IDE_SKIP_AUTO_INSTALL`). — [VS Code docs](https://code.claude.com/docs/en/vs-code)

### Inferences
- For Enlumine's embedded terminal (libvterm-based, per `cmake/Libvterm.cmake`):
  - Support synchronized output (DEC mode 2026) and the kitty keyboard protocol to get Shift+Enter and flicker-free rendering.
  - Handle OSC 9/777 (notifications), OSC 0/2 (titles), and ideally OSC 7501 (status) for working/waiting/done indicators without hooks.
  - Batch repaints, and be careful with resize reflow.
- Treat hooks as a fallback: merge OSC 7501, hook, and statusline signals.

### Gaps
- Not examined: the OSC 7501 Program Status Protocol specification itself (which values and encoding Claude emits).
- Claude Code bug #1422 was cited from the Emacs package but not checked for its current status.
