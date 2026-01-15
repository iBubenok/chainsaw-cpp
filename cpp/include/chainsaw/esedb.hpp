// ==============================================================================
// chainsaw/esedb.hpp - MOD-0010: ESEDB Parser (ESE Database)
// ==============================================================================
//
// MOD-0010 io::esedb
// SLICE-016: ESEDB Parser Implementation
// SPEC-SLICE-016: micro-spec поведения
// ADR-0012: libesedb для парсинга ESE Database
//
// ESE Database (Extensible Storage Engine):
// - Используется Windows для: SRUM, Windows Search, Exchange, AD
// - Формат файлов: .edb, .dat (SRUDB.dat)
// - libesedb обеспечивает доступ к таблицам и записям
//
// Источники:
// - https://github.com/libyal/libesedb
// - upstream/chainsaw/src/file/esedb/mod.rs
// - upstream/chainsaw/src/file/esedb/srum.rs
//
// ==============================================================================

#ifndef CHAINSAW_ESEDB_HPP
#define CHAINSAW_ESEDB_HPP

#include <chainsaw/value.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chainsaw::io {
// Forward declaration
class Reader;
}  // namespace chainsaw::io

namespace chainsaw::io::esedb {

// ============================================================================
// EsedbError - ошибки парсера
// ============================================================================

/// Типы ошибок ESEDB парсера
enum class EsedbErrorKind {
    FileNotFound,      // Файл не найден
    OpenError,         // Ошибка открытия ESE database
    TableNotFound,     // Таблица не найдена
    ColumnNotFound,    // Колонка не найдена
    ParseError,        // Ошибка парсинга записи
    UnsupportedValue,  // Неподдерживаемый тип значения
    NotSupported,      // ESEDB не поддерживается на этой платформе
    Other              // Другая ошибка
};

/// Ошибка ESEDB парсера
struct EsedbError {
    EsedbErrorKind kind;
    std::string message;

    /// Форматировать ошибку для вывода
    std::string format() const;
};

// ============================================================================
// SruDbIdMapTableEntry - запись из SruDbIdMapTable (SRUM)
// ============================================================================
//
// SPEC-SLICE-016 FACT-013: SruDbIdMapTable содержит маппинг IdIndex -> IdBlob
// Соответствие Rust srum.rs:6-11
//

/// Запись из SruDbIdMapTable таблицы SRUM
struct SruDbIdMapTableEntry {
    /// IdType: тип идентификатора
    std::int8_t id_type = 0;

    /// IdIndex: индекс (ключ для поиска)
    std::int32_t id_index = 0;

    /// IdBlob: бинарные данные (опционально)
    std::optional<std::vector<std::uint8_t>> id_blob;

    /// IdBlob как строка (для не-SID типов)
    /// SPEC-SLICE-016 FACT-014: конвертация IdBlob в строку для id_type != 3
    std::optional<std::string> id_blob_as_string;
};

// ============================================================================
// EsedbParser - парсер ESE Database
// ============================================================================
//
// SPEC-SLICE-016: EsedbParser API
// Соответствие Rust mod.rs:15-18 (Parser struct)
//

/// Парсер ESE Database файлов
///
/// Использование:
/// @code
///   EsedbParser parser;
///   auto result = parser.load(path);
///   if (!result) {
///       std::cerr << result.error().format();
///       return;
///   }
///   for (auto& record : parser.parse()) {
///       // обработка записи
///   }
/// @endcode
class EsedbParser {
public:
    EsedbParser();
    ~EsedbParser();

    // Non-copyable, movable
    EsedbParser(const EsedbParser&) = delete;
    EsedbParser& operator=(const EsedbParser&) = delete;
    EsedbParser(EsedbParser&&) noexcept;
    EsedbParser& operator=(EsedbParser&&) noexcept;

    // -------------------------------------------------------------------------
    // Загрузка
    // -------------------------------------------------------------------------

    /// Загрузить ESE database файл
    ///
    /// @param file_path Путь к .edb/.dat файлу
    /// @return true при успехе, false при ошибке (см. last_error())
    ///
    /// SPEC-SLICE-016 FACT-002: EseDb::open(file_path)
    bool load(const std::filesystem::path& file_path);

    /// Проверить, загружен ли файл
    bool is_loaded() const;

    /// Последняя ошибка
    const std::optional<EsedbError>& last_error() const;

    // -------------------------------------------------------------------------
    // Парсинг
    // -------------------------------------------------------------------------

    /// Распарсить все таблицы и записи
    ///
    /// @return Вектор записей, каждая запись — HashMap<колонка, значение>
    ///
    /// SPEC-SLICE-016 FACT-003: итерация по таблицам
    /// SPEC-SLICE-016 FACT-004: итерация по записям
    /// SPEC-SLICE-016 FACT-005: конверсия значений в JSON
    std::vector<std::unordered_map<std::string, Value>> parse();

    /// Распарсить SruDbIdMapTable (SRUM specific)
    ///
    /// @return HashMap<id_index, SruDbIdMapTableEntry>
    ///
    /// SPEC-SLICE-016 FACT-013: SruDbIdMapTable parsing
    std::unordered_map<std::string, SruDbIdMapTableEntry> parse_sru_db_id_map_table();

    // -------------------------------------------------------------------------
    // Итерация
    // -------------------------------------------------------------------------

    /// Проверить, есть ли ещё записи
    bool has_next() const;

    /// Проверить, достигнут ли конец файла
    bool eof() const;

    /// Получить следующую запись
    ///
    /// @param out[out] Запись (заполняется при успехе)
    /// @return true если запись получена, false если записи закончились
    bool next(std::unordered_map<std::string, Value>& out);

    // -------------------------------------------------------------------------
    // Информация
    // -------------------------------------------------------------------------

    /// Путь к файлу
    const std::filesystem::path& path() const;

    /// Проверить, поддерживается ли ESEDB на текущей платформе
    static bool is_supported();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Вспомогательные функции
// ============================================================================

/// Конвертировать OLE Automation Date в ISO8601 строку
///
/// @param ole_time OLE Automation Date (double)
/// @return ISO8601 строка (RFC3339 без дробных секунд)
///
/// SPEC-SLICE-016 FACT-007: DateTime -> OleTime -> ISO8601
/// OLE Automation Date: дни с 30 декабря 1899 года
std::string ole_time_to_iso8601(double ole_time);

/// Конвертировать FILETIME в ISO8601 строку
///
/// @param filetime FILETIME (100-nanosecond intervals since 1601)
/// @return ISO8601 строка
std::string filetime_to_iso8601(std::int64_t filetime);

/// Создать ESEDB Reader
///
/// @param path Путь к ESE database файлу
/// @param skip_errors Если true, возвращать пустой Reader при ошибке
/// @return unique_ptr на Reader
std::unique_ptr<Reader> create_esedb_reader(const std::filesystem::path& path, bool skip_errors);

}  // namespace chainsaw::io::esedb

#endif  // CHAINSAW_ESEDB_HPP
