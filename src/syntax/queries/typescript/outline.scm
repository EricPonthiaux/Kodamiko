; Structure d'un fichier TypeScript / TSX (voir README.md).

(internal_module name: (_) @name) @definition.namespace
(module name: (_) @name) @definition.namespace

(class_declaration name: (_) @name) @definition.class
(abstract_class_declaration name: (_) @name) @definition.class
(variable_declarator name: (identifier) @name value: (class)) @definition.class
(interface_declaration name: (_) @name) @definition.interface
(enum_declaration name: (_) @name) @definition.enum
(type_alias_declaration name: (_) @name) @definition.type

(function_declaration name: (_) @name) @definition.function
(generator_function_declaration name: (_) @name) @definition.function
(function_signature name: (_) @name) @declaration.function
(variable_declarator
  name: (identifier) @name
  value: [(arrow_function) (function_expression) (generator_function)]) @definition.function

(method_definition name: (_) @name) @definition.method
(method_signature name: (_) @name) @definition.method
(abstract_method_signature name: (_) @name) @definition.method

(public_field_definition name: (_) @name) @definition.field
(property_signature name: (_) @name) @definition.field
