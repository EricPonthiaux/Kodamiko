# CLAUDE.md — tonedyne

The project's baseline conventions. One goal: **C++ code that a human understands quickly, changes without fear, and
reviews without fatigue.** Every rule below serves to reduce *cognitive complexity*: the number of things a reader must
keep in mind at once to understand a piece of code.

> When a rule conflicts with clarity, clarity wins — but you must be able to explain why.

Main sources: C++ Core Guidelines (Stroustrup & Sutter), the *Cognitive Complexity* metric (SonarSource, G. Ann
Campbell), *A Philosophy of Software Design* (J. Ousterhout), *Clean Code* (R. Martin, read with hindsight), Google C++
Style Guide, talks by Kate Gregory, Sean Parent ("No raw loops"), Tony Van Eerd, Jason Turner (*C++ Best Practices*).

---

## 1. Guiding principles

1. **Code is read 10× more than it is written.** Optimize for the reader, not the author.
2. **One thing at a time.** One function = one responsibility; one line = one idea.
3. **Locality.** Everything needed to understand a piece of code should be visible *near* it (declaration close to use,
   no hidden global state, no side effects at a distance).
4. **Explicit > implicit.** No magic: implicit conversions, surprising operator overloads, macros and metaprogramming
   are suspect by default.
5. **Make invalid states unrepresentable.** Use the type system so the compiler checks for us.
6. **Deep modules** (Ousterhout): small, simple interface; implementation that absorbs the complexity. Avoid
   "pass-through" classes that only delegate.
7. **The simplest thing that works.** No abstraction before the 2nd/3rd real case (YAGNI). Light duplication > wrong
   abstraction.

---

## 2. Reducing the cognitive complexity of functions

Cognitive complexity grows with: each `if/else/switch/for/while/catch/?:`, **each nesting level** (increasing
penalty), `goto`/`break`/`continue` to labels, sequences of mixed logical operators, recursion.

**Rules:**

- **Target cognitive complexity ≤ 10 per function, max 15** (SonarQube threshold). Beyond that: split.
- **Nesting ≤ 3 levels.** Beyond that, extract a function.
- **Guard clauses / early return** rather than pyramids of `if`:
  ```cpp
  // ✗
  void process(const Order* order) {
      if (order) {
          if (order->isValid()) {
              if (!order->items().empty()) { /* ... */ }
          }
      }
  }
  // ✓
  void process(const Order* order) {
      if (!order) return;
      if (!order->isValid()) return;
      if (order->items().empty()) return;
      // nominal case, flat
  }
  ```
- **Name complex conditions**:
  ```cpp
  const bool isEligible = age >= 18 && hasAccount && !isBanned;
  if (isEligible) { ... }
  ```
- Do not mix `&&` and `||` in the same expression without an intermediate variable.
- **Short functions**: aim for < 30 lines, fit on one screen. A block preceded by a "// step X" comment is a candidate
  for extraction into a named function.
- **Few parameters**: ≤ 4. Beyond that, group them in a `struct` (with C++20 designated initialization).
- **No boolean parameters** that change behavior (`draw(true, false)` is unreadable) → `enum class` or two functions.
  ```cpp
  // ✗ render(mesh, true);
  // ✓ render(mesh, Shading::Smooth);
  ```
- **One level of abstraction per function**: do not mix business logic and byte manipulation.
- **Standard algorithms rather than raw loops** when the intent becomes clearer (`std::ranges::find_if`, `any_of`,
  `count_if`, `transform`…). But a simple `for` loop is still preferable to an unreadable chain of ranges.
- `switch` on `enum class` **without `default`** so the compiler flags missing cases (`-Wswitch`).

---

## 3. Naming

- Names are the primary documentation. **A name that needs a comment is a bad name.**
- Length proportional to scope: `i` in a 3-line loop, `pendingRequestCount` for a member.
- Units and meaning in the name when the type does not carry them: `timeoutMs`, `sampleRateHz`, `gainDb`. Better: a
  strong type (`std::chrono::milliseconds`).
- Functions = verbs (`computeSpectrum`, `loadPreset`); booleans = questions (`isEmpty`, `hasClipping`, `canRetry`).
- No obscure abbreviations, no Hungarian notation, no vague `Manager`/`Helper`/`Util`/`Data`.
- **Convention (to follow everywhere)**:
  | Element | Style |
  |---|---|
  | Types, classes, enums, concepts | `PascalCase` |
  | Functions, variables, parameters | `camelCase` |
  | Private members | `camelCase_` (`_` suffix) |
  | `constexpr` constants / enum values | `PascalCase` (`MaxChannels`, `Mode::Stereo`) |
  | Namespaces, directories | `snake_case` (`dsp::effects`, `app/panels/`) |
  | Files | `PascalCase`, named after the main type (`NoiseGate.hpp`) |
  | Macros (to avoid) | `UPPER_SNAKE_CASE` |

---

## 4. Types and semantics

- **`const` by default**, `constexpr` when possible. A non-const variable signals "careful, this changes".
- **`auto`** when the type is obvious from the right-hand side or verbose and uninteresting (iterators,
  `make_unique`). **Explicit type** when it carries meaning for the reader.
- **`enum class`** always, never a plain `enum`.
- **Strong types** to avoid mixing up parameters of the same primitive type (`Frequency`, `Gain`, `SampleIndex` rather
  than three `float`/`int`).
- `std::optional<T>` for "maybe a value", `std::expected<T, E>` (C++23) or a result type for expected errors,
  `std::variant` for closed alternatives. No sentinel values (`-1`, `nullptr` meaning "error").
- `std::span`/`std::string_view` for non-owning views as parameters.
- Single-argument constructors are **`explicit`**.
- No magic numbers: named `constexpr` constants.
- Uniform `{}` initialization; always initialize members (initializers in the declaration).

---

## 5. Resources and ownership (RAII)

- **Zero manual `new`/`delete`.** Only exception: what JUCE owns and destroys itself (`addParameter()`,
  `createEditor()`, `createPluginFilter()`); `src/processor/.clang-tidy` disables the corresponding rule.
- Otherwise: `std::unique_ptr` by default, `std::shared_ptr` only if ownership is genuinely shared (rare — justify it).
- **Raw pointer / reference = non-owning**, always. Ownership must be readable from the signature.
- **Rule of zero**: prefer members that manage their own resources so you write nothing. Otherwise, **rule of five**
  (define all, or `= default`/`= delete` all).
- Parameter passing (Core Guidelines F.15-F.20):
  | Intent | Signature |
  |---|---|
  | Read, cheap type (≤ 2 words) | `T` |
  | Read, expensive type | `const T&` |
  | Consume / store | `T` then `std::move` |
  | Modify the argument (rare) | `T&` |
  | Output | **return value** (struct/tuple if several), no output parameter |

---

## 6. Classes and interfaces

- **Interfaces easy to use correctly, hard to use incorrectly.**
- Members **private** by default; `struct` for data aggregates with no invariant, `class` as soon as there is an
  invariant.
- Order within a class: `public` → `protected` → `private` (the reader looks for the API first).
- **Composition > inheritance.** Inheritance only for real polymorphism; hierarchies ≤ 2 levels.
- Polymorphic base class: `virtual` destructor (or protected non-virtual). Overrides marked `override` (never
  `virtual` + `override`), `final` where relevant.
- Free functions in a namespace rather than static methods or "utility" classes.
- No trivial getters/setters by reflex: if everything is get/set, it's a `struct`.
- Avoid *friend*, unnatural operator overloading, multiple implementation inheritance.

---

## 7. Error handling

- Choose **one** consistent strategy per layer and stick to it:
    - **expected** errors (missing file, invalid input) → `std::expected` / `std::optional`;
    - **invariant** violations / bugs → `assert` (or contracts);
    - **exceptional** errors not recoverable locally → exceptions (if the project allows them; to be decided in §0).
- Never silently ignore an error. `[[nodiscard]]` on functions whose return value must not be ignored.
- Error messages that say *what*, *where*, and *with which value*.

---

## 8. Code organization

- **One file = one concept.** Files < ~400 lines; beyond that, ask whether there are two concepts.
- `.hpp` headers / `.cpp` sources; `#pragma once`.
- **Include What You Use**; forward declarations when they suffice; no `using namespace` in a header.
- Implementation details in an anonymous namespace in the `.cpp` (or `detail::`).
- Declare variables **as close as possible to their first use**, in the smallest possible scope.
- **Package = directory = namespace**, as in Java: `include/tonedyne/dsp/effects/Gain.hpp` +
  `src/dsp/effects/Gain.cpp` declare `tonedyne::dsp::effects::Gain`. A file is named after its main type; a file of
  free functions, after the concept (`Level.hpp`, `Tuning.hpp`).
- **Include path = namespace** (never `../`): `"tonedyne/dsp/neural/NamModel.hpp"` (public API),
  `"ir/IrConvolver.hpp"` (private dsp header, relative to `src/dsp/`), `"chain/ChainCanvas.hpp"` (editor, relative to
  `src/editor/`), `"support/StereoSignal.hpp"` (tests, relative to `tests/`).
- An editor package is never named after a module (`dsp`, `model`) or a third-party library (`nam`, `juce`): from
  `tonedyne::editor`, `model::` would otherwise refer to the wrong namespace.
- One-way dependencies between modules; no cycles.

---

## 9. Comments and documentation

- Comment the **why**, never the **what** (the code says the what).
- Document every public API: role, preconditions, what is returned, possible errors, ownership/lifetime.
- Useful comments: reason for a non-obvious choice, link to an algorithm/paper, known pitfall, performance constraint.
- No commented-out code (git remembers it). `TODO` always comes with context.
- A comment that paraphrases the code must go; a comment that compensates for a bad name → rename.

---

## 10. Templates, macros, metaprogramming

- Templates only when genericity is genuinely needed. **Constrain with `concept`s** (C++20): readable error messages
  and documented intent.
- No SFINAE/`enable_if` if a concept or `if constexpr` suffices.
- **Macros forbidden** except include guards, platform conditional compilation, or a justified case impossible
  otherwise. Replace with `constexpr`, `inline`, templates.
- No "clever code": if it needs a comment to explain a trick, rewrite it simply.

---

## 11. Tooling (automatic guardrails)

Warnings, formatting, clang-tidy and sanitizers are enforced by `cmake/`, `.clang-format` and `.clang-tidy`.

- **Tests**: one test = one behavior, with a name that describes the behavior (`returnsEmptyWhenInputIsSilent`).
  *Arrange / Act / Assert* structure. Framework: Catch2 or GoogleTest.

---

## 13. Review checklist

Before approving a change, ask yourself:

- [ ] Can a newcomer understand each function **without looking elsewhere**?
- [ ] No function > 15 cognitive complexity / > 3 nesting levels?
- [ ] Are the names sufficient without comments?
- [ ] Is the ownership of every resource readable from the types?
- [ ] Are error cases handled explicitly?
- [ ] Is there a premature abstraction or a needless level of indirection?
- [ ] Zero warnings, clean clang-tidy, green tests?

---

## Instructions for Claude

- Apply these rules to all code written or modified; flag (without rewriting unprompted) existing code that violates
  them.
- Favor small, readable changes; explain any choice that departs from this document.
- Respond in French.
