// ==============================================================================
// chainsaw/search.hpp - MOD-0003: Search Command Pipeline
// ==============================================================================
//
// SLICE-011: Search Command Implementation
// SPEC-SLICE-011: micro-spec поведения
//
// Назначение:
// - SearcherBuilder — builder pattern для создания Searcher
// - Searcher — поисковый движок (pattern + tau + time filtering)
// - SearchHits — итератор по результатам поиска
//
// Соответствие Rust:
// - upstream/chainsaw/src/search.rs:11-350
// - upstream/chainsaw/src/main.rs:873-1043 (search command)
//
// ==============================================================================

#ifndef CHAINSAW_SEARCH_HPP
#define CHAINSAW_SEARCH_HPP

#include <chainsaw/reader.hpp>
#include <chainsaw/tau.hpp>
#include <chainsaw/value.hpp>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace chainsaw::search {

// Forward declaration
using Value = chainsaw::Value;

// ============================================================================
// DateTime — NaiveDateTime аналог для time filtering
// ============================================================================

/// NaiveDateTime — дата и время без timezone
/// Соответствует chrono::NaiveDateTime в Rust
struct DateTime {
    int year = 0;
    int month = 1;        // 1-12
    int day = 1;          // 1-31
    int hour = 0;         // 0-23
    int minute = 0;       // 0-59
    int second = 0;       // 0-59
    int microsecond = 0;  // 0-999999

    /// Парсить datetime из строки
    /// SPEC-SLICE-011 FACT-009: %Y-%m-%dT%H:%M:%S%.fZ и %Y-%m-%dT%H:%M:%S
    static std::optional<DateTime> parse(std::string_view str);

    /// Сравнение
    bool operator<(const DateTime& other) const;
    bool operator<=(const DateTime& other) const;
    bool operator>(const DateTime& other) const;
    bool operator>=(const DateTime& other) const;
    bool operator==(const DateTime& other) const;

    /// Конвертировать в строку ISO 8601
    std::string to_string() const;
};

// ============================================================================
// Вспомогательные функции для regex matching
// ============================================================================

/// Проверить соответствие Value regex паттернам
/// SPEC-SLICE-011 FACT-004: поиск по JSON.to_string() with backslash normalization
/// @param value Документ для проверки
/// @param patterns Скомпилированные regex паттерны
/// @param match_any true = OR семантика, false = AND семантика
/// @return true если документ соответствует
bool value_matches_patterns(const Value& value, const std::vector<std::regex>& patterns,
                            bool match_any);

// ============================================================================
// SearchResult — результат поиска
// ============================================================================

/// Результат поиска — найденный документ
struct SearchResult {
    Value data;                              // Данные документа
    std::string source;                      // Путь к исходному файлу
    std::optional<std::uint64_t> record_id;  // ID записи (для EVTX)
    std::optional<std::string> timestamp;    // Timestamp (для EVTX)
};

// ============================================================================
// SearcherBuilder — builder pattern
// ============================================================================

class Searcher;  // forward declaration

/// Builder для создания Searcher
///
/// SPEC-SLICE-011 FACT-001: Searcher использует builder pattern
///
/// Использование:
/// @code
///   auto result = SearcherBuilder::create()
///       .patterns({"pattern1", "pattern2"})
///       .ignore_case(true)
///       .match_any(true)
///       .build();
///   if (result.ok) {
///       auto hits = result.searcher->search(path);
///       // ...
///   }
/// @endcode
class SearcherBuilder {
public:
    /// Создать builder
    static SearcherBuilder create();

    /// Установить текстовые паттерны (regex)
    /// SPEC-SLICE-011 FACT-002: компилируются в RegexSet
    SearcherBuilder& patterns(std::vector<std::string> patterns);

    /// Установить tau expressions (структурный поиск)
    /// SPEC-SLICE-011 FACT-003: парсятся через tau::parse_kv
    SearcherBuilder& tau(std::vector<std::string> expressions);

    /// Установить case-insensitive режим
    /// SPEC-SLICE-011 FACT-002: добавляет (?i) prefix
    SearcherBuilder& ignore_case(bool ignore);

    /// Установить OR семантику для множественных паттернов
    /// SPEC-SLICE-011 FACT-006/007: false=AND, true=OR
    SearcherBuilder& match_any(bool any);

    /// Установить начало временного диапазона
    /// SPEC-SLICE-011 FACT-008: требует timestamp поле
    SearcherBuilder& from(DateTime datetime);

    /// Установить конец временного диапазона
    /// SPEC-SLICE-011 FACT-008: требует timestamp поле
    SearcherBuilder& to(DateTime datetime);

    /// Установить имя поля timestamp
    /// SPEC-SLICE-011 FACT-008: --from/--to требуют --timestamp
    SearcherBuilder& timestamp(std::string field);

    /// Загружать файлы с неизвестным расширением
    SearcherBuilder& load_unknown(bool load);

    /// Пропускать ошибки чтения/парсинга
    SearcherBuilder& skip_errors(bool skip);

    /// Собрать Searcher
    /// @return Результат с Searcher или ошибкой
    struct BuildResult {
        bool ok = false;
        std::unique_ptr<Searcher> searcher;
        std::string error;
    };
    BuildResult build();

private:
    SearcherBuilder() = default;

    std::vector<std::string> patterns_;
    std::vector<std::string> tau_exprs_;
    bool ignore_case_ = false;
    bool match_any_ = false;
    std::optional<DateTime> from_;
    std::optional<DateTime> to_;
    std::optional<std::string> timestamp_;
    bool load_unknown_ = false;
    bool skip_errors_ = false;
};

// ============================================================================
// Searcher — поисковый движок
// ============================================================================

/// Поисковый движок
///
/// SPEC-SLICE-011: выполняет поиск по документам
class Searcher {
public:
    /// Builder factory
    static SearcherBuilder builder() { return SearcherBuilder::create(); }

    /// Выполнить поиск по файлу
    /// @param path Путь к файлу
    /// @return Вектор найденных документов
    ///
    /// SPEC-SLICE-011: итерирует по документам, проверяет соответствие
    std::vector<SearchResult> search(const std::filesystem::path& path) const;

    /// Проверить соответствие одного документа
    /// @param doc Документ для проверки
    /// @return true если документ соответствует критериям поиска
    bool matches(const io::Document& doc) const;

    /// Проверить, есть ли regex паттерны
    bool has_patterns() const { return !regex_patterns_.empty(); }

    /// Проверить, есть ли tau выражения
    bool has_tau() const { return tau_expression_.has_value(); }

    /// Проверить, есть ли time filtering
    bool has_time_filter() const {
        return timestamp_.has_value() && (from_.has_value() || to_.has_value());
    }

    /// Getter для load_unknown
    bool load_unknown() const { return load_unknown_; }

    /// Getter для skip_errors
    bool skip_errors() const { return skip_errors_; }

private:
    friend class SearcherBuilder;

    Searcher() = default;

    /// Проверить соответствие regex паттернам
    /// SPEC-SLICE-011 FACT-004: JSON.to_string() with backslash normalization
    bool matches_patterns(const Value& value) const;

    /// Проверить соответствие tau выражению
    /// SPEC-SLICE-011 FACT-005: tau_engine::core::solve()
    bool matches_tau(const Value& value) const;

    /// Проверить соответствие временному диапазону
    /// SPEC-SLICE-011 FACT-008-010
    bool matches_time_filter(const Value& value) const;

    // Скомпилированные regex паттерны
    std::vector<std::regex> regex_patterns_;

    // Tau expression (combined AND/OR)
    std::optional<tau::Expression> tau_expression_;

    // Time filtering
    std::optional<std::string> timestamp_;
    std::optional<DateTime> from_;
    std::optional<DateTime> to_;

    // Flags
    bool match_any_ = false;
    bool load_unknown_ = false;
    bool skip_errors_ = false;
};

// ============================================================================
// Вспомогательные функции
// ============================================================================

/// Нормализовать JSON строку для regex поиска
/// SPEC-SLICE-011 FACT-004: normalizes escaped backslashes
std::string normalize_json_for_search(const Value& value);

/// Извлечь timestamp из документа по имени поля
/// SPEC-SLICE-011 FACT-009: поддерживает dot-notation
std::optional<DateTime> extract_timestamp(const Value& value, std::string_view field);

}  // namespace chainsaw::search

#endif  // CHAINSAW_SEARCH_HPP
