# Next Version: Proposed Evolutions

Compared against Claude Code's official IDE integration (VS Code, JetBrains)
and the 2026 agentic coding tools (Codex app, Claude Code desktop, Parallel
Code).

## What Duet already does well

Following Claude through hooks, a snapshot before each prompt, staging by
hunk or line, Claude's state at a glance, usage, history and IntelliJ-style
navigation. That is ahead of many existing editors.

## What is missing (checked in `src/`)

- No LSP client: no completion, no diagnostics, no Find Usages, no Rename.
- No code folding, no multiple cursors, no blame.
- Git: no branches, push/pull, stash or merge conflict resolution.
- No implementation of the **`ide` MCP server** that Claude Code expects
  from an IDE.

## Proposals, by priority

### 1. Become an "IDE" in Claude Code's sense (highest impact)

The CLI looks for a `~/.claude/ide/<port>.lock` file (auth token,
WebSocket on 127.0.0.1) and connects to it automatically. By implementing
this server, Duet would get:

- **The selection and active file sent to Claude with every prompt**,
  without Ctrl+Alt+K (the transcript shows "⧉ Selected N lines from…").
- **`openDiff`: review each edit *before* Claude writes it**, in the Diff
  view, with Accept / Reject. Today the only option is going back afterwards
  through a snapshot.
- **`getDiagnostics`**: Claude reads the compiler errors itself and fixes
  them (requires item 2).

Risk: this protocol's internal tools are not documented as a stable API.
Draw on the community implementations (Sublime, Neovim) and expect breakage
across CLI versions.

### 2. LSP client (clangd, jdtls, typescript-language-server)

What everyone expects from an editor: underlined diagnostics and a
*Problems* panel, completion, hover, *Find Usages*, *Rename*, formatting.
The current tree-sitter-based Go to Definition stays as a fallback.

### 3. Review by prompt, building on the existing snapshots

- **Prompt timeline**: for each prompt, its text, the diff of what it
  changed, and "undo this prompt". A snapshot is already taken before each
  prompt, so the cost is low.
- **Comments in the diff, sent back to Claude as a prompt**, like a GitHub
  review ("line 42: handle the null case").

### 4. Parallel sessions with git worktrees

The big trend of 2026. Several Claude tabs, each in its own worktree and
branch, with a status dot per session. Compare them, merge the winner and
discard the rest.

### 5. Plan mode and session history

- Open Claude's plan in the Markdown viewer (cmark-gfm), with comments.
- A list of past conversations (`~/.claude/projects/*.jsonl`), searchable,
  resumed with `claude --resume`.

### 6. Editor basics

Code folding (native in QScintilla), multiple cursors, split editor, format
on save (clang-format, prettier), a preferences dialog with configurable
shortcuts.

### 7. Everyday git

Switch and create branches, fetch/pull/push, stash, blame in the margin,
three-way conflict resolution (conflicted files are already detected in the
tree).

### Later

A debugger via DAP (gdb/lldb, java-debug, node), which is a large
undertaking, and a test explorer (ctest, JUnit, jest).

## Recommendation

Start with 1 + 2 together: that is what turns Duet from an observer of
Claude into a real partner. Claude sees the selection, you approve its edits
before they apply, and it fixes its own errors. Then item 3, which is cheap
and unique.

## Sources

- [Claude Code – Use Claude Code in VS Code (official docs)](https://code.claude.com/docs/en/vs-code)
- [Claude Code IDE for Sublime Text (lock file / WebSocket protocol)](https://glama.ai/mcp/servers/Cognisant-llc/sublime-claude-code)
- [Claude Code VS Code extension guide 2026](https://thepromptshelf.dev/blog/claude-code-vscode-extension-complete-guide-2026/)
- [mcp.directory – IDE skill](https://mcp.directory/skills/ide)
- [Parallel code agents: worktrees, sandboxes, desktop patterns](https://docs.kanaries.net/topics/AICoding/parallel-code-agents)
- [Codex app worktrees explained](https://fenji.verdent.ai/guides/codex-app-worktrees-explained)
- [Parallel Code (open source)](https://rywalker.com/research/parallel-code)
- [Claude Code desktop redesign](https://www.hunted.space/dashboard/claude-redesigned)
- [Stack Overflow 2026 survey – Visual Studio Magazine](https://visualstudiomagazine.com/articles/2026/10/06/stack-overflow-2026-survey-vs-code-leads-visual-studio-takes-third.aspx)
- [Top 5 code editors/IDEs 2026](https://guptadeepak.com/tools/top-5-code-editors-ides-2026/)
- [LSP study, Chalmers 2022](https://www.cse.chalmers.se/~bergert/paper/2022-models-lspstudy.pdf)
