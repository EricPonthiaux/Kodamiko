# Terminal: proposed evolutions

## Today

Shells, run configurations, Claude's session and the agents (one per
worktree) open as tabs, which can move into a window of their own
(`DetachablePage`). Each terminal (`TerminalWidget` over libvterm's
`VtScreen`) has:

- selection, Copy and Paste in its context menu;
- a history of 10,000 lines (`maxScrollbackLines`);
- Ctrl+wheel zoom;
- mouse support for full-screen programs.

Missing: search, clickable links, splitting, and any idea of a "command".
A terminal bell is only a system beep (`QApplication::beep()`).

## Proposals, by value for the effort

| # | Proposal | What it adds | Effort |
|---|---|---|---|
| T1 | Clickable paths and links | `src/foo.cpp:42:7` in a compiler error, a failed test or a Claude reply opens in the editor at that line with Ctrl+click. Plain URLs and OSC 8 links (`ls --hyperlink`, `gcc`) open in the browser. Underlined on hover. | Medium |
| T2 | Send to Claude | Select text in any terminal, then "Send to Claude" in the menu (or a shortcut): the text goes into the session's prompt, quoted, with the terminal's name. Builds on what Generate and Ask already use (`typeInClaudePrompt`). | Small |
| T3 | Search in the terminal | A search bar over the terminal (Ctrl+Shift+F, a shortcut programs leave alone): highlights every match in the history, Enter / Shift+Enter to go from one to the next, "3 / 17". | Small to medium |
| T4 | Commands as blocks (shell integration) | Duet starts the shells, so it can teach them to mark their prompts (OSC 133, as VS Code and Warp do for bash, zsh and PowerShell). Each command and its output become a block. See [T4 in detail](#t4-in-detail). | Large |
| T5 | Activity on the tabs | A program's bell, or output in a tab you aren't looking at, marks that tab (a dot) instead of beeping. Claude's or an agent's session waiting for a permission or finished shows on its tab, with a system notification if Duet is in the background. | Small |
| T6 | Two terminals side by side | Split the terminal area in two (for example Claude's session on the left, a shell or a run configuration on the right), with a draggable divider. | Medium |
| T7 | Terminals that come back | When a project reopens, its shells come back in their folders, along with the run configurations that were open. The history isn't kept, only the tabs. | Medium |

### T4 in detail

Each command block gives:

- a green or red mark in the margin for its exit code;
- Ctrl+↑ / Ctrl+↓ to go from prompt to prompt;
- "Copy the output";
- on a failed command, "Ask Claude why", which sends the command, its output
  and the exit code.

## Recommended order

1. **T2, then T1**: they connect the terminal to the rest of Duet (Claude,
   the editor) cheaply.
2. **T3 and T5**: comfort.
3. **T4**: the most ambitious, and the one that most changes how you work.
4. **T6 and T7** can wait.
