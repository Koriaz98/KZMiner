#include "console_output.h"
#include "console_lock.h"
#include <iostream>
#include <deque>

namespace
{
    constexpr size_t kMaxLines = 30;

    std::deque<std::string>& logBuffer()
    {
        static std::deque<std::string> buf;
        return buf;
    }
}

bool isInteractiveTerminal()
{
    static bool result = (isatty(STDOUT_FILENO) != 0);
    return result;
}

void pushLogLine(const std::string& line)
{
    std::lock_guard<std::mutex> lock(consoleMutex());

    if(!isInteractiveTerminal())
    {
        std::cout << line << "\n" << std::flush;
        return;
    }

    logBuffer().push_back(line);
    while(logBuffer().size() > kMaxLines)
    {
        logBuffer().pop_front();
    }
}

std::vector<std::string> recentLogLines()
{
    std::lock_guard<std::mutex> lock(consoleMutex());
    return std::vector<std::string>(logBuffer().begin(), logBuffer().end());
}
