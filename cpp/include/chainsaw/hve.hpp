// ==============================================================================
// chainsaw/hve.hpp - MOD-0008: HVE Parser (Windows Registry Hive)
// ==============================================================================
//
// MOD-0008 io::hve
// SLICE-015: HVE Parser Implementation
// SPEC-SLICE-015: micro-spec поведения
// ADR-0009: custom REGF parser
//
// Назначение:
// - Парсинг Windows Registry Hive файлов (формат REGF)
// - Навигация по registry дереву (get_key, read_sub_keys)
// - Чтение значений (Binary, U32, U64, String, MultiString)
// - Transaction log support (.LOG, .LOG1, .LOG2)
// - Интеграция с Reader framework
//
// Соответствие Rust:
// - upstream/chainsaw/src/file/hve/mod.rs (Parser, load, parse)
// - external crate notatin (ParserBuilder, CellKeyNode, CellValue)
//
// ==============================================================================

#ifndef CHAINSAW_HVE_HPP
#define CHAINSAW_HVE_HPP

#include <chainsaw/value.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace chainsaw::io::hve {

// ----------------------------------------------------------------------------
// RegValueType - типы значений registry
// ----------------------------------------------------------------------------
//
// SPEC-SLICE-015 FACT-011..019: CellValue variants
// Соответствие Rust notatin::cell_value::CellValue
//

/// Тип значения registry
enum class RegValueType {
    Binary,          // REG_BINARY (Vec<u8>)
    Dword,           // REG_DWORD (U32)
    DwordBigEndian,  // REG_DWORD_BIG_ENDIAN
    Qword,           // REG_QWORD (U64)
    String,          // REG_SZ (String)
    ExpandString,    // REG_EXPAND_SZ
    MultiString,     // REG_MULTI_SZ (Vec<String>)
    Link,            // REG_LINK
    None,            // Пустое значение
    Error            // Ошибка чтения
};

/// Преобразовать RegValueType в строку
const char* reg_value_type_to_string(RegValueType type);

// ----------------------------------------------------------------------------
// RegValue - значение registry
// ----------------------------------------------------------------------------
//
// SPEC-SLICE-015: Аналог notatin::CellValue
//

/// Значение registry с именем и типом
struct RegValue {
    /// Имя значения (пустая строка для default value)
    std::string name;

    /// Тип значения
    RegValueType type = RegValueType::None;

    /// Данные значения
    /// SPEC-SLICE-015 FACT-011..019: разные типы данных
    std::variant<std::monostate,             // None/Error
                 std::vector<std::uint8_t>,  // Binary
                 std::uint32_t,              // Dword
                 std::uint64_t,              // Qword
                 std::string,                // String/ExpandString
                 std::vector<std::string>    // MultiString
                 >
        data;

    /// Получить данные как Binary
    const std::vector<std::uint8_t>* as_binary() const;

    /// Получить данные как Dword (u32)
    std::optional<std::uint32_t> as_u32() const;

    /// Получить данные как Qword (u64)
    std::optional<std::uint64_t> as_u64() const;

    /// Получить данные как String
    const std::string* as_string() const;

    /// Получить данные как MultiString
    const std::vector<std::string>* as_multi_string() const;
};

// ----------------------------------------------------------------------------
// RegKey - ключ registry
// ----------------------------------------------------------------------------
//
// SPEC-SLICE-015: Аналог notatin::CellKeyNode
//

/// Forward declaration
class HveParser;

/// Ключ registry с метаданными и значениями
class RegKey {
public:
    RegKey() = default;

    /// Имя ключа
    /// SPEC-SLICE-015 FACT-007: key_name field
    const std::string& name() const { return name_; }

    /// Полный путь ключа
    /// SPEC-SLICE-015 FACT-007: get_pretty_path()
    const std::string& path() const { return path_; }

    /// Время последней модификации ключа
    /// SPEC-SLICE-015 FACT-010: last_key_written_date_and_time()
    std::chrono::system_clock::time_point last_modified() const { return last_modified_; }

    /// Получить значение по имени
    /// SPEC-SLICE-015 FACT-007: get_value(name)
    /// @param name Имя значения (пустая строка для default)
    /// @return Значение или nullopt если не найдено
    std::optional<RegValue> get_value(std::string_view name) const;

    /// Получить все значения ключа
    /// SPEC-SLICE-015 FACT-007: value_iter()
    const std::vector<RegValue>& values() const { return values_; }

    /// Получить список подключей
    /// SPEC-SLICE-015 FACT-009: read_sub_keys()
    /// Примечание: в отличие от Rust, не требует mutable parser
    const std::vector<std::string>& subkey_names() const { return subkey_names_; }

    /// Количество подключей
    std::size_t subkey_count() const { return subkey_names_.size(); }

    /// Количество значений
    std::size_t value_count() const { return values_.size(); }

    /// Проверка валидности
    bool valid() const { return valid_; }

private:
    friend class HveParser;

    std::string name_;
    std::string path_;
    std::chrono::system_clock::time_point last_modified_;
    std::vector<RegValue> values_;
    std::vector<std::string> subkey_names_;
    bool valid_ = false;
};

// ----------------------------------------------------------------------------
// HveError - ошибки HVE парсера
// ----------------------------------------------------------------------------

/// Типы ошибок HVE парсера
enum class HveErrorKind {
    FileNotFound,      // Файл не найден
    InvalidSignature,  // Неверная сигнатура REGF
    CorruptedData,     // Повреждённые данные
    KeyNotFound,       // Ключ не найден
    ParseError,        // Ошибка парсинга
    IoError            // Ошибка ввода-вывода
};

/// Ошибка HVE парсера
struct HveError {
    HveErrorKind kind;
    std::string message;

    /// Форматировать ошибку
    std::string format() const;
};

// ----------------------------------------------------------------------------
// HveParser - парсер registry hive
// ----------------------------------------------------------------------------
//
// SPEC-SLICE-015: Аналог hve::Parser
// FACT-001: Parser::load() ищет transaction logs (.LOG, .LOG1, .LOG2)
// FACT-002: recover_deleted=true по умолчанию (dirty pages applied)
//

/// Парсер Windows Registry Hive
class HveParser {
public:
    HveParser();
    ~HveParser();

    // Запрещаем копирование
    HveParser(const HveParser&) = delete;
    HveParser& operator=(const HveParser&) = delete;

    // Разрешаем перемещение
    HveParser(HveParser&&) noexcept;
    HveParser& operator=(HveParser&&) noexcept;

    // -------------------------------------------------------------------------
    // Загрузка
    // -------------------------------------------------------------------------

    /// Загрузить hive файл
    /// SPEC-SLICE-015 FACT-001: ищет transaction logs (.LOG, .LOG1, .LOG2)
    /// SPEC-SLICE-015 FACT-002: recover_deleted=true по умолчанию
    ///
    /// При загрузке автоматически ищет .LOG, .LOG1, .LOG2 файлы в той же
    /// директории и применяет dirty pages из transaction logs для
    /// восстановления актуального состояния hive.
    ///
    /// @param path Путь к hive файлу
    /// @return true если успешно, false если ошибка
    bool load(const std::filesystem::path& path);

    /// Последняя ошибка
    const std::optional<HveError>& last_error() const { return error_; }

    /// Путь к загруженному файлу
    const std::filesystem::path& path() const { return path_; }

    /// Загружен ли файл
    bool loaded() const { return loaded_; }

    /// Были ли применены transaction logs при загрузке
    /// @return true если найдены и применены .LOG файлы
    bool transaction_logs_applied() const;

    // -------------------------------------------------------------------------
    // Навигация
    // -------------------------------------------------------------------------

    /// Получить ключ по пути
    /// SPEC-SLICE-015 FACT-007: get_key(path, with_logs)
    /// SPEC-SLICE-015 FACT-008: путь с backslash разделителем
    ///
    /// @param key_path Путь к ключу (например "SOFTWARE\\Microsoft")
    /// @return RegKey или nullopt если не найден
    std::optional<RegKey> get_key(std::string_view key_path);

    /// Получить корневой ключ
    std::optional<RegKey> get_root_key();

    // -------------------------------------------------------------------------
    // Итерация (для Reader framework)
    // -------------------------------------------------------------------------

    /// Итерация по всем ключам hive (DFS)
    /// Используется для Reader::next()
    class Iterator {
    public:
        /// Конструктор по умолчанию (для использования как member)
        Iterator(std::nullptr_t) : parser_(nullptr) {}

        /// Получить следующий ключ
        bool next(RegKey& out);

        /// Есть ли ещё ключи
        bool has_next() const;

    private:
        friend class HveParser;
        explicit Iterator(HveParser* parser);

        HveParser* parser_ = nullptr;
        std::vector<std::string> stack_;  // Стек путей для DFS
        bool initialized_ = false;
    };

    /// Создать итератор по ключам
    Iterator iter();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::filesystem::path path_;
    std::optional<HveError> error_;
    bool loaded_ = false;
};

// ============================================================================
// SrumRegInfo - результат parse_srum_entries()
// ============================================================================
//
// SPEC-SLICE-017 FACT-043..048: SRUM Registry Entries
// Соответствие Rust hve/srum.rs:SrumRegInfo
//

/// Информация из SOFTWARE registry hive для SRUM
struct SrumRegInfo {
    /// Global SRUM parameters (Tier1Period, Tier2Period, etc.)
    /// SPEC-SLICE-017 FACT-044: defaults + overrides
    chainsaw::Value global_parameters;

    /// SRUM Extensions (GUID → {(default), DllName, ...})
    /// SPEC-SLICE-017 FACT-045: Extensions path
    chainsaw::Value extensions;

    /// User info from ProfileList (SID → {GUID, SID, Username})
    /// SPEC-SLICE-017 FACT-047: ProfileList path
    chainsaw::Value user_info;
};

/// Парсить SRUM entries из SOFTWARE hive
///
/// @param parser Загруженный HveParser с SOFTWARE hive
/// @return SrumRegInfo или nullopt при ошибке
///
/// SPEC-SLICE-017 FACT-043..048
std::optional<SrumRegInfo> parse_srum_entries(HveParser& parser);

}  // namespace chainsaw::io::hve

// Forward declaration для Reader
namespace chainsaw::io {
class Reader;
}  // namespace chainsaw::io

namespace chainsaw::io::hve {

// ----------------------------------------------------------------------------
// HveReader - Reader для Reader framework
// ----------------------------------------------------------------------------

/// Создать HVE Reader для интеграции с Reader::open()
/// @param path Путь к hive файлу
/// @param skip_errors Пропускать ошибки
std::unique_ptr<chainsaw::io::Reader> create_hve_reader(const std::filesystem::path& path,
                                                        bool skip_errors);

}  // namespace chainsaw::io::hve

#endif  // CHAINSAW_HVE_HPP
