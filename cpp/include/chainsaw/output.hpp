// ==============================================================================
// chainsaw/output.hpp - MOD-0003: Пользовательский вывод
// ==============================================================================
//
// MOD-0003 output
// ADR-0006: собственный слой CLI и вывода для byte-to-byte совпадения
// ADR-0007: форматирование строк
// ADR-0003: RapidJSON для JSON сериализации
// GUIDE-0001 G-011: только этот модуль пишет в stdout/stderr
// SPEC-SLICE-002: Output Layer спецификация
//
// Назначение:
// - Единственная точка записи в stdout/stderr
// - Форматирование результатов (таблицы/CSV/JSON/JSONL)
// - Цветной вывод (ANSI escape codes)
// - Прогресс-индикатор
// - Вывод в файл (--output)
//
// ==============================================================================

#ifndef CHAINSAW_OUTPUT_HPP
#define CHAINSAW_OUTPUT_HPP

#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Forward declarations для JSON/YAML
namespace rapidjson {
class CrtAllocator;
template <typename BaseAllocator>
class MemoryPoolAllocator;
template <typename Encoding, typename Allocator>
class GenericValue;
template <typename CharType>
struct UTF8;
using Value = GenericValue<UTF8<char>, MemoryPoolAllocator<CrtAllocator>>;
}  // namespace rapidjson

namespace chainsaw::output {

// ----------------------------------------------------------------------------
// Потоки вывода
// ----------------------------------------------------------------------------

enum class Stream { Stdout, Stderr };

// ----------------------------------------------------------------------------
// Формат вывода (FACT-W01)
// ----------------------------------------------------------------------------

enum class Format {
    Std,   // Стандартный (таблицы/текст)
    Csv,   // CSV
    Json,  // JSON массив
    Jsonl  // JSON Lines (один объект на строку)
};

// ----------------------------------------------------------------------------
// ANSI цвета для терминала
// ----------------------------------------------------------------------------

enum class Color {
    Default,
    Green,   // Успех, информация
    Yellow,  // Предупреждения
    Red,     // Ошибки
    Cyan,    // Отладка
    Magenta  // Выделение
};

// ----------------------------------------------------------------------------
// Конфигурация вывода (FACT-W01)
// ----------------------------------------------------------------------------

struct OutputConfig {
    bool quiet = false;           // -q: подавить informational stderr
    int verbose = 0;              // -v: уровень подробности (0..2+)
    bool no_banner = false;       // --no-banner: скрыть ASCII-баннер
    bool full_output = false;     // --full: не обрезать длинные строки
    Format format = Format::Std;  // Формат вывода

    // Путь для вывода (--output)
    std::optional<std::filesystem::path> output_path;
};

// ----------------------------------------------------------------------------
// Платформозависимые константы (FACT-W13, FACT-W14)
// ----------------------------------------------------------------------------

#ifdef _WIN32
// Windows: ASCII fallback для совместимости с cmd.exe (FACT-W13)
constexpr const char* RULE_PREFIX = "+";
// Windows: ASCII spinner (FACT-W14)
constexpr const char* TICK_CHARS = "-\\|/";
constexpr int TICK_MS = 200;
#else
// Unix: Unicode символы
constexpr const char* RULE_PREFIX = "\xe2\x80\xa3";  // ‣ U+2023
// Unix: Braille spinner (FACT-W14)
constexpr const char* TICK_CHARS = "\xe2\xa0\x8b\xe2\xa0\x99\xe2\xa0\xb9\xe2\xa0\xb8\xe2\xa0\xbc"
                                   "\xe2\xa0\xb4\xe2\xa0\xa6\xe2\xa0\xa7\xe2\xa0\x87\xe2\xa0\x8f";
constexpr int TICK_MS = 80;
#endif

// ----------------------------------------------------------------------------
// Writer - единый слой вывода (TOBE-0001 4.3)
// ----------------------------------------------------------------------------

class Writer {
public:
    explicit Writer(const OutputConfig& cfg);
    ~Writer();

    // Базовый вывод
    // -------------------------------------------------------------------------

    /// Записать байты в поток (FACT-W02)
    void write(Stream s, std::string_view bytes);

    /// Записать строку с переводом строки (FACT-W03)
    void write_line(Stream s, std::string_view bytes);

    // Сообщения с префиксами
    // -------------------------------------------------------------------------

    /// Записать информационное сообщение в stderr (если не quiet)
    /// Формат: "[+] <message>" (FACT-W06)
    void info(std::string_view message);

    /// Записать предупреждение в stderr (если не quiet)
    /// Формат: "[!] <message>"
    void warn(std::string_view message);

    /// Alias для warn (для единообразия API)
    void warning(std::string_view message) { warn(message); }

    /// Записать ошибку в stderr (всегда)
    /// Формат: "[x] <message>"
    void error(std::string_view message);

    /// Записать отладочное сообщение в stderr (только при verbose > 0)
    /// Формат: "[*] <message>" (FACT-W04)
    void debug(std::string_view message);

    /// Записать трассировочное сообщение в stderr (только при verbose > 1)
    /// (FACT-W05)
    void trace(std::string_view message);

    // Цветной вывод (FACT-W11, FACT-W12)
    // -------------------------------------------------------------------------

    /// Записать зелёную строку в stdout
    void green_line(std::string_view message);

    /// Записать зелёную строку в stderr (если не quiet)
    void green_line_stderr(std::string_view message);

    /// Записать жёлтую строку в stderr (если не quiet)
    void yellow_line(std::string_view message);

    /// Записать красную строку в stderr
    void red_line(std::string_view message);

    // JSON/YAML вывод (FACT-W07, FACT-W08, FACT-W09)
    // -------------------------------------------------------------------------

    /// Записать JSON значение (потоковая сериализация)
    void write_json(const rapidjson::Value& value);

    /// Записать JSON значение + newline (JSONL формат)
    void write_json_line(const rapidjson::Value& value);

    /// Записать pretty JSON (с отступами)
    void write_json_pretty(const rapidjson::Value& value);

    // Прогресс-индикатор (FACT-W15)
    // -------------------------------------------------------------------------

    /// Начать прогресс-индикатор
    void progress_begin(std::string_view label, size_t total);

    /// Обновить прогресс
    void progress_tick(size_t current);

    /// Завершить прогресс-индикатор
    void progress_end();

    // Управление
    // -------------------------------------------------------------------------

    /// Сбросить буферы
    void flush();

    /// Получить текущую конфигурацию
    const OutputConfig& config() const { return config_; }

    /// Открыть файл для вывода (при output_path задан)
    bool open_output_file();

    /// Закрыть файл вывода
    void close_output_file();

    /// Проверить, открыт ли файл для вывода
    bool has_output_file() const { return output_file_ != nullptr; }

private:
    /// Записать байты (внутренний метод с учётом output file)
    void write_impl(Stream s, std::string_view bytes);

    /// Записать с цветом
    void write_colored(Stream s, std::string_view message, Color color);

    /// Получить FILE* для потока
    FILE* get_file(Stream s) const;

    OutputConfig config_;
    FILE* output_file_ = nullptr;  // Файл для вывода (если --output)

    // Состояние прогресс-бара
    std::string progress_label_;
    size_t progress_total_ = 0;
    size_t progress_current_ = 0;
    bool progress_active_ = false;
};

// ----------------------------------------------------------------------------
// Table - форматирование таблиц (FACT-W10, FACT-W16)
// ----------------------------------------------------------------------------

class Table {
public:
    Table();

    /// Добавить заголовки
    void set_headers(const std::vector<std::string>& headers);

    /// Добавить строку данных
    void add_row(const std::vector<std::string>& cells);

    /// Установить ширину столбца
    void set_column_width(size_t col, size_t width);

    /// Вывести таблицу через Writer (FACT-W16 Unicode box-drawing)
    void print(Writer& w);

    /// Вывести таблицу в строку
    std::string to_string() const;

    /// Получить количество строк (без заголовка)
    size_t row_count() const { return rows_.size(); }

private:
    /// Форматировать горизонтальную линию
    std::string format_line(char left, char middle, char right, char fill) const;

    /// Форматировать строку данных
    std::string format_row(const std::vector<std::string>& cells) const;

    /// Вычислить ширину столбцов
    void calculate_widths();

    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<size_t> col_widths_;
    bool widths_calculated_ = false;
};

// ----------------------------------------------------------------------------
// Вспомогательные функции
// ----------------------------------------------------------------------------

/// Форматирует информационное сообщение: "[+] <message>\n"
std::string format_info(std::string_view message);

/// Форматирует сообщение об ошибке: "[x] <message>\n"
std::string format_error(std::string_view message);

/// Форматирует предупреждение: "[!] <message>\n"
std::string format_warning(std::string_view message);

/// Форматирует отладочное сообщение: "[*] <message>\n"
std::string format_debug(std::string_view message);

/// Форматировать поле с ограничением длины (FACT-W17)
/// Удаляет \n, \r, \t, заменяет двойные пробелы
/// При full_output=false обрезает длинные строки
std::string format_field_length(std::string_view field, size_t col_width, bool full_output);

/// Получить ANSI escape code для цвета
std::string ansi_color_code(Color color);

/// Получить ANSI reset code
std::string ansi_reset_code();

/// Проверить, поддерживает ли поток цвета (TTY check)
bool supports_color(Stream s);

}  // namespace chainsaw::output

#endif  // CHAINSAW_OUTPUT_HPP
