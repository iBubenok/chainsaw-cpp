// ==============================================================================
// chainsaw/rule.hpp - MOD-0008: Rules Loader (Chainsaw Format)
// ==============================================================================
//
// SLICE-009: Chainsaw Rules Loader Implementation
// SPEC-SLICE-009: micro-spec поведения
//
// Назначение:
// - Загрузка и парсинг правил в формате Chainsaw YAML
// - Структуры данных для правил (ChainsawRule, Field, Container, etc.)
// - Фильтрация и валидация правил
// - Оптимизация filter через tau engine passes
//
// Соответствие Rust:
// - upstream/chainsaw/src/rule/mod.rs
// - upstream/chainsaw/src/rule/chainsaw.rs
// - upstream/chainsaw/src/ext/tau.rs
//
// ==============================================================================

#ifndef CHAINSAW_RULE_HPP
#define CHAINSAW_RULE_HPP

#include <chainsaw/reader.hpp>
#include <chainsaw/tau.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

namespace chainsaw::rule {

// ============================================================================
// Enums
// ============================================================================

/// Тип правила (chainsaw или sigma)
/// Соответствует rule/mod.rs:105-138
enum class Kind { Chainsaw, Sigma };

/// Уровень критичности правила
/// Соответствует rule/mod.rs:140-176
enum class Level { Critical, High, Medium, Low, Info };

/// Статус зрелости правила
/// Соответствует rule/mod.rs:178-205
enum class Status { Stable, Experimental };

// ============================================================================
// Container - формат вложенных данных
// ============================================================================

/// Формат контейнера
/// Соответствует rule/chainsaw.rs:18-28
enum class ContainerFormat { Json, Kv };

/// Параметры Key-Value формата
struct KvFormat {
    std::string delimiter;
    std::string separator;
    bool trim = false;
};

/// Контейнер для извлечения вложенных данных
/// Соответствует rule/chainsaw.rs:30-35
struct Container {
    std::string field;
    ContainerFormat format = ContainerFormat::Json;
    std::optional<KvFormat> kv_params;  // только если format == Kv
};

// ============================================================================
// Field - поле для вывода
// ============================================================================

/// Поле для вывода в результатах
/// Соответствует rule/chainsaw.rs:37-46
/// SPEC-SLICE-009 FACT-002: десериализация полей
struct Field {
    std::string name;  // имя для отображения
    std::string from;  // исходное поле в документе
    std::string to;    // целевое поле (может содержать cast)

    std::optional<tau::ModSym> cast;     // int/str/flt
    std::optional<Container> container;  // контейнер для вложенных данных
    bool visible = true;                 // показывать в выводе
};

// ============================================================================
// Aggregate - группировка результатов
// ============================================================================

/// Агрегация для подсчёта совпадений
/// Соответствует rule/mod.rs:90-95
struct Aggregate {
    tau::Pattern count;               // паттерн для подсчёта
    std::vector<std::string> fields;  // поля для группировки
};

// ============================================================================
// Filter - фильтр правила (Detection или Expression)
// ============================================================================

/// Filter - untagged union между Detection и Expression
/// Соответствует rule/mod.rs:97-103
using Filter = std::variant<tau::Detection, tau::Expression>;

// ============================================================================
// ChainsawRule - правило в формате Chainsaw
// ============================================================================

/// Правило Chainsaw
/// Соответствует rule/chainsaw.rs:153-172
struct ChainsawRule {
    std::string name;                  // title/name
    std::string group;                 // группа правила
    std::string description;           // описание
    std::vector<std::string> authors;  // авторы

    io::DocumentKind kind;  // тип файлов (evtx, mft, json, xml, ...)
    Level level;            // уровень критичности
    Status status;          // статус зрелости
    std::string timestamp;  // поле временной метки

    std::vector<Field> fields;           // поля для вывода
    Filter filter;                       // фильтр (Detection или Expression)
    std::optional<Aggregate> aggregate;  // агрегация (опционально)
};

// ============================================================================
// LogSource — источник логов для Sigma правила (SLICE-010)
// ============================================================================

/// Источник логов для Sigma правила
/// Соответствует sigma.rs:111-121
struct LogSource {
    std::optional<std::string> category;
    std::optional<std::string> definition;
    std::optional<std::string> product;
    std::optional<std::string> service;
};

// ============================================================================
// SigmaRule — правило в формате Sigma (SLICE-010)
// ============================================================================

/// Правило Sigma
/// Соответствует sigma.rs:16-42
struct SigmaRule {
    // Metadata (обязательные)
    std::string name;                      // title/name
    std::string description;               // описание
    std::vector<std::string> authors;      // авторы (разделены по ,)
    Level level = Level::Info;             // уровень критичности
    Status status = Status::Experimental;  // статус зрелости

    // Metadata (опциональные)
    std::optional<std::string> id;                           // UUID правила
    std::optional<LogSource> logsource;                      // источник логов
    std::optional<std::vector<std::string>> references;      // ссылки
    std::optional<std::vector<std::string>> tags;            // теги
    std::optional<std::vector<std::string>> falsepositives;  // ложные срабатывания

    // Detection (после конвертации и оптимизации)
    tau::Detection detection;
    std::optional<Aggregate> aggregate;

    /// Document interface: доступ к метаданным для фильтрации
    /// Соответствует sigma.rs:44-73 (Document trait impl)
    std::optional<std::string> find(const std::string& key) const;
};

// ============================================================================
// Rule - объединение всех типов правил
// ============================================================================

/// Правило (Chainsaw или Sigma)
/// Соответствует rule/mod.rs:23-27
using Rule = std::variant<ChainsawRule, SigmaRule>;

// ============================================================================
// Rule interface functions
// ============================================================================

/// Получить aggregate из правила
/// Соответствует Rule::aggregate() в mod.rs:31-36
const std::optional<Aggregate>& rule_aggregate(const Rule& r);

/// Проверить, соответствует ли правило типу
/// Соответствует Rule::is_kind() в mod.rs:38-44
bool rule_is_kind(const Rule& r, Kind kind);

/// Получить уровень правила
/// Соответствует Rule::level() в mod.rs:46-52
Level rule_level(const Rule& r);

/// Получить тип файлов для правила
/// Соответствует Rule::types() в mod.rs:54-60
io::DocumentKind rule_types(const Rule& r);

/// Получить имя правила
/// Соответствует Rule::name() в mod.rs:62-68
const std::string& rule_name(const Rule& r);

/// Проверить документ на соответствие правилу
/// Соответствует Rule::solve() в mod.rs:70-79
bool rule_solve(const Rule& r, const tau::Document& doc);

/// Получить статус правила
/// Соответствует Rule::status() в mod.rs:81-88
Status rule_status(const Rule& r);

// ============================================================================
// Error handling
// ============================================================================

/// Ошибка загрузки/парсинга правила
struct Error {
    std::string message;
    std::string path;

    std::string format() const;
};

// ============================================================================
// Load/Lint functions
// ============================================================================

/// Опции загрузки правил
struct LoadOptions {
    std::optional<std::unordered_set<Kind>> kinds;
    std::optional<std::unordered_set<Level>> levels;
    std::optional<std::unordered_set<Status>> statuses;
};

/// Результат загрузки правил (аналог Result в Rust)
struct LoadResult {
    bool ok = false;
    std::vector<Rule> rules;
    Error error;

    explicit operator bool() const { return ok; }
};

/// Результат lint (аналог Result в Rust)
struct LintResult {
    bool ok = false;
    std::vector<Filter> filters;
    Error error;

    explicit operator bool() const { return ok; }
};

/// Загрузить правило из файла
/// Соответствует rule::load() в mod.rs:206-268
///
/// SPEC-SLICE-009 FACT-007: проверка расширения .yml/.yaml
/// SPEC-SLICE-009 FACT-008..010: фильтрация по kind/level/status
/// SPEC-SLICE-009 FACT-011: применение оптимизации к filter
///
/// @param kind Тип правила (Chainsaw/Sigma)
/// @param path Путь к файлу правила
/// @param options Опции фильтрации
/// @return LoadResult с правилами или ошибкой
LoadResult load(Kind kind, const std::filesystem::path& path, const LoadOptions& options = {});

/// Валидация правила (lint)
/// Соответствует rule::lint() в mod.rs:270-307
///
/// @param kind Тип правила
/// @param path Путь к файлу
/// @return LintResult с фильтрами или ошибкой
LintResult lint(Kind kind, const std::filesystem::path& path);

// ============================================================================
// Parse helpers
// ============================================================================

/// Разобрать Kind из строки
/// Соответствует Kind::from_str() в mod.rs:127-138
/// @throw std::invalid_argument если строка не распознана
Kind parse_kind(std::string_view s);

/// Разобрать Level из строки
/// Соответствует Level::from_str() в mod.rs:162-176
Level parse_level(std::string_view s);

/// Разобрать Status из строки
/// Соответствует Status::from_str() в mod.rs:194-204
Status parse_status(std::string_view s);

/// Преобразовать Kind в строку
/// Соответствует Kind::fmt() в mod.rs:118-125
std::string to_string(Kind k);

/// Преобразовать Level в строку
/// Соответствует Level::fmt() в mod.rs:150-160
std::string to_string(Level l);

/// Преобразовать Status в строку
/// Соответствует Status::fmt() в mod.rs:185-192
std::string to_string(Status s);

// ============================================================================
// Internal helpers (for tau integration)
// ============================================================================

/// Разобрать поле с возможным cast
/// Соответствует ext/tau.rs:130-140
/// Примеры: "int(field)" -> Cast(field, Int), "field" -> Field(field)
tau::Expression parse_field(std::string_view key);

/// Разобрать numeric pattern
/// Соответствует ext/tau.rs:18-36
/// Примеры: "100" -> Equal(100), ">100" -> GreaterThan(100)
std::optional<tau::Pattern> parse_numeric(std::string_view str);

}  // namespace chainsaw::rule

#endif  // CHAINSAW_RULE_HPP
