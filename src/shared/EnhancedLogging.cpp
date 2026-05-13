#include "shared/EnhancedLogging.h"
#include <filesystem>
#include <windows.h>
#include <cstdarg>
#include <iostream>
#include <vector>
#include "shared/State.h"

std::atomic<LogLevel> g_logLevel(LogLevel::Info);

void InitEnhancedLogging() {
    std::wstring logPath = GetAppRootPath() + L"logs\\debug.log";
    EnhancedLogger::Instance().Initialize(logPath);
}

void ShutdownEnhancedLogging() {
    EnhancedLogger::Instance().Flush();
}

void SetLogLevel(LogLevel level) {
    g_logLevel = level;
}

void LogStartup() {
    LOG_INFO("--- BetterAngle Pro Startup ---");
    LOG_INFO("Build Version: %hs", VERSION_STR);
    
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    LOG_INFO("Processor Architecture: %u", si.wProcessorArchitecture);
    LOG_INFO("Number of Processors: %u", si.dwNumberOfProcessors);
}

void LogWindowInfo(const wchar_t* label, HWND hwnd) {
    if (!hwnd) {
        LOG_INFO(L"%ls: (NULL)", label);
        return;
    }
    char className[256];
    GetClassNameA(hwnd, className, sizeof(className));
    char title[256];
    GetWindowTextA(hwnd, title, sizeof(title));
    LOG_INFO(L"%ls: HWND=%p, Class=%hs, Title=%hs", label, hwnd, className, title);
}

// ANSI LogMessage
void LogMessage(LogLevel level, const char* file, int line, const char* format, ...) {
    if (level < g_logLevel) return;

    va_list args;
    va_start(args, format);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    EnhancedLogger::Instance().Log(level, file, line, std::string(buffer));
}

// Wide LogMessage
void LogMessage(LogLevel level, const char* file, int line, const wchar_t* format, ...) {
    if (level < g_logLevel) return;

    va_list args;
    va_start(args, format);
    wchar_t buffer[4096];
    vswprintf(buffer, 4096, format, args);
    va_end(args);

    EnhancedLogger::Instance().Log(level, file, line, std::wstring(buffer));
}

EnhancedLogger& EnhancedLogger::Instance() {
    static EnhancedLogger instance;
    return instance;
}

void EnhancedLogger::Initialize(const std::wstring& logPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return;

    m_logPath = logPath;
    std::filesystem::path path(logPath);
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    m_stream.open(logPath, std::ios::out | std::ios::app);
    m_initialized = m_stream.is_open();
}

void EnhancedLogger::Log(LogLevel level, const char* file, int line, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !m_stream.is_open()) return;

    std::string filename = file;
    size_t lastSlash = filename.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }

    m_stream << "[" << TimestampNow() << "] [" << LevelToString(level) << "] ["
             << filename << ":" << line << "] " << message << "\n";
    m_stream.flush(); // v5.5.99: write immediately — buffering hid every log
                      // line until the app shut down.

    // Check rotation occasionally
    CheckRotation();
}

void EnhancedLogger::Log(LogLevel level, const char* file, int line, const std::wstring& message) {
    Log(level, file, line, ToNarrow(message));
}

void EnhancedLogger::Flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stream.is_open()) {
        m_stream.flush();
    }
}

EnhancedLogger::~EnhancedLogger() {
    Flush();
}

void EnhancedLogger::CheckRotation() {
    if (!m_initialized || !m_stream.is_open() || m_logPath.empty()) return;

    // Fast check for file size
    if (m_stream.tellp() > static_cast<std::streamoff>(m_maxFileSize)) {
        RotateLogs();
    }
}

void EnhancedLogger::RotateLogs() {
    m_stream.close();

    std::filesystem::path base(m_logPath);
    
    // Shift old logs: log.4 -> log.5, log.3 -> log.4 ...
    for (int i = m_maxRotation - 1; i >= 1; --i) {
        std::filesystem::path oldF = base;
        oldF += "." + std::to_string(i);
        std::filesystem::path newF = base;
        newF += "." + std::to_string(i + 1);
        
        std::error_code ec;
        if (std::filesystem::exists(oldF, ec)) {
            std::filesystem::rename(oldF, newF, ec);
        }
    }

    // Rename current to .1
    std::filesystem::path backup = base;
    backup += ".1";
    std::error_code ec;
    std::filesystem::rename(base, backup, ec);

    // Reopen fresh
    m_stream.open(m_logPath, std::ios::out | std::ios::trunc);
}

std::string EnhancedLogger::LevelToString(LogLevel level) const {
    switch (level) {
    case LogLevel::Trace:   return "TRACE";
    case LogLevel::Debug:   return "DEBUG";
    case LogLevel::Info:    return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error:   return "ERROR";
    case LogLevel::Fatal:   return "FATAL";
    default:                return "UNKNOWN";
    }
}

std::string EnhancedLogger::TimestampNow() const {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto time = system_clock::to_time_t(now);
    std::tm tmValue{};
    localtime_s(&tmValue, &time);
    std::stringstream ss;
    ss << std::put_time(&tmValue, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string EnhancedLogger::ToNarrow(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Performance Logger Implementation
PerformanceLogger& PerformanceLogger::Instance() {
    static PerformanceLogger instance;
    return instance;
}

void PerformanceLogger::Initialize(const std::wstring& logPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return;
    m_stream.open(logPath, std::ios::out | std::ios::trunc); // Overwrite fresh for each run
    if (m_stream.is_open()) {
        m_stream << "Timestamp,CPU_Usage_Pct,Memory_MB,Scan_Latency_MS,HUD_FPS\n";
        m_initialized = true;
    }
}

void PerformanceLogger::LogMetrics(double cpuPct, double ramMb, int scanMs, int fps) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !m_stream.is_open()) return;

    using namespace std::chrono;
    auto now = system_clock::now();
    auto time = system_clock::to_time_t(now);
    std::tm tmValue{};
    localtime_s(&tmValue, &time);

    m_stream << std::put_time(&tmValue, "%H:%M:%S") << ","
             << std::fixed << std::setprecision(2) << cpuPct << ","
             << ramMb << ","
             << scanMs << ","
             << fps << "\n";
    m_stream.flush();
}
