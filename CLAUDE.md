# CLAUDE.md — Duet

Duet is a Qt 6 / C++20 code editor built around a Claude Code session. This
file holds the project's conventions. One goal: **code that a human
understands quickly, changes without fear, and reviews without fatigue.**
Every rule below reduces *cognitive complexity*: the number of things a reader
must keep in mind at once to understand a piece of code.

> When a rule conflicts with clarity, clarity wins — but you must be able to
> explain why.

The rules describe the code as it is: when the code and this file disagree,
one of them is a bug. Formatting, warnings and static analysis are enforced by
tools (§11); the rest is for the reviewer.

Main sources: C++ Core Guidelines (Stroustrup & Sutter), the *Cognitive
Complexity* metric (G. Ann Campbell, SonarSource; validated against human
comprehension by Muñoz Barón, Wyrich & Wagner, ESEM 2020), *A Philosophy of
Software Design* (J. Ousterhout), Google C++ Style Guide, Chromium's C++ Dos
and Don'ts, Qt's coding conventions.

---

## 1. Guiding principles

1. **Code is read 10× more than it is written.** Optimize for the reader, not
   the author.
2. **One thing at a time.** One function = one responsibility; one line = one
   idea.
3. **Locality.** Everything needed to understand a piece of code is visible
   *near* it: declaration close to use, no hidden global state, no side effect
   at a distance.
4. **Explicit > implicit.** No magic: implicit conversions, surprising
   overloads, macros and metaprogramming are suspect by default.
5. **Make invalid states unrepresentable.** Let the type system check for us.
6. **Deep modules** (Ousterhout): a small, simple interface; an
   implementation that absorbs the complexity. No pass-through classes.
7. **The simplest thing that works.** No abstraction before the 2nd or 3rd
   real case. Light duplication > the wrong abstraction.

---

## 2. Functions

Cognitive complexity grows with each `if/else/switch/for/while/catch/?:`, each
**nesting level** (increasing penalty), jumps, and sequences of mixed logical
operators. It correlates with the time a reader takes to understand a function.

- **Cognitive complexity ≤ 15 per function** (clang-tidy enforces it); aim for
  10. A lambda's body counts in its enclosing function: a function that
  registers many handlers registers named functions, not long lambdas.
- **Nesting ≤ 3 levels.** Beyond that, extract a function.
- **Guard clauses / early return** rather than pyramids of `if`:
  ```cpp
  // ✗
  void process(const Order *order) {
      if (order) {
          if (order->isValid()) { /* ... */ }
      }
  }
  // ✓
  void process(const Order *order) {
      if (!order || !order->isValid())
          return;
      // nominal case, flat
  }
  ```
- **Name complex conditions**:
  ```cpp
  const bool isEligible = age >= 18 && hasAccount && !isBanned;
  if (isEligible) { ... }
  ```
- Do not mix `&&` and `||` in one expression without a named intermediate.
- **No nested conditional operator** (`a ? b : c ? d : e`): an `if` chain, a
  `switch` or a small function.
- **Short functions**: aim for < 40 lines, one screen. A block preceded by a
  "// step X" comment is a function waiting for its name.
- **Few parameters**: ≤ 4. Beyond that, a `struct` (designated initializers).
- **No boolean parameter that changes behavior** in new code: an `enum class`
  or two functions (`render(mesh, Shading::Smooth)`, not `render(mesh, true)`).
- **One level of abstraction per function**: do not mix the feature's logic
  with byte or Scintilla message handling.
- **Standard algorithms** when they say the intent (`std::ranges::find_if`,
  `any_of`, `count_if`…); a plain `for` loop beats an unreadable chain.
- `switch` on an `enum class` **without `default`**, so that the compiler
  reports a missing case.

---

## 3. Naming

- Names are the primary documentation. **A name that needs a comment is a bad
  name.**
- Length proportional to scope: `i` in a 3-line loop, `pendingRequestCount`
  for a member.
- Units in the name when the type does not carry them: `timeoutMs`,
  `marginPx`. Better: a strong type (`std::chrono::milliseconds`).
- Functions are verbs (`loadPreset`, `showDiff`); booleans are questions
  (`isEmpty`, `hasSelection`, `canContinue`).
- No obscure abbreviations, no Hungarian notation, no vague
  `Manager`/`Helper`/`Util`/`Data`.

| Element | Style | Example |
|---|---|---|
| Types, classes, enums, concepts | `PascalCase` | `EditorWidget` |
| Enum values | `PascalCase` | `Kind::Explain` |
| Functions, variables, parameters | `camelCase` | `openFile`, `lineCount` |
| Constants (`constexpr`, `const` at namespace scope) | `camelCase` | `statusMessageMs` |
| Private and protected members | `camelCase_` | `filePath_` |
| Namespaces of free functions | `PascalCase`, named after the concept | `AppFonts`, `Git` |
| Namespaces of the diagram engine | `lowercase` | `diagram::explainer` |
| Files | `PascalCase`, named after the main type or concept | `EditorWidget.h` |
| Macros (to avoid) | `UPPER_SNAKE_CASE` | `DUET_WITH_DIAGRAMS` |

---

## 4. Types and semantics

- **`const` by default**, `constexpr` when possible: a non-const variable
  signals "careful, this changes".
- **`auto`** when the type is obvious from the right-hand side or verbose and
  uninteresting (iterators, `make_unique`); an explicit type when it tells the
  reader something.
- **`enum class`**, never a plain `enum`.
- `std::optional<T>` for "maybe a value", `std::variant` for closed
  alternatives. No sentinel values (`-1`, `nullptr` meaning "error") in new
  interfaces.
- Single-argument constructors are **`explicit`**.
- No magic numbers: named constants, unless the number is self-evident
  (`0`, `1`, `* 2`, a percentage in a color).
- Members initialized in their declaration.
- **Casts**: a functional cast (`int(x)`, `qreal(n)`) for arithmetic
  conversions, as Qt code does; `static_cast` for anything else;
  `qobject_cast` to go down a `QObject` hierarchy; never a C-style cast
  (`(int)x`).

---

## 5. Resources and ownership

- **Qt ownership**: a `QObject` created with `new` and a parent is owned by its
  parent; that is the only `new` without a smart pointer.
- Otherwise `std::unique_ptr` by default, `std::shared_ptr` only when
  ownership is genuinely shared (justify it). No `delete`.
- **Raw pointer / reference = non-owning**, always. Ownership must be readable
  from the signature; say "Not owned" or "which owns it" in the comment when
  Qt parents make it ambiguous.
- A `QPointer` for a `QObject` that may die before a callback runs.
- **Rule of zero**; otherwise rule of five.
- Parameter passing (Core Guidelines F.15–F.20):
  | Intent | Signature |
  |---|---|
  | Read, cheap type (≤ 2 words, Qt implicitly shared types included when stored) | `T` |
  | Read, expensive type | `const T &` |
  | Consume / store | `T` then `std::move` |
  | Modify the argument (rare) | `T &` |
  | Output | **return value** (a struct if several) |

---

## 6. Classes and interfaces

- **Easy to use correctly, hard to use incorrectly.**
- Members private by default; `struct` for an aggregate without invariant,
  `class` as soon as there is one.
- Order: `public` → `signals` → `protected` → `private`; the reader looks for
  the API first.
- **Composition > inheritance.** Inheritance for real polymorphism and Qt
  widgets; hierarchies ≤ 2 levels of our own.
- Overrides marked `override` (never `virtual` + `override`), `final` where
  relevant; an override keeps the base's visibility.
- **Free functions in a namespace** rather than static methods or utility
  classes (`AppFonts::load()`, `CodeRequest::prompt()`).
- No trivial getters/setters by reflex: if everything is get/set, it's a
  `struct`.

---

## 7. Error handling

One strategy per layer:

- **Git writes** (`src/git`): `GitError`, thrown by `Git::run` and caught by
  `GitCommands`, which reports it to the UI; `GitError::Kind` says which
  failures the UI offers to recover from.
- **Expected failures** elsewhere (missing file, invalid input): a
  `std::optional`, an empty result, or the existing `bool f(..., QString
  *error)` pattern of the file and process APIs.
- **Invariant violations**: `Q_ASSERT`.
- Never ignore an error silently; `[[nodiscard]]` on functions whose result
  must be checked.
- Messages that say *what*, *where* and *with which value*, in English, for a
  user who did not write the code.

---

## 8. Code organization

- **One file = one concept.** A `.cpp` beyond ~400 lines probably holds two;
  beyond 800, split it.
- `.h` headers, `.cpp` sources, `#pragma once`.
- **Directory = feature package** (`src/editor`, `src/git`, `src/claude/mcp`…);
  no `using namespace` in a header.
- **Include paths** relative to `src/` (`"editor/EditorWidget.h"`), or bare
  for a header of the same directory (`"EditorWidget.h"`); never `../`.
- Include blocks, in this order, separated by a blank line (clang-format
  enforces it): the file's own header; the project's headers; Qt;
  third-party C APIs (Scintilla, libgit2, libvterm…); the standard library.
- **Include what you use**; a forward declaration when it suffices.
- Implementation details in an anonymous namespace in the `.cpp`.
- Declare variables **as close as possible to their first use**.
- One-way dependencies between packages; no cycles.

---

## 9. Comments and documentation

- Comment the **why**, never the **what**.
- Document every public API in its header: role, preconditions, result,
  errors, ownership and lifetime.
- Useful comments: the reason for a non-obvious choice, a known pitfall, a
  performance constraint, a link to an algorithm or a protocol.
- No commented-out code. A `TODO` says what and why.
- A comment that paraphrases the code goes; a comment that compensates for a
  bad name becomes a rename.
- All code, comments, UI strings and documentation in **English**.

---

## 10. Templates, macros, metaprogramming

- Templates only when genericity is genuinely needed; constrained with
  `concept`s.
- **Macros forbidden** except Qt's (`Q_OBJECT`, `signals`…), include guards
  and platform conditional compilation.
- No clever code: if a trick needs a comment to be understood, rewrite it
  simply.

---

## 11. Tooling

Configured in `.clang-format`, `.clang-tidy` and `cmake/CodeQuality.cmake`:

- `cmake --build build --target format` reformats the sources;
  `format-check` fails if one is not formatted.
- `cmake --build build --target tidy` analyzes every source; it must report
  nothing. A check that does not fit the project is turned off in
  `.clang-tidy` with its reason, never silenced line by line without one
  (`// NOLINT(check): reason`).
- The compiler warnings (`-Wall -Wextra -Wpedantic -Wshadow
  -Wnon-virtual-dtor…`) must stay at zero.
- `tools/git-hooks/pre-commit` checks the formatting of the staged files:
  `git config core.hooksPath tools/git-hooks` enables it.
- **Tests**: Qt Test, one test = one behavior, named after it
  (`highlightsEveryOccurrenceOfTheNameAtTheCursor`), *Arrange / Act /
  Assert*. Run them with `ctest --test-dir build` (`QT_QPA_PLATFORM=offscreen`
  without a display).

---

## 12. Review checklist

- [ ] Can a newcomer understand each function **without looking elsewhere**?
- [ ] No function above 15 of cognitive complexity or 3 levels of nesting?
- [ ] Are the names sufficient without comments?
- [ ] Is the ownership of every resource readable from the types?
- [ ] Are error cases handled explicitly?
- [ ] Is there a premature abstraction or a needless level of indirection?
- [ ] Zero warnings, clean clang-tidy, formatted, green tests?

---

## Instructions for Claude

- Apply these rules to all code written or modified; flag (without rewriting
  unprompted) existing code that violates them.
- Favor small, readable changes; explain any choice that departs from this
  document.
- Respond in French.
