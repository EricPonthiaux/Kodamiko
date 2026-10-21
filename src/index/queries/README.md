# Occurrence queries (occurrences.scm)

tree-sitter queries of the occurrence index, written for Duet. Every
identifier of a file is an occurrence, a reference unless a query gives
it another role:

- `@definition`: the name a declaration introduces (function, variable,
  parameter, type, field…);
- `@call`: the name of a called function or method, or of a constructed
  type;
- `@write`: a name assigned to or incremented;
- `@import`: a whole include or import (its path, its module name):
  every word in it is an import.

Comments and strings are read word by word, as comment and string
occurrences. Like the highlighting queries, the C query is read before the
C++ one, and the JavaScript one before the TypeScript one;
`javascript/occurrences-params.scm` holds what TypeScript cannot read.
