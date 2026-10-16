# Agent-workflow features in AI-agent-centric code editors (2025-2026)

Research date: 2026-10-15. Many sources are vendor docs/marketing or third-party guides; this is flagged per item. About 16 tool calls, so coverage of Windsurf, Kiro and Copilot is thinner than coverage of Claude Code, Zed and Conductor.

## How do leading tools handle review, checkpoints, parallel agents, plan/spec modes, context, notifications, usage visibility and session history?

### Takeaway
By late 2026 the leading tools agree on a common feature set. It has six parts: (1) a multi-file diff review with accept/reject per hunk or change; (2) automatic per-prompt checkpoints with rewind of code and/or conversation; (3) parallel sessions isolated in git worktrees; (4) a plan mode whose output is an editable or commentable document; (5) @-mention context covering files, line ranges, symbols, terminal output and diffs; (6) status signals ("needs input", "done"), desktop notifications, and usage/limit dashboards. Tools differ in how well these pieces fit together, not in whether they have them.

### Cited Findings

**Reviewing agent changes**
- Claude Code VS Code extension: in Manual mode each edit opens a side-by-side diff with accept / reject / "tell Claude what to do instead". If the user edits the proposed content before accepting, Claude is told so it doesn't assume its original proposal was applied — [Claude Code docs: VS Code](https://code.claude.com/docs/en/vs-code)
- The same extension has per-change **Accept this change / Reject this change** buttons, plus "Accept/Reject Change at Cursor" commands (v2.1.275+). Diffs with more than 100 changes fall back to whole-file review — [Claude Code docs: VS Code](https://code.claude.com/docs/en/vs-code)
- Zed: the panel shows counts of changed files and lines. **Review Changes** (ctrl-shift-r) opens a multi-buffer tab where you accept/reject each hunk or everything at once. `agent.single_file_review` puts the same controls inline in each file — [Zed docs: Agent Panel](https://zed.dev/docs/ai/agent-panel)
- Cursor 2.0 (Oct 2025, older): a review interface gathers changes across files into one view. Changes can be accepted partially or fully per agent, and undo is per agent — [SD Times](https://sdtimes.com/ai/cursor-2-0-enables-eight-agents-to-work-in-parallel-without-interfering-with-each-other); [i-programmer](https://www.i-programmer.info/news/90-tools/18466-cursor-2-enables-multi-agent-working.html)
- Cursor 3.6 (May 2026) added an "Auto-review" run mode between Allowlist and Run Everything, so fewer approval prompts interrupt the agent. It reached the CLI in a June 2026 entry — [Cursor CLI changelog](https://cursor.com/docs/cli/changelog.md); [anycap tracker](https://anycap.ai/page/en-US/ai/cursor-changelog-july-2026) (dates differ between sources)
- VS Code Copilot agent mode: **Keep / Undo** controls in the editor or Chat view accept or reject individual or all edits — [VS Code docs: review code edits](https://code.visualstudio.com/docs/agents/run/review-code-edits)
- Conductor: per-workspace Diff Viewer (Cmd+Shift+D). A Checks tab tracks git status, CI, deployments, comments and todos. Each workspace has its own PR path — [Conductor docs](https://www.conductor.build/docs/guides/parallel-agents/run-multiple-claude-code-sessions)
- Warp: inline comments, snippets or files from Warp's code review can be sent straight into a running third-party agent session (Claude Code, Codex, Gemini CLI) — [Warp: Claude Code page](https://www.warp.dev/agents/claude-code) (vendor)
- Nimbalyst (formerly Crystal): visual diff review with accept/reject, AI-assisted commits, embedded terminal — [SitePoint](https://www.sitepoint.com/nimbalyst-the-visual-workspace-for-building-with-claude-code-and-codex/); [implicator.ai](https://www.implicator.ai/nimbalyst-claude-code-codex-mac-windows-linux/)

**Checkpoints / rewind**
- Claude Code (CLI): checkpoints capture code state before each edit, and every user prompt creates one. Rewind via Esc Esc or `/rewind`. Checkpoints persist across sessions, with cleanup after 30 days by default. **Limitation:** changes made through Bash (rm, mv, cp) and edits made outside Claude or by other concurrent sessions are not tracked — [Claude Code docs: Checkpointing](https://code.claude.com/docs/en/checkpointing)
- Claude Code VS Code: hover any message for "Rewind code to here" (keeps the conversation) or "Fork conversation and rewind code". An older ClaudeLog page said the extension had no rewind, which is now outdated — [Claude Code docs: VS Code](https://code.claude.com/docs/en/vs-code); contradicted by older [ClaudeLog FAQ](https://claudelog.com/faqs/claude-code-vscode-rewind-edit-conversations/)
- Zed: a "Restore Checkpoint" button on each user message restores the codebase, even after an interrupted edit — [Zed docs](https://zed.dev/docs/ai/agent-panel). Earlier Zed docs listed checkpointing, editing past messages and resuming history as **not supported for external agents such as Claude Code over ACP** (older; may have changed) — [Zed external-agents doc mirror](https://git.secluded.site/zed/blob/v0.223.5/docs/src/ai/external-agents.md?raw=1)
- VS Code Copilot: `chat.checkpoints.enabled` snapshots files before each request. Restoring drops later requests and resets files, with Redo available. `chat.checkpoints.showFileChanges` shows files and line counts per request. Editing a past prompt reverts that request's changes and all later ones — [VS Code docs](https://code.visualstudio.com/docs/agents/run/review-code-edits)
- Cursor: `/checkpoint` added to Cursor CLI in July 2026 — [techdevnotes](https://techdevnotes.com/releases/cursor-cli/2026.07.20-8cc9c0b)
- Windsurf: checkpoints exist, and Cascade can revert its changes, but per a secondary source the docs warn that a revert cannot itself be undone — [graphify.net](https://graphify.net/ai-coding-tools/windsurf/) (secondary)
- opcode (ex-Claudia, Tauri 2): visual session timeline with checkpoints, branching, one-click restore, and a diff between checkpoints — [implicator.ai](https://www.implicator.ai/claudia-gives-claude-code-a-gui-makeover-aiming-to-broaden-ai-coding-access/); [claudelog](https://claudelog.com/addons/claudia)

**Parallel agents / worktrees**
- Cursor 2.0: up to 8 agents in parallel, isolated with git worktrees or remote machines. Several models can attempt the same task and the user picks the best result — [SD Times](https://sdtimes.com/ai/cursor-2-0-enables-eight-agents-to-work-in-parallel-without-interfering-with-each-other)
- Conductor (Mac): one workspace per task. Each is a git worktree and branch with its own terminal, diff, checks and PR. Setup and run scripts, "Files to copy" / `.worktreeinclude` for gitignored files such as `.env.local`, a `CONDUCTOR_PORT` port range per workspace, archiving of unused workspaces, and a `.context` directory — [Conductor docs](https://www.conductor.build/docs/guides/parallel-agents/run-multiple-claude-code-sessions)
- Conductor's own comparison notes that Claude Code now ships a native `claude --worktree`. Conductor's added value is the sidebar, scripts, diff review, checks and PR flow — [Conductor vs Claude Code](https://conductor.build/compare/claude-code)
- Zed: several threads at once, and a title-bar worktree picker isolates a thread in a git worktree — [Zed docs](https://zed.dev/docs/ai/agent-panel)
- Nimbalyst: optional worktrees, each with its own editor context and agent sessions, plus a kanban-style session board. Free for individuals, MIT desktop core, Electron (reviewers mention resource cost) — [implicator.ai](https://www.implicator.ai/nimbalyst-claude-code-codex-mac-windows-linux/)
- Warp: agent management UI showing the status of every running agent, grouped in vertical tabs with git branch and PR metadata — [Warp](https://www.warp.dev/agents/claude-code); [SD Times on Warp 2.0](https://sdtimes.com/ai/warp-2-0-evolves-its-terminal-experience-into-an-agentic-development-environment/)
- Other worktree GUIs on Show HN: Superset, Agentastic.dev ("Ghostty + git worktrees"), Maestro (open source, described as a Conductor clone) — [HN Superset](https://news.ycombinator.com/item?id=46368739); [HN Agentastic](https://news.ycombinator.com/item?id=46501758); [dev.to](https://dev.to/jamilxt/three-ai-agents-one-laptop-the-git-worktree-trick-that-makes-parallel-coding-work-h5e)
- Claude Code VS Code: multiple conversations in tabs or windows. Session groups, filters (Needs input / Working / Completed) and an "Active" toggle — [Claude Code docs: VS Code](https://code.claude.com/docs/en/vs-code)

**Plan / spec modes**
- Claude Code VS Code: Plan mode opens the plan as a full Markdown document where you add **inline comments** before Claude starts editing. `/plan`, `/plan <task>` and `/plan open` commands. Can be set as the default via `initialPermissionMode` — [Claude Code docs: VS Code](https://code.claude.com/docs/en/vs-code)
- Kiro (AWS): spec-driven flow in three phases, written to `.kiro/specs/`: `requirements.md` (user stories with EARS-notation acceptance criteria), `design.md`, and `tasks.md` (a checkbox task list). Steering files hold persistent constraints and can load conditionally by file type. Hooks fire on agent events (agentSpawn, userPromptSubmit, preToolUse, postToolUse, stop) — [OpenReplay](https://blog.openreplay.com/kiro-aws-ai-coding-tool/); [lablab.ai](https://lablab.ai/ai-tutorials/aws-kiro-hooks-steering-custom-agents-deep-dive) (third-party; layouts differ between sources)
- Cursor: the agent shares a plan for approval before starting work triggered from Slack (July 2026). Plan Mode is described as a human intervention point — [anycap tracker](https://anycap.ai/page/en-US/ai/cursor-changelog-july-2026); [EvoMap](https://evomap.ai/blog/cursor-review) (opinion piece)
- Windsurf: "Plan and Act" with an explicit approval step (secondary, may be outdated) — [graphify.net](https://graphify.net/ai-coding-tools/windsurf/)

**Context management**
- Claude Code VS Code: the agent sees selected text automatically, and the footer shows the selected line count. Alt+K inserts `@file.ts#5-10`. Fuzzy @-mentions for files and folders. Selection is withheld for files matched by files.exclude or gitignore — [Claude Code docs: VS Code](https://code.claude.com/docs/en/vs-code)
- Zed: `@` can add files, directories, symbols, past threads, skills, diagnostics, branch diffs or URLs, plus buffer or terminal selections and pasted images — [Zed docs](https://zed.dev/docs/ai/agent-panel)
- Windsurf: per an Aug 2026 guide, Memories apply only to the legacy Cascade agent. The new default "Devin Local" agent uses skills instead, and a migration wizard is provided — [MemoryLake](https://www.memorylake.ai/en/blogs/stop-windsurf-cascade-losing-context) (third-party)
- Kiro steering files (see above). Conductor `.context` directory — [Conductor docs](https://www.conductor.build/docs/guides/parallel-agents/run-multiple-claude-code-sessions)

**Notifications / waiting for input**
- Claude Code hooks: the `Notification` event has the matchers `permission_prompt` (approval needed) and `idle_prompt` (finished, waiting for the next prompt). `Stop` fires after every response and can't take a matcher. Built-in terminal notifications only appear automatically in some terminals (Ghostty, Kitty, iTerm2 per one guide) — [Claude Code hooks guide](https://code.claude.com/docs/en/hooks-guide.md); [alexop.dev](https://alexop.dev/posts/claude-code-notification-hooks/); [vibeisland](https://vibeisland.app/guides/claude-code-notifications-mac/)
- Claude Code VS Code: a coloured dot on the tab icon, blue for a pending permission and orange for "finished while hidden". The screen reader announces permission prompts, questions, and a finished plan awaiting review — [Claude Code docs: VS Code](https://code.claude.com/docs/en/vs-code)
- Zed: `agent.notify_when_agent_waiting` (desktop notification), `agent.play_sound_when_agent_done`, and `agent.prevent_idle_sleep` — [Zed docs](https://zed.dev/docs/ai/agent-panel)
- Warp: a unified notification center across all coding agents and terminals, with in-app and system notifications when an agent finishes or needs input — [Warp](https://www.warp.dev/agents/claude-code) (vendor)

**Cost / token / usage visibility**
- Claude Code `/usage`: for plan users, usage bars for session and weekly limits with reset countdowns. It flags behaviours that account for 10% or more of recent usage (cache misses, long context, subagent-heavy or parallel sessions) with tips, and shows attribution tables and a Day/Week toggle. API-key users see session cost and tokens instead — [Claude Code docs: VS Code](https://code.claude.com/docs/en/vs-code)
- Zed: token usage of the active thread shown near the profile selector. Auto-compaction and `/compact` — [Zed docs](https://zed.dev/docs/ai/agent-panel)
- opcode: real-time usage and cost dashboard broken down by model and project — [implicator.ai](https://www.implicator.ai/claudia-gives-claude-code-a-gui-makeover-aiming-to-broaden-ai-coding-access/)

**Session history**
- Claude Code VS Code: Session history is searchable by keyword or time, and resuming restores full history and plan mode. A Web tab lists claude.ai cloud and Remote Control sessions. Cmd+Shift+T reopens a closed session. A `vscode://anthropic.claude-code/open?prompt=…&session=…` URI handler is available — [Claude Code docs: VS Code](https://code.claude.com/docs/en/vs-code)
- Zed: Thread History across projects, a Threads Sidebar, and archiving — [Zed docs](https://zed.dev/docs/ai/agent-panel)
- Cursor v3.11 (July 2026): "Side Chats" and "Conversation Search" — [havoptic tracker](https://www.havoptic.com/tools/cursor) (tracker)
- Claude Code VS Code also has `/btw` side questions that answer in a separate panel without adding to the context, and a "Focus view" that hides tool calls — [Claude Code docs: VS Code](https://code.claude.com/docs/en/vs-code)

### Inferences
- Review granularity matters. Claude Code's GUI, Zed and Copilot all now go below file level to hunk or change level, so per-hunk accept/reject is expected rather than a differentiator. Enlumine already supports stage/unstage by line, which is finer than most.
- Rewind is converging on three operations: code only, conversation only, and fork plus code. All tools carry the same Bash-blindness caveat. A git-snapshot-based checkpoint in a GUI (Enlumine already snapshots files before discards) could cover what Claude's own checkpoints miss: bash edits, manual edits, and concurrent sessions.
- Plans are becoming documents you annotate (Claude Code VS Code inline comments, Kiro spec files) rather than chat messages.

### Gaps
- No official Windsurf docs were fetched. Checkpoints, plan mode and memories come only from secondary sources.
- Cursor rules files (`.cursor/rules`), the cross-tool `AGENTS.md` convention and CLAUDE.md editing UIs were not researched in this pass.
- Nothing found on GitHub "Agent HQ" / mission control for multiple Copilot sessions.
- Crystal's original feature set before the Nimbalyst rename was not verified from its repo.

## Which features are repeatedly cited as "killer features" or reasons for switching?

### Takeaway
The most-cited value is managing several agents and reviewing their output quickly: worktree isolation plus a fast, consolidated diff review. Commentators consistently say human review, not generation, is the bottleneck. Checkpoints/rewind and plan approval are cited as the safety net that makes letting agents run tolerable.

### Cited Findings
- Commentary on Conductor says the diff review UI is the core value: "you see the diff, you see what changed, you decide whether to keep it". A critical transcript calls it "git worktrees under the hood… the product is the orchestration layer and the review UI" (source of unclear provenance) — [Conductor workflows](https://www.conductor.build/workflows); [podcast transcript](https://sthbxxuzklalnaiaeagd.supabase.co/storage/v1/object/public/exploring-next-audio/episodes/conductor-run-parallel-coding-agents-on-your-mac-1781630495314.txt)
- HN (Conductor Show HN): the bottleneck "was never the AI not being able to write 10 different POCs… simultaneously" but human review and steering — [HN 44594584](https://news.ycombinator.com/item?id=44594584)
- HN (Superset Show HN): unstructured review is the real problem, so worktrees need narrow scope and small diffs — [HN 46368739](https://news.ycombinator.com/item?id=46368739)
- Parallelism makes merge, testing and review worse unless work is isolated, scoped and validated — [aakashx.com](https://www.aakashx.com/blog/parallel-claude-code-agents/)
- Cursor 2.0's headline features were parallel agents (up to 8) and best-of-N model comparison with a consolidated review — [SD Times](https://sdtimes.com/ai/cursor-2-0-enables-eight-agents-to-work-in-parallel-without-interfering-with-each-other). A hands-on review found it useful but still struggling on complex projects — [lilys.ai](https://lilys.ai/en/notes/cursor-20-20251106/cursor-2-review-game-changer-or-hype)
- The spread of worktree-orchestrator launches on HN in 2025-2026 (Conductor, Superset, Agentastic, Maestro) shows strong demand — [HN Agentastic](https://news.ycombinator.com/item?id=46501758); [dev.to](https://dev.to/jamilxt/three-ai-agents-one-laptop-the-git-worktree-trick-that-makes-parallel-coding-work-h5e)
- Kiro's spec-first workflow is its main positioning — [softwareseni](https://www.softwareseni.com/aws-kiro-amazons-spec-first-bet-on-agentic-development/)
- Approval fatigue drives features such as Cursor's Auto-review mode and Claude Code's Auto permission mode (the default from v2.1.283) — [Cursor CLI changelog](https://cursor.com/docs/cli/changelog.md); [Claude Code docs: VS Code](https://code.claude.com/docs/en/vs-code)

### Inferences
- In order of how often they come up: (1) parallel isolated sessions with a status overview, (2) fast multi-file and hunk review that feeds comments back to the agent, (3) checkpoints/rewind, (4) plan review before edits, (5) notifications when attention is needed, (6) usage/limit visibility. Items 5-6 are rarely headline features but are "quality of life" requirements that show up in guides and settings.
- Being able to send review comments back into the agent (Warp, the Claude Code plan comments) closes the loop and appears to be an emerging differentiator.

### Gaps
- No quantitative survey (for example a Stack Overflow or JetBrains survey) ranking these features was found in this pass. The ranking above is qualitative.
- Few independent reviews of Conductor or Nimbalyst exist. Most claims come from vendors.

## What gaps exist for terminal-centric agents like Claude Code that a companion GUI can fill?

### Takeaway
Claude Code's CLI leaves visual, multi-session and review work to companions. The gaps are: a consolidated diff and hunk review, a visual checkpoint timeline, a dashboard of parallel sessions and worktrees with "needs input" states, desktop notifications in any terminal, usage and limit display, plan documents you can annotate, and session history browsing. The ecosystem (Conductor, opcode, Nimbalyst, Warp, Zed ACP, the official VS Code extension) exists largely to fill these gaps.

### Cited Findings
- The CLI's checkpoints have no visual timeline, and Bash or external edits aren't tracked — [Claude Code checkpointing](https://code.claude.com/docs/en/checkpointing). opcode fills this with a visual timeline, branching and diffs between checkpoints — [claudelog](https://claudelog.com/addons/claudia)
- A comparison table describes CLI checkpoints as manual git commits, while the extension's are automatic with one-click rewind (third-party; partly outdated given native `/rewind`) — [morphllm](https://www.morphllm.com/claude-code-vscode)
- Terminal notifications only work automatically in some terminals, so users write hook scripts (osascript, sounds) — [vibeisland](https://vibeisland.app/guides/claude-code-notifications-mac/); [alexop.dev](https://alexop.dev/posts/claude-code-notification-hooks/). The hook events `permission_prompt` and `idle_prompt` are exactly the signals a GUI can consume — [Claude Code hooks guide](https://code.claude.com/docs/en/hooks-guide.md)
- Native `claude --worktree` exists, but Conductor's pitch is that setup and run scripts, gitignored file copying, port allocation, diff review, checks and PR flow are missing around it — [Conductor vs Claude Code](https://conductor.build/compare/claude-code)
- Over ACP, Zed historically lacked checkpointing, history resume and message editing for Claude Code. Embedding the agent loses features unless the GUI rebuilds them (older doc) — [Zed external-agents doc mirror](https://git.secluded.site/zed/blob/v0.223.5/docs/src/ai/external-agents.md?raw=1)
- The official VS Code extension shows what Anthropic itself thinks the GUI should add: plan-as-document with inline comments, per-change diff review, session list with status filters and groups, status dots, `/usage` dialog, Focus view, and a URI handler for launching or resuming sessions — [Claude Code docs: VS Code](https://code.claude.com/docs/en/vs-code)
- Warp adds cross-agent notification center, agent status UI, and review comments piped into the agent session — [Warp](https://www.warp.dev/agents/claude-code)

### Inferences (Enlumine-specific)
- Enlumine already has an embedded terminal, HookServer, a git Diff mode with line-level staging, History and snapshots. That puts it close to the "review cockpit" that HN commenters call the bottleneck. The highest-leverage additions suggested by this research:
  1. **Session status from hooks**: map `Notification` (permission_prompt / idle_prompt), `Stop` and `PreToolUse` into a tab badge (needs input / working / done) plus OS desktop notifications. This works independently of the terminal emulator.
  2. **Per-turn change review**: use `PostToolUse` Edit/Write events or a snapshot at `UserPromptSubmit` to show "files changed by this prompt", with hunk revert. This mirrors Zed's Review Changes and Copilot's `showFileChanges`.
  3. **GUI checkpoint timeline** built on Enlumine snapshots. It covers Bash-made and manual edits that Claude's `/rewind` misses.
  4. **Worktree sessions**: create a worktree per Claude tab, with setup script, `.env` copying and a port variable (the Conductor pattern), and archive when done.
  5. **Send selection or review comments to the agent**: insert `@file#L5-10` or a comment into the terminal's input (Claude Code's Alt+K pattern, Warp's review-to-agent).
  6. **Plan view**: render the plan file as Markdown with comment-back.
  7. **Usage panel**: show limits, cost and tokens (from statusline JSON or `/usage` data), plus session history browsing of `~/.claude/projects` transcripts (as opcode does).
- These are design inferences, not sourced claims.

### Gaps
- Did not verify what data Claude Code exposes for usage programmatically (statusline JSON fields, transcript token fields). The claude-code-guide docs should be checked before relying on it.
- Did not research Claude Code's own desktop app or claude.ai/code GUI features, which may overlap with this list.
