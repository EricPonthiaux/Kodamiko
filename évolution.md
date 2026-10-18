# Visual evolutions

Proposals to make the editor more beautiful, ranked by impact versus effort.
They come from reading the code (`src/ui/Theme.cpp`, `src/ui/ThemeStyle.cpp`,
`src/editor/EditorWidget.cpp`, `src/syntax/ScintillaStyles.cpp`), not from
screenshots.

What is already sound: Inter for the interface, JetBrains Mono for code, a
tuned line height, popups with shadows, file icons.

## Progress

Done: the "Duet" theme as the default (brand colors, flat selections, three
surface levels), chevron fold marks, the active indentation guide, the
brighter current line number, and the editor's diagnostic and unmatched
brace colors taken from `Theme`. Still open in section 1: the tree selection
as a translucent accent with a left bar, and the error and warning colors
hard-coded outside the editor (`ProblemsView`, `MainWindow`).

## 1. Quick wins (a day or two, immediate effect)

- **Flat colors instead of gradients.** The blue-to-light-blue gradients on
  menu items, tree selection and the default button look dated. Recent
  editors (Zed, Fleet, VS Code) use a flat accent. Tree selection: the accent
  at 15–20% opacity with a 2px accent bar on the left.
- **Distinct surface levels.** `window` (#21252b) and `background` (#1f2329)
  are nearly identical, so panels and editor blend together. Define three
  clear levels: sunken (panels), base (editor), raised (popups, with the
  existing shadow).
- **Fold margin.** `BoxedTreeFoldStyle` (boxed +/− joined by lines) is the
  most dated element of the editor. Use thin chevrons ▸/▾ shown only when the
  margin is hovered.
- **Active indentation guides.** Keep the guides very faint and highlight the
  one of the block holding the caret (`SCI_SETHIGHLIGHTGUIDE`).
- **Diagnostic colors from the theme.** `#e5534b` and `#d4a72c` are
  hard-coded in `EditorWidget::setDiagnostics`, as is the unmatched brace
  color (`#e06c75`). Move them to `Theme`.
- **Current line number** brighter than the other line numbers.

## 2. Richer syntax highlighting

- **More categories.** A function definition and a call share one color, as
  do `return` and `const`. Add `function.definition`, `keyword.control`,
  `comment.documentation`, `type.definition`, and use italics for parameters
  and control flow (as Tokyo Night or Catppuccin do).
- **Rainbow brackets**, colored by nesting depth. Tree-sitter already gives
  the pairs.
- **Occurrences of the word under the caret**, subtly boxed (the find bar
  already uses `INDIC_ROUNDBOX`).
- **LSP semantic tokens** as an option (local vs member, mutable vs
  constant), on top of tree-sitter.

## 3. Themes: the strongest lever

- **A signature "Duet" theme, the default.** Built on the brand colors: tile
  #1f232c, block #4f7cf0, caret #e7e9ef. The current dark theme is a One Dark
  clone whose accent (#528bff) is not quite the logo's. A theme of its own
  gives the editor an identity.
- **Import VS Code themes.** Their `tokenColors` scopes map almost directly
  to the tree-sitter capture names `Theme` already uses. This opens thousands
  of themes at once — probably the most wanted feature.
- **Ship a few curated themes:** Tokyo Night, Catppuccin (Mocha/Latte), Rosé
  Pine, GitHub Dark/Light.

## 4. Typography

- **Optional ligatures**, in the editor only: the terminal keeps the NL
  version for cell alignment.
- **Font weight and line height settings.** `AppFonts::codeLineHeight` is
  fixed today; some prefer Light, others a 1.6 line height.
- **A curated font list** in Preferences: Geist Mono, Commit Mono,
  Monaspace, Iosevka.

## 5. Structural visual features (more effort)

- **Overview ruler in the scroll bar:** marks for git changes, errors, search
  results and the caret. More useful and more elegant than a minimap.
- **Sticky scroll:** the signature of the enclosing function or class stays
  pinned at the top while scrolling. Tree-sitter already provides the scopes
  (see the outline code).
- **Inline diagnostics** (Error Lens style): the message in gray at the end
  of the faulty line. Depends on the Scintilla version bundled with
  QScintilla (end-of-line annotations, or annotations below the line); to be
  checked.
- **Zen mode:** panels hidden, text centered, limited width.

## 6. Interface polish

- **Tabs:** show the ● modified dot in place of the close cross instead of
  prefixing the name (as VS Code and Zed do); put the active tab's accent on
  top rather than at the bottom.
- **Micro-animations** of 100–150 ms on hover, panel opening and popup fades.
  `QPropertyAnimation` is already used in `PopupWidgets`.
- **Adjustable density** "Compact / Comfortable" for trees, tabs and the
  status bar.

## Suggested order

1. The signature "Duet" theme, with flat colors, surface levels and chevrons.
2. VS Code theme import.
3. The overview ruler in the scroll bar.

The first two change the first impression; the third improves everyday use.
