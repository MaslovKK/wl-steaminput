// SPDX-FileCopyrightText: 2026 MaslovKK
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/log.h"
#include "steam/steam.h"
#include "wayland/wayland.h"

#include <algorithm>
#include <cstring>
#include <iostream>

#include <dlfcn.h>
#include <elf.h>
#include <link.h>

#include <dobby.h>
#include <wayland-client.h>

namespace wls
{
    namespace
    {
        using AddListenerFn = int (*)(wl_proxy* proxy, void (**implementation)(void), void* data);
        using DestroyFn = void (*)(wl_proxy* proxy);
        using GetVersionFn = int (*)(wl_proxy* proxy);

        constexpr int kMaxTopLevels = 4;
        // A process can hold the library more than once: the game's own copy and the
        // copy a graphics layer drags in have separate symbol tables, and hooking one
        // does not touch the other. Hook every mapping that appears.
        constexpr int kMaxCopies = 4;

        // xdg-shell.xml, xdg_toplevel, enum state.
        constexpr uint32_t kActivatedState = 4;

        // The listener of the interface, in the order the protocol code puts it:
        // configure and close exist in every version, configure_bounds and
        // wm_capabilities were appended with interface versions 4 and 5, so a
        // listener bound against an older version has no room for the later ones.
        struct ToplevelListener
        {
            void (*configure)(void*, wl_proxy*, int32_t, int32_t, wl_array*);
            void (*close)(void*, wl_proxy*);
            void (*configure_bounds)(void*, wl_proxy*, int32_t, int32_t);
            void (*wm_capabilities)(void*, wl_proxy*, wl_array*);
        };

        struct ToplevelContext
        {
            wl_proxy* proxy = nullptr; // the toplevel being watched, null when free
            const ToplevelListener* listener = nullptr;
            void* listenerData = nullptr;
            int version = 0;
            bool activated = false;
        };

        ToplevelContext g_topLevels[kMaxTopLevels];
        ToplevelListener g_chain;

        struct Copy
        {
            AddListenerFn realAddListener = nullptr;
            DestroyFn realDestroy = nullptr;
            GetVersionFn getVersion = nullptr;
        };

        Copy g_copies[kMaxCopies];
        uintptr_t g_hooked[kMaxCopies];
        int g_copyCount = 0;

        // wl_proxy starts with wl_object, which starts with the interface
        // descriptor. Wayland keeps that layout private, but it has been stable
        // since the protocol was written.
        const wl_interface* ProxyInterface(wl_proxy* proxy)
        {
            return *reinterpret_cast<const wl_interface**>(proxy);
        }

        bool AnyToplevelActivated()
        {
            return std::any_of(std::begin(g_topLevels), std::end(g_topLevels), [](const ToplevelContext& context)
            {
                return context.activated;
            });
        }

        // The process is focused while any window it has open is the activated one.
        // A configure carries the whole state list, so this record cannot drift:
        // a repeated configure repeats the state, and the report follows only the
        // crossings of the state of the set, not the order events arrived in.
        void SetToplevelActivated(ToplevelContext& context, bool activated)
        {
            const bool was = AnyToplevelActivated();
            context.activated = activated;
            if (AnyToplevelActivated() != was)
                ReportFocus(activated);
        }

        // A toplevel going away while activated is a focus loss no configure comes
        // to say: a destroyed proxy receives no events at all.
        void ToplevelDestroyed(wl_proxy* proxy)
        {
            for (ToplevelContext& context : g_topLevels)
            {
                if (context.proxy != proxy)
                    continue;

                const bool was = AnyToplevelActivated();
                context.activated = false;
                context.listener = nullptr; // the slot is reusable; the proxy is gone

                Log() << "toplevel destroyed" << (was ? ", focus lost" : ", focus unchanged");
                if (was && !AnyToplevelActivated())
                    ReportFocus(false);
                return;
            }
        }

        void OnConfigure(void* data, wl_proxy* toplevel, int32_t width, int32_t height, wl_array* states)
        {
            auto* context = static_cast<ToplevelContext*>(data);
            bool activated = false;

            if (states != nullptr)
            {
                const uint8_t* end = static_cast<const uint8_t*>(states->data) + states->size;
                for (const uint32_t* value = static_cast<const uint32_t*>(states->data);
                     reinterpret_cast<const uint8_t*>(value) < end; ++value)
                    activated = activated || *value == kActivatedState;
            }

            const bool was = context->activated;
            SetToplevelActivated(*context, activated);

            if (was != activated)
                Log() << (activated ? "toplevel activated, size " : "toplevel deactivated, size ") << width << "x"
                      << height;

            if (context->listener->configure != nullptr)
                context->listener->configure(context->listenerData, toplevel, width, height, states);
        }

        void OnClose(void* data, wl_proxy* toplevel)
        {
            auto* context = static_cast<ToplevelContext*>(data);
            if (context->listener->close != nullptr)
                context->listener->close(context->listenerData, toplevel);
        }

        void OnConfigureBounds(void* data, wl_proxy* toplevel, int32_t width, int32_t height)
        {
            auto* context = static_cast<ToplevelContext*>(data);
            if (context->version >= 4 && context->listener->configure_bounds != nullptr)
                context->listener->configure_bounds(context->listenerData, toplevel, width, height);
        }

        void OnWmCapabilities(void* data, wl_proxy* toplevel, wl_array* capabilities)
        {
            auto* context = static_cast<ToplevelContext*>(data);
            if (context->version >= 5 && context->listener->wm_capabilities != nullptr)
                context->listener->wm_capabilities(context->listenerData, toplevel, capabilities);
        }

        // One instantiation per copy: the call has to continue inside the copy it
        // came through, and the address that continues it is known per hook.
        template <int Index>
        int HookedAddListenerAt(wl_proxy* proxy, void (**implementation)(void), void* data)
        {
            const wl_interface* interface = ProxyInterface(proxy);
            if (interface != nullptr && interface->name != nullptr && std::strcmp(interface->name, "xdg_toplevel") == 0)
            {
                ToplevelContext* slot = nullptr;
                for (ToplevelContext& context : g_topLevels)
                    if (context.listener == nullptr)
                    {
                        slot = &context;
                        break;
                    }

                if (slot != nullptr)
                {
                    slot->proxy = proxy;
                    slot->listener = reinterpret_cast<const ToplevelListener*>(implementation);
                    slot->listenerData = data;
                    slot->version = g_copies[Index].getVersion(proxy);
                    slot->activated = false;

                    implementation = reinterpret_cast<void (**)(void)>(&g_chain);
                    data = slot;
                    Log() << "chained xdg_toplevel version " << slot->version;
                }
            }

            return g_copies[Index].realAddListener(proxy, implementation, data);
        }

        template <int Index>
        void HookedDestroyAt(wl_proxy* proxy)
        {
            ToplevelDestroyed(proxy);
            g_copies[Index].realDestroy(proxy);
        }

        AddListenerFn kAddListenerHooks[kMaxCopies] = {&HookedAddListenerAt<0>, &HookedAddListenerAt<1>,
                                                       &HookedAddListenerAt<2>, &HookedAddListenerAt<3>};
        DestroyFn kDestroyHooks[kMaxCopies] = {&HookedDestroyAt<0>, &HookedDestroyAt<1>, &HookedDestroyAt<2>,
                                               &HookedDestroyAt<3>};

        // One mapping's export, read off that mapping's own symbol table: dlsym finds
        // only whichever copy the loader would answer with, which need not be this one.
        uintptr_t MappingSymbol(const dl_phdr_info& info, const char* name)
        {
            const auto* dynamic = static_cast<const ElfW(Dyn)*>(nullptr);
            for (int i = 0; i < info.dlpi_phnum; ++i)
                if (info.dlpi_phdr[i].p_type == PT_DYNAMIC)
                    dynamic = reinterpret_cast<const ElfW(Dyn)*>(info.dlpi_addr + info.dlpi_phdr[i].p_vaddr);

            if (dynamic == nullptr)
                return 0;

            const auto* symtab = static_cast<const ElfW(Sym)*>(nullptr);
            const char* strtab = nullptr;
            size_t strsz = 0;
            bool terminated = false;

            ElfW(Addr) span = 0;
            for (int i = 0; i < info.dlpi_phnum; ++i)
                if (info.dlpi_phdr[i].p_type == PT_LOAD)
                {
                    ElfW(Addr) end = info.dlpi_phdr[i].p_vaddr + info.dlpi_phdr[i].p_memsz;
                    if (end > span)
                        span = end;
                }

            for (const ElfW(Dyn)* entry = dynamic; entry->d_tag != DT_NULL; ++entry)
            {
                // The loader relocates the table pointers in the dynamic section in
                // place, so an entry is absolute once it points inside the mapping
                // and relative while it is still a link-time offset.
                auto address = [&info, span](ElfW(Addr) value)
                {
                    if (value >= info.dlpi_addr && value - info.dlpi_addr < span)
                        return value;
                    return info.dlpi_addr + value;
                };

                switch (entry->d_tag)
                {
                case DT_SYMTAB:
                    symtab = reinterpret_cast<const ElfW(Sym)*>(address(entry->d_un.d_ptr));
                    break;
                case DT_STRTAB:
                    strtab = reinterpret_cast<const char*>(address(entry->d_un.d_ptr));
                    break;
                case DT_STRSZ:
                    strsz = entry->d_un.d_val;
                    break;
                // Either version table says this really is a dynamic symbol table.
                case DT_VERNEED:
                case DT_VERSYM:
                    terminated = true;
                    break;
                default:
                    break;
                }
            }

            if (symtab == nullptr || strtab == nullptr || !terminated)
                return 0;

            // Dynamic symbol tables are small and dense; the size bound only stops a
            // walk that runs off the end of a layout that stopped being what it is.
            for (int i = 0; i < 4096; ++i)
            {
                const ElfW(Sym) & symbol = symtab[i];
                if (symbol.st_value == 0 || symbol.st_name == 0 || symbol.st_name >= strsz)
                    continue;

                if (std::strcmp(strtab + symbol.st_name, name) == 0)
                    return info.dlpi_addr + symbol.st_value;
            }

            return 0;
        }

        bool HookMapping(const dl_phdr_info& info)
        {
            uintptr_t addListener = MappingSymbol(info, "wl_proxy_add_listener");
            uintptr_t destroy = MappingSymbol(info, "wl_proxy_destroy");
            uintptr_t getVersion = MappingSymbol(info, "wl_proxy_get_version");
            if (addListener == 0 || destroy == 0 || getVersion == 0 || g_copyCount == kMaxCopies)
                return false;

            for (int seen = 0; seen < g_copyCount; ++seen)
                if (g_hooked[seen] == addListener)
                    return false;

            int index = g_copyCount;
            Copy& copy = g_copies[index];
            copy.getVersion = reinterpret_cast<GetVersionFn>(getVersion);

            void* realAddListener = nullptr;
            if (DobbyHook(reinterpret_cast<void*>(addListener), reinterpret_cast<void*>(kAddListenerHooks[index]),
                          &realAddListener) != 0)
            {
                Log() << "DobbyHook(wl_proxy_add_listener) failed for the copy at " << std::hex << std::showbase
                      << addListener << std::dec << std::noshowbase;
                return false;
            }

            void* realDestroy = nullptr;
            if (DobbyHook(reinterpret_cast<void*>(destroy), reinterpret_cast<void*>(kDestroyHooks[index]),
                          &realDestroy) != 0)
            {
                Log() << "DobbyHook(wl_proxy_destroy) failed for the copy at " << std::hex << std::showbase << destroy
                      << std::dec << std::noshowbase;
                return false;
            }

            copy.realAddListener = reinterpret_cast<AddListenerFn>(realAddListener);
            copy.realDestroy = reinterpret_cast<DestroyFn>(realDestroy);
            g_hooked[index] = addListener;
            ++g_copyCount;

            Log() << "hooked wl_proxy_add_listener and wl_proxy_destroy, copy " << index << " at " << std::hex
                  << std::showbase << addListener << std::dec << std::noshowbase;
            return true;
        }

        bool g_chainBuilt = false;
        bool g_newCopy = false;

        int EachWayland(dl_phdr_info* info, size_t, void*)
        {
            if (info->dlpi_name != nullptr && std::strstr(info->dlpi_name, "libwayland-client") != nullptr &&
                HookMapping(*info))
                g_newCopy = true;

            return 0;
        }
    } // namespace

    // Called after every library load: the game's copy of the library is there before
    // the game adds a listener, whether the game linked it or loaded it itself, and a
    // layer's copy that appears later is caught by the next call. Reports true once a
    // copy is hooked and the scan found nothing new to hook.
    bool TryInstallWaylandHook()
    {
        if (!g_chainBuilt)
        {
            g_chain = {
                &OnConfigure,
                &OnClose,
                &OnConfigureBounds,
                &OnWmCapabilities,
            };
            g_chainBuilt = true;
        }

        g_newCopy = false;
        dl_iterate_phdr(EachWayland, nullptr);

        return g_copyCount > 0 && !g_newCopy;
    }
} // namespace wls
