// SPDX-FileCopyrightText: 2026 MaslovKK
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/log.h"
#include "steam/client.h"
#include "steam/steam.h"

#include <cstdint>

namespace wls
{
    namespace
    {
        // A focus loss is not reported as "nothing focused": the report names the
        // app whose binding the desktop should have, and that id is the client's
        // own, read from its code by Resolve() - the client's focus receiver
        // settles an unfocus on the desktop app, but reaching it over the pipe
        // activates that id as itself first, which suspends polling (controller
        // dead) until the client's own desktop fallback activates the
        // configuration app seconds later. Reporting the configuration app
        // directly lands the unfocus on the state the X11 path ends in.

        int g_pipe = 0;
        const char* g_reported = nullptr;
    } // namespace

    bool MaybeResolveEngine()
    {
        if (client::Resolve())
            return true;
        if (client::Reason() != g_reported)
        {
            g_reported = client::Reason();
            Log() << "waiting for the client: " << g_reported;
        }
        return false;
    }

    void ReportFocus(bool focused)
    {
        if (!MaybeResolveEngine())
            return;

        // The pipe is looked for once: the client's own answer to the language call
        // is what says it is real and that the connection is its own.
        if (g_pipe == 0 && (g_pipe = client::FindGamePipe()) == 0)
        {
            if (client::Reason() != g_reported)
            {
                g_reported = client::Reason();
                Log() << "cannot report focus: " << g_reported;
            }
            return;
        }

        // One context per call, the way the client does it: the call takes the
        // context it is handed, so reusing one across calls is a call into freed
        // memory.
        uint64_t appId = focused ? client::AppId() : client::DesktopConfigAppId();
        client::SetFocusedWindow(client::MakeCallContext(client::kInterfaceClientUtils, g_pipe), &appId, 1, 0, 0, 0, 0);
        Log() << "SetFocusedWindow(" << static_cast<uint32_t>(appId) << ") over pipe " << g_pipe;
    }
} // namespace wls
