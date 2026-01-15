// ==============================================================================
// chainsaw/evtx.hpp - MOD-0007: EVTX Parser
// ==============================================================================
//
// MOD-0007 formats/evtx
// SLICE-007: EVTX Parser
// SPEC-SLICE-007: micro-spec поведения
// ADR-0009: собственная реализация EVTX парсера
//
// Назначение:
// - Парсинг Windows Event Log (.evtx) файлов
// - Конверсия Binary XML → JSON с _attributes семантикой
// - Соответствие Rust crate evtx с separate_json_attributes(true)
//
// Соответствие Rust:
// - upstream/chainsaw/src/file/evtx.rs:18-23 (Parser::load)
// - upstream/chainsaw/src/file/evtx.rs:25-30 (Parser::parse)
// - upstream/chainsaw/src/file/evtx.rs:32-62 (Wrapper алиасы)
//
// Формат EVTX:
// - File header: "ElfFile\0" + metadata (4096 bytes)
// - Chunks: "ElfChnk\0" + records (65536 bytes each)
// - Records: Binary XML data с template substitution
//
// ==============================================================================

#ifndef CHAINSAW_EVTX_HPP
#define CHAINSAW_EVTX_HPP

#include <chainsaw/value.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chainsaw::evtx {

/// Структура для хранения шаблона Binary XML
struct BinXmlTemplate {
    std::string xml_template;            // Шаблон XML с placeholder'ами ${N}
    std::size_t substitution_count = 0;  // Количество подстановок
};

// ============================================================================
// Константы формата EVTX
// ============================================================================

/// Магия заголовка файла
constexpr char FILE_HEADER_MAGIC[] = "ElfFile\0";
constexpr std::size_t FILE_HEADER_SIZE = 4096;

/// Магия заголовка чанка
constexpr char CHUNK_HEADER_MAGIC[] = "ElfChnk\0";
constexpr std::size_t CHUNK_SIZE = 65536;
constexpr std::size_t CHUNK_HEADER_SIZE = 512;

// ============================================================================
// Структуры EVTX формата
// ============================================================================

/// Заголовок файла EVTX
struct FileHeader {
    char magic[8];  // "ElfFile\0"
    std::uint64_t first_chunk_number;
    std::uint64_t last_chunk_number;
    std::uint64_t next_record_id;
    std::uint32_t header_size;
    std::uint16_t minor_version;
    std::uint16_t major_version;
    std::uint16_t header_block_size;
    std::uint16_t chunk_count;
    // ... остальные поля (не критичны для парсинга)
};

/// Заголовок чанка
struct ChunkHeader {
    char magic[8];  // "ElfChnk\0"
    std::uint64_t first_event_record_number;
    std::uint64_t last_event_record_number;
    std::uint64_t first_event_record_id;
    std::uint64_t last_event_record_id;
    std::uint32_t header_size;
    std::uint32_t last_event_record_data_offset;
    std::uint32_t free_space_offset;
    std::uint32_t event_records_checksum;
    // ... остальные поля
};

/// Заголовок записи
struct RecordHeader {
    std::uint32_t signature;  // 0x00002a2a ("**\0\0")
    std::uint32_t size;
    std::uint64_t record_id;
    std::uint64_t timestamp;  // Windows FILETIME
};

// ============================================================================
// EVTX Record - запись события
// ============================================================================

/// EVTX запись (аналог SerializedEvtxRecord<Json> в Rust)
/// SPEC-SLICE-007 FACT-001, FACT-002
struct EvtxRecord {
    /// JSON представление события с _attributes семантикой
    Value data;

    /// Timestamp в формате ISO 8601 (2022-10-11T19:26:52.154080Z)
    /// SPEC-SLICE-007 INV-003
    std::string timestamp;

    /// ID записи в логе
    std::uint64_t record_id;
};

// ============================================================================
// Binary XML Token Types
// ============================================================================

enum class BinXmlToken : std::uint8_t {
    EndOfStream = 0x00,
    OpenStartElement = 0x01,
    CloseStartElement = 0x02,
    CloseEmptyElement = 0x03,
    CloseElement = 0x04,
    Value = 0x05,
    Attribute = 0x06,
    CDataSection = 0x07,
    EntityReference = 0x08,
    ProcessingInstructionTarget = 0x0a,
    ProcessingInstructionData = 0x0b,
    TemplateInstance = 0x0c,
    NormalSubstitution = 0x0d,
    ConditionalSubstitution = 0x0e,
    StartOfStream = 0x0f
};

// ============================================================================
// Binary XML Value Types
// ============================================================================

enum class BinXmlValueType : std::uint8_t {
    Null = 0x00,
    WString = 0x01,
    AnsiString = 0x02,
    Int8 = 0x03,
    UInt8 = 0x04,
    Int16 = 0x05,
    UInt16 = 0x06,
    Int32 = 0x07,
    UInt32 = 0x08,
    Int64 = 0x09,
    UInt64 = 0x0a,
    Float = 0x0b,
    Double = 0x0c,
    Bool = 0x0d,
    Binary = 0x0e,
    Guid = 0x0f,
    SizeT = 0x10,
    FileTime = 0x11,
    SystemTime = 0x12,
    Sid = 0x13,
    Hex32 = 0x14,
    Hex64 = 0x15,
    BinXml = 0x21,
    WStringArray = 0x81
};

// ============================================================================
// EvtxParser - основной парсер EVTX
// ============================================================================

/// Ошибка парсинга EVTX
struct EvtxError {
    std::string message;
    std::size_t offset;
};

class EvtxParser;

/// Итератор по записям EVTX
class EvtxRecordIterator {
public:
    EvtxRecordIterator(EvtxParser* parser, bool end = false);

    EvtxRecordIterator& operator++();
    bool operator!=(const EvtxRecordIterator& other) const;
    const EvtxRecord& operator*() const;

private:
    EvtxParser* parser_;
    bool end_;
    std::optional<EvtxRecord> current_;

    void advance();
};

/// Парсер EVTX файлов
/// SPEC-SLICE-007 FACT-003, FACT-004
class EvtxParser {
public:
    EvtxParser() = default;
    ~EvtxParser() = default;

    // Non-copyable
    EvtxParser(const EvtxParser&) = delete;
    EvtxParser& operator=(const EvtxParser&) = delete;

    // Movable
    EvtxParser(EvtxParser&&) noexcept = default;
    EvtxParser& operator=(EvtxParser&&) noexcept = default;

    /// Загрузить EVTX файл
    /// @param path Путь к файлу
    /// @return true при успехе
    bool load(const std::filesystem::path& path);

    /// Получить следующую запись
    /// @param record Запись (заполняется при успехе)
    /// @return true если запись получена, false если записи закончились
    bool next(EvtxRecord& record);

    /// Получить последнюю ошибку
    const std::optional<EvtxError>& last_error() const { return error_; }

    /// Получить путь к файлу
    const std::filesystem::path& path() const { return path_; }

    /// Проверить, достигнут ли конец файла
    bool eof() const { return eof_; }

    /// Итераторы для range-based for
    EvtxRecordIterator begin() { return EvtxRecordIterator(this); }
    EvtxRecordIterator end() { return EvtxRecordIterator(this, true); }

private:
    std::filesystem::path path_;
    std::ifstream file_;
    std::optional<EvtxError> error_;

    // Состояние парсинга
    std::uint64_t file_size_ = 0;
    std::uint64_t current_chunk_ = 0;
    std::uint64_t current_chunk_offset_ = 0;
    std::uint64_t current_record_offset_ = 0;
    std::uint64_t chunk_end_offset_ = 0;
    bool eof_ = false;

    // Кеш строк чанка (для template substitution)
    std::unordered_map<std::uint32_t, std::string> string_cache_;

    // Кеш шаблонов чанка (BUGFIX: должен сохраняться между записями)
    std::unordered_map<std::uint32_t, BinXmlTemplate> template_cache_;

    // Методы парсинга
    bool read_file_header();
    bool read_chunk_header();
    bool read_record(EvtxRecord& record);

    // Binary XML парсинг
    Value parse_binxml(const std::vector<std::uint8_t>& data);

    // Утилиты чтения
    template <typename T>
    bool read_value(T& value);

    bool read_bytes(void* buffer, std::size_t size);
    std::string read_utf16_string(const std::vector<std::uint8_t>& data, std::size_t& offset,
                                  std::size_t char_count);

public:
    // Конверсия timestamp (public для использования в binxml парсере)
    static std::string filetime_to_iso8601(std::uint64_t filetime);
};

// ============================================================================
// Вспомогательные функции
// ============================================================================

/// Найти поле с алиасами (для tau_engine Document::find)
/// SPEC-SLICE-007 FACT-012
///
/// Алиасы:
/// - Event.System.Provider → Event.System.Provider_attributes.Name
/// - Event.System.TimeCreated → Event.System.TimeCreated_attributes.SystemTime
///
/// @param value JSON документ EVTX события
/// @param key Путь к полю (с поддержкой алиасов)
/// @return Значение поля или nullptr
const Value* find_with_aliases(const Value& value, std::string_view key);

/// Найти поле по пути (разделитель ".")
/// @param value JSON документ
/// @param path Путь к полю (например "Event.System.EventID")
/// @return Значение поля или nullptr
const Value* find_by_path(const Value& value, std::string_view path);

}  // namespace chainsaw::evtx

#endif  // CHAINSAW_EVTX_HPP
