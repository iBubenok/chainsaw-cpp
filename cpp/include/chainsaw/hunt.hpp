// ==============================================================================
// chainsaw/hunt.hpp - MOD-0012: Hunt Command Pipeline
// ==============================================================================
//
// SLICE-012: Hunt Command Implementation
// SPEC-SLICE-012: micro-spec поведения
//
// Назначение:
// - HunterBuilder — builder pattern для создания Hunter
// - Hunter — движок детектирования угроз
// - Mapping/Group — структуры для mapping YAML
// - Mapper — преобразование полей документа
//
// Соответствие Rust:
// - upstream/chainsaw/src/hunt.rs
// - upstream/chainsaw/src/main.rs (hunt command)
//
// ==============================================================================

#ifndef CHAINSAW_HUNT_HPP
#define CHAINSAW_HUNT_HPP

#include <chainsaw/reader.hpp>
#include <chainsaw/rule.hpp>
#include <chainsaw/search.hpp>  // DateTime
#include <chainsaw/tau.hpp>
#include <chainsaw/value.hpp>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace chainsaw::hunt {

// Forward declarations
class Hunter;
class HunterBuilder;

// Reuse DateTime from search module
using DateTime = search::DateTime;

// ============================================================================
// UUID - упрощённый UUID для идентификации (16 байт)
// ============================================================================

/// UUID для идентификации hunts/rules
/// Соответствует uuid::Uuid в Rust
struct UUID {
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    /// Создать новый случайный UUID
    static UUID generate();

    /// Сравнение
    bool operator==(const UUID& other) const { return high == other.high && low == other.low; }

    bool operator!=(const UUID& other) const { return !(*this == other); }

    bool operator<(const UUID& other) const {
        if (high != other.high)
            return high < other.high;
        return low < other.low;
    }

    /// Hash функция для использования в unordered_map
    struct Hash {
        std::size_t operator()(const UUID& id) const {
            return std::hash<std::uint64_t>{}(id.high) ^ (std::hash<std::uint64_t>{}(id.low) << 1);
        }
    };
};

// ============================================================================
// Precondition — дополнительный фильтр для правил
// ============================================================================

/// Precondition — фильтр с условием применения
/// Соответствует hunt.rs:35-40
struct Precondition {
    std::unordered_map<std::string, std::string> for_;  // условия matching
    tau::Expression filter;                             // фильтр для правила
};

// ============================================================================
// Extensions — расширения mapping
// ============================================================================

/// Extensions — дополнительные настройки mapping
/// Соответствует hunt.rs:42-46
struct Extensions {
    std::optional<std::vector<Precondition>> preconditions;
};

// ============================================================================
// Group — группа в mapping
// ============================================================================

/// Group — группа документов в mapping
/// Соответствует hunt.rs:48-57
struct Group {
    UUID id;                          // уникальный идентификатор
    std::vector<rule::Field> fields;  // поля для вывода
    tau::Expression filter;           // фильтр группы
    std::string name;                 // название группы
    std::string timestamp;            // поле timestamp
};

// ============================================================================
// Mapping — конфигурация mapping файла
// ============================================================================

/// Mapping — загруженный mapping YAML
/// Соответствует hunt.rs:59-68
struct Mapping {
    std::unordered_set<std::string> exclusions;  // исключённые правила
    std::optional<Extensions> extensions;        // расширения
    std::vector<Group> groups;                   // группы
    io::DocumentKind kind;                       // тип файлов
    rule::Kind rules;                            // тип правил (sigma)
};

/// Загрузить mapping из YAML файла
/// @param path Путь к YAML файлу
/// @return Mapping или ошибка
struct MappingResult {
    bool ok = false;
    Mapping mapping;
    std::string error;
};
MappingResult load_mapping(const std::filesystem::path& path);

// ============================================================================
// Mapper — преобразование полей документа
// ============================================================================

/// Режим работы Mapper
/// Соответствует hunt.rs:562-566
enum class MapperMode {
    None,  // bypass — поля не преобразуются
    Fast,  // простое переименование полей
    Full   // с cast и container
};

/// Mapper — преобразование полей документа
/// Соответствует hunt.rs:568-714
class Mapper {
public:
    /// Default constructor
    Mapper() = default;

    /// Move constructor
    Mapper(Mapper&&) = default;
    Mapper& operator=(Mapper&&) = default;

    /// Copy constructor
    Mapper(const Mapper&) = default;
    Mapper& operator=(const Mapper&) = default;

    /// Создать Mapper из списка полей
    static Mapper from(std::vector<rule::Field> fields);

    /// Получить список полей
    const std::vector<rule::Field>& fields() const { return fields_; }

    /// Получить режим работы
    MapperMode mode() const { return mode_; }

    /// Преобразовать значение поля
    /// @param doc Исходный документ
    /// @param key Имя поля
    /// @return Преобразованное значение или nullopt
    std::optional<Value> find(const tau::Document& doc, std::string_view key) const;

private:
    std::vector<rule::Field> fields_;
    MapperMode mode_ = MapperMode::None;

    // Fast mode: from -> to
    std::unordered_map<std::string, std::string> fast_map_;

    // Full mode: from -> (to, container, cast)
    struct FullEntry {
        std::string to;
        std::optional<rule::Container> container;
        std::optional<tau::ModSym> cast;
    };
    std::unordered_map<std::string, FullEntry> full_map_;
};

/// MappedDocument — Document wrapper с применённым Mapper
class MappedDocument : public tau::Document {
public:
    MappedDocument(const tau::Document& doc, const Mapper& mapper) : doc_(doc), mapper_(mapper) {}

    std::optional<Value> find(std::string_view key) const override;

private:
    const tau::Document& doc_;
    const Mapper& mapper_;
    mutable std::unordered_map<std::string, Value> container_cache_;
};

// ============================================================================
// HuntKind — тип hunt (Group или Rule)
// ============================================================================

/// HuntKind::Group — для mapping groups
struct HuntKindGroup {
    std::unordered_set<UUID, UUID::Hash> exclusions;  // исключённые правила
    tau::Expression filter;                           // фильтр группы
    rule::Kind kind = rule::Kind::Sigma;              // тип правил
    std::unordered_map<UUID, tau::Expression, UUID::Hash> preconditions;

    HuntKindGroup() = default;
    HuntKindGroup(HuntKindGroup&&) = default;
    HuntKindGroup& operator=(HuntKindGroup&&) = default;
    HuntKindGroup(const HuntKindGroup&) = delete;
    HuntKindGroup& operator=(const HuntKindGroup&) = delete;
};

/// HuntKind::Rule — для standalone Chainsaw правил
struct HuntKindRule {
    std::optional<rule::Aggregate> aggregate;  // агрегация
    rule::Filter filter;                       // фильтр правила

    HuntKindRule() = default;
    HuntKindRule(HuntKindRule&&) = default;
    HuntKindRule& operator=(HuntKindRule&&) = default;
    HuntKindRule(const HuntKindRule&) = delete;
    HuntKindRule& operator=(const HuntKindRule&) = delete;
};

/// HuntKind — вариант типа hunt
using HuntKind = std::variant<HuntKindGroup, HuntKindRule>;

// ============================================================================
// Hunt — единица детектирования
// ============================================================================

/// Hunt — конфигурация одного hunt
/// Соответствует hunt.rs:733-750
struct Hunt {
    UUID id{};              // уникальный идентификатор
    std::string group;      // название группы
    HuntKind kind;          // тип hunt
    Mapper mapper;          // преобразование полей
    std::string timestamp;  // поле timestamp

    io::DocumentKind file = io::DocumentKind::Unknown;  // тип файлов

    /// Default constructor
    Hunt() = default;

    /// Move constructor
    Hunt(Hunt&&) = default;
    Hunt& operator=(Hunt&&) = default;

    /// No copy (HuntKind contains non-copyable tau::Expression)
    Hunt(const Hunt&) = delete;
    Hunt& operator=(const Hunt&) = delete;

    /// Проверить, использует ли hunt агрегацию
    bool is_aggregation() const;
};

// ============================================================================
// Hit — совпадение правила
// ============================================================================

/// Hit — результат совпадения
/// Соответствует hunt.rs:70-74
struct Hit {
    UUID hunt;           // ID hunt
    UUID rule;           // ID правила
    DateTime timestamp;  // время события
};

// ============================================================================
// Detection — результат детектирования
// ============================================================================

/// Document — сериализованный документ
/// Соответствует hunt.rs:83-106
struct Document {
    io::DocumentKind kind;  // тип документа
    std::string path;       // путь к файлу
    Value data;             // данные документа
};

/// Kind::Individual — один документ
struct KindIndividual {
    Document document;
};

/// Kind::Aggregate — агрегированные документы
struct KindAggregate {
    std::vector<Document> documents;
};

/// Kind::Cached — кешированный документ (cache-to-disk)
struct KindCached {
    io::DocumentKind kind;
    std::string path;
    std::size_t offset;
    std::size_t size;
};

/// Kind — вариант типа результата
using DetectionKind = std::variant<KindIndividual, KindAggregate, KindCached>;

/// Detections — результаты детектирования для одного документа
/// Соответствует hunt.rs:76-79
struct Detections {
    std::vector<Hit> hits;  // совпадения
    DetectionKind kind;     // данные документа
};

// ============================================================================
// HunterBuilder — builder pattern
// ============================================================================

/// Builder для создания Hunter
/// Соответствует hunt.rs:134-547
class HunterBuilder {
public:
    /// Создать builder
    static HunterBuilder create();

    /// Установить пути к mapping файлам
    HunterBuilder& mappings(std::vector<std::filesystem::path> paths);

    /// Установить правила
    HunterBuilder& rules(std::vector<rule::Rule> rules);

    /// Загружать файлы с неизвестным расширением
    HunterBuilder& load_unknown(bool load);

    /// Использовать локальный timezone
    HunterBuilder& local(bool local);

    /// Включить preprocessing (BETA)
    HunterBuilder& preprocess(bool preprocess);

    /// Установить начало временного диапазона
    HunterBuilder& from(DateTime datetime);

    /// Установить конец временного диапазона
    HunterBuilder& to(DateTime datetime);

    /// Пропускать ошибки чтения/парсинга
    HunterBuilder& skip_errors(bool skip);

    /// Установить timezone
    HunterBuilder& timezone(std::string tz);

    /// Собрать Hunter
    struct BuildResult {
        bool ok = false;
        std::unique_ptr<Hunter> hunter;
        std::string error;
    };
    BuildResult build();

private:
    HunterBuilder() = default;

    std::optional<std::vector<std::filesystem::path>> mappings_;
    std::optional<std::vector<rule::Rule>> rules_;

    std::optional<bool> load_unknown_;
    std::optional<bool> local_;
    std::optional<bool> preprocess_;
    std::optional<DateTime> from_;
    std::optional<bool> skip_errors_;
    std::optional<std::string> timezone_;
    std::optional<DateTime> to_;
};

// ============================================================================
// Hunter — движок детектирования
// ============================================================================

/// Hunter — основной движок hunt
/// Соответствует hunt.rs:764-1146
class Hunter {
public:
    /// Builder factory
    static HunterBuilder builder() { return HunterBuilder::create(); }

    /// Выполнить hunt по файлу
    /// @param path Путь к файлу
    /// @param cache_file Опциональный файл для кеширования
    /// @return Вектор результатов детектирования
    struct HuntResult {
        bool ok = false;
        std::vector<Detections> detections;
        std::string error;
    };
    HuntResult hunt(const std::filesystem::path& path, std::FILE* cache_file = nullptr) const;

    /// Получить расширения файлов для hunt
    std::unordered_set<std::string> extensions() const;

    /// Получить список hunts
    const std::vector<Hunt>& hunts() const { return hunts_; }

    /// Получить правила (по UUID)
    const std::unordered_map<UUID, rule::Rule, UUID::Hash>& rules() const { return rules_; }

    /// Getter для load_unknown
    bool load_unknown() const { return load_unknown_; }

    /// Getter для skip_errors
    bool skip_errors() const { return skip_errors_; }

private:
    friend class HunterBuilder;

    Hunter() = default;

    /// Проверить, нужно ли пропустить документ по времени
    bool should_skip(const DateTime& timestamp) const;

    std::vector<Hunt> hunts_;
    std::vector<std::string> fields_;  // для preprocessing
    std::unordered_map<UUID, rule::Rule, UUID::Hash> rules_;

    bool load_unknown_ = false;
    bool preprocess_ = false;
    bool skip_errors_ = false;

    std::optional<DateTime> from_;
    std::optional<DateTime> to_;
};

// ============================================================================
// Output formatting
// ============================================================================

/// Сериализовать Detections в JSON
std::string detections_to_json(const Detections& det, const std::vector<Hunt>& hunts,
                               const std::unordered_map<UUID, rule::Rule, UUID::Hash>& rules,
                               bool local_time = false);

/// Сериализовать Detections в JSONL (одна строка)
std::string detections_to_jsonl(const Detections& det, const std::vector<Hunt>& hunts,
                                const std::unordered_map<UUID, rule::Rule, UUID::Hash>& rules,
                                bool local_time = false);

/// Форматировать таблицу детектирований
/// @param detections Вектор детектирований
/// @param hunts Список hunts
/// @param rules Правила
/// @param column_width Ширина колонок (0 = авто)
/// @param full Показывать все поля
/// @param metadata Показывать метаданные
/// @param local_time Использовать локальное время
/// @return Отформатированная таблица
std::string format_table(const std::vector<Detections>& detections, const std::vector<Hunt>& hunts,
                         const std::unordered_map<UUID, rule::Rule, UUID::Hash>& rules,
                         std::uint32_t column_width = 0, bool full = false, bool metadata = false,
                         bool local_time = false);

// ============================================================================
// Platform-specific constants
// ============================================================================

#ifdef _WIN32
/// RULE_PREFIX для Windows
constexpr const char* RULE_PREFIX = "+";
#else
/// RULE_PREFIX для Unix/macOS
constexpr const char* RULE_PREFIX = "\xe2\x80\xa3";  // Unicode '‣' (U+2023)
#endif

}  // namespace chainsaw::hunt

#endif  // CHAINSAW_HUNT_HPP
