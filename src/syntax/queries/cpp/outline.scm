; Outline of a C++ file (see README.md). Out-of-class definitions
; ("void Foo::bar()") are methods.

(namespace_definition name: (_) @name) @definition.namespace
(namespace_definition !name body: (_)) @definition.namespace

(class_specifier name: (_) @name body: (_)) @definition.class
(struct_specifier name: (_) @name body: (_)) @definition.struct
(union_specifier name: (_) @name body: (_)) @definition.struct
(enum_specifier name: (_) @name body: (_)) @definition.enum

(alias_declaration name: (_) @name) @definition.type
(type_definition
  declarator: [
    (type_identifier) @name
    (pointer_declarator declarator: (type_identifier) @name)
  ]) @definition.type

(function_definition
  declarator: [
    (function_declarator declarator: [(identifier) (field_identifier) (destructor_name) (operator_name) (template_function)] @name)
    (pointer_declarator declarator: (function_declarator declarator: [(identifier) (field_identifier) (operator_name) (template_function)] @name))
    (reference_declarator (function_declarator declarator: [(identifier) (field_identifier) (operator_name) (template_function)] @name))
  ]) @definition.function

(function_definition
  declarator: [
    (function_declarator declarator: (qualified_identifier) @name)
    (pointer_declarator declarator: (function_declarator declarator: (qualified_identifier) @name))
    (reference_declarator (function_declarator declarator: (qualified_identifier) @name))
  ]) @definition.method

; Prototypes, including methods and constructors declared inside a class.
(declaration
  declarator: [
    (function_declarator declarator: [(identifier) (field_identifier) (destructor_name) (operator_name) (qualified_identifier)] @name)
    (pointer_declarator declarator: (function_declarator declarator: [(identifier) (field_identifier) (operator_name)] @name))
    (reference_declarator (function_declarator declarator: [(identifier) (field_identifier) (operator_name)] @name))
  ]) @declaration.function

(field_declaration
  declarator: [
    (function_declarator declarator: [(identifier) (field_identifier) (destructor_name) (operator_name) (qualified_identifier)] @name)
    (pointer_declarator declarator: (function_declarator declarator: [(identifier) (field_identifier) (operator_name)] @name))
    (reference_declarator (function_declarator declarator: [(identifier) (field_identifier) (operator_name)] @name))
  ]) @declaration.function

(field_declaration
  declarator: [
    (field_identifier) @name
    (pointer_declarator declarator: (field_identifier) @name)
    (reference_declarator (field_identifier) @name)
    (array_declarator declarator: (field_identifier) @name)
  ]) @definition.field

(preproc_function_def name: (identifier) @name) @definition.macro
