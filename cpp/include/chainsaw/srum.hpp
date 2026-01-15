// ==============================================================================
// chainsaw/srum.hpp - MOD-0011: SRUM Analyser (System Resource Usage Monitor)
// ==============================================================================
//
// MOD-0011 analyse::srum
// SLICE-017: Analyse SRUM Command Implementation
// SPEC-SLICE-017: micro-spec поведения
//
// SRUM (System Resource Usage Monitor):
// - Механизм Windows 8+ для отслеживания программ, сервисов, сети
// - Данные хранятся в ESE database: %SystemRoot%\System32\sru\SRUDB.dat
// - Параметры/расширения в SOFTWARE hive: HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\SRUM
//
// Источники:
// - upstream/chainsaw/src/analyse/srum.rs
// - upstream/chainsaw/src/file/hve/srum.rs
// - upstream/chainsaw/src/file/esedb/srum.rs
//
// ==============================================================================

#ifndef CHAINSAW_SRUM_HPP
#define CHAINSAW_SRUM_HPP

#include <chainsaw/value.hpp>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chainsaw::analyse {

// ============================================================================
// TableDetails - информация о SRUM таблице
// ============================================================================
//
// SPEC-SLICE-017 FACT-021..025: TableDetails struct
// Соответствие Rust srum.rs:21-27
//

/// Детали SRUM таблицы
struct TableDetails {
    std::string table_name;                     // Имя таблицы
    std::optional<std::string> dll_path;        // DLL провайдера
    std::optional<std::string> from;            // Начало временного диапазона (ISO8601)
    std::optional<std::string> to;              // Конец временного диапазона (ISO8601)
    std::optional<double> retention_time_days;  // Ожидаемое время хранения (дни)
};

// ============================================================================
// SrumDbInfo - результат parse_srum_database()
// ============================================================================
//
// SPEC-SLICE-017 FACT-029..032: SrumDbInfo struct
// Соответствие Rust srum.rs:29-32
//

/// Результат анализа SRUM базы данных
struct SrumDbInfo {
    /// Детали таблиц (GUID -> TableDetails)
    std::vector<std::pair<std::string, TableDetails>> table_details;

    /// Содержимое базы данных (JSON массив записей)
    Value db_content;
};

// ============================================================================
// SrumAnalyser - анализатор SRUM
// ============================================================================
//
// SPEC-SLICE-017 FACT-034..042: SrumAnalyser class
// Соответствие Rust srum.rs:34-38, 89-413
//

/// Анализатор SRUM (System Resource Usage Monitor)
///
/// Использование:
/// @code
///   SrumAnalyser analyser(srum_path, software_hive_path);
///   auto result = analyser.parse_srum_database();
///   if (result) {
///       // result->table_details - детали таблиц
///       // result->db_content - записи базы данных
///   }
/// @endcode
class SrumAnalyser {
public:
    /// Конструктор
    ///
    /// @param srum_path Путь к SRUDB.dat
    /// @param software_hive_path Путь к SOFTWARE hive
    SrumAnalyser(std::filesystem::path srum_path, std::filesystem::path software_hive_path);

    /// Парсить SRUM базу данных
    ///
    /// @return SrumDbInfo или nullopt при ошибке
    ///
    /// SPEC-SLICE-017 FACT-039..042: parse_srum_database()
    std::optional<SrumDbInfo> parse_srum_database();

    /// Получить последнюю ошибку
    const std::optional<std::string>& last_error() const { return error_; }

private:
    std::filesystem::path srum_path_;
    std::filesystem::path software_hive_path_;
    std::optional<std::string> error_;
};

// ============================================================================
// Вспомогательные функции
// ============================================================================

/// Конвертировать байты SID в строку формата "S-1-5-21-..."
///
/// @param bytes SID байты
/// @return SID строка или nullopt при ошибке
///
/// SPEC-SLICE-017 FACT-003..007: bytes_to_sid_string()
std::optional<std::string> bytes_to_sid_string(const std::vector<std::uint8_t>& bytes);

/// Форматировать длительность в читаемую строку
///
/// @param days Количество дней (с дробной частью)
/// @return Строка вида "X days, Y hours, Z minutes"
///
/// SPEC-SLICE-017 FACT-008..013: format_duration()
std::string format_duration(double days);

/// Конвертировать Windows timestamp (100-ns intervals since 1601) в ISO8601
///
/// @param win_ts Windows timestamp
/// @return ISO8601 строка
std::string win32_ts_to_iso8601(std::uint64_t win_ts);

}  // namespace chainsaw::analyse

#endif  // CHAINSAW_SRUM_HPP
