# Outline queries (outline.scm)

tree-sitter queries for the Outline panel, written for Duet (the
highlighting queries come from the grammars themselves). Conventions:

- `@definition.<kind>` covers the whole element (body included): its range
  is used to nest elements and to follow the cursor;
- `@declaration.<kind>`: a mere declaration (prototype, signature),
  ignored inside a function;
- `@name`: the displayed name, where the cursor is placed.

Kinds: `namespace`, `class`, `struct`, `interface`, `enum`, `type`,
`function`, `method`, `field`, `macro`. A function contained in a
class, a struct or an interface is displayed as a method.
