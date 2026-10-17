# Explainer boards (.board)

A JSON scene, the format of sidecar's explainer boards, for free visual
explanations where the place of each element is part of the meaning:

```json
{
  "format": "sciilo-board",
  "version": 1,
  "elements": [
    {"id": "collect", "type": "frame", "title": "Collect", "x": 20, "y": 20, "width": 420, "height": 180},
    {"id": "logs", "type": "node", "parent": "collect", "shape": "rounded", "label": "Logs", "x": 60, "y": 80},
    {"id": "store", "type": "node", "shape": "circle", "label": "Store", "tone": "accent", "x": 500, "y": 60},
    {"id": "push", "type": "edge", "from": "logs", "to": "store", "label": "push", "arrow": "end"},
    {"id": "axis", "type": "stroke", "x": 40, "y": 260, "points": [[0, 0], [400, 0]], "arrow": "end", "label": "time"}
  ]
}
```

- `type`: `node`, `frame` (holds the elements naming it as `parent`),
  `edge` (between two elements, `from` / `to`), `stroke` (a free line,
  `points` relative to its `x` / `y`).
- `shape` of a node: `rectangle`, `rounded`, `circle`, `ellipse`,
  `triangle`, `diamond`, `hexagon`, `text` (no outline).
- `tone`: `neutral`, `accent`, `info`, `success`, `warning`, `danger`.
- `arrow`: `none`, `start`, `end`, `both`; `line`: `solid`, `dashed`,
  `dotted`. Optional: `description`, `width`, `height`, `appearAt`.
- Ids: short, readable, unique. Give nodes and frames `x` and `y`; a scene
  with none at all is laid out automatically.
