#pragma once

#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

enum class LogSeverity
{
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
};

bool configureLogFile(const std::filesystem::path& path, std::string* outError = nullptr);
void closeLogFile();

void logMessage(LogSeverity severity, std::string_view subsystem, std::string_view message);
[[noreturn]] void logAndExit(std::string_view subsystem, std::string_view message,
                             int exitCode = 1);

inline void logDebug(std::string_view subsystem, std::string_view message)
{
    logMessage(LogSeverity::Debug, subsystem, message);
}

inline void logInfo(std::string_view subsystem, std::string_view message)
{
    logMessage(LogSeverity::Info, subsystem, message);
}

inline void logWarning(std::string_view subsystem, std::string_view message)
{
    logMessage(LogSeverity::Warning, subsystem, message);
}

inline void logError(std::string_view subsystem, std::string_view message)
{
    logMessage(LogSeverity::Error, subsystem, message);
}

inline void logFatal(std::string_view subsystem, std::string_view message)
{
    logMessage(LogSeverity::Fatal, subsystem, message);
}

template <typename... Parts>
std::string makeLogMessage(Parts&&... parts)
{
    std::ostringstream oss;
    (oss << ... << std::forward<Parts>(parts));
    return oss.str();
}
