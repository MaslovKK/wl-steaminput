The shim stays small: one shared object, libc, libdl, Dobby, wayland headers. If a
change adds a dependency or assumes something is already loaded, open an issue
first. The README explains how it works; this file says what must not break.

# Rules

- Never call an address that a pattern scan did not find and a segment check did not
  confirm. A client update must cause a `waiting for the client` log line, not a
  crash.
- A pattern must match exactly once. When several places match, pass a predicate
  that picks ours and say why it picks the right one.
- Check every new pattern against both client libraries: `linux64/steamclient.so`
  and `steamrt64/steamclient.so`. Every install has both files.
- Builder patterns have a fixed `$` group order, read by the code that consumes them
  (0 = connection accessor, 5 = call table). Change the pattern and the consumer
  together.
- Focus reports: the game's app id when one of its windows is activated, the desktop
  config app id when none is. Reporting zero or the client's internal desktop id
  (202355) stops the client's input polling; see the comment in
  `src/steam/steam.cpp`.
- Struct layouts (`CUtlLinkedList`, pipe records, `CallContext`) are the client's.
  Change one only against a library you have disassembled.

# Style

C++23, Allman braces, four-space indent, `g_camel` globals, `kCamelCase` constants,
client names for client types (`m_` fields, `__vftable`). Comment only the
non-obvious reason; delete comments that restate the line below them.

# Testing

No unit test; run a real client. Dota 2 (native Wayland) and a Proton game with
`PROTON_USE_WAYLAND=1`, both with `WLSTEAMINPUT_DEBUG=1`, then read
`~/.local/share/Steam/logs/console-linux.txt` and expect:

- one `found` line naming senders, table, builder and desktop config app;
- `toplevel activated/deactivated` on every window change;
- `SetFocusedWindow(<game app id>)` on activation and `SetFocusedWindow(413080)` on
  deactivation.

If your change touches patterns, repeat on a game that maps the runtime library.

# Commits

Conventional commits: `feat:`, `fix:`, `docs:`, `refactor:`, `build:`, `test:`,
`chore:`. Imperative description, the reason in the body, one change per commit.
GPL-3.0-or-later like everything here; you keep copyright of what you contribute.
