# Code Completion

How code completion works in Duet: what it offers, how it ranks and inserts
suggestions, and how its sources fit together. The goal is completion on par
with CLion's (JetBrains); the last section lists what is still missing and
the limits of what is there.

## At a glance

| Feature | Where |
|---|---|
| Popup with kind badges, highlighted matches, detail, key hints | `CompletionPopup` |
| Fuzzy matching on word starts (`gsv` → `getSelectedValue`) | `FuzzyMatch` |
| Ranking by source, server order and past choices | `CompletionPopup`, `CompletionHistory` |
| Three auto-popup modes (Off, Calm, Eager) | `CompletionController`, Preferences ▸ Editor |
| Documentation beside the list | `CompletionPopup` |
| Gray inline preview of the rest of the name | `CompletionController` |
| Snippets with tab stops and mirrored placeholders | `Snippet`, `SnippetSession` |
| Postfix templates (`.if`, `.for`, `.var`, `.make_unique`…), with a choice of expression | `Templates` |
| Live templates (`for`, `iter`, `itit`, `main`, `sout`, `log`…) | `Templates` |
| Language servers: clangd, jdtls, typescript-language-server, pyright/pylsp | `LspCompletionProvider`, `LanguageServers` |
| Automatic `#include` / `import` with the chosen symbol | `CompletionItem::additionalEdits`, `resolveEdits` |
| Language server warm-up when a file opens | `LanguageServers::warmUp` |
| Parameter info (Ctrl+P, automatic on `(` and `,`, and after choosing a function) | `CodeIntelligence`, `CompletionController::callInserted` |
| Smart completion (Ctrl+Shift+Space): only the expected type | `ExpectedType`, `LspCompletionProvider` |
| Extended completion (Ctrl+Space again): more candidates | `CompletionController`, `FuzzyMatch` |
| Full Line completion from a local model | `FullLineModel`, `CompletionController` |
| Doxygen, Javadoc and JSDoc comments: stub, tags, parameter names | `DocComments` |
| Override and implementation generation | `CodeGeneration` |
| Name suggestions in declarations (`QListWidget ` → `listWidget`) | `NameSuggestions` |
| Screen reader support | `CompletionPopup::announce` |

## Architecture

```
EditorWidget ──► CompletionController ──► CompletionService ──► providers
                   │   (one per editor)      (shared)            ├─ LspCompletionProvider
                   ├─ CompletionPopup          │                 ├─ ProjectSymbolsProvider
                   ├─ InlinePreview            └─ FullLineModel  ├─ KeywordProvider
                   └─ SnippetSession                             ├─ DocumentWordsProvider
                                                                 ├─ PostfixTemplateProvider
                                                                 ├─ LiveTemplateProvider
                                                                 ├─ NameSuggestionProvider
                                                                 ├─ CodeGenerationProvider
                                                                 └─ DocCommentProvider
```

- **`Completion.h`**: the shared vocabulary. `CompletionItem` (label, text to
  insert, detail, kind, priority, server order, snippet flag, value type,
  additional edits, choices, documentation, lazy resolvers, `replaceStart`),
  `CompletionContext` (file, language, text, cursor, prefix, trigger
  character, explicit request, mode, expected type, documentation comment,
  other open files) and the `ICompletionProvider` interface.
- **`CompletionService`** (one, owned by `MainWindow`): asks every provider
  that handles the context and merges their answers, then calls back each time
  a provider answers. Local providers answer at once, language servers later.
  A newer request, or `cancel()`, makes late answers of an older one ignored.
  In a documentation comment it only asks the providers that serve comments
  (`servesComments()`); in smart completion, those that serve it
  (`servesSmart()`). It also owns the `CompletionHistory`, and points to the
  Full Line model when that is on.
- **`CodeScan`**: reading C-like code without a parser, shared by the
  providers: comments and strings blanked out, matching brackets, the
  enclosing block, a function header split into return type, scope, name,
  parameters and qualifiers, the declarations of a class body, a class's
  name and bases, the indentation of the file.
- **`CompletionController`** (one per editor, created on
  `EditorTabs::editorOpened`): decides when to open the popup, filters as the
  user types, handles keys, and inserts the chosen candidate.
- **`CompletionPopup`**: the list and the documentation panel. It never takes
  the focus: the editor keeps the keyboard and forwards the navigation keys.

### Merging candidates

`CompletionService::merge` keeps one item per label, the richest one: a
language server's method wins over a word of the file. A mere word of the file
never replaces a keyword or a symbol, even with a higher priority. (Before
this rule, a document word "for" could replace the keyword, then be hidden as
"the word already typed", and the keyword vanished.) Templates (kind
`Snippet`, or items with a `replaceStart`) have their own key, so the
template `for` shows next to the keyword `for`, and the postfix `.begin` next
to a member `begin()`.

## When the popup opens

Ctrl+Space always opens it, even with nothing typed; pressed again while it
is open, it shows more (see *Extended completion*). Ctrl+Shift+Space opens
smart completion. Otherwise it follows Preferences ▸ Editor ▸ *Suggest while
typing*:

- **Off**: only on Ctrl+Space.
- **Calm**: after a 400 ms pause, from the third letter of a name.
- **Eager** (default): 60 ms after typing a name, or right after a trigger
  character.

Trigger characters come from the providers: `.` for members (and for
postfix templates, so it works without a language server), plus `->` and `::`
in C++ (`::` also without a language server, for the functions to
implement). `>` only counts after `-` and `:` only after `:`, so `a > b` and
`case x:` do not open it. Nothing opens inside comments or strings (checked
from the lexer's style at the cursor), except documentation comments (see
below), and not while typing a number.

A name typed right after `.`, `->` or `::` is treated as a member access
whoever opened the list: keywords and file words are then left out.

The preference was a boolean before. It is read from the old key once
(`editor/autoCompletion`: on → Eager) and saved under `editor/completionMode`.

## The list

- Each row shows a badge for the kind (`m` method, `c` class, `S`
  snippet…), the label with the matched letters highlighted, and the detail at
  the right (signature, type, file).
- **Matching** (`FuzzyMatch`, IntelliJ-style): the pattern's letters in
  order, the first one at the start of a word of the candidate (`gsv`
  matches `getSelectedValue` and `get_saved_value`, `get` does not match
  `widget`). `_`, `-`, `$`, `:`, `.`, `~` and spaces separate words, so
  `si` matches `Foo::size`. Prefixes, same case, consecutive letters and
  word starts score higher; long candidates lower.
- **Ranking**: match score + the item's priority. Ties go to the server's
  order, then to the shorter label. At most 200 rows are kept.
- **Priorities by source**:

  | Source | Priority |
  |---|---|
  | Language server | 10 + 0…8 by the server's own order (expected type first) |
  | Project symbols | 6 in the current file, 5 elsewhere |
  | Document words | 4 for the 20 nearest the cursor, 1 beyond |
  | Name suggestions | 30 (a name is what goes there) |
  | Overrides, implementations | 8 |
  | Postfix templates | 3 |
  | Keywords | 2 |
  | Live templates | 1 (below the keyword of the same name) |

- **History** (`CompletionHistory`, per project, a small JSON file in the
  application's data folder): a candidate chosen often and lately gains up to
  +14. Its weight halves after 14 days without being chosen again. A
  candidate erased within 3 seconds of being inserted (Ctrl+Z, deleted) counts
  as rejected and drops down to -9.
- **Incomplete answers**: when a server says it sent only part of its list,
  typing further asks again while the current list stays shown.
- **Documentation panel**: beside the list, shows the selected candidate's
  kind, signature and documentation. If the server only sends it on request
  (jdtls), it is fetched with `completionItem/resolve` once the selection
  rests. Ctrl+Q toggles the panel (Ctrl+Space too, once the list is
  extended or smart), and the choice is remembered.
- **Inline preview**: the rest of the selected name in gray after the cursor,
  with the key that inserts it. It only shows when nothing follows the cursor
  on the line, because Scintilla has no inline virtual text.
- **Accessibility**: rows read as "getValue, method, int getValue()".
  Opening the list announces "N suggestions" and the current item, and moving
  announces each new item.

## Keys while the popup is open

| Key | Action |
|---|---|
| Up / Down / Page Up / Page Down | Move in the list |
| Enter | Insert (unless *Enter inserts the suggestion* is off; then Enter is a new line) |
| Tab | Insert, replacing the rest of the identifier after the cursor |
| Ctrl+Right | Insert up to the end of the candidate's next word (`get` → `getSelected`) |
| Ctrl+Space | Show more candidates (extended completion); once extended, show or hide the documentation |
| Ctrl+Q | Show or hide the documentation |
| Escape | Close |

The bottom line of the popup shows these hints, led by the kind of list
("All suggestions", "Expected int") when it is not the basic one.

## Extended completion (Ctrl+Space twice)

As CLion's second basic completion: pressing Ctrl+Space with the list open
asks again for more candidates. The typed letters may then start inside a
word (`get` matches `widget`, ranked below the usual matches), the words of
the other open files of the language are added, and up to 500 rows are kept
instead of 200. With `--all-scopes-completion`, clangd already offers the
symbols of namespaces not visible from the cursor in the basic list.

## Smart completion (Ctrl+Shift+Space)

Only the candidates of the type expected at the cursor. `ExpectedType::at`
reads it from the text:

- the declared type in `Widget *w = |`, `QList<int> items{|}`… (not `auto`);
- the variable's declaration in `count = |`, `this->ratio = |`, `a == |`;
- the enclosing function's return type after `return` (a lambda's own when
  written; `auto` gives none);
- `bool` in `if (|`, `while (|`, after `!`, `&&`, `||`;
- `name: Type` in TypeScript.

Inside a call's arguments the text does not tell: `LspCompletionProvider`
first asks the server's `textDocument/signatureHelp` and takes the active
parameter's type. Each candidate's value type (`CompletionItem::type`) comes
from the server's detail (`int`, jdtls's `Main.count : int`, pyright's
`-> str`). `ExpectedType::accepts` keeps a candidate when its type is the
same once qualifiers, references and `std::` are left out, or of the same
family (numbers, strings, booleans), or when it is the class to construct
(after `new`, or `Foo f = Foo(…)`). Without an expected type, smart
completion keeps the values (variables, fields, functions) and leaves out
keywords, words and templates. The keyword provider adds `true`/`false`
where a boolean is expected and `nullptr`/`NULL`/`null` where a pointer or
an object is.

## Inserting a candidate

- The typed prefix (or, for a postfix template, everything from
  `replaceStart`: the expression and the dot) is replaced in **one undo step**,
  together with any import that comes with it.
- **Snippets** (LSP snippet syntax, `printValue(${1:int value})$0`) are
  expanded by `Snippet::expand`. Placeholders keep their text, choices keep
  their first option, variables keep their default, and `\$` is a literal
  dollar. A tab stop repeated without text (`${1:i} < n; ++$1`) repeats its
  placeholder's text.
- **Choices**: an item may come with several ways to insert it
  (`CompletionItem::choices`): choosing it lists them instead of the
  candidates, the expression of the selected one highlighted in the code
  (indicator 12). Enter or Tab inserts that one, Escape or typing gives up.
  Postfix templates use it for the expression to wrap (see below).
- **Parameter info**: after inserting a function or a method with its
  arguments to fill in, the controller signals `callInserted` and
  `CodeIntelligence::requestParameterInfo` shows its parameters, as if `(`
  had been typed.
- **Additional edits**: `#include` / `import` lines sent with the item are
  applied in the same undo step, from the end of the file backwards, with
  positions adjusted for the inserted text. When the server only gives them
  on request (jdtls), they are fetched with `completionItem/resolve` after
  insertion and applied when the answer arrives.

### Tab stops (`SnippetSession`)

As in CLion and IntelliJ live templates:

- After inserting a snippet with numbered stops, the first placeholder is
  selected, so typing replaces it.
- Every stop is boxed; the current one uses the accent color (Scintilla
  indicators 10 and 11).
- **Tab** goes to the next stop, **Shift+Tab** to the previous one. On the
  last one (`$0`, or the snippet's end if there is no `$0`) the cursor goes
  there and the session ends.
- What is typed in one occurrence of a stop is copied to the others (the
  `i` of `for (int i = 0; i < n; ++i)`).
- The session also ends on **Escape**, on undo/redo, or when the cursor
  leaves the snippet.
- While the popup is open it takes Tab and Escape first: the first Escape
  closes the list, the next one ends the snippet.

## Providers

### Language servers (`LspCompletionProvider`)

Sends `textDocument/completion` with the trigger kind and character, after
syncing the document (didOpen the first time, then didChange with the whole
text). From each item it takes:

- the name to match and show: the first plain identifier among the insert
  text, the filter text and the label. jdtls labels look like
  `equals(Object obj) : boolean`. clangd's leading space or `•` is dropped.
- the detail: signature and short type (`labelDetails`, else the rest of the
  label, else `detail`).
- the snippet flag, additional edits, documentation, and lazy resolvers when
  the server supports `resolveProvider`.
- the server's order (`sortText`), used as a priority bonus and as the
  tie-breaker.

Servers, started for the open folder (`LanguageServers::known()`):

| Server | Languages | Notes |
|---|---|---|
| clangd | C, C++ | `--background-index --header-insertion=iwyu --all-scopes-completion --pch-storage=memory --completion-parse=always` |
| jdtls | Java | needs Java 21+; workspace data in a cache folder per project |
| typescript-language-server | JS, TS, TSX | `--stdio` |
| pyright / pylsp | Python | |

With `--header-insertion=iwyu`, a symbol from a header not yet included comes
with its `#include`. With `--all-scopes-completion`, symbols of namespaces not
visible from the cursor are offered and qualified on insertion. Together they
match CLion's auto-import. `--completion-parse=always` avoids answers made of
plain words while headers are still being read.

### Warm-up (`LanguageServers::warmUp`)

So that the first completion does not wait:

- When a file opens in an editor (deferred until the tab is shown), its
  server is started and given the file. clangd builds the preamble, jdtls
  imports the project, tsserver loads its libraries.
- The first time for each server, a completion at the start of the file is
  also sent and its answer dropped. That loads what the user's first request
  would otherwise wait for.
- This runs again for every open editor when the project changes
  (`setProject`) and when language servers are re-enabled in the preferences.
- Requests sent before the server finishes `initialize` are queued by
  `LspClient`.

### Local providers (`LocalCompletion`)

- **`DocumentWordsProvider`**: identifiers of the current file (3+
  characters, not starting with a digit), nearest to the cursor first.
- **`KeywordProvider`**: keywords of C, C++, Java, JS/TS, Python, Bash and
  CMake. None after a trigger character.
- **`ProjectSymbolsProvider`**: classes, functions and fields of every file
  of the project (from `SymbolIndex`), with their container or file as
  detail. Rebuilt only when the index changes.

### Postfix templates (`PostfixTemplateProvider`)

Typed after an expression and a dot, a postfix template wraps the
expression: `items.for` → `for (auto &item : items) { }`. The list comes from
CLion's Radler engine (ReSharper C++) and IntelliJ:

| Template | Result | Languages |
|---|---|---|
| `if`, `else` | `if (expr) {}`, `if (!expr) {}` | all |
| `nn` / `notnull`, `null` | `if (expr != nullptr) {}` (`NULL` in C, `null` in Java/JS) | all |
| `for` | `for (auto &item : expr)` / `for (var item : expr)` / `for (const item of expr)` | C++, Java, JS |
| `fori`, `forr` | counting loop up to / down from `expr` | all |
| `while`, `do`, `switch` | the statement around `expr` | all |
| `return`, `throw` | `return expr;`, `throw expr;` | all / C++, Java, JS |
| `var` (`let` in JS) | `auto name = expr;` with a suggested name | C++, Java, JS |
| `co_return`, `co_await`, `co_yield` | coroutines | C++ |
| `cout`, `sout`, `serr`, `log` | print the value | C++, Java, JS |
| `not`, `par`, `arg` | `!expr`, `(expr)`, `function(expr)` | all |
| `cast`, `as` | `((type) expr)`, `(expr as Type)` | C, Java / TS |
| `static_cast`, `dynamic_cast`, `const_cast`, `reinterpret_cast` | `static_cast<type>(expr)`… | C++ |
| `move`, `forward` | `std::move(expr)`, `std::forward<decltype(expr)>(expr)` | C++ |
| `make_unique`, `make_shared`, `new` | `std::make_unique<Type>()`, `new Type()` | C++ / C++, Java, JS |
| `begin`, `cbegin` | `expr.begin(), expr.end()` | C++ |

- **The expression** (`expressionStart`): read backwards from the dot. It is
  a chain of names, calls, subscripts and member accesses (`.`, `->`, `::`,
  `?.`), or a parenthesized expression. Template arguments are included
  (`make<int>(x)`), and so is a leading `new` (`new Foo()`). Brackets and
  string literals are matched; the scan goes back at most 4000 bytes. Numbers
  are left out, so `3.f` offers nothing.
- **A choice of expression** (`expressionStarts`): from the nearest
  operand, the expression grows over the binary operators before it (`+`,
  `&&`, `==`, `<<`…) and a `!`: `x = n * f(y) - z.` offers `z`,
  `f(y) - z` and `n * f(y) - z`. It stops at an assignment, a `(`, a `,` or
  the start of the statement. When more than one fits the template, the
  user picks it, as in CLion; a compound expression is parenthesized where
  it becomes an operand (`.not` → `!(a + b)`, `.cast`, `.begin`).
- **Statements only start statements**: `if`, `for`, `return`, `var`… are
  only offered for an expression that starts a statement (only blanks
  before it on the line, or after `{`, `}`, `;`). In `int n = items.` you
  get casts, `not` and `par`, but not `if`; in `a + b.if`, only `a + b`
  fits `if`, so it is taken without asking.
- **Suggested names** (`suggestName`): from the last name of the expression,
  outside its arguments, without a `get`/`is`/`to` prefix:
  `widget.getSelectedItem()` → `selectedItem`, `new Widget(p)` →
  `widget`. It falls back to `value`.
- Multi-line results are indented like the expression's line. One indentation
  step is guessed from the file: tabs, or its smallest space indentation, else
  4 spaces.
- The detail shows the shape (`if (expr) { }`); the documentation panel shows
  the real result.

### Live templates (`LiveTemplateProvider`)

Abbreviations expanded into code, with tab stops. Offered where a statement
starts (`lambda` and `arf` anywhere), never after a trigger character:

- **C/C++**: `for` (index loop, as in CLion), `iter` (range-based for:
  container, then variable, then type, as in CLion), `itit`, `ritit`, `main`,
  `incl`, `cout`, `lambda`, `class`, `struct`, `enum` (enum class), `ns`,
  `tryc`.
- **Java**: `main` / `psvm`, `sout`, `serr`, `souf`, `fori`, `iter`, `ifn`,
  `inn`, `thr`, `psf`, `prsf`, `tryc`.
- **JS/TS**: `log`, `fori`, `iter` (for…of), `forin`, `arf` (arrow
  function), `imp`, `ifn`, `inn`, `thr`, `tryc`.

`for`, `iter` and `itit` mirror CLion's own definitions
(`liveTemplates/OCLegacyDefaultTemplates.xml` in its Radler plugin).

### Name suggestions (`NameSuggestionProvider`)

In C, C++ and Java, where a name is declared after its type (a variable, a
field, a parameter), names made from the type, as CLion suggests them:

| Type | Names |
|---|---|
| `QListWidget` | `listWidget`, `widget` (Qt's `Q` and an interface's `I` left out) |
| `HTMLParser` | `htmlParser`, `parser` |
| `std::vector<Item>`, `QList<Entry *>`, `Box[]` | `items`, `entries`, `boxes` |
| `std::unique_ptr<HttpRequest>`, `optional<T>`… | the names of the type inside |
| `my_type_t` | `my_type`, `type` |
| `int`, `bool`, `QString` | `i`, `n`, `count`; `b`, `enabled`; `text`, `name`, `s` |

`declaredTypeAt` finds the type before the cursor: names, `::`, template
arguments, `[]`, after qualifiers (`const`, `static`, `final`, `private`…)
and the start of a statement or a parameter. Not after `return`, `new`,
`else`…, nor after a lowercase name in Java, nor after a lowercase C++
name that does not look like a type (no `::`, `<…>` or `_t`).

### Overrides and implementations (`CodeGenerationProvider`)

As CLion's Generate ▸ Override and Implement, from the completion list:

- **In a class body** (C++, Java), at the start of a member: the methods of
  the base classes that can be overridden and are not yet. In C++, the
  `virtual` ones (or those marked `override`), not `final`; in Java, those
  neither `static`, `private` nor `final`, plus `toString`, `equals` and
  `hashCode`. The bases are followed up the hierarchy (12 classes at most),
  found in the current file, then through the project's `SymbolIndex`.
  Choosing `paint` inserts `void paint(int x) const override` with a body
  calling `Base::paint(x)` (empty for a pure method) in a source file, the
  declaration alone (`…override;`) in a header; `@Override public …` with
  `super.paint(x)` in Java. Default arguments are left out.
- **At the top level of a C++ source file** (or in a namespace): the
  functions of its header (`CodeIntelligence::counterpartOf`) that have no
  definition yet, nor a body, nor are pure, deleted, templates, `inline`,
  `constexpr`, friends or Qt signals. `size` (or `Foo::si`) inserts
  `int Foo::size() const` and an empty body; constructors (`Foo`) and
  destructors (`Foo`, inserted as `Foo::~Foo`) too.

### Documentation comments (`DocComments`)

Doxygen in C and C++, Javadoc in Java, JSDoc in JavaScript and TypeScript:

- **Enter after `/**`** (or `/*!`) on a line of its own writes the comment of
  the declaration below, as CLion does: a summary line, then `@tparam` per
  template parameter, `@param` per parameter, `@return` when it returns a
  value (`@returns` in JSDoc), `@throws` per exception of a Java `throws`
  clause. Each text to write is a tab stop, the summary first. Not when the
  comment is closed already.
- **In a documentation comment**, the popup opens after `@` (and `\` in C
  and C++) with the tags (`param`, `return`, `brief`, `throws`…), and after
  `@param ` (`@param[in] `, `@param {Type} `, `@tparam `) with the
  parameters of the function documented that have no `@param` yet, in the
  order of the declaration, their type as detail. Only `DocCommentProvider`
  is asked there: no keywords, no words.

## Full Line completion

As CLion's Full Line: a code model on this machine suggests the rest of the
line, in gray after the cursor. Preferences ▸ Editor ▸ *Full Line* turns it
on (off by default) and names the server:

- **Ollama** (`http://localhost:11434`, `POST /api/generate` with `prompt`
  and `suffix`), with a model that fills in the middle, such as
  `qwen2.5-coder:1.5b`;
- **llama.cpp's `llama-server`** (`http://localhost:8080`, `POST /infill`
  with `input_prefix` and `input_suffix`).

The model is asked 300 ms after typing stops at the end of a non-blank line,
outside comments and strings, while no list or snippet is open; it gets up
to 6000 bytes before the cursor and 2000 after (whole lines), and answers
with at most 64 tokens, stopped at the line's end. `FullLineModel::lineOf`
keeps the first line, without what already follows the cursor. Tab inserts
the suggestion, Ctrl+Right its next word, Escape dismisses it; typing the
characters it shows keeps the rest, anything else removes it. A server that
cannot be reached is said once in the status bar. The code sent never
leaves the configured address.

## Parameter info

Not part of the completion popup but closely related (`CodeIntelligence`):
`textDocument/signatureHelp` is shown as a Scintilla call tip, with the
current parameter highlighted and `1/3` for overloads. It opens on Ctrl+P,
automatically after typing `(` or `,`, and after choosing a function in the
list. It closes on `)` or when the cursor leaves the call's line.

## Tests

`tests/tst_completion.cpp` covers matching and ranking, merging, snippet
expansion (stops, mirrors, escapes), insertion with imports (immediate and
resolved), the three modes, Enter/Tab/Ctrl+Right, the preview, documentation,
screen reader announcements, history, comments and strings, late answers, tab
stop navigation and mirroring, the end of a session when the cursor leaves,
expression detection, name suggestions, postfix insertion (`if`, `var`,
`static_cast`), statement-only templates and live templates. It also covers
extended and smart completion, expected types, the Full Line suggestion and
its HTTP requests (against a local fake server), documentation comments
(stubs, tags, parameters), `CodeScan`, overrides and implementations, names
from types, parameter info after a function, and the choice of a postfix
expression. It includes two benchmarks (filtering many candidates,
collecting document words). `tests/tst_lsp.cpp` checks the value types and
the active parameter's type, and smart completion with a real clangd when
it is installed.

Run it with:

```
QT_QPA_PLATFORM=offscreen build/tests/tst_completion
```

## Still missing compared to CLion

- **User-defined live templates**: the templates are built in, with no
  editor yet.
- **Overrides of library classes**: base classes are only found in the
  project (current file and `SymbolIndex`), not in system or library headers
  (`QWidget`, `Runnable`); a language server's own override completions,
  when it sends them, still show.
- **Smart completion's types** come from the text and from the server's
  details: no real type checker. Conversions beyond the families (a derived
  class for its base, a constructor taking the value) are not accepted.
- **Full Line** needs a model server installed and running (Ollama or
  llama.cpp); CLion ships its own model. The suggestion only shows at the
  end of a line (Scintilla has no inline virtual text).
