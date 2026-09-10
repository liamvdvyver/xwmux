# xwmux internals

This directory describes the implementation that is in this repository, rather
than proposing a configuration interface.

- [Architecture](architecture.md) introduces the processes, ownership boundaries,
  and state held by the window manager.
- [Event flows](event-flows.md) follows window creation, tmux-driven state
  reconciliation, focus/visibility, geometry, and teardown.
- [Prefix handling](prefix-handling.md) explains the X keyboard grab and the
  temporary terminal-focus override.
- [IPC and tmux integration](ipc-and-tmux.md) documents the X client-message
  protocol, the root-window properties, terminal initialization, and the
  current tmux hook path.
- [Testing and inspection](testing.md) describes the headless integration
  harness and `dump-state`.

For installation and user-facing configuration, start at the repository
[README](../README.md).
