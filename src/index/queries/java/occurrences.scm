(class_declaration name: (identifier) @definition)
(interface_declaration name: (identifier) @definition)
(enum_declaration name: (identifier) @definition)
(record_declaration name: (identifier) @definition)
(annotation_type_declaration name: (identifier) @definition)
(method_declaration name: (identifier) @definition)
(constructor_declaration name: (identifier) @definition)
(variable_declarator name: (identifier) @definition)
(formal_parameter name: (identifier) @definition)
(enum_constant name: (identifier) @definition)

(method_invocation name: (identifier) @call)
(object_creation_expression type: (type_identifier) @call)

(assignment_expression left: (identifier) @write)
(assignment_expression left: (field_access field: (identifier) @write))
(update_expression (identifier) @write)

(import_declaration (_) @import)
