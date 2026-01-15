// ==============================================================================
// chainsaw/sigma.hpp - MOD-0008: Sigma Rules Loader
// ==============================================================================
//
// SLICE-010: Sigma Rules Loader + Conversion
// SPEC-SLICE-010: micro-spec поведения
//
// Назначение:
// - Загрузка и парсинг правил в формате Sigma YAML
// - Конвертация Sigma detection → Tau format
// - Обработка модификаторов (contains, endswith, startswith, re, base64, etc.)
// - Поддержка Rule Collections (multi-doc YAML)
//
// Соответствие Rust:
// - upstream/chainsaw/src/rule/sigma.rs
//
// ==============================================================================

#ifndef CHAINSAW_SIGMA_HPP
#define CHAINSAW_SIGMA_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace chainsaw::rule::sigma {

// ============================================================================
// LogSource — источник логов для Sigma правила
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
// SigmaAggregate — агрегация для Sigma
// ============================================================================

/// Агрегация для Sigma правила
/// Соответствует sigma.rs:76-79
struct SigmaAggregate {
    std::string count;                // ">=5", ">10", etc.
    std::vector<std::string> fields;  // поля для группировки
};

// ============================================================================
// SigmaRuleData — данные правила Sigma (после конвертации)
// ============================================================================

/// Данные правила после загрузки и конвертации
struct SigmaRuleData {
    std::string title;
    std::string description;
    std::vector<std::string> authors;
    std::string level;   // "critical", "high", "medium", "low", "info"
    std::string status;  // "stable", "experimental"

    std::optional<std::string> id;
    std::optional<LogSource> logsource;
    std::optional<std::vector<std::string>> references;
    std::optional<std::vector<std::string>> tags;
    std::optional<std::vector<std::string>> falsepositives;

    // Detection converted to Tau format (as raw YAML string for further parsing)
    std::string detection_yaml;

    std::optional<SigmaAggregate> aggregate;
};

// ============================================================================
// Error
// ============================================================================

/// Ошибка при обработке Sigma правила
struct Error {
    std::string message;
    std::string context;

    std::string format() const;
};

// ============================================================================
// LoadResult
// ============================================================================

struct LoadResult {
    bool ok = false;
    std::vector<SigmaRuleData> rules;
    Error error;

    explicit operator bool() const { return ok; }
};

// ============================================================================
// Match trait functions — pattern transformations
// ============================================================================

/// Преобразует строку в contains паттерн
/// "foobar" → "i*foobar*"
/// Соответствует sigma.rs:208-210
std::string as_contains(const std::string& value);

/// Преобразует строку в endswith паттерн
/// "foobar" → "i*foobar"
/// Соответствует sigma.rs:211-213
std::string as_endswith(const std::string& value);

/// Преобразует строку в startswith паттерн
/// "foobar" → "ifoobar*"
/// Соответствует sigma.rs:259-261
std::string as_startswith(const std::string& value);

/// Преобразует строку в match паттерн
/// Возвращает nullopt для nested wildcards
/// Соответствует sigma.rs:214-231
std::optional<std::string> as_match(const std::string& value);

/// Преобразует строку в regex паттерн
/// "foobar" → "?foobar"
/// @param convert если true, конвертирует wildcard в regex
/// Соответствует sigma.rs:232-258
std::optional<std::string> as_regex(const std::string& value, bool convert);

// ============================================================================
// Base64 encoding
// ============================================================================

/// Кодирует строку в base64
std::string base64_encode(const std::string& value);

/// Кодирует строку в base64 с офсетами (для base64offset модификатора)
/// Возвращает 3 варианта для покрытия всех офсетов
std::vector<std::string> base64_offset_encode(const std::string& value);

// ============================================================================
// Modifier support
// ============================================================================

/// Поддерживаемые модификаторы
extern const std::unordered_set<std::string> SUPPORTED_MODIFIERS;

/// Проверяет поддержку модификатора
bool is_modifier_supported(const std::string& modifier);

/// Проверяет наличие неподдерживаемых модификаторов
/// Возвращает список неподдерживаемых модификаторов (или пустой список)
std::vector<std::string> get_unsupported_modifiers(
    const std::unordered_set<std::string>& modifiers);

// ============================================================================
// Condition checking
// ============================================================================

/// Проверяет, содержит ли условие неподдерживаемые элементы
/// Соответствует sigma.rs:182-197
bool is_condition_unsupported(const std::string& condition);

// ============================================================================
// Load function — main entry point
// ============================================================================

/// Загружает Sigma правило(а) из файла
/// Поддерживает одиночные правила и Rule Collections
/// Соответствует sigma.rs:773-868
LoadResult load(const std::filesystem::path& path);

}  // namespace chainsaw::rule::sigma

#endif  // CHAINSAW_SIGMA_HPP
