#include "Core/Logger.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace
{

std::mutex& loggerMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::ofstream& logFile()
{
    static std::ofstream stream;
    return stream;
}

const char* severityLabel(LogSeverity severity)
{
    switch (severity)
    {
        case LogSeverity::Debug:
            return "DEBUG";
        case LogSeverity::Info:
            return "INFO";
        case LogSeverity::Warning:
            return "WARN";
        case LogSeverity::Error:
            return "ERROR";
        case LogSeverity::Fatal:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

std::string makeTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string formatLogPrefix(LogSeverity severity, std::string_view subsystem)
{
    std::ostringstream oss;
    oss << "[" << makeTimestamp() << "]"
        << "[" << severityLabel(severity) << "]"
        << "[" << (subsystem.empty() ? "General" : subsystem) << "] ";
    return oss.str();
}

std::ostream& consoleStreamFor(LogSeverity severity)
{
    switch (severity)
    {
        case LogSeverity::Warning:
        case LogSeverity::Error:
        case LogSeverity::Fatal:
            return std::cerr;
        case LogSeverity::Debug:
        case LogSeverity::Info:
        default:
            return std::cout;
    }
}

} // namespace

bool configureLogFile(const std::filesystem::path& path, std::string* outError)
{
    std::lock_guard<std::mutex> lock(loggerMutex());

    if (outError != nullptr)
    {
        outError->clear();
    }

    logFile().close();
    logFile().clear();

    if (path.empty())
    {
        if (outError != nullptr)
        {
            *outError = "Logger path is empty";
        }
        return false;
    }

    std::error_code ec;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            if (outError != nullptr)
            {
                *outError = ec.message();
            }
            return false;
        }
    }

    logFile().open(path, std::ios::out | std::ios::app);
    if (!logFile().is_open())
    {
        if (outError != nullptr)
        {
            *outError = "Failed to open log file";
        }
        return false;
    }

    return true;
}

void closeLogFile()
{
    std::lock_guard<std::mutex> lock(loggerMutex());
    if (logFile().is_open())
    {
        logFile().flush();
        logFile().close();
    }
}

void logMessage(LogSeverity severity, std::string_view subsystem, std::string_view message)
{
    const std::string prefix = formatLogPrefix(severity, subsystem);
    const std::string messageText(message);

    std::lock_guard<std::mutex> lock(loggerMutex());
    std::ostream& console = consoleStreamFor(severity);
    size_t lineStart = 0;
    while (lineStart <= messageText.size())
    {
        const size_t lineEnd = messageText.find('\n', lineStart);
        const size_t lineLength =
            lineEnd == std::string::npos ? messageText.size() - lineStart : lineEnd - lineStart;

        std::string_view lineView(messageText.data() + lineStart, lineLength);
        if (!lineView.empty() && lineView.back() == '\r')
        {
            lineView.remove_suffix(1);
        }

        console << prefix << lineView << "\n";
        if (logFile().is_open())
        {
            logFile() << prefix << lineView << "\n";
        }

        if (lineEnd == std::string::npos)
        {
            break;
        }

        lineStart = lineEnd + 1;
        if (lineStart == messageText.size())
        {
            break;
        }
    }

    console.flush();
    if (logFile().is_open())
    {
        logFile().flush();
    }
}

[[noreturn]] void logAndExit(std::string_view subsystem, std::string_view message, int exitCode)
{
    logMessage(LogSeverity::Fatal, subsystem, message);
    closeLogFile();
    std::exit(exitCode);
}
