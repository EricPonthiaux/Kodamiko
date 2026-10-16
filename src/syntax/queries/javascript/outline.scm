; Outline of a JavaScript / JSX file (see README.md).

(class_declaration name: (_) @name) @definition.class
(variable_declarator name: (identifier) @name value: (class)) @definition.class

(function_declaration name: (_) @name) @definition.function
(generator_function_declaration name: (_) @name) @definition.function
(variable_declarator
  name: (identifier) @name
  value: [(arrow_function) (function_expression) (generator_function)]) @definition.function

(method_definition name: (_) @name) @definition.method
(field_definition property: (_) @name) @definition.field
