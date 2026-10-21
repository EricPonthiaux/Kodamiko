# Evolution

## Studies

- Full refactoring capability (what is a full refactoring capability)
- Full "find references" capability (what is a full references capability)

Both are answered by the refactoring work below.

## Refactoring as powerful as IntelliJ's, fast on large code bases

Goal: IntelliJ's power (preview, conflicts, comments and strings, undo
everywhere, a wide catalogue) without its slowness on large projects
(indexes on the JVM heap, serial resolution, work on the UI thread).

Design, hybrid: Duet keeps its own native, incremental, persistent index
to find candidates in milliseconds; the language servers (clangd, jdtls,
typescript-language-server, pyright) decide the semantics; Duet alone
handles the preview, the conflicts, the transaction and the undo.
Target: about 1 M lines. Rules: nothing on the UI thread beyond 16 ms,
everything cancellable, parallel on all cores, only what changed read
again.

### Step 0: LSP foundations — done

- Requests can be cancelled (`$/cancelRequest`) and given a timeout.
- Work done progress: the status bar shows what the servers are busy with
  ("clangd: indexing 42%").
- Workspace edits keep their order, document versions and file
  operations (create, rename, delete); an edit computed for an older
  version of a file is refused; `didClose` is sent.
- `prepareRename` before renaming; Go to Implementation
  (Ctrl+Alt+Shift+B) and Go to Type Declaration (Ctrl+Shift+B).
- A scripted fake language server tests the client without clangd.

### Step 1: preview, transaction, undo — done

- Every refactoring is all or nothing: an edit journal reverts what was
  made when a change fails.
- Edit ▸ Undo Refactoring (Ctrl+Alt+Shift+Z) reverts the last
  refactoring in every file it changed, open or not, unless one changed
  since.
- Refactoring Preview: the files changed, the diff of each, files with
  text edits only can be unchecked; Rename offers Preview (Alt+P) next to
  Refactor.

### Step 2: occurrence index — done

- `src/index`: every word of the project with its role (definition,
  reference, call, write, import, comment, string, text), from
  tree-sitter queries for C, C++, Java, JavaScript, TypeScript and Python
  (grammar added for the index only).
- Parsed on all cores but one, encoded in a few bytes per occurrence,
  saved in the cache folder and loaded back at startup.
- `tools/bench-index`: Qt's headers (834k lines) index in 1.3 s cold,
  0.1 s warm; a word's files in under a millisecond. `/usr/include`
  (10.5M lines): 10 s cold, 1.2 s warm.

### Step 3: comments and strings, conflicts, usages — done

- Rename's "Search in comments and strings" also replaces the name in
  comments, strings and non-code files.
- Conflicts: the new name already declared, read-only files, files
  outside the project, a server still reading the project. With textual
  occurrences or conflicts, the preview opens first, as in IntelliJ.
- Usages pane (Alt+3): Find Usages by file with each usage's kind,
  filters, rerun, history, Next/Previous Usage (Ctrl+Alt+Down/Up);
  without a server, every occurrence of the word from the index.
- Hierarchy pane (Alt+8): Call Hierarchy (Ctrl+Alt+H) and Type Hierarchy
  (Ctrl+H), unfolded one level at a time.

### Step 4: Refactor This, file moves, Move Class — done

- Moving or renaming a file or folder in the tree updates C and C++
  includes, JavaScript and TypeScript relative imports and Python
  imports (and the module's uses), as one undoable refactoring.
- Edit ▸ Refactor This (Ctrl+Alt+Shift+T): Duet's refactorings, then the
  server's "refactor" code actions, each shown in the preview.
- Move Class to Its Own File (F6), for C++ (header and members'
  definitions, namespaces, guard style, Qt macros, statics, C typedef
  structs) and Java.
- The editor's context menu starts with the refactorings, the
  navigation, then the agent's submenu.

### Step 5: Change Signature, Claude's refactorings — done

- Change Signature (Ctrl+F6): add, remove, reorder parameters in the
  declarations and at every call, for C, C++, Java, JavaScript,
  TypeScript and Python (self, keyword arguments); the places it cannot
  change are listed as conflicts.
- MCP tools go through the preview and answer once the user chose:
  `rename_symbol` (preview, comments and strings), `apply_code_action`
  (preview), and the new `find_usages` (kinds), `rename_file` and
  `change_signature`.

### Tests

- `tests/refactoring`: a corpus of 116 cases (before, expected after,
  undo checked) over C, C++, JavaScript, TypeScript and Python — file
  moves, Move Class, comments and strings, renames, Change Signature and
  Find Usages through the real servers. Run by `tst_refactorcorpus`.
- Unit tests in `tst_lsp`, `tst_index`, `tst_refactor`,
  `tst_codeintelligence`, `tst_navigation`, `tst_mcptools`.

### Known limits

- Python relative imports (`from . import mod`) and TypeScript ESM
  imports written `./x.js` for `x.ts` are not updated by file moves.
- Find in Files does not use the index yet: keywords are not indexed.
- `SymbolIndex` still parses on its own, beside the occurrence index.
- Change Signature writes a parameter list on one line, and reaches only
  the places the server returns (not overrides).
- The "new name already declared" conflict is checked per file, not per
  scope.
