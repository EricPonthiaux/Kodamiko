# Contributing to Duet

Thank you for your interest. Bug reports, ideas and pull requests are all
welcome. Duet is young: before a large change, open an issue to talk about
it, so that no work is lost.

## Reporting a bug

Open an issue with the *Bug report* template: what you did, what you
expected, what happened, and your system, Qt and Claude Code versions
(*Help ▸ About Duet*). A security problem is not reported in an issue: see
[`SECURITY.md`](SECURITY.md).

## Building and testing

The requirements and the build steps are in the [`README.md`](README.md#getting-started).
For development, use the debug preset:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug          # QT_QPA_PLATFORM=offscreen without a display
```

## Before you open a pull request

- The code follows the project's conventions, written in
  [`CLAUDE.md`](CLAUDE.md): it is read by people and by Claude alike. In
  short: functions of cognitive complexity ≤ 15 and nesting ≤ 3, names that
  need no comment, ownership readable from the types, comments that say
  *why*, and everything in English.
- A behavior change comes with a Qt Test named after the behavior
  (`highlightsEveryOccurrenceOfTheNameAtTheCursor`).
- It builds without warnings, and these report nothing:
  ```sh
  cmake --build build-debug --target format-check
  cmake --build build-debug --target tidy
  ctest --preset debug
  ```
  `git config core.hooksPath tools/git-hooks` checks the formatting before
  each commit.
- One pull request does one thing. Its commit messages say what changes
  for the user, in the style of the history (`git log`).

## License

Duet is distributed under the GNU GPL v3 with an additional permission for
Graphviz (see [`README.md`](README.md#license)). By contributing, you agree
that your contribution is distributed under the same terms. Every new source
file starts with `// SPDX-License-Identifier: GPL-3.0-only`.
