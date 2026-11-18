// SPDX-FileCopyrightText: 2026 MaslovKK
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <syncstream>

namespace wls
{
    void SetDebug(bool enabled);

    // One line of the trace: Log() << "text " << value. The line is buffered and
    // emitted to stderr as a whole when the temporary dies at the end of the
    // statement. Insertions are skipped while tracing is off, so a disabled Log()
    // formats nothing and outputs nothing.
    class LogLine
    {
    public:
        LogLine();
        LogLine(LogLine&&) = default;
        ~LogLine();

        template <typename Value>
        LogLine& operator<<(const Value& value)
        {
            if (enabled)
                stream << value;
            return *this;
        }

    private:
        std::osyncstream stream;
        bool enabled;
    };

    LogLine Log();
} // namespace wls
