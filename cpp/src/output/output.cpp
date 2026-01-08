// ==============================================================================
// output.cpp - MOD-0003: Пользовательский вывод
// ==============================================================================
//
// MOD-0003 output
// ADR-0006: собственный слой вывода для byte-to-byte совпадения
// GUIDE-0001 G-011: только этот модуль пишет в stdout/stderr
// GUIDE-0001 G-032: байты первичны, избегаем std::endl
//
// ==============================================================================

#include "chainsaw/output.hpp"

#include <cstdio>

namespace chainsaw::output {

// ----------------------------------------------------------------------------
// Writer
// ----------------------------------------------------------------------------

Writer::Writer(const OutputConfig& cfg) : config_(cfg) {}

Writer::~Writer() {
    flush();
}

void Writer::write(Stream s, std::string_view bytes) {
    FILE* f = (s == Stream::Stdout) ? stdout : stderr;
    std::fwrite(bytes.data(), 1, bytes.size(), f);
}

void Writer::write_line(Stream s, std::string_view bytes) {
    write(s, bytes);
    write(s, "\n");
}

void Writer::info(std::string_view message) {
    // Подавляем информационные сообщения при --quiet (CLI-0001 1.4)
    if (config_.quiet) {
        return;
    }
    write(Stream::Stderr, "[+] ");
    write_line(Stream::Stderr, message);
}

void Writer::warn(std::string_view message) {
    write(Stream::Stderr, "[!] ");
    write_line(Stream::Stderr, message);
}

void Writer::error(std::string_view message) {
    // Ошибки всегда печатаются, даже при --quiet (CLI-0001 1.6.1)
    write(Stream::Stderr, "[x] ");
    write_line(Stream::Stderr, message);
}

void Writer::debug(std::string_view message) {
    // Debug только при verbose (CLI-0001 1.4)
    if (config_.verbose <= 0) {
        return;
    }
    write(Stream::Stderr, "[*] ");
    write_line(Stream::Stderr, message);
}

void Writer::flush() {
    std::fflush(stdout);
    std::fflush(stderr);
}

// ----------------------------------------------------------------------------
// Функции форматирования
// ----------------------------------------------------------------------------

std::string format_info(std::string_view message) {
    std::string result = "[+] ";
    result.append(message);
    result.append("\n");
    return result;
}

std::string format_error(std::string_view message) {
    std::string result = "[x] ";
    result.append(message);
    result.append("\n");
    return result;
}

std::string format_warning(std::string_view message) {
    std::string result = "[!] ";
    result.append(message);
    result.append("\n");
    return result;
}

std::string format_debug(std::string_view message) {
    std::string result = "[*] ";
    result.append(message);
    result.append("\n");
    return result;
}

}  // namespace chainsaw::output
