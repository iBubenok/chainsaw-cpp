// ==============================================================================
// output.cpp - MOD-0003: Пользовательский вывод
// ==============================================================================
//
// MOD-0003 output
// ADR-0006: собственный слой вывода для byte-to-byte совпадения
// ADR-0003: RapidJSON для JSON сериализации
// GUIDE-0001 G-011: только этот модуль пишет в stdout/stderr
// GUIDE-0001 G-032: байты первичны, избегаем std::endl
// SPEC-SLICE-002: Output Layer спецификация
//
// ==============================================================================

#include "chainsaw/output.hpp"

#include "chainsaw/platform.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace chainsaw::output {

// ----------------------------------------------------------------------------
// ANSI Escape Codes
// ----------------------------------------------------------------------------

namespace {

// ANSI SGR (Select Graphic Rendition) коды
constexpr const char* ANSI_RESET = "\x1b[0m";
constexpr const char* ANSI_GREEN = "\x1b[32m";
constexpr const char* ANSI_YELLOW = "\x1b[33m";
constexpr const char* ANSI_RED = "\x1b[31m";
constexpr const char* ANSI_CYAN = "\x1b[36m";
constexpr const char* ANSI_MAGENTA = "\x1b[35m";

// Unicode box-drawing characters для таблиц (FACT-W16)
// UTF-8 кодировка
constexpr const char* BOX_V = "\xe2\x94\x82";      // │ U+2502
constexpr const char* BOX_H = "\xe2\x94\x80";      // ─ U+2500
constexpr const char* BOX_TL = "\xe2\x94\x8c";     // ┌ U+250C
constexpr const char* BOX_TR = "\xe2\x94\x90";     // ┐ U+2510
constexpr const char* BOX_BL = "\xe2\x94\x94";     // └ U+2514
constexpr const char* BOX_BR = "\xe2\x94\x98";     // ┘ U+2518
constexpr const char* BOX_LT = "\xe2\x94\x9c";     // ├ U+251C
constexpr const char* BOX_RT = "\xe2\x94\xa4";     // ┤ U+2524
constexpr const char* BOX_TT = "\xe2\x94\xac";     // ┬ U+252C
constexpr const char* BOX_BT = "\xe2\x94\xb4";     // ┴ U+2534
constexpr const char* BOX_CROSS = "\xe2\x94\xbc";  // ┼ U+253C

// Лимит длины поля при !full_output (FACT-W17)
constexpr size_t FIELD_LENGTH_LIMIT = 496;

}  // namespace

// ----------------------------------------------------------------------------
// Writer
// ----------------------------------------------------------------------------

Writer::Writer(const OutputConfig& cfg) : config_(cfg) {
    // Открыть файл вывода, если путь задан
    if (config_.output_path.has_value()) {
        open_output_file();
    }
}

Writer::~Writer() {
    close_output_file();
    flush();
}

void Writer::write(Stream s, std::string_view bytes) {
    write_impl(s, bytes);
}

void Writer::write_line(Stream s, std::string_view bytes) {
    write(s, bytes);
    write(s, "\n");
}

void Writer::write_impl(Stream s, std::string_view bytes) {
    FILE* f = nullptr;

    // При записи в stdout с output_file — пишем в файл (FACT-W02)
    if (s == Stream::Stdout && output_file_ != nullptr) {
        f = output_file_;
    } else {
        f = get_file(s);
    }

    if (f != nullptr) {
        std::fwrite(bytes.data(), 1, bytes.size(), f);
    }
}

FILE* Writer::get_file(Stream s) const {
    return (s == Stream::Stdout) ? stdout : stderr;
}

void Writer::info(std::string_view message) {
    // Подавляем информационные сообщения при --quiet (FACT-W06, CLI-0001 1.4)
    if (config_.quiet) {
        return;
    }
    // Цветной вывод для stderr при TTY
    if (supports_color(Stream::Stderr)) {
        write(Stream::Stderr, ANSI_GREEN);
        write(Stream::Stderr, "[+] ");
        write(Stream::Stderr, ANSI_RESET);
    } else {
        write(Stream::Stderr, "[+] ");
    }
    write_line(Stream::Stderr, message);
}

void Writer::warn(std::string_view message) {
    // Предупреждения подавляются при quiet (FACT-W12)
    if (config_.quiet) {
        return;
    }
    if (supports_color(Stream::Stderr)) {
        write(Stream::Stderr, ANSI_YELLOW);
        write(Stream::Stderr, "[!] ");
        write(Stream::Stderr, ANSI_RESET);
    } else {
        write(Stream::Stderr, "[!] ");
    }
    write_line(Stream::Stderr, message);
}

void Writer::error(std::string_view message) {
    // Ошибки всегда печатаются, даже при --quiet (CLI-0001 1.6.1)
    if (supports_color(Stream::Stderr)) {
        write(Stream::Stderr, ANSI_RED);
        write(Stream::Stderr, "[x] ");
        write(Stream::Stderr, ANSI_RESET);
    } else {
        write(Stream::Stderr, "[x] ");
    }
    write_line(Stream::Stderr, message);
}

void Writer::debug(std::string_view message) {
    // Debug только при verbose > 0 (FACT-W04, CLI-0001 1.4)
    if (config_.verbose <= 0) {
        return;
    }
    if (supports_color(Stream::Stderr)) {
        write(Stream::Stderr, ANSI_CYAN);
        write(Stream::Stderr, "[*] ");
        write(Stream::Stderr, ANSI_RESET);
    } else {
        write(Stream::Stderr, "[*] ");
    }
    write_line(Stream::Stderr, message);
}

void Writer::trace(std::string_view message) {
    // Trace только при verbose > 1 (FACT-W05)
    if (config_.verbose <= 1) {
        return;
    }
    if (supports_color(Stream::Stderr)) {
        write(Stream::Stderr, ANSI_MAGENTA);
        write(Stream::Stderr, "[~] ");
        write(Stream::Stderr, ANSI_RESET);
    } else {
        write(Stream::Stderr, "[~] ");
    }
    write_line(Stream::Stderr, message);
}

void Writer::green_line(std::string_view message) {
    write_colored(Stream::Stdout, message, Color::Green);
    write(Stream::Stdout, "\n");
}

void Writer::green_line_stderr(std::string_view message) {
    if (config_.quiet) {
        return;
    }
    write_colored(Stream::Stderr, message, Color::Green);
    write(Stream::Stderr, "\n");
}

void Writer::yellow_line(std::string_view message) {
    if (config_.quiet) {
        return;
    }
    write_colored(Stream::Stderr, message, Color::Yellow);
    write(Stream::Stderr, "\n");
}

void Writer::red_line(std::string_view message) {
    write_colored(Stream::Stderr, message, Color::Red);
    write(Stream::Stderr, "\n");
}

void Writer::write_colored(Stream s, std::string_view message, Color color) {
    // При записи в файл — без ANSI codes (FACT-W11)
    bool use_color = (s == Stream::Stdout && output_file_ == nullptr && supports_color(s)) ||
                     (s == Stream::Stderr && supports_color(s));

    if (use_color) {
        write(s, ansi_color_code(color));
        write(s, message);
        write(s, ANSI_RESET);
    } else {
        write(s, message);
    }
}

void Writer::write_json(const rapidjson::Value& value) {
    // Потоковая JSON сериализация (FACT-W07)
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);

    write(Stream::Stdout, std::string_view(buffer.GetString(), buffer.GetSize()));
    flush();
}

void Writer::write_json_line(const rapidjson::Value& value) {
    // JSONL: JSON + newline (FACT-W19)
    write_json(value);
    write(Stream::Stdout, "\n");
}

void Writer::write_json_pretty(const rapidjson::Value& value) {
    // Pretty JSON с отступами (FACT-W08)
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);

    write(Stream::Stdout, std::string_view(buffer.GetString(), buffer.GetSize()));
    write(Stream::Stdout, "\n");
    flush();
}

void Writer::progress_begin(std::string_view label, size_t total) {
    // Прогресс скрыт при verbose или quiet (FACT-W15)
    if (config_.verbose > 0 || config_.quiet) {
        return;
    }

    progress_label_ = std::string(label);
    progress_total_ = total;
    progress_current_ = 0;
    progress_active_ = true;
}

void Writer::progress_tick(size_t current) {
    if (!progress_active_) {
        return;
    }

    progress_current_ = current;
    // Базовая реализация: просто обновляем состояние
    // Полноценный progress bar требует терминального вывода с возвратом каретки
}

void Writer::progress_end() {
    if (!progress_active_) {
        return;
    }

    progress_active_ = false;
    progress_label_.clear();
}

void Writer::flush() {
    std::fflush(stdout);
    std::fflush(stderr);
    if (output_file_ != nullptr) {
        std::fflush(output_file_);
    }
}

bool Writer::open_output_file() {
    if (!config_.output_path.has_value()) {
        return false;
    }

    const auto& path = config_.output_path.value();
    std::string path_str = platform::path_to_utf8(path);

#ifdef _WIN32
    // Windows: использовать _wfopen для Unicode путей
    output_file_ = _wfopen(path.c_str(), L"wb");
#else
    output_file_ = std::fopen(path_str.c_str(), "wb");
#endif

    return output_file_ != nullptr;
}

void Writer::close_output_file() {
    if (output_file_ != nullptr) {
        std::fflush(output_file_);
        std::fclose(output_file_);
        output_file_ = nullptr;
    }
}

// ----------------------------------------------------------------------------
// Table
// ----------------------------------------------------------------------------

Table::Table() = default;

void Table::set_headers(const std::vector<std::string>& headers) {
    headers_ = headers;
    widths_calculated_ = false;
}

void Table::add_row(const std::vector<std::string>& cells) {
    rows_.push_back(cells);
    widths_calculated_ = false;
}

void Table::set_column_width(size_t col, size_t width) {
    if (col >= col_widths_.size()) {
        col_widths_.resize(col + 1, 0);
    }
    col_widths_[col] = width;
}

void Table::calculate_widths() {
    if (widths_calculated_) {
        return;
    }

    size_t num_cols = headers_.size();
    for (const auto& row : rows_) {
        num_cols = std::max(num_cols, row.size());
    }

    if (col_widths_.size() < num_cols) {
        col_widths_.resize(num_cols, 0);
    }

    // Вычисляем минимальную ширину на основе содержимого
    for (size_t i = 0; i < headers_.size(); ++i) {
        col_widths_[i] = std::max(col_widths_[i], headers_[i].size());
    }

    for (const auto& row : rows_) {
        for (size_t i = 0; i < row.size(); ++i) {
            col_widths_[i] = std::max(col_widths_[i], row[i].size());
        }
    }

    widths_calculated_ = true;
}

std::string Table::format_line(char left, char middle, char right,
                               [[maybe_unused]] char fill) const {
    // Используем Unicode box-drawing (FACT-W16)
    // Параметр fill зарезервирован для будущего использования (ASCII fallback)
    std::string line;

    // left corner
    if (left == 'T') {
        line += BOX_TL;  // ┌
    } else if (left == 'M') {
        line += BOX_LT;  // ├
    } else if (left == 'B') {
        line += BOX_BL;  // └
    }

    for (size_t i = 0; i < col_widths_.size(); ++i) {
        // padding (1 space each side) + content width
        for (size_t j = 0; j < col_widths_[i] + 2; ++j) {
            line += BOX_H;  // ─
        }

        if (i < col_widths_.size() - 1) {
            // middle connector
            if (middle == 'T') {
                line += BOX_TT;  // ┬
            } else if (middle == 'M') {
                line += BOX_CROSS;  // ┼
            } else if (middle == 'B') {
                line += BOX_BT;  // ┴
            }
        }
    }

    // right corner
    if (right == 'T') {
        line += BOX_TR;  // ┐
    } else if (right == 'M') {
        line += BOX_RT;  // ┤
    } else if (right == 'B') {
        line += BOX_BR;  // ┘
    }

    return line;
}

std::string Table::format_row(const std::vector<std::string>& cells) const {
    std::string line;
    line += BOX_V;  // │

    for (size_t i = 0; i < col_widths_.size(); ++i) {
        line += ' ';  // padding

        std::string cell = (i < cells.size()) ? cells[i] : "";
        line += cell;

        // Добавляем пробелы до нужной ширины
        if (cell.size() < col_widths_[i]) {
            line.append(col_widths_[i] - cell.size(), ' ');
        }

        line += ' ';    // padding
        line += BOX_V;  // │
    }

    return line;
}

std::string Table::to_string() const {
    // Нужно вычислить ширины (const_cast для ленивого вычисления)
    const_cast<Table*>(this)->calculate_widths();

    std::string result;

    // Top border: ┌───┬───┐
    result += format_line('T', 'T', 'T', '-');
    result += '\n';

    // Headers
    if (!headers_.empty()) {
        result += format_row(headers_);
        result += '\n';

        // Header separator: ├───┼───┤
        result += format_line('M', 'M', 'M', '-');
        result += '\n';
    }

    // Rows
    for (const auto& row : rows_) {
        result += format_row(row);
        result += '\n';
    }

    // Bottom border: └───┴───┘
    result += format_line('B', 'B', 'B', '-');
    result += '\n';

    return result;
}

void Table::print(Writer& w) {
    std::string table_str = to_string();
    w.write(Stream::Stdout, table_str);
}

// ----------------------------------------------------------------------------
// Вспомогательные функции
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

std::string format_field_length(std::string_view field, size_t col_width, bool full_output) {
    // FACT-W17: очистка и форматирование полей
    std::string result;
    result.reserve(field.size());

    bool prev_space = false;
    for (char c : field) {
        // Удаляем \n, \r, \t
        if (c == '\n' || c == '\r' || c == '\t') {
            if (!prev_space) {
                result += ' ';
                prev_space = true;
            }
            continue;
        }

        // Заменяем двойные пробелы на одинарные
        if (c == ' ') {
            if (!prev_space) {
                result += c;
                prev_space = true;
            }
            continue;
        }

        result += c;
        prev_space = false;
    }

    // Разбиваем на chunks по col_width
    if (col_width > 0 && result.size() > col_width) {
        std::string chunked;
        for (size_t i = 0; i < result.size(); i += col_width) {
            if (i > 0) {
                chunked += '\n';
            }
            chunked.append(result, i, col_width);
        }
        result = std::move(chunked);
    }

    // Обрезка при !full_output (FACT-W17)
    if (!full_output && result.size() > FIELD_LENGTH_LIMIT) {
        result.resize(FIELD_LENGTH_LIMIT);
        result += "...\n(use --full to show all content)";
    }

    return result;
}

std::string ansi_color_code(Color color) {
    switch (color) {
    case Color::Green:
        return ANSI_GREEN;
    case Color::Yellow:
        return ANSI_YELLOW;
    case Color::Red:
        return ANSI_RED;
    case Color::Cyan:
        return ANSI_CYAN;
    case Color::Magenta:
        return ANSI_MAGENTA;
    case Color::Default:
    default:
        return "";
    }
}

std::string ansi_reset_code() {
    return ANSI_RESET;
}

bool supports_color(Stream s) {
    // Проверяем TTY через platform модуль
    if (s == Stream::Stdout) {
        return platform::is_tty_stdout();
    } else {
        return platform::is_tty_stderr();
    }
}

}  // namespace chainsaw::output
