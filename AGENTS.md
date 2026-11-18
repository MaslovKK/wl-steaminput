Guidance for coding agents working in this repository. Humans: CONTRIBUTING.md.

What it is: an LD_PRELOAD shim; the README's How section is the design. Read it
before touching `src/steam/client.cpp`.

# Build and checks

    cmake -B build
    cmake --build build -j
    clang-format --dry-run -Werror src/*/*.cpp src/*/*.h

Warnings are errors in practice: the target builds with `-Wall -Wextra` clean.
`.clang-format` is the style authority; format touched files with
`clang-format -i`, do not hand-align.

# What you can verify from a shell

The live scan, without launching a game:

    WLSTEAMINPUT_DEBUG=1 SteamAppId=480 python3 -c "
    import ctypes
    ctypes.CDLL('$PWD/build/libwlsteaminput.so')
    ctypes.CDLL('/home/maslovkk/.local/share/Steam/linux64/steamclient.so')"

Success is the line `call senders, call table, context builder and desktop
config app found`. Swap in the runtime library to check the other branch:
`.../Steam/steamrt64/steamclient.so`. A pattern change that breaks either line
is a broken change; both files are also the corpus for uniqueness checks
(`objdump -d`, or the python byte-pattern match the commit history used).

What no shell can verify: the focus transitions themselves. Those need a real
compositor and a real game; say so in the PR body instead of claiming a test
that did not happen.

# Rules that outlive any refactor

The list is in CONTRIBUTING.md; the two an agent is most likely to violate:

- Every client address is pattern-found and segment-checked before use. New
  patterns match once in both libraries or carry a predicate with a stated
  reason. No speculative fallbacks, no optional abstractions, no new
  dependencies: one shared object against libc, libdl, Dobby.
- `SPDX-FileCopyrightText` and `SPDX-License-Identifier` headers on new files;
  GPL-3.0-or-later.

# Commits and environment

Conventional commits, one change each; imperative subject, reason in the body.
The user's machine loads the shim from `~/wl-steam-build` via game launch
options; that tree and anything outside this repository is not yours to write
to, refresh, or reformat.
