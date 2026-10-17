---
name: diagrams
description: Draw or edit a diagram the user sees in Duet's diagram editor — flowchart, sequence, class, state, data model (ER), C4 architecture, mind map, user journey, product vision or explainer board — as a Mermaid (.mmd), PlantUML (.puml) or explainer board (.board) file. Use whenever the user asks for a diagram, chart, schema, "schéma", map of a process, of classes, of exchanges, of states, of tables or of an architecture, or to change an existing diagram file.
---

# Diagrams in Duet

Duet shows diagram files in its diagram editor, in a tab, laid out
automatically with Graphviz. Write the file with your usual tools; Duet opens
it as soon as you have written it, and redraws it every time you change it.

## Which diagram

Pick the kind that fits what the user wants to see:

| Kind | File | First line |
|---|---|---|
| Flowchart: steps, decisions, a process, an algorithm | `.mmd` | `flowchart TB` (or `LR`) |
| Sequence diagram: exchanges between actors over time | `.mmd` | `sequenceDiagram` |
| Class diagram: classes, attributes, methods, relations | `.mmd` | `classDiagram` |
| State diagram: states and transitions, a lifecycle | `.mmd` | `stateDiagram-v2` |
| Data model: tables, columns, keys, relationships | `.mmd` | `erDiagram` |
| C4 architecture: people, systems, containers, components | `.mmd` | `C4Context`, `C4Container`, `C4Component` |
| Mind map: a central idea and its branches | `.mmd` | `mindmap` |
| User journey: steps of a user's experience with a score | `.mmd` | `journey` |
| Product vision: the fixed sections of a product's vision | `.mmd` | `%% product-vision` then `mindmap` |
| Explainer board: free shapes, frames, arrows, annotations | `.board` | a JSON scene (see below) |

Mermaid is the format by default. Class, state and sequence diagrams may also
be written in PlantUML (`.puml`, `@startuml` … `@enduml`) when the project
already uses PlantUML.

Other Mermaid kinds (pie, gantt, git graph…) are not supported: say so and
offer one of the kinds above, or a Mermaid block in Markdown.

## Where

Unless the user names a file, write `docs/diagrams/<topic>.mmd` (kebab-case
name, in the language the user speaks).

## Writing Mermaid

Write standard Mermaid, with short, readable ids (`check`, `paid`,
`OrderService`): they are how Duet keeps each element's place in the layout.
What Duet does not keep: flowchart subgraphs are flattened, and styling
(`style`, `classDef`, `linkStyle`, `UpdateElementStyle`…) is ignored.

## Explainer boards (.board)

A JSON scene whose coordinates are part of the meaning. Read
[board-format.md](board-format.md) before writing one.

## Layout

Do not write coordinates in Mermaid or PlantUML: Duet lays the diagram out.
Once the user has arranged it in Duet, the file ends with lines such as

    %% Layout kept by Duet (positions, sizes, colors, curves)
    %% duet {"key":"check","x":40,"y":120}

(`'` instead of `%%` in PlantUML). They hold the user's arrangement:

- keep them as they are when you edit the diagram; never write new ones;
- a new element (an id without such a line) is placed beside the elements it
  is linked to, the others keep their place;
- an element removed from the source simply loses its line;
- for a deep restructuring, drop all the `duet` lines: the whole diagram is
  laid out again.

## Duet's tools

In a session started by Duet, its MCP server (tools `mcp__duet__…`) sees the
diagram editor:

- `get_diagram`: a diagram as the user has it, unsaved changes included,
  with the elements they selected ("this box", "the selected ones");
- `apply_diagram_source`: changes a diagram open in Duet without writing
  the file (the user saves; one step they can undo) — prefer it while the
  user is editing that diagram;
- `layout_diagram`, `select_diagram_elements`, `export_diagram`,
  `get_diagram_problems`, `open_file`.

## Readability

Keep a diagram to about 25 elements, with short texts (a few words, the
user's vocabulary, not code identifiers unless asked). Split a larger
diagram into several files, one per part, and say which is which.

## Checking

After each write, Duet reads the file as its editor does. If something could
not be read (an unknown kind of diagram, a statement it does not understand,
invalid JSON), the problems come back to you right away: fix the file before
going on.
