// ==============================================================================
// chainsaw/shimcache.hpp - MOD-0015: Shimcache Parser & Analyser
// ==============================================================================
//
// MOD-0015 analyse::shimcache
// SLICE-019: Analyse Shimcache Command
// SPEC-SLICE-019: micro-spec поведения
//
// Назначение:
// - Парсинг Windows shimcache (AppCompatCache) из SYSTEM registry hive
// - Поддержка версий: Windows 7, 8, 8.1, 10, 10 Creators
// - Timeline generation с pattern matching и amcache enrichment
// - CSV/Table вывод результатов
//
// Соответствие Rust:
// - upstream/chainsaw/src/file/hve/shimcache.rs (ShimcacheEntry, parse)
// - upstream/chainsaw/src/file/hve/amcache.rs (FileEntry, ProgramEntry)
// - upstream/chainsaw/src/analyse/shimcache.rs (ShimcacheAnalyser, Timeline)
//
// ==============================================================================

#ifndef CHAINSAW_SHIMCACHE_HPP
#define CHAINSAW_SHIMCACHE_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <variant>
#include <vector>

namespace chainsaw::io::hve {
class HveParser;  // Forward declaration
}

namespace chainsaw::analyse::shimcache {

// ============================================================================
// Enums
// ============================================================================

/// Версия shimcache (соответствует Rust ShimcacheVersion)
/// SPEC-SLICE-019 FACT-007
enum class ShimcacheVersion {
    Unknown,
    Windows10,
    Windows10Creators,
    Windows7x64Windows2008R2,
    Windows7x86,
    Windows80Windows2012,
    Windows81Windows2012R2,
    WindowsVistaWin2k3Win2k8,  // НЕ ПОДДЕРЖИВАЕТСЯ
    WindowsXP                  // НЕ ПОДДЕРЖИВАЕТСЯ
};

/// Преобразовать ShimcacheVersion в строку
const char* shimcache_version_to_string(ShimcacheVersion version);

/// CPU архитектура (соответствует Rust CPUArchitecture)
/// SPEC-SLICE-019 FACT-006
enum class CPUArchitecture {
    Amd64,  // 34404 (0x8664)
    Arm,    // 452 (0x1C4)
    I386,   // 332 (0x14C)
    Ia64,   // 512 (0x200)
    Unknown
};

/// Создать CPUArchitecture из u16 значения
CPUArchitecture cpu_architecture_from_u16(std::uint16_t value);

/// Преобразовать CPUArchitecture в строку
const char* cpu_architecture_to_string(CPUArchitecture arch);

/// Тип timestamp в timeline (соответствует Rust TimestampType)
/// SPEC-SLICE-019 FACT-003
enum class TimestampType { AmcacheRangeMatch, NearTSMatch, PatternMatch, ShimcacheLastUpdate };

/// Преобразовать TimestampType в строку
const char* timestamp_type_to_string(TimestampType type);

// ============================================================================
// Shimcache Entry Types
// ============================================================================

/// File entry type (соответствует Rust EntryType::File)
/// SPEC-SLICE-019 FACT-005
struct FileEntryType {
    std::string path;
};

/// Program entry type (соответствует Rust EntryType::Program)
/// SPEC-SLICE-019 FACT-005
struct ProgramEntryType {
    std::string raw_entry;
    std::string unknown_u32;
    CPUArchitecture architecture = CPUArchitecture::Unknown;
    std::string program_name;
    std::string program_version;
    std::string sdk_version;
    std::string publisher_id;
    bool neutral = false;
};

/// Тип записи shimcache
/// SPEC-SLICE-019 FACT-005
using EntryType = std::variant<FileEntryType, ProgramEntryType>;

/// Проверить, является ли entry типом File
bool is_file_entry(const EntryType& entry);

/// Получить path из File entry (или empty если Program)
const std::string& get_entry_path(const EntryType& entry);

/// Получить program_name из Program entry (или path если File)
const std::string& get_entry_display_name(const EntryType& entry);

// ============================================================================
// Shimcache Entry
// ============================================================================

/// Запись shimcache (соответствует Rust ShimcacheEntry)
/// SPEC-SLICE-019 FACT-004
struct ShimcacheEntry {
    std::uint32_t cache_entry_position = 0;
    std::uint32_t controlset = 0;
    std::optional<std::size_t> data_size;
    std::optional<std::vector<std::uint8_t>> data;
    EntryType entry_type;
    std::optional<bool> executed;
    std::optional<std::chrono::system_clock::time_point> last_modified_ts;
    std::size_t path_size = 0;
    std::optional<std::string> signature;
};

// ============================================================================
// Shimcache Artefact
// ============================================================================

/// Shimcache артефакт (соответствует Rust ShimcacheArtefact)
/// SPEC-SLICE-019 FACT-008
struct ShimcacheArtefact {
    std::vector<ShimcacheEntry> entries;
    std::chrono::system_clock::time_point last_update_ts;
    ShimcacheVersion version = ShimcacheVersion::Unknown;
};

// ============================================================================
// Shimcache Error
// ============================================================================

/// Типы ошибок shimcache парсера
enum class ShimcacheErrorKind {
    KeyNotFound,         // Registry key не найден
    ValueNotFound,       // Registry value не найден
    InvalidType,         // Неверный тип данных
    InvalidFormat,       // Неверный формат shimcache
    UnsupportedVersion,  // Неподдерживаемая версия
    ParseError           // Ошибка парсинга
};

/// Ошибка shimcache парсера
struct ShimcacheError {
    ShimcacheErrorKind kind;
    std::string message;

    std::string format() const;
};

// ============================================================================
// Shimcache Parser
// ============================================================================

/// Парсить shimcache из SYSTEM registry hive
/// SPEC-SLICE-019 FACT-011
///
/// @param parser Загруженный HveParser с SYSTEM hive
/// @return ShimcacheArtefact или error message
std::variant<ShimcacheArtefact, ShimcacheError> parse_shimcache(
    chainsaw::io::hve::HveParser& parser);

// ============================================================================
// Amcache Structures
// ============================================================================

/// Amcache file entry (соответствует Rust FileEntry)
/// SPEC-SLICE-019 FACT-030
struct AmcacheFileEntry {
    std::optional<std::string> file_id;
    std::chrono::system_clock::time_point key_last_modified_ts;
    std::optional<std::chrono::system_clock::time_point> file_last_modified_ts;
    std::optional<std::chrono::system_clock::time_point> link_date;
    std::string path;
    std::optional<std::string> program_id;
    std::optional<std::string> sha1_hash;
};

/// Amcache program entry (соответствует Rust ProgramEntry)
/// SPEC-SLICE-019 FACT-031
struct AmcacheProgramEntry {
    std::optional<std::chrono::system_clock::time_point> install_date;
    std::optional<std::chrono::system_clock::time_point> uninstall_date;
    std::chrono::system_clock::time_point last_modified_ts;
    std::string program_id;
    std::string program_name;
    std::string version;
    std::optional<std::string> root_directory_path;
    std::optional<std::string> uninstall_string;
};

/// Amcache артефакт (соответствует Rust AmcacheArtefact)
struct AmcacheArtefact {
    std::vector<AmcacheFileEntry> file_entries;
    std::vector<AmcacheProgramEntry> program_entries;
};

/// Парсить amcache из Amcache.hve
/// SPEC-SLICE-019 FACT-012
///
/// @param parser Загруженный HveParser с Amcache.hve
/// @return AmcacheArtefact или error message
std::variant<AmcacheArtefact, ShimcacheError> parse_amcache(chainsaw::io::hve::HveParser& parser);

// ============================================================================
// Timeline Structures
// ============================================================================

/// Timeline timestamp (соответствует Rust TimelineTimestamp)
/// SPEC-SLICE-019 FACT-002
struct TimelineTimestamp {
    /// Тип timestamp
    enum class Kind {
        Exact,      // Exact(DateTime, TimestampType)
        Range,      // Range { from, to }
        RangeEnd,   // RangeEnd(DateTime)
        RangeStart  // RangeStart(DateTime)
    };

    Kind kind = Kind::Exact;

    /// Для Exact: timestamp и type
    std::chrono::system_clock::time_point exact_ts;
    TimestampType exact_type = TimestampType::ShimcacheLastUpdate;

    /// Для Range: from и to
    std::chrono::system_clock::time_point range_from;
    std::chrono::system_clock::time_point range_to;

    /// Создать Exact timestamp
    static TimelineTimestamp make_exact(std::chrono::system_clock::time_point ts,
                                        TimestampType type);

    /// Создать Range timestamp
    static TimelineTimestamp make_range(std::chrono::system_clock::time_point from,
                                        std::chrono::system_clock::time_point to);

    /// Создать RangeStart timestamp
    static TimelineTimestamp make_range_start(std::chrono::system_clock::time_point ts);

    /// Создать RangeEnd timestamp
    static TimelineTimestamp make_range_end(std::chrono::system_clock::time_point ts);

    /// Получить отображаемый timestamp
    std::chrono::system_clock::time_point display_timestamp() const;
};

/// Timeline entity (соответствует Rust TimelineEntity)
/// SPEC-SLICE-019 FACT-001
struct TimelineEntity {
    std::shared_ptr<AmcacheFileEntry> amcache_file;
    std::shared_ptr<AmcacheProgramEntry> amcache_program;
    std::optional<ShimcacheEntry> shimcache_entry;
    std::optional<TimelineTimestamp> timestamp;

    /// Создать entity с shimcache entry
    static TimelineEntity with_shimcache_entry(ShimcacheEntry entry);
};

// ============================================================================
// ShimcacheAnalyser
// ============================================================================

/// Shimcache analyser (соответствует Rust ShimcacheAnalyser)
/// SPEC-SLICE-019 FACT-009, FACT-010
class ShimcacheAnalyser {
public:
    /// Конструктор
    /// SPEC-SLICE-019 FACT-009
    ShimcacheAnalyser(std::filesystem::path shimcache_path,
                      std::optional<std::filesystem::path> amcache_path = std::nullopt);

    ~ShimcacheAnalyser();

    // Запрещаем копирование
    ShimcacheAnalyser(const ShimcacheAnalyser&) = delete;
    ShimcacheAnalyser& operator=(const ShimcacheAnalyser&) = delete;

    // Разрешаем перемещение
    ShimcacheAnalyser(ShimcacheAnalyser&&) noexcept;
    ShimcacheAnalyser& operator=(ShimcacheAnalyser&&) noexcept;

    /// Создать timeline с amcache enrichment
    /// SPEC-SLICE-019 FACT-010
    ///
    /// @param regex_patterns Регулярные выражения для pattern matching
    /// @param ts_near_pair_matching Включить near timestamp pair detection
    /// @return Timeline entities или error
    std::variant<std::vector<TimelineEntity>, ShimcacheError> amcache_shimcache_timeline(
        const std::vector<std::string>& regex_patterns, bool ts_near_pair_matching = false);

    /// Последняя ошибка
    const std::optional<ShimcacheError>& last_error() const;

    /// Получить версию shimcache (после анализа)
    ShimcacheVersion shimcache_version() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Output Helpers
// ============================================================================

/// Форматировать timestamp в RFC3339
std::string format_timestamp_rfc3339(const std::chrono::system_clock::time_point& ts);

/// Форматировать TimelineEntity как CSV строку
/// SPEC-SLICE-019 FACT-026
std::string format_timeline_entity_csv(const TimelineEntity& entity, std::size_t entry_number);

/// Получить CSV заголовок
/// SPEC-SLICE-019 FACT-026
std::string get_csv_header();

}  // namespace chainsaw::analyse::shimcache

#endif  // CHAINSAW_SHIMCACHE_HPP
