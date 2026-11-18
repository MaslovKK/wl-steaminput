// SPDX-FileCopyrightText: 2026 MaslovKK
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace wls
{
    // Resolves the client's call senders and their context builder inside the
    // client library, once it is in the process. Safe to call repeatedly: the game
    // loads that library when it initialises Steamworks, which is after we are
    // loaded. Returns true when the client can be talked over.
    bool MaybeResolveEngine();

    // Tells the client that this process is (or is no longer) the focused game.
    // Tries to resolve first, because a focus change is often the first moment the
    // client library is there to talk to.
    void ReportFocus(bool focused);
} // namespace wls
