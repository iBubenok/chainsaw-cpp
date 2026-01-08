// ==============================================================================
// chainsaw/output.hpp - MOD-0003: Пользовательский вывод
// ==============================================================================
//
// MOD-0003 output
// ADR-0006: собственный слой CLI и вывода для byte-to-byte совпадения
// ADR-0007: форматирование строк
// GUIDE-0001 G-011: только этот модуль пишет в stdout/stderr
//
// Назначение:
// - Единственная точка записи в stdout/stderr
// - Форматирование результатов (таблицы/CSV/JSON/JSONL)
// - Прогресс-индикатор
//
// ==============================================================================

#ifndef CHAINSAW_OUTPUT_HPP
#define CHAINSAW_OUTPUT_HPP

#include <string>
#include <string_view>

namespace chainsaw::output {

// ----------------------------------------------------------------------------
// Потоки вывода
// ----------------------------------------------------------------------------

enum class Stream { Stdout, Stderr };

// ----------------------------------------------------------------------------
// Конфигурация вывода
// ----------------------------------------------------------------------------

struct OutputConfig {
    bool quiet = false;      // -q: подавить informational stderr
    int verbose = 0;         // -v: уровень подробности
    bool no_banner = false;  // --no-banner: скрыть ASCII-баннер
};

// ----------------------------------------------------------------------------
// Writer - единый слой вывода (TOBE-0001 4.3)
// ----------------------------------------------------------------------------

class Writer {
public:
    explicit Writer(const OutputConfig& cfg);
    ~Writer();

    /// Записать байты в поток
    void write(Stream s, std::string_view bytes);

    /// Записать строку с переводом строки
    void write_line(Stream s, std::string_view bytes);

    /// Записать информационное сообщение в stderr (если не quiet)
    /// Формат: "[+] <message>"
    void info(std::string_view message);

    /// Записать предупреждение в stderr
    /// Формат: "[!] <message>"
    void warn(std::string_view message);

    /// Alias для warn (для единообразия API)
    void warning(std::string_view message) { warn(message); }

    /// Записать ошибку в stderr
    /// Формат: "[x] <message>"
    void error(std::string_view message);

    /// Записать отладочное сообщение в stderr (только при verbose)
    /// Формат: "[*] <message>"
    void debug(std::string_view message);

    /// Сбросить буферы
    void flush();

    /// Получить текущую конфигурацию
    const OutputConfig& config() const { return config_; }

private:
    OutputConfig config_;
};

// ----------------------------------------------------------------------------
// Функции форматирования сообщений
// ----------------------------------------------------------------------------

/// Форматирует информационное сообщение: "[+] <message>\n"
std::string format_info(std::string_view message);

/// Форматирует сообщение об ошибке: "[x] <message>\n"
std::string format_error(std::string_view message);

/// Форматирует предупреждение: "[!] <message>\n"
std::string format_warning(std::string_view message);

/// Форматирует отладочное сообщение: "[*] <message>\n"
std::string format_debug(std::string_view message);

}  // namespace chainsaw::output

#endif  // CHAINSAW_OUTPUT_HPP
