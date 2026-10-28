# Third-party files

Copied as published, without changes to their content:

| Folder | Source | Version | License |
|---|---|---|---|
| `github-markdown-css/` | [sindresorhus/github-markdown-css](https://github.com/sindresorhus/github-markdown-css) (npm `github-markdown-css`) | 5.9.0 | MIT |
| `octicons/` | [primer/octicons](https://github.com/primer/octicons) (npm `@primer/octicons`, `build/svg/`) | 19.40.0 | MIT |
| `schemastore/claude-code-settings.json` | [SchemaStore/schemastore](https://github.com/SchemaStore/schemastore) (`https://json.schemastore.org/claude-code-settings.json`) | 2026-10-17 | Apache 2.0 (`schemastore/LICENSE`) |
| `fonts/UbuntuSans-*.ttf` | [canonical/Ubuntu-Sans-fonts](https://github.com/canonical/Ubuntu-Sans-fonts) (variable fonts, upright and italic) | 1.006 | Ubuntu Font Licence 1.0 (`fonts/UbuntuSans-UFL.txt`) |
| `fonts/UbuntuSansMono-*.ttf` | [canonical/Ubuntu-Sans-Mono-fonts](https://github.com/canonical/Ubuntu-Sans-Mono-fonts) (variable fonts, upright and italic) | 1.100 | Ubuntu Font Licence 1.0 (`fonts/UbuntuSans-UFL.txt`) |
| `fonts/DroidSansMono.ttf` | [Android Open Source Project](https://android.googlesource.com/platform/frameworks/base/+/android-2.3.7_r1/data/fonts/) (`data/fonts/`, regular) | android-2.3.7_r1 | Apache 2.0 (`fonts/Droid-NOTICE.txt`) |
| `fonts/JetBrainsMono-*.ttf` | [JetBrains/JetBrainsMono](https://github.com/JetBrains/JetBrainsMono) (`fonts/ttf/`, with ligatures) | 2.304 | SIL OFL 1.1 (`fonts/JetBrainsMono-OFL.txt`) |
| `fonts/JetBrainsMonoNL-*.ttf` | [JetBrains/JetBrainsMono](https://github.com/JetBrains/JetBrainsMono) (`fonts/ttf/`, the "NL" version without ligatures) | 2.304 | SIL OFL 1.1 (`fonts/JetBrainsMono-OFL.txt`) |

The Markdown preview renders with GitHub's style sheet and icons. The
interface is set in Ubuntu Sans and the code (editor, terminal, diffs) in
Droid Sans Mono, embedded so that Duet looks the same everywhere. Ubuntu
Sans Mono and JetBrains Mono are embedded alternatives. When JetBrains Mono
is selected, its "NL" version is used by default and in the terminal, the
other one in the editor when ligatures are on. Claude Code's settings files are
checked against SchemaStore's schema. To update them, replace the files with
those of a newer version and change the versions above.

The libraries Duet is built with (QScintilla, tree-sitter, libvterm,
libgit2, cmark-gfm) are not copied here: CMake downloads them at the
versions pinned in `cmake/Dependencies.cmake`. Their licenses are listed in
the main `README.md`, with the additional permission that lets Duet link
Graphviz.
