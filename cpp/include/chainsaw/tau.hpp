// ==============================================================================
// chainsaw/tau.hpp - MOD-0009: Tau Engine (IR + Solver)
// ==============================================================================
//
// SLICE-008: Tau Engine Implementation
// SPEC-SLICE-008: micro-spec поведения
//
// Назначение:
// - Expression IR (intermediate representation) для правил
// - Solver для вычисления Expression против Document
// - Parser для разбора YAML/текста в Expression
// - Optimiser для оптимизации AST
//
// Соответствие Rust:
// - tau-engine crate v1.0 (external)
// - upstream/chainsaw/src/ext/tau.rs (extensions)
//
// ==============================================================================

#ifndef CHAINSAW_TAU_HPP
#define CHAINSAW_TAU_HPP

#include <algorithm>
#include <chainsaw/value.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace chainsaw::tau {

// ============================================================================
// Enums
// ============================================================================

/// Булевый оператор (AND/OR и сравнения)
/// Соответствует tau_engine::core::parser::BoolSym
enum class BoolSym {
    And,                 // AND группа
    Or,                  // OR группа
    Equal,               // ==
    GreaterThan,         // >
    GreaterThanOrEqual,  // >=
    LessThan,            // <
    LessThanOrEqual      // <=
};

/// Модификатор типа (cast)
/// Соответствует tau_engine::core::parser::ModSym
enum class ModSym {
    Int,  // cast to int
    Str,  // cast to string
    Flt   // cast to float
};

/// Тип совпадения для AhoCorasick
/// Соответствует tau_engine::core::parser::MatchType
enum class MatchType { Contains, EndsWith, Exact, StartsWith };

// ============================================================================
// Pattern - паттерны для сопоставления значений
// ============================================================================

/// Паттерн: точное равенство целому числу
struct PatternEqual {
    std::int64_t value;
};

/// Паттерн: больше целого числа
struct PatternGreaterThan {
    std::int64_t value;
};

/// Паттерн: больше или равно целому числу
struct PatternGreaterThanOrEqual {
    std::int64_t value;
};

/// Паттерн: меньше целого числа
struct PatternLessThan {
    std::int64_t value;
};

/// Паттерн: меньше или равно целому числу
struct PatternLessThanOrEqual {
    std::int64_t value;
};

/// Паттерн: точное равенство float
struct PatternFEqual {
    double value;
};

/// Паттерн: больше float
struct PatternFGreaterThan {
    double value;
};

/// Паттерн: больше или равно float
struct PatternFGreaterThanOrEqual {
    double value;
};

/// Паттерн: меньше float
struct PatternFLessThan {
    double value;
};

/// Паттерн: меньше или равно float
struct PatternFLessThanOrEqual {
    double value;
};

/// Паттерн: любое непустое значение
struct PatternAny {};

/// Паттерн: regex match
struct PatternRegex {
    std::regex regex;
    std::string pattern;  // оригинальный паттерн для копирования
};

/// Паттерн: substring contains
struct PatternContains {
    std::string value;
};

/// Паттерн: suffix match
struct PatternEndsWith {
    std::string value;
};

/// Паттерн: exact string match
struct PatternExact {
    std::string value;
};

/// Паттерн: prefix match
struct PatternStartsWith {
    std::string value;
};

/// Объединённый тип Pattern
using Pattern =
    std::variant<PatternEqual, PatternGreaterThan, PatternGreaterThanOrEqual, PatternLessThan,
                 PatternLessThanOrEqual, PatternFEqual, PatternFGreaterThan,
                 PatternFGreaterThanOrEqual, PatternFLessThan, PatternFLessThanOrEqual, PatternAny,
                 PatternRegex, PatternContains, PatternEndsWith, PatternExact, PatternStartsWith>;

// ============================================================================
// Search - стратегии поиска
// ============================================================================

/// Поиск: поле существует и не null
struct SearchAny {};

/// Поиск: regex match
struct SearchRegex {
    std::regex regex;
    std::string pattern;  // оригинальный паттерн для копирования
    bool ignore_case;
};

/// Тип совпадения с паттерном (для AhoCorasick)
struct MatchTypeEntry {
    MatchType type;
    std::string value;
};

/// Поиск: multi-pattern DFA (Aho-Corasick)
/// В C++ реализуем упрощённо через std::string операции с case folding
struct SearchAhoCorasick {
    std::vector<MatchTypeEntry> match_types;
    bool ignore_case;
};

/// Поиск: case-sensitive contains
struct SearchContains {
    std::string value;
};

/// Поиск: case-sensitive endswith
struct SearchEndsWith {
    std::string value;
};

/// Поиск: case-sensitive exact
struct SearchExact {
    std::string value;
};

/// Поиск: case-sensitive startswith
struct SearchStartsWith {
    std::string value;
};

/// Объединённый тип Search
using Search = std::variant<SearchAny, SearchRegex, SearchAhoCorasick, SearchContains,
                            SearchEndsWith, SearchExact, SearchStartsWith>;

// ============================================================================
// Expression - AST выражений
// ============================================================================

// Forward declarations
struct Expression;
using ExpressionPtr = std::unique_ptr<Expression>;
using ExpressionVec = std::vector<Expression>;

/// Boolean группа (AND/OR с множеством выражений)
struct ExprBooleanGroup {
    BoolSym op;
    ExpressionVec expressions;
};

/// Boolean выражение (left op right)
struct ExprBooleanExpression {
    ExpressionPtr left;
    BoolSym op;
    ExpressionPtr right;
};

/// Negation (NOT)
struct ExprNegate {
    ExpressionPtr inner;
};

/// Field access
struct ExprField {
    std::string name;
};

/// Cast (int(field), str(field), flt(field))
struct ExprCast {
    std::string field;
    ModSym mod;
};

/// Nested object access
struct ExprNested {
    std::string field;
    ExpressionPtr inner;
};

/// Pattern match
struct ExprMatch {
    Pattern pattern;
    ExpressionPtr inner;
};

/// Search (search strategy on field)
struct ExprSearch {
    Search search;
    std::string field;
    bool cast_to_str;
};

/// Matrix (multi-field match)
struct ExprMatrix {
    std::vector<std::string> fields;
    std::vector<std::pair<std::vector<Pattern>, bool>> rows;
};

/// Boolean literal
struct ExprBoolean {
    bool value;
};

/// Float literal
struct ExprFloat {
    double value;
};

/// Integer literal
struct ExprInteger {
    std::int64_t value;
};

/// Null literal
struct ExprNull {};

/// Identifier reference
struct ExprIdentifier {
    std::string name;
};

/// Expression variant
using ExpressionVariant =
    std::variant<ExprBooleanGroup, ExprBooleanExpression, ExprNegate, ExprField, ExprCast,
                 ExprNested, ExprMatch, ExprSearch, ExprMatrix, ExprBoolean, ExprFloat, ExprInteger,
                 ExprNull, ExprIdentifier>;

/// Expression - рекурсивный AST узел
struct Expression {
    ExpressionVariant data;

    // Конструкторы для удобства
    Expression() : data(ExprNull{}) {}

    explicit Expression(ExpressionVariant v) : data(std::move(v)) {}

    // Factory methods
    static Expression make_null() { return Expression(ExprNull{}); }
    static Expression make_bool(bool v) { return Expression(ExprBoolean{v}); }
    static Expression make_int(std::int64_t v) { return Expression(ExprInteger{v}); }
    static Expression make_float(double v) { return Expression(ExprFloat{v}); }
    static Expression make_field(std::string name) {
        return Expression(ExprField{std::move(name)});
    }
    static Expression make_identifier(std::string name) {
        return Expression(ExprIdentifier{std::move(name)});
    }

    // Type checks
    bool is_null() const { return std::holds_alternative<ExprNull>(data); }
    bool is_bool() const { return std::holds_alternative<ExprBoolean>(data); }
    bool is_int() const { return std::holds_alternative<ExprInteger>(data); }
    bool is_float() const { return std::holds_alternative<ExprFloat>(data); }
    bool is_field() const { return std::holds_alternative<ExprField>(data); }
    bool is_search() const { return std::holds_alternative<ExprSearch>(data); }
    bool is_boolean_group() const { return std::holds_alternative<ExprBooleanGroup>(data); }
    bool is_negate() const { return std::holds_alternative<ExprNegate>(data); }

    // Accessors
    const ExprBoolean* get_bool() const { return std::get_if<ExprBoolean>(&data); }
    const ExprInteger* get_int() const { return std::get_if<ExprInteger>(&data); }
    const ExprFloat* get_float() const { return std::get_if<ExprFloat>(&data); }
    const ExprField* get_field() const { return std::get_if<ExprField>(&data); }
    const ExprSearch* get_search() const { return std::get_if<ExprSearch>(&data); }
    const ExprBooleanGroup* get_boolean_group() const {
        return std::get_if<ExprBooleanGroup>(&data);
    }
    const ExprNegate* get_negate() const { return std::get_if<ExprNegate>(&data); }
    const ExprIdentifier* get_identifier() const { return std::get_if<ExprIdentifier>(&data); }
};

// ============================================================================
// Detection - правило обнаружения
// ============================================================================

/// Detection содержит expression и именованные identifiers
struct Detection {
    Expression expression;
    std::unordered_map<std::string, Expression> identifiers;
};

// ============================================================================
// Document - интерфейс документа для solver
// ============================================================================

/// Document trait - абстрактный интерфейс для поиска полей в документе
class Document {
public:
    virtual ~Document() = default;

    /// Найти значение по ключу (поддержка dot-notation)
    virtual std::optional<Value> find(std::string_view key) const = 0;
};

/// ValueDocument - Document wrapper для Value
class ValueDocument : public Document {
public:
    explicit ValueDocument(const Value& value) : value_(value) {}

    std::optional<Value> find(std::string_view key) const override;

private:
    const Value& value_;
};

// ============================================================================
// Solver - вычисление Expression против Document
// ============================================================================

/// Решить Detection против Document
bool solve(const Detection& detection, const Document& document);

/// Решить Expression против Document
bool solve(const Expression& expression, const Document& document);

// ============================================================================
// Parser - парсинг выражений
// ============================================================================

/// Результат парсинга identifier
struct IdentifierResult {
    Pattern pattern;
    bool ignore_case;
};

/// Распарсить identifier string (например "i*value*", "?regex")
std::optional<IdentifierResult> parse_identifier_string(std::string_view str);

/// Распарсить numeric pattern (например "100", ">100", ">=100")
std::optional<Pattern> parse_numeric(std::string_view str);

/// Распарсить поле с возможным cast (например "int(field)", "str(field)")
Expression parse_field(std::string_view key);

/// Распарсить key-value пару (например "EventID: 4688")
std::optional<Expression> parse_kv(std::string_view kv);

// ============================================================================
// Optimiser - оптимизация AST
// ============================================================================

/// Подстановка identifiers в expression
Expression coalesce(Expression expr,
                    const std::unordered_map<std::string, Expression>& identifiers);

/// Tree shaking - удаление мёртвого кода
Expression shake(Expression expr);

/// Rewrite - переписывание для эффективности
Expression rewrite(Expression expr);

/// Matrix - объединение Search в Matrix
Expression matrix(Expression expr);

// ============================================================================
// Field utilities
// ============================================================================

/// Извлечь имена всех полей из Expression
std::unordered_set<std::string> extract_fields(const Expression& expr);

/// Заменить имена полей в Expression
Expression update_fields(Expression expr,
                         const std::unordered_map<std::string, std::string>& lookup);

// ============================================================================
// Utility functions
// ============================================================================

/// ASCII lowercase (для case-insensitive сравнения)
std::string ascii_lowercase(std::string_view str);

/// Case-insensitive string contains
bool icontains(std::string_view haystack, std::string_view needle);

/// Case-insensitive string equals
bool iequals(std::string_view a, std::string_view b);

/// Case-insensitive starts_with
bool istarts_with(std::string_view str, std::string_view prefix);

/// Case-insensitive ends_with
bool iends_with(std::string_view str, std::string_view suffix);

/// Глубокое копирование Expression
Expression clone(const Expression& expr);

// ============================================================================
// YAML Serialization - for lint --tau output
// ============================================================================

/// Сериализовать Pattern в строку (для YAML вывода)
std::string pattern_to_string(const Pattern& pattern);

/// Сериализовать Expression в YAML формат
/// @return YAML строка представляющая expression
std::string expression_to_yaml(const Expression& expr, int indent = 0);

/// Сериализовать Detection в YAML формат
/// Аналог serde_yaml::to_string(&detection) в Rust
/// @return YAML строка представляющая detection
std::string detection_to_yaml(const Detection& detection);

}  // namespace chainsaw::tau

#endif  // CHAINSAW_TAU_HPP
