# Claude integration: next steps

Proposals to work with Claude from the code itself, not only from the
terminal, ranked by value. They come from reading Duet's code and from what
VS Code, JetBrains, Cursor and Zed offer (sources at the end).

## What Duet already has

- **Editor context with each prompt** (hooks): the file in front of you, the
  cursor, the selection and its text, the problems, the unsaved files.
- **Duet's MCP server**: 40+ tools that read the editor, git, terminals and
  diagrams and act in them; sensitive ones wait for *Allow*.
- **Snapshots** before each prompt, to undo what Claude did.
- **Unsaved edits guard**: a `PreToolUse` hook refuses Claude's edit of a file
  with unsaved changes.
- **Send to Claude** from the file tree (Ctrl+Alt+K), commit messages and
  diagrams drawn by a separate `claude --print`.

What is missing: everything starts from the terminal. Nothing asks Claude
from the code itself, and nothing lets you sort Claude's changes before they
are written.

## Progress

Done: **2**, the editor's context menu. *Explain This Code*, *Review This
Code* and *Write Tests for This Code* are submitted to the session at once;
*Add to Claude's Prompt* (Ctrl+Alt+K in the editor) types `@file#L10-24`
without sending it. They name the selected lines, or the cursor's line and
the word under it, and warn Claude when the file has unsaved changes
(`src/agent/vendor/claude/CodeRequest`). The same actions are in the *Claude* menu, so
the command palette offers them too.

## A. Ask Claude from the editor

### 1. Ctrl+K on the selection

A small field opens above the code, with two modes:

- **Edit**: Claude rewrites the selection; the result shows as a diff in the
  editor with *Accept*, *Reject* and *Rephrase*. Runs as a separate
  `claude --print` (like `CommitMessage`), so it does not clutter the session
  and works with the user's subscription.
- **Ask**: the question goes to the Claude session, which already receives
  the selection as context.

Practitioners cite this select-and-describe workflow as faster than a chat
pane, because you never leave the code.

### 2. Editor context menu (done)

- *Explain*, *Review*, *Write Tests For*: canned prompts sent to the session
  (they can reuse the MCP prompts such as `review-selection`).
- *Add to Prompt*: inserts `@file#L10-24` in Claude's prompt without sending
  it; the equivalent of VS Code's `at_mentioned`, the most used day to day.

### 3. "Fix with Claude" where the error is

On an underlined problem (in the quick fix light bulb), on a row of the
Problems pane, on a failed run: Claude receives the message, the location and
the output. It is the question asked most often.

### 4. "Why is this code here?" in the gutter

On a block of lines, Claude receives their history (`git log -L`) and explains
it. Useful in old code nobody remembers.

## B. Review what Claude writes (the real lever)

### 5. Review before writing, hunk by hunk

The `PreToolUse` hook that `UnsavedEditGuard` already uses can intercept
Edit/Write, show the diff in Duet and let the user accept or reject each
hunk; a rejection goes back to Claude with its reason. Behind a setting, so it
does not slow down users who let Claude work.

VS Code's extension only accepts or rejects a whole change; hunk-by-hunk
review is an open request there, and a Cursor regression on this point
drew complaints. It would set Duet apart.

### 6. Margin comments, sent as one review

After a prompt, annotate the changed lines ("not here", "rename this"), then
*Send Review* gives them all to Claude at once, like a pull request review.
Far more precise than describing lines in words.

### 7. Plan review

When Claude leaves plan mode (`ExitPlanMode`), the plan opens in a Markdown
tab you can comment on, then *Approve* or *Send Back* with the comments.

## C. Official protocols: to evaluate, not a priority

### 8. Claude Code's IDE protocol (`/ide`)

A WebSocket server with a lock file in `~/.claude/ide/<port>.lock`, and tools
such as `openDiff` (blocking, returns `FILE_SAVED` or `DIFF_REJECTED`),
`getDiagnostics`, `getCurrentSelection`, plus `selection_changed` and
`at_mentioned` notifications. It would give the native "In file.cpp"
indicator and Claude's own diff flow. But it is not officially documented
(reverse-engineered by the community), and Duet's MCP server and hooks
already cover most of it.

### 9. ACP or stream-json

A real chat pane instead of the terminal, through the Agent Client Protocol
(Zed's adapter wraps Claude's SDK) or `--input-format stream-json`. A large
piece of work, Zed's adapter asks for an API key, and it would duplicate the
terminal.

## What looks like a gimmick

- **Completion or next edit prediction by Claude**: latency and cost do not
  fit, and the subscription is not meant for it; the tools that do it well
  use dedicated small models.
- **Explanation on hover**: noisy and costly.
- **A chat pane that duplicates the terminal.**

## Suggested order

1. **5** — hunk-by-hunk review: what sets Duet apart the most.
2. **1** — Ctrl+K *Edit* / *Ask*.
3. **3** — *Fix with Claude*: light, it reuses the requests of 2.
4. **6** and **7** — margin comments and plan review.

## Sources

- [coder/claudecode.nvim — PROTOCOL.md](https://github.com/coder/claudecode.nvim/blob/main/PROTOCOL.md)
- [Zed discussion #58338 — native Claude Code IDE integration](https://github.com/zed-industries/zed/discussions/58338)
- [Claude Code IDE for Sublime Text](https://packagecontrol.io/packages/Claude%20Code%20IDE)
- [IDE connection (nikiforovall)](https://nikiforovall.blog/claude-code-rules/tips-and-tricks/ide-connection/)
- [Zed — Claude Code via ACP](https://zed.dev/blog/claude-code-via-acp)
- [harukitosa/claude-code-acp](https://github.com/harukitosa/claude-code-acp)
- [What is the Agent Client Protocol?](https://www.morphllm.com/agent-client-protocol)
- [Claude Code headless mode](https://docs.claude.com/en/docs/claude-code/headless)
- [anthropics/claude-code #31395 — per-hunk diff approval](https://github.com/anthropics/claude-code/issues/31395)
- [Claude Code in VS Code (morphllm)](https://www.morphllm.com/claude-code-vscode)
- [Claude Code with VS Code and JetBrains (noqta)](https://www.noqta.tn/en/blog/claude-code-ide)
- [Cursor forum — edits applied without diff approval](https://forum.cursor.com/t/regression-ai-edits-applying-automatically-without-diff-approval-ui/154887)
- [Real Python — Cursor vs Copilot](https://realpython.com/cursor-vs-copilot/)
- [DataCamp — Cursor vs VS Code](https://www.datacamp.com/blog/cursor-vs-vs-code)
- [Zed — edit prediction](https://zed.dev/edit-prediction)
