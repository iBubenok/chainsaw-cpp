// ==============================================================================
// platform.cpp - MOD-0004: Платформенные абстракции
// ==============================================================================
//
// MOD-0004 platform
// ADR-0010: std::filesystem::path + явные преобразования path <-> UTF-8
// GUIDE-0001 G-010: платформенная специфика изолирована здесь
//
// ==============================================================================

#include "chainsaw/platform.hpp"

#ifdef _WIN32
#include <io.h>
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace chainsaw::platform {

// ----------------------------------------------------------------------------
// Преобразования путей (ADR-0010)
// ----------------------------------------------------------------------------

std::filesystem::path path_from_utf8(std::string_view u8str) {
#ifdef _WIN32
    // Windows: конвертируем UTF-8 -> UTF-16 для native path
    if (u8str.empty()) {
        return {};
    }
    int len =
        MultiByteToWideChar(CP_UTF8, 0, u8str.data(), static_cast<int>(u8str.size()), nullptr, 0);
    if (len <= 0) {
        // Fallback: просто используем как есть
        return std::filesystem::path(u8str);
    }
    std::wstring wstr(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8str.data(), static_cast<int>(u8str.size()), wstr.data(), len);
    return std::filesystem::path(wstr);
#else
    // Unix: пути уже в UTF-8 (или native encoding)
    return std::filesystem::path(u8str);
#endif
}

std::string path_to_utf8(const std::filesystem::path& p) {
#ifdef _WIN32
    // Windows: конвертируем UTF-16 -> UTF-8
    const std::wstring& wstr = p.native();
    if (wstr.empty()) {
        return {};
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr,
                                  0, nullptr, nullptr);
    if (len <= 0) {
        // Fallback
        return p.string();
    }
    std::string result(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), len,
                        nullptr, nullptr);
    return result;
#else
    // Unix: native string обычно UTF-8
    return p.string();
#endif
}

// ----------------------------------------------------------------------------
// TTY detection
// ----------------------------------------------------------------------------

bool is_tty_stdout() {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

bool is_tty_stderr() {
#ifdef _WIN32
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

// ----------------------------------------------------------------------------
// Платформенные константы (CLI-0001 1.5)
// ----------------------------------------------------------------------------

const char* rule_prefix() {
#ifdef _WIN32
    return "+";
#else
    // Unicode bullet point (U+2023)
    return "\xe2\x80\xa3";  // "‣" в UTF-8
#endif
}

// ----------------------------------------------------------------------------
// Информация о платформе
// ----------------------------------------------------------------------------

std::string os_name() {
#ifdef _WIN32
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

}  // namespace chainsaw::platform
