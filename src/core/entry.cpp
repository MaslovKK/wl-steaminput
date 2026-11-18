// SPDX-FileCopyrightText: 2026 MaslovKK
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/log.h"
#include "steam/steam.h"
#include "wayland/wayland.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

#include <dlfcn.h>

#include <dobby.h>

namespace
{
    using DlopenFn = void* (*)(const char* filename, int flags);

    DlopenFn g_realDlopen = nullptr;
    thread_local bool g_inside = false;
    std::atomic_flag g_installing = ATOMIC_FLAG_INIT;
    bool g_waylandHooked = false;
    bool g_engineResolved = false;

    // Neither half is present when we are loaded: SDL3 or Wine's Wayland driver
    // dlopens wayland when the first window appears, and Steamworks dlopens the
    // client library when the game initialises. Retrying after every dlopen is what
    // catches both, and it stops costing anything once both are in.
    void TryInstall(const char* filename)
    {
        if (g_inside || (g_waylandHooked && g_engineResolved) || g_installing.test_and_set())
            return;

        g_inside = true;
        bool waylandWas = g_waylandHooked;
        bool engineWas = g_engineResolved;
        g_waylandHooked = wls::TryInstallWaylandHook() || waylandWas;
        g_engineResolved = wls::MaybeResolveEngine() || engineWas;
        g_inside = false;
        g_installing.clear();

        if (g_waylandHooked != waylandWas || g_engineResolved != engineWas)
            wls::Log() << "after dlopen(" << (filename != nullptr ? filename : "?") << "): wayland "
                       << (g_waylandHooked ? "hooked" : "pending") << ", client "
                       << (g_engineResolved ? "resolved" : "pending");
    }

    void* HookedDlopen(const char* filename, int flags)
    {
        if (g_inside)
            return g_realDlopen(filename, flags);

        void* handle = g_realDlopen(filename, flags);
        TryInstall(filename);

        return handle;
    }

    bool FlagSet(const char* name)
    {
        const char* value = std::getenv(name);
        return value != nullptr && std::strcmp(value, "0") != 0;
    }
} // namespace

// The conventional shared-object initialization name; the attribute is what
// actually files it for the loader (.init_array), since modern linkers no longer
// wire a shared library's _init into DT_INIT themselves.
__attribute__((constructor)) void _init()
{
    wls::SetDebug(FlagSet("WLSTEAMINPUT_DEBUG"));
    if (FlagSet("WLSTEAMINPUT_DISABLE"))
        return;

    void* target = dlsym(RTLD_NEXT, "dlopen");
    if (target == nullptr ||
        DobbyHook(target, reinterpret_cast<void*>(&HookedDlopen), reinterpret_cast<void**>(&g_realDlopen)) != 0)
    {
        wls::Log() << "cannot hook dlopen, only libraries the game links against will be seen";
        return;
    }

    wls::Log() << "hooked dlopen";
    TryInstall(nullptr);
}
