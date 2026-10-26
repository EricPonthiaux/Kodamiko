This is an Angular {{angular}} application: standalone components, signals,
zoneless change detection (no zone.js), built with the Angular CLI
(`npx ng ...`).

- Give each component its template in its own `.html` file (`templateUrl`)
  and its styles in a `.css` file (`styleUrl`): Duet's Angular language
  server completes and checks those files, not inline templates.
- Use the built-in control flow (`@if`, `@for`, `@switch`), `input()`,
  `output()`, `computed()` and `inject()`.
- Generate with the CLI (`npx ng generate component name`), so that files
  follow the project's conventions.
