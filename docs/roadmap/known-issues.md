# Known issues

Small problems and ideas waiting for an issue of their own.

## Interface

- On small screens, the tab labels are cut: they need another way to fit.
- With many build configurations, the build menu is crowded: it could
  scroll.
- An image opened in the editor cannot be zoomed.

## Dependencies

- On Linux and macOS, Graphviz comes from the system (a package or
  Homebrew); only on Windows is its official release downloaded and shipped
  with Duet. Whether the macOS package should carry it too is open. Its
  license (Eclipse Public License 1.0) is covered by the additional
  permission in `README.md`.

## Ideas

- Offer Graphviz's other layout engines (neato, fdp, circo…) in the
  diagram editor.
