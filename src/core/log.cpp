// SPDX-FileCopyrightText: 2026 MaslovKK
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/log.h"

#include <iostream>

namespace wls
{
    namespace
    {
        bool g_debug = false;
    }

    void SetDebug(bool enabled)
    {
        g_debug = enabled;
    }

    LogLine::LogLine() : stream(std::cerr), enabled(g_debug)
    {
        if (enabled)
            stream << "wl-steaminput: ";
    }

    LogLine::~LogLine()
    {
        if (enabled)
            stream << '\n';
    }

    LogLine Log()
    {
        return LogLine();
    }
} // namespace wls
