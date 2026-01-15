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

#include <random>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define NOMINMAX
#include <windows.h>
#else
#include <cstdlib>
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
// Временные файлы
// ----------------------------------------------------------------------------

std::filesystem::path make_temp_file(std::string_view prefix) {
#ifdef _WIN32
    // Генерация случайного суффикса для имени файла
    auto generate_random_suffix = [](size_t length = 8) -> std::string {
        static const char chars[] = "0123456789abcdef";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, sizeof(chars) - 2);

        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            result += chars[dis(gen)];
        }
        return result;
    };
    // Windows: GetTempPath() + CreateFile()
    wchar_t temp_path[MAX_PATH + 1];
    DWORD len = GetTempPathW(MAX_PATH + 1, temp_path);
    if (len == 0 || len > MAX_PATH) {
        throw std::runtime_error("Failed to get temp path");
    }

    // Формируем имя файла: prefix + random suffix
    std::wstring temp_dir(temp_path, len);
    std::string filename = std::string(prefix) + "_" + generate_random_suffix() + ".tmp";

    // Конвертируем filename в wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), static_cast<int>(filename.size()),
                                   nullptr, 0);
    if (wlen <= 0) {
        throw std::runtime_error("Failed to convert filename to wide string");
    }
    std::wstring wfilename(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), static_cast<int>(filename.size()),
                        wfilename.data(), wlen);

    std::wstring full_path = temp_dir + wfilename;

    // Создаём файл
    HANDLE h = CreateFileW(full_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Failed to create temp file");
    }
    CloseHandle(h);

    return std::filesystem::path(full_path);
#else
    // Unix: mkstemp()
    std::string temp_dir = "/tmp";
    const char* tmpdir = std::getenv("TMPDIR");
    if (tmpdir != nullptr && tmpdir[0] != '\0') {
        temp_dir = tmpdir;
    }

    std::string tmpl = temp_dir + "/" + std::string(prefix) + "_XXXXXX";
    std::vector<char> tmpl_buf(tmpl.begin(), tmpl.end());
    tmpl_buf.push_back('\0');

    int fd = mkstemp(tmpl_buf.data());
    if (fd == -1) {
        throw std::runtime_error("Failed to create temp file");
    }
    close(fd);

    return std::filesystem::path(tmpl_buf.data());
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
