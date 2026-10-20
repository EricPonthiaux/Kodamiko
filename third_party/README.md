# Third-party files

Copied as published, without changes (Inter's file renamed):

| Folder | Source | Version | License |
|---|---|---|---|
| `github-markdown-css/` | [sindresorhus/github-markdown-css](https://github.com/sindresorhus/github-markdown-css) (npm `github-markdown-css`) | 5.9.0 | MIT |
| `octicons/` | [primer/octicons](https://github.com/primer/octicons) (npm `@primer/octicons`, `build/svg/`) | 19.40.0 | MIT |
| `schemastore/claude-code-settings.json` | [SchemaStore/schemastore](https://github.com/SchemaStore/schemastore) (`https://json.schemastore.org/claude-code-settings.json`) | 2026-10-17 | Apache 2.0 |
| `fonts/Inter-Variable.ttf` | [rsms/inter](https://github.com/rsms/inter) (`docs/font-files/InterVariable.ttf`) | 4.1 | SIL OFL 1.1 |
| `ksyntaxhighlighting-themes/` | [KDE's syntax-highlighting](https://invent.kde.org/frameworks/syntax-highlighting) (`data/themes/`), the editor themes of Kate and KDevelop; each file names its authors | master 03107845 (2026-10-17) | MIT |
| `vscode-themes/LICENSE.md` | The notices of the five VS Code themes whose colors `src/ui/VsCodeThemes.cpp` reproduces (One Dark Pro, Dracula, GitHub Dark, Tokyo Night, Catppuccin Mocha); versions and sources in the file | see the file | MIT |
| `fonts/JetBrainsMonoNL-*.ttf` | [JetBrains/JetBrainsMono](https://github.com/JetBrains/JetBrainsMono) (`fonts/ttf/`, the "NL" version without ligatures) | 2.304 | SIL OFL 1.1 |

The Markdown preview renders with GitHub's style sheet and icons. The
interface is set in Inter and the code (editor, terminal, diffs) in
JetBrains Mono, embedded so that Duet looks the same everywhere. Claude
Code's settings files are checked against SchemaStore's schema. KDE's
editor themes are offered beside Duet's own (see `src/ui/KdeThemes.cpp`). To update
them, replace the files with those of a newer version and change the
versions above.
