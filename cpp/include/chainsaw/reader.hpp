// ==============================================================================
// chainsaw/reader.hpp - MOD-0006: Reader Framework
// ==============================================================================
//
// MOD-0006 io::reader
// MOD-0007 formats (JSON/JSONL парсеры интегрированы)
// SLICE-005: Reader Framework + JSON Parser
// SPEC-SLICE-005: micro-spec поведения
// ADR-0003: RapidJSON
// ADR-0010: std::filesystem::path
//
// Назначение:
// - Унифицированный интерфейс чтения файлов разных форматов
// - DocumentKind enum (типы документов)
// - Document struct (документ с метаданными)
// - Reader class (итерация по документам)
//
// Соответствие Rust:
// - upstream/chainsaw/src/file/mod.rs:24-32 (Document enum)
// - upstream/chainsaw/src/file/mod.rs:38-68 (Kind enum + extensions)
// - upstream/chainsaw/src/file/mod.rs:99-433 (Reader)
// - upstream/chainsaw/src/file/json.rs (JSON/JSONL parsers)
//
// ==============================================================================

#ifndef CHAINSAW_READER_HPP
#define CHAINSAW_READER_HPP

#include <chainsaw/value.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace chainsaw::io {

// ----------------------------------------------------------------------------
// DocumentKind - типы документов
// ----------------------------------------------------------------------------
//
// SPEC-SLICE-005 FACT-001: Document представляет распарсенный документ
// SPEC-SLICE-005 FACT-003: Kind — тип входного файла
//
// Соответствие Rust mod.rs:24-32, 38-49:
//   pub enum Document { Evtx, Hve, Json, Mft, Xml, Esedb }
//   pub enum Kind { Evtx, Hve, Json, Jsonl, Mft, Xml, Esedb, Unknown }
//

/// Тип входного файла (Kind в Rust)
/// Определяет какой парсер использовать
enum class DocumentKind {
    Evtx,    // Windows Event Log (.evt, .evtx)
    Hve,     // Registry Hive (.hve)
    Json,    // JSON файл (.json)
    Jsonl,   // JSON Lines (.jsonl)
    Mft,     // Master File Table (.mft, .bin, $MFT)
    Xml,     // XML файл (.xml)
    Esedb,   // ESE Database (.dat, .edb)
    Unknown  // Неизвестный тип
};

/// Преобразовать DocumentKind в строку
const char* document_kind_to_string(DocumentKind kind);

/// Получить расширения для DocumentKind
/// SPEC-SLICE-005 FACT-005: расширения БЕЗ точки
std::vector<std::string> document_kind_extensions(DocumentKind kind);

/// Определить DocumentKind по расширению файла
/// SPEC-SLICE-005 FACT-007: extension-first выбор парсера
/// @param ext Расширение без точки (например "json", не ".json")
/// @return DocumentKind::Unknown если расширение не распознано
DocumentKind document_kind_from_extension(std::string_view ext);

/// Определить DocumentKind по пути файла
/// Использует расширение файла + специальный случай $MFT
/// SPEC-SLICE-005 FACT-012: $MFT edge case
DocumentKind document_kind_from_path(const std::filesystem::path& path);

// ----------------------------------------------------------------------------
// Document - документ с метаданными
// ----------------------------------------------------------------------------
//
// TOBE-0001 4.6.2: каноническое представление документа
//

/// Документ с метаданными источника
struct Document {
    /// Тип документа
    DocumentKind kind = DocumentKind::Unknown;

    /// Каноническое содержимое (Value)
    Value data;

    /// Путь к исходному файлу (UTF-8)
    std::string source;

    /// Номер записи (для EVTX/MFT) или строки (для JSONL)
    std::optional<std::uint64_t> record_id;

    /// Timestamp записи (если применимо)
    std::optional<std::string> timestamp;
};

// ----------------------------------------------------------------------------
// ReaderError - ошибки Reader
// ----------------------------------------------------------------------------

/// Типы ошибок Reader
enum class ReaderErrorKind {
    FileNotFound,       // Файл не найден
    PermissionDenied,   // Нет доступа
    ParseError,         // Ошибка парсинга
    UnsupportedFormat,  // Неподдерживаемый формат
    IoError,            // Ошибка ввода-вывода
    Other               // Другая ошибка
};

/// Ошибка Reader
struct ReaderError {
    ReaderErrorKind kind;
    std::string message;
    std::string path;

    /// Форматировать ошибку для вывода
    /// SPEC-SLICE-005 FACT-008: "[!] failed to load file '<path>' - <error>\n"
    std::string format() const;
};

// ----------------------------------------------------------------------------
// Reader - унифицированный интерфейс чтения
// ----------------------------------------------------------------------------
//
// SPEC-SLICE-005: Reader::load() алгоритм
// TOBE-0001 4.6.3: Reader API
//

/// Результат открытия Reader
struct ReaderResult {
    bool ok;
    std::unique_ptr<class Reader> reader;
    ReaderError error;

    /// Проверка успешности
    explicit operator bool() const { return ok; }
};

/// Унифицированный интерфейс чтения файлов разных форматов
///
/// Использование:
/// @code
///   auto result = Reader::open(path, false, false);
///   if (!result) {
///       std::cerr << result.error.format();
///       return;
///   }
///   Document doc;
///   while (result.reader->next(doc)) {
///       // обработка документа
///   }
/// @endcode
class Reader {
public:
    virtual ~Reader() = default;

    // -------------------------------------------------------------------------
    // Фабричный метод
    // -------------------------------------------------------------------------

    /// Открыть файл и создать Reader
    ///
    /// @param file Путь к файлу
    /// @param load_unknown Если true, пробовать fallback для неизвестных расширений
    /// @param skip_errors Если true, возвращать пустой Reader вместо ошибки
    /// @return ReaderResult с Reader или ошибкой
    ///
    /// SPEC-SLICE-005 алгоритм:
    /// 1. Получить расширение файла
    /// 2. Выбрать парсер по расширению (FACT-007)
    /// 3. При ошибке загрузки + skip_errors → пустой Reader (FACT-009)
    /// 4. При неизвестном расширении + load_unknown → fallback (FACT-010)
    static ReaderResult open(const std::filesystem::path& file, bool load_unknown = false,
                             bool skip_errors = false);

    // -------------------------------------------------------------------------
    // Итерация
    // -------------------------------------------------------------------------

    /// Получить следующий документ
    ///
    /// @param out[out] Документ (заполняется при успехе)
    /// @return true если документ получен, false если документы закончились
    ///
    /// При ошибке парсинга конкретной записи:
    /// - Возвращает false и устанавливает last_error()
    virtual bool next(Document& out) = 0;

    /// Проверить, есть ли ещё документы
    virtual bool has_next() const = 0;

    // -------------------------------------------------------------------------
    // Информация
    // -------------------------------------------------------------------------

    /// Текущий тип файла
    /// SPEC-SLICE-005 FACT-015
    virtual DocumentKind kind() const = 0;

    /// Путь к файлу
    virtual const std::filesystem::path& path() const = 0;

    /// Последняя ошибка (если была)
    virtual const std::optional<ReaderError>& last_error() const = 0;

protected:
    Reader() = default;
};

// ----------------------------------------------------------------------------
// Специализированные Reader'ы (внутренние)
// ----------------------------------------------------------------------------

/// Создать JSON Reader
/// SPEC-SLICE-005: JSON parser (json.rs:12-53)
std::unique_ptr<Reader> create_json_reader(const std::filesystem::path& path, bool skip_errors);

/// Создать JSONL Reader
/// SPEC-SLICE-005: JSONL parser (json.rs:70-125)
std::unique_ptr<Reader> create_jsonl_reader(const std::filesystem::path& path, bool skip_errors);

/// Создать XML Reader
/// SPEC-SLICE-006: XML parser (xml.rs)
std::unique_ptr<Reader> create_xml_reader(const std::filesystem::path& path, bool skip_errors);

/// Создать пустой Reader (для skip_errors или Unknown)
std::unique_ptr<Reader> create_empty_reader(const std::filesystem::path& path,
                                            DocumentKind kind = DocumentKind::Unknown);

// ----------------------------------------------------------------------------
// Вспомогательные функции
// ----------------------------------------------------------------------------

/// Форматировать сообщение об ошибке загрузки файла
/// SPEC-SLICE-005 FACT-008
std::string format_load_error(const std::filesystem::path& path, const std::string& error);

/// Форматировать сообщение о неподдерживаемом формате
/// SPEC-SLICE-005 FACT-011
std::string format_unsupported_error(const std::filesystem::path& path, bool skip_errors);

}  // namespace chainsaw::io

#endif  // CHAINSAW_READER_HPP
