# Duet

**Duet is a full-featured code editor built around a Claude Code session.** You
write code on one side, Claude works on the other, and the editor keeps both in
sync: Claude sees what you see, and you see what Claude changes.

It is open source (GPL v3), written in C++23 with Qt 6, and runs on Linux,
Windows and macOS.

## Why Duet

Today's editors were designed for a human typing code, and AI was added
later: a chat panel on the side, or an agent in a terminal that cannot see
your editor. The assistant does not know what you are looking at, you lose
track of what it changes, and undoing its work is up to you.

Duet starts from the other end: an editor built for working *with* an AI
agent, where you and Claude share the same view of the project.

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

Requirements: **Qt 6.10+** (Widgets, Network, PrintSupport, Test, WebEngine),
CMake 3.24+, Ninja and a C++23 compiler (GCC 13+, Clang 17+, Apple Clang 16+
or MSVC 2022).
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

On Windows, from an *x64 Native Tools Command Prompt* (Visual Studio 2022
or later, which also provides CMake and Ninja), with Qt's `msvc2022_64`
kit in `CMAKE_PREFIX_PATH` and its `bin` directory in `PATH`:

```bat
set CMAKE_PREFIX_PATH=C:\Qt\6.10.3\msvc2022_64
set PATH=%CMAKE_PREFIX_PATH%\bin;%PATH%
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
build-win\bin\duet.exe [project-folder]
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

### Recipes

A *recipe* is how to make one kind of project on one system: a Qt Widgets
application with MSVC on Windows, a Python package… *File ▸ New Project*
lists the recipes of your system; the project gets the recipe's first
files, and from then on:

- its terminals, run configurations and Claude sessions get the recipe's
  environment (Visual Studio's compiler, CMake and Ninja, the Qt kit in
  `CMAKE_PREFIX_PATH`…), without an *x64 Native Tools* prompt;
- Claude gets the recipe's skills and instructions;
- the *Project* menu offers the recipe's prompts, and the *Run* menu its
  Build, Run and Debug configurations.

A recipe belongs to one language (`"language": "cpp"` in its `recipe.json`),
and a project has at most one recipe per language: in *Project Settings*,
select a language, then one of its recipes (its files are not copied). A
project using C++ and Java can combine a Qt recipe and a Maven one; their
environments, tools, configurations, skills and prompts add up. A recipe
is a folder with a `recipe.json`, its `files/`, an
optional `instructions.md` and an optional `skills/` folder (a Claude Code
plugin): see `src/recipes/Recipe.h` and Duet's own in `tools/recipes/`. Yours
go in Duet's application data folder (`recipes/`), or in a project's
`.duet/recipes/` to share them with its repository; one with the same name
replaces Duet's.

*Preferences ▸ Terminal* adds folders of your own in front of the `PATH` of
every program Duet starts.

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
| Droid Sans Mono (in `third_party/fonts/`) | embedded | Apache 2.0 |
| JetBrains Mono (in `third_party/fonts/`) | embedded | SIL Open Font License 1.1 |
| One Dark Pro, Dracula, GitHub, Tokyo Night and Catppuccin theme colors (notices in `third_party/vscode-themes/`) | reproduced | MIT |

The versions of the downloaded components are pinned in
`cmake/Dependencies.cmake`; those of the embedded files in
`third_party/README.md`.
