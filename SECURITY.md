# Security policy

Kodamiko runs an AI agent next to your code: it starts processes, runs
commands in terminals, reads and writes files, and serves tools to Claude
Code over a local socket. A flaw there can reach your machine, so please
report it privately.

## Reporting a vulnerability

Use GitHub's private reporting: on the repository's **Security** tab,
**Report a vulnerability**. Do not open a public issue.

Say what is affected, how to reproduce it, and what an attacker gains.
You will get an answer within a week; once a fix is released, the report is
published with credit to you, unless you prefer otherwise.

## Supported versions

Kodamiko is in preview: only the latest release and the `main` branch receive
fixes.

## In scope

For example:

- a sensitive tool of Kodamiko's MCP server (commit, push, discard, restore a
  snapshot…) that runs without the user's *Allow*;
- another local user or process reaching Kodamiko's hook or MCP sockets;
- a project file (settings, run configurations, diagrams, Markdown) that
  runs code or reads files when it is merely opened;
- credentials leaking from the git askpass helper or the logs.

Claude Code's own behavior, and the commands the user allows it to run, are
outside Kodamiko's scope: report those to Anthropic.
