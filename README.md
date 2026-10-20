# Duet

**Duet is a code editor built around a Claude Code session.** You write code
on one side, Claude works on the other, and the editor keeps both in sync:
Claude sees what you see, and you see what Claude changes.

It is open source (GPL v3), written in C++20 with Qt 6, and runs on Linux,
Windows and macOS.

## Why Duet

- **Claude is part of the editor.** A Claude Code session runs in a built-in
  terminal, started in your project. With each prompt, Duet tells Claude
  which file is open, where the cursor is, what is selected and which
  problems are underlined, so "fix this" needs no `@path`.
- **Claude works in the editor.** Through Duet's own MCP server, Claude can
  read your open files (unsaved changes included), find symbols and
  references, rename through the language server, open a file at a line,
  show a diff or draw a diagram. Anything that changes the repository waits
  for your *Allow*.
- **You follow every change.** Open files reload as Claude edits them, and
  the Diff view updates after each step. A snapshot of the project is taken
  before each prompt: *Git ▸ Restore a Snapshot* undoes everything Claude
  did. Claude never overwrites a file you have not saved.
- **Git without leaving the editor.** Stage by file, hunk or line, commit
  (with a message Claude writes in the style of your history), browse the
  history, switch, merge and rebase branches, pull, push and stash.
- **A real code editor.** Tree-sitter highlighting for C, C++, Java,
  JavaScript and TypeScript, language servers (completion, diagnostics,
  go to definition, quick fixes), an outline, find in files, a command
  palette (Ctrl+Shift+P) and a rendered Markdown viewer.
- **Diagrams.** Ask for a flowchart, a sequence, a class or an architecture
  diagram: Claude writes it in Mermaid and it opens laid out in Duet's
  diagram editor, where you can rearrange it.

## Getting started

Requirements: **Qt 6.6+** (Widgets, Network, PrintSupport, Test, WebEngine),
CMake 3.24+, Ninja and a C++20 compiler (GCC 11+, Clang 14+ or MSVC 2022).
The other dependencies (QScintilla, tree-sitter, libvterm, libgit2,
cmark-gfm) are downloaded and built automatically on the first
configuration. [Graphviz](https://graphviz.org) (`libgraphviz-dev`,
optional) enables the diagram editor.

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
./build/bin/duet [project-folder]
```

At runtime Duet needs [Claude Code](https://claude.com/claude-code) 2.1+
and git 2.23+. If either is missing, *Help ▸ Check Prerequisites* opens on
startup and offers to install it.

## Using Duet

The window has four modes, switched with the tabs at the bottom right:

| Mode | Shortcut | What it shows |
|---|---|---|
| Terminal | Ctrl+1 | The Claude session and your shells |
| Edit | Ctrl+2 | The file tree, the outline and the editor |
| Diff | Ctrl+3 | Pending changes, staging and commit |
| History | Ctrl+4 | The commits of the current branch |

A few shortcuts to start with: F11 jumps to the terminal and back,
Ctrl+Shift+P opens the command palette, Ctrl+Shift+N goes to a file,
Ctrl+Alt+K adds the selected lines to Claude's prompt, and right-clicking
code offers *Explain*, *Review* and *Write Tests*.

## How it works

Duet launches `claude` with hooks and an MCP server of its own, without
touching your Claude configuration. Two small programs shipped with the
editor relay them over a local socket: `duet-hook` (prompts, edits,
permissions, status line) and `duet-mcp` (the tools Claude uses to act in
the editor).

The sources are organized by feature under `src/` (`editor`, `git`,
`agent`, `terminal`, `viewers`…); `CLAUDE.md` holds the coding conventions.

## License

Duet is distributed under the **GNU GPL v3** (see `LICENSE`), the license
required by QScintilla, with the additional permission below for Graphviz.

**Additional permission under GNU GPL version 3 section 7.** If you modify
this Program, or any covered work, by linking or combining it with Graphviz
(or a modified version of that library), containing parts covered by the
terms of the Eclipse Public License 1.0, the licensors of this Program grant
you additional permission to convey the resulting work. Corresponding Source
for a non-source form of such a combination shall include the source code
for the parts of Graphviz used as well as that of the covered work.

The permission is needed because the EPL 1.0 is not compatible with the GPL,
and the diagram editor links Graphviz's library. It covers Duet's own code
only; the other components keep their licenses.

Third-party components:

| Component | Distributed | License |
|---|---|---|
| Qt 6 | linked dynamically | LGPL v3 |
| QScintilla | built and linked statically | GPL v3 |
| tree-sitter and grammars | built and linked statically | MIT |
| libvterm | built and linked statically | MIT |
| libgit2 | built and linked statically | GPL v2 with linking exception |
| cmark-gfm | built and linked statically | BSD 2-Clause |
| Graphviz (optional, diagram editor) | linked from the system | Eclipse Public License 1.0 |
| github-markdown-css, Octicons (in `third_party/`) | embedded | MIT |
| KDE's editor themes (in `third_party/ksyntaxhighlighting-themes/`) | embedded | MIT |
| SchemaStore's Claude Code settings schema (in `third_party/schemastore/`) | embedded | Apache 2.0 |
| Ubuntu Sans, Ubuntu Sans Mono (in `third_party/fonts/`) | embedded | Ubuntu Font Licence 1.0 |
| JetBrains Mono (in `third_party/fonts/`) | embedded | SIL Open Font License 1.1 |
| One Dark Pro, Dracula, GitHub, Tokyo Night and Catppuccin theme colors (notices in `third_party/vscode-themes/`) | reproduced | MIT |

The versions of the downloaded components are pinned in
`cmake/Dependencies.cmake`; those of the embedded files in
`third_party/README.md`.
