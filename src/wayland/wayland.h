// SPDX-FileCopyrightText: 2026 MaslovKK
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace wls
{
    // Chains the game's xdg_toplevel listeners so the window states in the
    // compositor's own configure events drive the focus report. Returns true once
    // the hook is in. Called repeatedly: games dlopen wayland (SDL3, Wine's Wayland
    // driver) long after we are loaded, so the first calls usually have nothing to
    // hook yet.
    bool TryInstallWaylandHook();
} // namespace wls
