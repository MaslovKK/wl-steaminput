// SPDX-FileCopyrightText: 2026 MaslovKK
// SPDX-License-Identifier: GPL-3.0-or-later

#include "steam/client.h"

#include "core/log.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <ios>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>

#include <dlfcn.h>
#include <link.h>

namespace wls
{
    namespace
    {
        // The library is found by byte patterns, not fixed addresses: the client
        // moves everything a few kilobytes on an ordinary update, and a pattern
        // that no longer matches is a log line, where a stale offset is a call
        // into whatever is there now. The numbers that survive are positions in
        // an interface's call table (part of the interface contract) and the
        // layout of the runtime objects, which only the running client could
        // answer anyway.

        // A byte pattern: two hex digits per byte, "??" matches any byte, and a
        // "$" marks a four-byte group whose bytes the matcher reports - as the
        // address it reaches when the group is a call or rip-relative
        // displacement, and always as the raw value.
        constexpr int kMaxGroups = 8;

        constexpr int kPipeStateField = 0xa0;
        constexpr uint32_t kMaxPipes = 16;
        // Where IClientUtils keeps its two calls in its table; the table is found
        // by the pair of senders themselves, this only names where the table
        // starts once the pair has been seen.
        constexpr int kSlotSetFocusedWindow = 48;

        // Both senders are found by short byte signatures; each is unique in the
        // library. The language sender's carries the hash of its first parameter
        // block - a number computed from the method's name and parameters, so it
        // changes when the interface changes, not when the library is rebuilt, and
        // it is the only thing that tells this method's marshalling apart from the
        // other interfaces that marshal a call just as shaped. Every call target
        // and data reference is wild: the code around them moves freely between
        // builds.
        constexpr char kSetFocusedWindowPattern[] = "41 57 41 56 41 89 d6 ba 20 00 00 00 41 55 41";
        constexpr char kGetUiLanguagePattern[] =
            "41 54 be 40 00 00 00 31 c9 ba 20 00 00 00 55 53 48 89 fb 48 83 ec 60 48 8d 6c 24 ?? 48 89 ef "
            "e8 ?? ?? ?? ?? be 01 00 00 00 48 89 ef e8 ?? ?? ?? ?? be 04 00 00 00 48 89 ef e8 ?? ?? ?? ?? "
            "8b 43 ?? 48 8d 74 24 ?? ba 04 00 00 00 48 89 ef 89 44 24 ?? e8 ?? ?? ?? ?? "
            "48 8d 74 24 ?? ba 04 00 00 00 48 89 ef c7 44 24 ?? 34 96 b5 3c e8 ?? ?? ?? ??";
        // One builder shape is shared by every interface - make a context, store
        // the interface index, the pipe, the validity flag and the caller's pipe
        // list - so a match is only ours when the table it stores into the context
        // is the one found above. The first displacement is the connection
        // accessor, which the builder calls twice; that is the last thing to find.
        constexpr char kBuilderPattern[] =
            "41 55 41 89 fd 41 54 55 53 89 f3 48 83 ec 08 "
            "e8 $ $ $ $ 89 de 48 8d 78 08 e8 $ $ $ $ bf 20 00 00 00 89 c5 e8 $ $ $ $ 49 89 c4 "
            "48 8d 05 $ $ $ $ 45 89 6c 24 08 49 89 04 24 41 89 5c 24 0c 41 88 6c 24 10 "
            "e8 $ $ $ $ 48 83 c0 08 49 89 44 24 18 48 8d 05 $ $ $ $ 49 89 04 24 48 83 c4 08 4c 89 e0 "
            "5b 5d 41 5c 41 5d c3";
        // The builder is a shape shared by every interface, so it is taken only
        // when the last displacement group - the table it stores into the context -
        // is the one the senders were found in. Its first group is the call of the
        // connection accessor, which the builder calls twice.
        constexpr int kBuilderGroupConnection = 0;
        constexpr int kBuilderGroupTable = 5;

        // The client library ships in two branches with different codegen: the
        // modern linux64 one, which native games map, and the older runtime one
        // (steamrt64), which Proton games map through the Steam Linux Runtime.
        // The interface contract - the call table, its slot numbers, the context
        // layout the senders read, even the source line of the pipe validity
        // assert - is identical between them; only the compiled shapes differ, so
        // each scan tries the modern shape and then these, which were taken from
        // the runtime branch the same way. The runtime builder is the same
        // template the modern one is - the group order carries over - and its
        // table predicate is the same because the table is the same.
        constexpr char kSetFocusedWindowPatternRuntime[] =
            "41 57 41 56 41 89 d6 ba 20 00 00 00 41 55 41 89 cd 31 c9 41 54 49 89 f4 be 40 00 00 00";
        constexpr char kGetUiLanguagePatternRuntime[] =
            "41 55 be 40 00 00 00 31 c9 ba 20 00 00 00 41 54 55 53 48 83 ec 78 64 48 8b 2c 25 28 00 00 00 "
            "48 89 6c 24 ?? 48 89 fd 48 8d 5c 24 ?? 48 89 df e8 ?? ?? ?? ?? be 01 00 00 00 48 89 df "
            "e8 ?? ?? ?? ?? be 04 00 00 00 48 89 df e8 ?? ?? ?? ?? 8b 45 08 48 8d 74 24 ?? ba 04 00 00 00 "
            "48 89 df 89 44 24 ?? e8 ?? ?? ?? ?? 48 8d 74 24 ?? ba 04 00 00 00 48 89 df "
            "c7 44 24 ?? 34 96 b5 3c e8 ?? ?? ?? ??";
        constexpr char kBuilderPatternRuntime[] =
            "55 66 0f 6e ce 66 0f 6e c7 53 66 0f 62 c1 89 f3 48 83 ec 18 66 0f d6 44 24 ?? "
            "e8 $ $ $ $ 89 de 48 8d 78 08 e8 $ $ $ $ bf 20 00 00 00 89 c5 e8 $ $ $ $ "
            "f3 0f 7e 44 24 ?? 48 89 c3 48 8d 05 $ $ $ $ 66 0f d6 43 08 48 89 03 40 88 6b 10 "
            "e8 $ $ $ $ 48 83 c0 08 48 89 43 18 48 8d 05 $ $ $ $ 48 89 03 48 83 c4 18 48 89 d8 5b 5d c3";

        // The client's own comparisons for "this app id is one of the internal
        // ones": a chain of immediate compares whose first link is the desktop's
        // configuration app id, followed by the client beta and branch ids. The
        // first link is captured, not matched, so the number stays the client's
        // own, and a client that moved it is read, not contradicted. Chosen over
        // the configuration-app bit-test for sitting byte-identical in both the
        // steamrt64 and linux64 client branches; the bit-test exists only in the
        // former.
        constexpr char kConfigAppPattern[] = "41 b8 01 00 00 00 3d $ $ $ $ 74 ?? 3d ac fa 08 00";

        bool g_resolved = false;
        bool g_giveUp = false;
        const char* g_reason = "not attempted";
        uint32_t g_desktopConfigAppId = 0;

        // The library's mappings, copied out of the dl_iterate_phdr callback,
        // whose argument is only good during the callback.
        struct Module
        {
            static constexpr int kMaxSegments = 16;

            uintptr_t base = 0;
            int phnum = 0;
            ElfW(Phdr) phdrs[kMaxSegments];
        };

        // The library's loaded pieces, executable or not, whichever the caller means.
        auto Segments(const Module& module, bool executable)
        {
            return module.phdrs | std::views::take(module.phnum) |
                   std::views::filter([=](const ElfW(Phdr) & head)
            {
                return head.p_type == PT_LOAD && (!executable || (head.p_flags & PF_X) != 0);
            });
        }

        struct PatternMatch
        {
            uintptr_t at;
            uintptr_t targets[kMaxGroups];
            uint32_t values[kMaxGroups];
            int groups;
        };

        std::optional<PatternMatch> PatternAt(std::string_view pattern, std::span<const uint8_t> code, uintptr_t addr)
        {
            auto nibble = [](char c)
            {
                return c >= 'a' ? c - 'a' + 10 : c - '0';
            };
            struct Capture
            {
                uintptr_t at;
                uint32_t value;
                int bytes;
            };

            PatternMatch match{.at = addr, .targets = {}, .values = {}, .groups = 0};
            std::array<Capture, kMaxGroups> captures{};
            Capture* current = nullptr;
            std::string_view rest = pattern;
            for (size_t pos = 0; !rest.empty(); ++pos)
            {
                const size_t space = rest.find(' ');
                const std::string_view token = space != std::string_view::npos ? rest.substr(0, space) : rest;
                rest = space != std::string_view::npos ? rest.substr(space + 1) : std::string_view{};

                if (pos == code.size())
                    return {};

                if (token == "$")
                {
                    if (current == nullptr)
                    {
                        if (match.groups == kMaxGroups)
                            return {};
                        current = &captures[match.groups++];
                        *current = {.at = addr + pos, .value = 0, .bytes = 0};
                    }

                    if (current->bytes == 4) // a run longer than one displacement
                        return {};

                    current->value |= static_cast<uint32_t>(code[pos]) << (8 * current->bytes);
                    ++current->bytes;
                }
                else
                {
                    current = nullptr;
                    if (token != "??" && (nibble(token[0]) << 4 | nibble(token[1])) != code[pos])
                        return {};
                }
            }

            if (current != nullptr && current->bytes != 4) // the pattern ended inside a displacement
                return {};

            for (int g = 0; g < match.groups; ++g)
            {
                match.values[g] = captures[g].value;
                match.targets[g] = static_cast<uintptr_t>(static_cast<int64_t>(captures[g].at + 4) +
                                                          static_cast<int32_t>(captures[g].value));
            }
            return match;
        }

        // Scans the library's executable mappings for the pattern. Without a
        // predicate the pattern has to match exactly once; with one, exactly one
        // match may take it - a shape shared by every interface is only ours once.
        std::optional<PatternMatch> ScanCode(const Module& module, std::string_view pattern,
                                             const std::function<bool(const PatternMatch&)>& accept = {})
        {
            std::optional<PatternMatch> only;
            int taken = 0;
            for (const ElfW(Phdr) & head : Segments(module, true))
            {
                const auto code = std::span(reinterpret_cast<const uint8_t*>(module.base + head.p_vaddr), head.p_memsz);
                for (size_t i = 0; i < code.size(); ++i)
                {
                    const auto match = PatternAt(pattern, code.subspan(i), module.base + head.p_vaddr + i);
                    if (!match || (accept && !accept(*match)))
                        continue;

                    if (++taken > 1)
                        return {};

                    only = match;
                }
            }
            return only;
        }

        // The client library's two branches compile the same functions
        // differently; try each shape in turn and take the first that matches
        // uniquely.
        std::optional<PatternMatch> ScanAnyOf(const Module& module, std::initializer_list<std::string_view> patterns,
                                              const std::function<bool(const PatternMatch&)>& accept = {})
        {
            for (const std::string_view pattern : patterns)
                if (const auto found = ScanCode(module, pattern, accept))
                    return found;

            return {};
        }

        // The interface's table is wherever the two senders sit next to each other
        // in the library's read-only data, which is what the load-time relocations
        // of a call table make them.
        std::optional<uintptr_t> FindCallSlot(const Module& module, uintptr_t sender, uintptr_t senderNext)
        {
            const uintptr_t pair[2] = {sender, senderNext};
            std::optional<uintptr_t> slot;
            int matches = 0;

            for (const ElfW(Phdr) & head : Segments(module, false))
            {
                const auto data = std::span(reinterpret_cast<const uint8_t*>(module.base + head.p_vaddr), head.p_memsz);
                for (size_t offset = 0; offset + sizeof(pair) <= data.size(); offset += 8)
                {
                    if (std::memcmp(&data[offset], pair, sizeof(pair)) != 0)
                        continue;

                    ++matches;
                    slot = module.base + head.p_vaddr + offset;
                }
            }

            return matches == 1 ? slot : std::nullopt;
        }

        struct ModuleProbe
        {
            std::string_view name;
            std::optional<Module> module;
        };

        int MatchModule(dl_phdr_info* info, size_t, void* user)
        {
            auto* probe = static_cast<ModuleProbe*>(user);
            if (info->dlpi_name == nullptr)
                return 0;

            // The exact file name, not a substring of the path: Proton games map
            // a wine wrapper called lsteamclient.so, which ends with the client's
            // name and would be scanned in the client's place.
            const std::string_view path(info->dlpi_name);
            const size_t slash = path.rfind('/');
            if ((slash == std::string_view::npos ? path : path.substr(slash + 1)) != probe->name)
                return 0;

            if (info->dlpi_phnum > Module::kMaxSegments)
                return 0;

            Module& module = probe->module.emplace();
            module.base = info->dlpi_addr;
            module.phnum = info->dlpi_phnum;
            std::memcpy(module.phdrs, info->dlpi_phdr, sizeof(ElfW(Phdr)) * info->dlpi_phnum);
            return 1;
        }

        std::optional<Module> FindModule(std::string_view name)
        {
            ModuleProbe probe{name, {}};
            dl_iterate_phdr(MatchModule, &probe);
            return std::move(probe.module);
        }

        // True if the offset falls in one of the module's mappings, so that reading
        // or calling it after a client update is a log line instead of a fault.
        bool InSegment(const Module& module, uintptr_t offset, bool executable)
        {
            return std::ranges::any_of(Segments(module, executable), [&](const ElfW(Phdr) & head)
            {
                return head.p_offset <= offset && offset - head.p_offset < head.p_filesz;
            });
        }
    } // namespace

    // Names below are the client's own, from its RTTI and the debug strings of
    // common/ipcclient.cpp.

    struct CClientPipe; // the pipe object; the flag the validity check reads
    // sits inside it, at kPipeStateField

    // The client's own pipe validity check (ipcclient.cpp:478, "IsValidHSteamPipe")
    // finds a node by index, confirms the links carry that index, and reads the
    // flag from the pipe object.
    template <typename T, typename I = int>
    struct CUtlLinkedList
    {
        struct ListElem_t
        {
            T m_Element;   // +0
            I m_pPrevious; // +8
            I m_nNext;     // +12
        };

        void** __vftable;        // +0
        ListElem_t* m_pElements; // +8   the nodes, indexed by element number
        uint8_t unused_16[24];   // +16  the list's own bookkeeping
        I m_nCount;              // +40  total element count
    };

    // The one instantiation the client carries: its RTTI reads exactly as
    // CUtlLinkedList<CClientPipe*, int>, with the node type nested inside.
    using ClientPipes_t = CUtlLinkedList<CClientPipe*, int>;

    static_assert(sizeof(ClientPipes_t::ListElem_t) == 16);
    static_assert(offsetof(ClientPipes_t, m_pElements) == 8 && offsetof(ClientPipes_t, m_nCount) == 40);

    // The process' connection to the client: the CSteamClient singleton the
    // accessor hands out, whose pipe list is at +8.
    struct CSteamClient
    {
        void** __vftable;      // +0
        ClientPipes_t m_Pipes; // +8
    };

    static_assert(offsetof(CSteamClient, m_Pipes) == 8);

    // The context the client's builder makes for one call: the sender dispatches
    // from the table, sends to the context's pipe, and a context without a pipe
    // and a pipe list would send through a null pointer. The class carries no
    // RTTI of its own (+0 is the call table, not a vptr); the field names are the
    // client's, from the call-sending block of ipcclient.cpp.
    struct CallContext
    {
        void** m_ppCallTable;    // +0   the interface's sender table
        int m_iInterface;        // +8
        uint32_t m_hSteamPipe;   // +12
        uint8_t m_ePipeState;    // +16  the flag the validity check read for the pipe
        uint8_t unused_17[7];    // +17  not set by the builder
        ClientPipes_t* m_pPipes; // +24  the connection's pipe list
    };

    static_assert(sizeof(CallContext) == 32);

    namespace client
    {
        CallContext* (*MakeCallContext)(int interfaceIndex, int pipe) = nullptr;
        CSteamClient* (*Connection)() = nullptr;
        const char* (*GetSteamUILanguage)(CallContext* context) = nullptr;
        void (*SetFocusedWindow)(CallContext* context, const uint64_t* appId, uint8_t focused, uint8_t kind,
                                 int32_t window, int16_t platform, int16_t revision) = nullptr;

        uint32_t AppId()
        {
            // Read once, on first need: the value is set by the launcher before the
            // game's code runs, and an empty one never comes back.
            static const uint32_t appId = []
            {
                const char* value = std::getenv("SteamAppId");
                if (value == nullptr)
                    value = std::getenv("SteamGameId");
                return value != nullptr ? static_cast<uint32_t>(std::strtoul(value, nullptr, 10)) : 0U;
            }();
            return appId;
        }

        uint32_t DesktopConfigAppId()
        {
            return g_desktopConfigAppId;
        }

        // How every attempt that does not succeed ends: the reason is what the log
        // line will say, and unless the attempt can never succeed it is repeated at
        // the next focus change.
        void Decline(const char* reason, bool forever = false)
        {
            g_reason = reason;
            g_giveUp = g_giveUp || forever;
        }

        bool Resolve()
        {
            if (g_resolved || g_giveUp)
                return g_resolved;

            // A process without an app id is not a game the client tracks, and
            // nothing later can change that.
            if (AppId() == 0)
            {
                Decline("no SteamAppId in the environment", true);
                return false;
            }

            const auto mapped = FindModule("steamclient.so");
            if (!mapped)
            {
                Decline("steamclient.so is not mapped");
                return false;
            }
            const Module& module = *mapped;

            const auto sender = ScanAnyOf(module, {kSetFocusedWindowPattern, kSetFocusedWindowPatternRuntime});
            const auto senderNext = ScanAnyOf(module, {kGetUiLanguagePattern, kGetUiLanguagePatternRuntime});
            if (!sender || !senderNext)
            {
                Decline("the IClientUtils call senders are not in steamclient.so", true);
                return false;
            }

            const auto slot = FindCallSlot(module, sender->at, senderNext->at);
            if (!slot)
            {
                Decline("no call table holds the IClientUtils call senders", true);
                return false;
            }

            const uintptr_t table = *slot - sizeof(uintptr_t) * kSlotSetFocusedWindow;
            uintptr_t connection = 0;
            const auto builder =
                ScanAnyOf(module, {kBuilderPattern, kBuilderPatternRuntime}, [&](const PatternMatch& match)
            {
                if (match.groups <= kBuilderGroupTable || match.targets[kBuilderGroupTable] != table)
                    return false;
                connection = match.targets[kBuilderGroupConnection];
                return true;
            });
            if (!builder)
            {
                Decline("no call context builder builds for the IClientUtils table", true);
                return false;
            }
            if (!InSegment(module, connection - module.base, true))
            {
                Decline("the connection accessor is not code in steamclient.so", true);
                return false;
            }

            const auto configApp = ScanCode(module, kConfigAppPattern);
            if (!configApp)
            {
                Decline("the client's configuration app predicate is not in steamclient.so", true);
                return false;
            }
            g_desktopConfigAppId = configApp->values[0];

            MakeCallContext = reinterpret_cast<decltype(MakeCallContext)>(builder->at);
            Connection = reinterpret_cast<decltype(Connection)>(connection);
            SetFocusedWindow = reinterpret_cast<decltype(SetFocusedWindow)>(sender->at);
            GetSteamUILanguage = reinterpret_cast<decltype(GetSteamUILanguage)>(senderNext->at);
            Log() << "steamclient base " << std::hex << std::showbase << module.base << std::dec << std::noshowbase
                  << ", call senders, call table, context builder and desktop config app found";
            return g_resolved = true;
        }

        const char* Reason()
        {
            return g_reason;
        }

        // The pipe list says how many pipes this process has, and the client's own
        // answer to the language call says which of them it will take a call from -
        // the flag the validity check reads is not that, it is zero for the game's
        // own pipe. The game's pipe is not necessarily the first one, since the
        // overlay renderer opens one too.
        int FindGamePipe()
        {
            CSteamClient* steamClient = Connection();
            if (steamClient == nullptr)
            {
                Decline("the client has no connection object yet");
                return 0;
            }

            ClientPipes_t& pipes = steamClient->m_Pipes;
            Log() << "connection " << static_cast<void*>(steamClient) << ", " << pipes.m_nCount << " pipes";
            if (pipes.m_pElements == nullptr || pipes.m_nCount <= 1 ||
                static_cast<uint32_t>(pipes.m_nCount) > kMaxPipes)
            {
                Decline("this process has no pipe to the client yet");
                return 0;
            }

            // Node 0 is the list's end sentinel; the pipes start at 1.
            for (ClientPipes_t::ListElem_t& record : std::span(pipes.m_pElements + 1, pipes.m_nCount - 1))
            {
                int pipe = static_cast<int>(&record - pipes.m_pElements);
                CallContext* context = MakeCallContext(kInterfaceClientUtils, pipe);
                if (context == nullptr || context->m_hSteamPipe == 0 || context->m_pPipes == nullptr)
                    continue;

                const char* language = GetSteamUILanguage(context);
                const unsigned state =
                    record.m_Element == nullptr ? 0 : std::bit_cast<const uint8_t*>(record.m_Element)[kPipeStateField];

                Log() << "pipe " << pipe << ", links " << record.m_pPrevious << '/' << record.m_nNext << ", state "
                      << state << ", language " << (language != nullptr ? language : "?");

                if (language != nullptr)
                {
                    Log() << "reporting over pipe " << pipe << ", app id " << AppId();
                    return pipe;
                }
            }

            // Retried at the next focus change: a pipe that is there but not answering
            // yet is worth another look, and the cost is one call.
            Decline("no pipe of this process answers the client");
            return 0;
        }
    } // namespace client
} // namespace wls
