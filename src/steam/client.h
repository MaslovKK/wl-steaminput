// SPDX-FileCopyrightText: 2026 MaslovKK
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace wls
{
    // The half of steamclient.so the shim talks to, under the names the client uses
    // for it. The pointers are null until Resolve() succeeds; the signatures were
    // read off the 2026-11 client, in both the linux64 branch native games map and
    // the Steam Runtime branch Proton games map.

    struct CallContext;  // opaque: made by the client's builder, owned by one call
    struct CSteamClient; // opaque: handed out by the connection accessor

    namespace client
    {
        // IClientUtils' place in the client's interface map.
        constexpr int kInterfaceClientUtils = 0;

        extern CallContext* (*MakeCallContext)(int interfaceIndex, int pipe);
        extern CSteamClient* (*Connection)();
        extern const char* (*GetSteamUILanguage)(CallContext* context);
        extern void (*SetFocusedWindow)(CallContext* context, const uint64_t* appId, uint8_t focused, uint8_t kind,
                                        int32_t window, int16_t platform, int16_t revision);

        // Finds the two call senders, their table and the context builder inside
        // steamclient.so by byte patterns, so an update that moves code changes
        // nothing. Safe to call repeatedly: the game loads the library when it
        // initialises Steamworks, which is after we are loaded; once resolved, or
        // once the library proves not to hold what the patterns look for, the
        // answer stops changing. The reason is reported for the caller to log.
        bool Resolve();
        const char* Reason();

        // The app the environment says this process is playing, zero if the
        // environment says it is not playing anything.
        uint32_t AppId();

        // The app id the client binds the controller to when no game is focused,
        // read out of the client's own code; valid once Resolve() says true.
        uint32_t DesktopConfigAppId();

        // The game's own pipe, as the client's answer to a reading call confirms it
        // can be sent over; zero when no pipe answers, which is retried later.
        int FindGamePipe();
    } // namespace client
} // namespace wls
