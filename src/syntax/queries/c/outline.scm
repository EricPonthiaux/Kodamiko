; Outline of a C file (see README.md).

(function_definition
  declarator: [
    (function_declarator declarator: (_) @name)
    (pointer_declarator declarator: (function_declarator declarator: (_) @name))
    (pointer_declarator declarator: (pointer_declarator declarator: (function_declarator declarator: (_) @name)))
  ]) @definition.function

(declaration
  declarator: [
    (function_declarator declarator: (identifier) @name)
    (pointer_declarator declarator: (function_declarator declarator: (identifier) @name))
  ]) @declaration.function

(struct_specifier name: (_) @name body: (_)) @definition.struct
(union_specifier name: (_) @name body: (_)) @definition.struct
(enum_specifier name: (_) @name body: (_)) @definition.enum

(type_definition
  declarator: [
    (type_identifier) @name
    (pointer_declarator declarator: (type_identifier) @name)
  ]) @definition.type

(field_declaration
  declarator: [
    (field_identifier) @name
    (pointer_declarator declarator: (field_identifier) @name)
    (array_declarator declarator: (field_identifier) @name)
  ]) @definition.field

(preproc_function_def name: (identifier) @name) @definition.macro
