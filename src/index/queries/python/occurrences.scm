(function_definition name: (identifier) @definition)
(class_definition name: (identifier) @definition)
(parameters (identifier) @definition)
(default_parameter name: (identifier) @definition)
(typed_parameter (identifier) @definition)
(typed_default_parameter name: (identifier) @definition)
(assignment left: (identifier) @definition)
(for_statement left: (identifier) @definition)

(call function: (identifier) @call)
(call function: (attribute attribute: (identifier) @call))

(assignment left: (attribute attribute: (identifier) @write))
(augmented_assignment left: (identifier) @write)
(augmented_assignment left: (attribute attribute: (identifier) @write))

(import_statement name: (dotted_name) @import)
(import_statement name: (aliased_import name: (dotted_name) @import))
(import_from_statement module_name: (_) @import)
