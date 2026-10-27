# Site screenshots

The pictures of `site/assets/screenshots/` are real captures of Duet, taken
offscreen on a fresh clone of this repository.

1. Build `build-debug` (`cmake --build --preset debug`).
2. Start Duet with the scene driver, at scale 2:

   ```sh
   tools/site-screenshots/capture.sh /tmp/shots 2 &
   ```

   Duet runs with its own HOME and `Duet.conf` (the default theme), on
   `/tmp/shots/Duet`; the user's settings are neither read nor written. Its
   Claude sessions use the real Claude Code installation and account: the
   prompts of a scene cost real usage, and Claude asks once to trust the
   folder (`focus TerminalWidget`, `key Down`, `key Return`).
3. Set a scene up by writing commands to `/tmp/shots/shots/command`, one per
   line (the list is at the top of `SceneDriver.cpp`); `done` appears once
   they ran. Files open, diagrams lay out and elements get selected through
   Duet's own MCP tools, by Claude or by `duet-mcp` with `DUET_SOCKET`
   pointing at the `/tmp/duet-<pid>-…` socket.
4. `grab <name>` saves each window at 3200 × 2000; the 1× images are these
   halved. `quit` ends Duet without saving its settings.

The scenes of the site:

| Image | Scene |
|---|---|
| `hero-editor`, `hero-claude` | Terminals detached; an error planted in `src/history/HistoryView.cpp:151`, cursor on it; the prompt “fix this” |
| `diagram`, `diagram-claude` | “Draw the path of a prompt through Duet…”, then “Where does Claude wait before touching the code? Show me in the diagram.” |
| `review` | `a0208aa` staged again (`git reset --soft`), Claude › Review Staged Changes, then Generate |
| `history` | History mode, `bc75e28` (made with Claude) unfolded |
| `edit` | Outline side tab, completion after `commits_->setSel` |

When a capture changes, check the sentences beside it in `site/index.html`
and `site/fr/index.html`: each picture shows what its text says.
