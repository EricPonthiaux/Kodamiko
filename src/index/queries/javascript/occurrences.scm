(function_declaration name: (identifier) @definition)
(generator_function_declaration name: (identifier) @definition)
(class_declaration name: (_) @definition)
(method_definition name: (property_identifier) @definition)
(variable_declarator name: (identifier) @definition)

(call_expression function: (identifier) @call)
(call_expression function: (member_expression property: (property_identifier) @call))
(new_expression constructor: (identifier) @call)

(assignment_expression left: (identifier) @write)
(assignment_expression left: (member_expression property: (property_identifier) @write))
(augmented_assignment_expression left: (identifier) @write)
(update_expression argument: (identifier) @write)

(import_statement source: (string) @import)
