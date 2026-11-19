# wl-steaminput

Fixes Steam Input for native Wayland and `wine-wayland` games on Linux.

When you run a game directly on Wayland (without XWayland), Steam cannot detect when the game window is focused. As a result, Steam Input keeps your controller bound to your desktop configuration instead of switching to your game's profile—an issue open since 2021 ([ValveSoftware/steam-for-linux#8020](https://github.com/ValveSoftware/steam-for-linux/issues/8020)).

`wl-steaminput` is a lightweight, compositor-agnostic `LD_PRELOAD` shim that bridges window focus from the game's Wayland connection directly to the Steam client.

---

## Do You Need This?

* **Native Wayland games or Proton with `PROTON_USE_WAYLAND=1`:** **Yes.** If your controller inputs are triggering desktop actions while in-game, this fixes it.
* **Default Proton (XWayland):** **No.** Games running as X11 windows through XWayland already report focus to Steam natively.
* **Steam Deck / Gamescope:** **No.** Gamescope manages and reports window focus to Steam itself.
* **Desktop Environment / Compositor:** Works on **all** Wayland compositors (KDE Plasma, GNOME, Hyprland, Sway, etc.) without compositor-specific extensions.

---

## Quickstart

### 1. Download or Build

Grab the prebuilt `libwlsteaminput.so` from the [latest release](https://github.com/MaslovKK/wl-steaminput/releases/latest), or build it from source:

```bash
cmake -B build
cmake --build build -j
```

### 2. Add to Steam Launch Options

Open Steam, right-click your game, go to **Properties → General → Launch Options**, and add:

```bash
LD_PRELOAD=/absolute/path/to/libwlsteaminput.so %command%
```

> **Note:** If you are using `PROTON_USE_WAYLAND=1`, ensure both variables appear before `%command%`:
> ```bash
> PROTON_USE_WAYLAND=1 LD_PRELOAD=/absolute/path/to/libwlsteaminput.so %command%
> ```
>
>

### 3. Launch and Play

No Steam restart is required. Your controller will switch to the game's profile on focus and revert to desktop bindings when you Alt+Tab out.

---

## Environment Variables

You can pass optional configuration variables in your game's Launch Options before `%command%`:

| Variable | Effect |
| --- | --- |
| `WLSTEAMINPUT_DISABLE=1` | Loads the library as a no-op (useful for testing without altering paths). |
| `WLSTEAMINPUT_DEBUG=1` | Prints step-by-step trace logs (`wl-steaminput: ...`) to Steam's console log. |

### Reading Logs

Debug output is written to `~/.local/share/Steam/logs/console-linux.txt`:

* `waiting for the client:` — Normal startup behavior while the shim waits for the Steam client API to initialize.
* `SetFocusedWindow(...)` — Confirms a focus state change was successfully sent to Steam.

For a complete breakdown of log lines, see [Reading the trace](HOWITWORKS.md#reading-the-trace-log).

---

## Anti-Cheat Warning

> **⚠️ Do not use this shim with kernel or user-space anti-cheat systems.**

`wl-steaminput` injects into the game process and hooks dynamic library loading—behavior that anti-cheat software explicitly flags.

* **Safe:** Single-player games, local co-op, and peer-to-peer games without anti-cheat.
* **Unsafe:** Online multiplayer games protected by Easy Anti-Cheat (EAC), BattlEye, or similar systems. Preloading unauthorized `.so` libraries violates terms of service and risks account bans (most of these titles run under XWayland anyway and do not need this fix).

---

## Known Limitation: The Steam Overlay (`Shift+Tab`)

Window focus and the Steam Overlay are separate issues on Wayland ([steam-for-linux#8020](https://github.com/ValveSoftware/steam-for-linux/issues/8020), [#11818](https://github.com/ValveSoftware/steam-for-linux/issues/11818)). The Steam Overlay composites onto an X11 window inside the game process, which pure Wayland windows lack.

* **Native Wayland / `PROTON_USE_WAYLAND=1`:** Steam Input works via this shim, **but the Steam Overlay will not open** (unless running inside a nested Gamescope session, which renders the overlay itself).
* **XWayland (`SDL_VIDEO_DRIVER=x11`):** Both the Steam Overlay and Steam Input work natively without this shim.

---

## Comparison with Other Tools

Different tools in the ecosystem solve different parts of Wayland input and overlay support:

| Tool | Wayland Focus Fix | `wine-wayland` Overlay | Requirements & Caveats |
| --- | --- | --- | --- |
| **wl-steaminput** | Native + All Proton | No | Single `LD_PRELOAD` launch option; survives Steam updates via pattern scanning. |
| **[GE-Proton](https://github.com/GloriousEggroll/proton-ge-custom)** (`overlay_bridge`) | `wine-wayland` only | Yes | Requires GE-Proton; tied to specific `steamclient` build IDs and breaks on unapproved Steam updates. |
| **[extest](https://github.com/Supreeeme/extest)** | No (X11/desktop input translation only) | No | Must be preloaded onto the Steam client itself. |

*Note: GE-Proton's `overlay_bridge` targets the exact same internal `SetFocusedWindow` IPC call, independently confirming this approach. Unlike GE-Proton's allowlisted offsets, `wl-steaminput` uses signature pattern scanning so Steam client updates won't break your controller.*

---

## How It Works

A Wayland game communicates with two separate systems: the Wayland compositor (for display and window events) and the Steam client (via local IPC pipes). On X11, Steam monitors window focus directly; on Wayland, nothing bridges these two channels.

`wl-steaminput` hooks into the game's Wayland connection to listen for surface focus events and forwards them over the game's existing Steam IPC pipe. For a deep dive into the architecture, signature scanning, and IPC internals, read [HOWITWORKS.md](HOWITWORKS.md).

---

## Contributing & License

* **Contributing:** See [CONTRIBUTING.md](CONTRIBUTING.md) for code invariants, style guidelines, and required real-client test criteria.
* **License:** [GPL-3.0-or-later](LICENSE). `Dobby` (fetched at build time) is subject to its own license terms.
