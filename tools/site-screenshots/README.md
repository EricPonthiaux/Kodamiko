# Site screenshots

The pictures of `site/assets/screenshots/` are real captures of Kodamiko, taken
offscreen on a fresh clone of this repository.

1. Build `build-debug` (`cmake --build --preset debug`).
2. Start Kodamiko with the scene driver, at scale 2:

   ```sh
   tools/site-screenshots/capture.sh /tmp/shots 2 &
   ```

   A third argument names another theme than `kodamiko-violet`, whose tones
   the site shares.

   Kodamiko runs with its own HOME and `Kodamiko.conf`, on
   `/tmp/shots/Kodamiko`; the user's settings are neither read nor written. Its
   Claude sessions use the real Claude Code installation and account: the
   prompts of a scene cost real usage, and Claude asks once to trust the
   folder (`focus TerminalWidget`, `key Down`, `key Return`).
3. Set a scene up by writing commands to `/tmp/shots/shots/command`, one per
   line (the list is at the top of `SceneDriver.cpp`); `done` appears once
   they ran. Files open, diagrams lay out and elements get selected through
   Kodamiko's own MCP tools, by Claude or by `kodamiko-mcp` with `KDK_SOCKET`
   pointing at the `/tmp/kodamiko-<pid>-…` socket.
4. `grab <name>` saves each window at 3200 × 2000; the 1× images are these
   halved. `quit` ends Kodamiko without saving its settings.

The scenes of the site:

| Image | Scene |
|---|---|
| `hero-editor`, `hero-claude` | Terminals detached; an error planted in `src/history/HistoryView.cpp:151`, cursor on it; the prompt “fix this” |
| `diagram`, `diagram-claude` | “Draw the path of a prompt through Kodamiko as a flowchart, in about ten boxes.”, then “Where does Claude wait before touching the code? Show me in the diagram.”, then `layout_diagram` and Fit, the selection kept |
| `review` | `8d23dfe` reverted then picked again without committing (`git revert --no-edit 8d23dfe`, `git cherry-pick --no-commit 8d23dfe`), no unsaved tab (the review refuses to start), Claude › Review Staged Changes, then Generate; `CommandPalette.cpp` selected |
| `history` | History mode, `90caac2` (made with Claude) unfolded: reached with Down from the top, then pressed; another row pressed first if the diff shown lags behind the selection |
| `docs/screenshots/kodamiko-violet` (the README's, window `place main 0 0 1400 900`) | `src/history/HistoryView.cpp` opened at line 146 (`open_file`), Files side tab, the tree located on the file |
| `edit` | Outline side tab, completion after `commits_->setSel` on a new line 154, scrolled to show line 134 first |

When a capture changes, check the sentences beside it in `site/index.html`
and `site/fr/index.html`: each picture shows what its text says.
