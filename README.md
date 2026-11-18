# wl-steaminput

![license](https://img.shields.io/badge/license-GPL3-blue)
![platform](https://img.shields.io/badge/platform-Linux-lightgrey)

Your controller stops working when you play Steam games on Wayland — the
bindings you set in Steam actuate the desktop instead of the game, no matter
what the game settings say. This is a Wayland gap in Steam itself, not a driver
problem, and it is fixable with one launch option.

## Fix your game

1. Build the shim:

       cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
       cmake --build build -j

   The result is `build/libwlsteaminput.so`.

2. Steam → your game → Properties → Launch Options:

       LD_PRELOAD=/absolute/path/to/libwlsteaminput.so %command%

   Proton games played through `PROTON_USE_WAYLAND=1` need that variable in the
   same line, before `%command%`.

3. Play. The controller works in the game and goes back to the desktop when you
   alt-tab. Native Wayland games and Proton ones both work; Steam itself does
   not need restarting, the next game launch picks it up.

Works under any Wayland compositor. On the Steam Deck you do not need this:
gamescope reports focus to the client itself.

## Optional knobs

| Put in Launch Options | Effect |
|---|---|
| `WLSTEAMINPUT_DISABLE=1` | the shim loads and does nothing |
| `WLSTEAMINPUT_DEBUG=1` | a `wl-steaminput:` line per step, in the game's log |

The game's log is `~/.local/share/Steam/logs/console-linux.txt`: a
`waiting for the client:` line is the shim still loading what it needs, not an
error; a `SetFocusedWindow(...)` line is a focus report actually delivered.
[Reading the trace](HOWITWORKS.md#reading-the-trace) explains every line.

## Games with anti-cheat

The fix works by living inside the game process and hooking its library loader —
which is precisely what third-party anti-cheat looks at. Single-player and
co-op games are fine; nothing reads the process but Steam. In online games with
Easy Anti-Cheat, BattlEye or similar, an unexpected preloaded library in the
game can be flagged or is against the terms — if the account matters, leave
those games alone; they also tend to run X11 and need no help anyway.

## The overlay (Shift+Tab)

Not this shim's business, and it cannot be: the overlay needs an X11 window
inside the game, which a Wayland game does not have — Valve's own gap, open
since 2021 ([#8020](https://github.com/ValveSoftware/steam-for-linux/issues/8020),
[#11818](https://github.com/ValveSoftware/steam-for-linux/issues/11818)). In
practice:

- **Play X11** (`SDL_VIDEO_DRIVER=x11` launch option, runs on XWayland): overlay
  works, there Steam already sees the focus itself and this shim is not needed.
- **Play Wayland**: controller works through this shim, overlay does not — on
  any setup except a full gamescope session, where the compositor implements
  the overlay itself.

Controller or overlay, pick per game; both are one launch option away.

## How it works

Short version: the shim watches the game window's focus through the game's own
Wayland connection and tells the Steam client over the game's own Steam pipe —
the two channels a Wayland game actually has, which Valve's focus tracking
listens to on X11 and nobody feeds on Wayland. The
[full account](HOWITWORKS.md) covers why that is the only channel there is, how
it survives Steam updates, and what every trace line means.

# Contributing

The invariants, the style and the real-client test a change must show:
[CONTRIBUTING.md](CONTRIBUTING.md).

# License

GPL-3.0-or-later. Dobby, fetched at build time, carries its own license.
