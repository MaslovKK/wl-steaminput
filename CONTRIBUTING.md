# Contributing to `wl-steaminput`

`wl-steaminput` is intentionally minimal: a single shared object linking only against `libc`, `libdl`, `Dobby`, and Wayland protocol headers.

If a proposed change introduces a new dependency or assumes a library is already loaded at startup, **open an issue first** to discuss it. For architectural context, see [README.md](README.md) and [HOWITWORKS.md](HOWITWORKS.md); this document defines the invariants and standards that contributions must preserve.

---

## Core Invariants

* **Fail-Safe Address Resolution:** Never read from or jump to an address that a pattern scan did not locate and a memory segment bounds-check did not confirm. If a Steam client update moves a symbol, the shim must gracefully degrade to a `waiting for the client` log line—never a segmentation fault.
* **Unique Pattern Matching:** Every byte pattern must match **exactly once**. If a signature matches multiple locations, supply a disambiguating predicate and document *why* it selects the target site.
* **Dual-Binary Verification:** Validate every new or modified pattern against both `linux64/steamclient.so` and `steamrt64/steamclient.so` (both binaries are present in every standard Steam installation).
* **Capture Group Ordering:** Builder patterns use a fixed `$` capture-group order consumed directly by the resolver (`0` = connection accessor, `5` = call table). Always update the pattern and its consumer in lockstep.
* **Focus Report AppIDs:** Report the game's AppID while any of its windows is active, and the desktop controller configuration AppID (`413080`) when none are active. Never report `0` or Steam's internal desktop AppID (`202355`), as doing so suspends Steam Input polling (see comments in `src/steam/steam.cpp`).
* **Client Struct Layouts:** Definitions such as `CUtlLinkedList`, pipe records, and `CallContext` mirror Valve's internal binary layouts. Modify them only after verifying against a disassembled `steamclient.so` binary.

---

## Code Style

* **Standard & Formatting:** C++23, Allman brace style, 4-space indentation.
* **Naming Conventions:**
  * Globals: `g_camelCase`
  * Constants: `kCamelCase`
  * Reversed Steam client types: Retain Valve's original naming conventions (e.g., `m_` member prefixes, `__vftable`).


* **Comments:** Document *why* non-obvious behavior exists (especially around ABI quirks or IPC workarounds). Remove comments that merely restate what the code immediately below already expresses.

---

## Testing & Verification

Because the shim relies on runtime dynamic linking and live Steam IPC pipes, changes are verified directly against real games rather than mocked unit tests.

1. Launch **both** a native Wayland game (e.g., Dota 2) and a Proton title running with `PROTON_USE_WAYLAND=1`.
2. Set `WLSTEAMINPUT_DEBUG=1` in the launch options for both games.
3. Inspect `~/.local/share/Steam/logs/console-linux.txt` and verify:
* A single `found` line identifying the senders, call table, builder, and desktop configuration AppID.
* `toplevel activated` and `toplevel deactivated` entries firing reliably on every window focus change.
* `SetFocusedWindow(<game_app_id>)` on window activation and `SetFocusedWindow(413080)` on deactivation.



> **Pattern Changes:** If your PR modifies byte signatures, you must verify on a game that maps `steamrt64/steamclient.so` (Steam Linux Runtime) in addition to `linux64/steamclient.so`.

---

## Commits & Licensing

* **Commit Format:** Use [Conventional Commits](https://www.conventionalcommits.org/) (`feat:`, `fix:`, `docs:`, `refactor:`, `build:`, `test:`, `chore:`). Keep commits atomic (one logical change per commit), use an imperative subject line, and explain the *reason* for the change in the commit body.
* **License:** All contributions are licensed under **GPL-3.0-or-later** to match the rest of the repository. Contributors retain copyright over their original work.
