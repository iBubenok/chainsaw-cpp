// ==============================================================================
// tau.cpp - MOD-0009: Tau Engine Implementation
// ==============================================================================
//
// SLICE-008: Tau Engine
// SPEC-SLICE-008: micro-spec поведения
//
// ==============================================================================

#include <algorithm>
#include <cctype>
#include <chainsaw/tau.hpp>
#include <charconv>
#include <cstring>
#include <sstream>

namespace chainsaw::tau {

// ============================================================================
// Utility functions
// ============================================================================

std::string ascii_lowercase(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool icontains(std::string_view haystack, std::string_view needle) {
    if (needle.empty())
        return true;
    if (haystack.size() < needle.size())
        return false;

    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](char a, char b) {
                              return std::tolower(static_cast<unsigned char>(a)) ==
                                     std::tolower(static_cast<unsigned char>(b));
                          });
    return it != haystack.end();
}

bool istarts_with(std::string_view str, std::string_view prefix) {
    if (str.size() < prefix.size())
        return false;
    return iequals(str.substr(0, prefix.size()), prefix);
}

bool iends_with(std::string_view str, std::string_view suffix) {
    if (str.size() < suffix.size())
        return false;
    return iequals(str.substr(str.size() - suffix.size()), suffix);
}

// ============================================================================
// Deep copy (clone)
// ============================================================================

namespace {

Pattern clone_pattern(const Pattern& p) {
    return std::visit(
        [](const auto& pat) -> Pattern {
            using T = std::decay_t<decltype(pat)>;
            if constexpr (std::is_same_v<T, PatternRegex>) {
                PatternRegex result;
                result.pattern = pat.pattern;
                result.regex = std::regex(pat.pattern, std::regex::ECMAScript);
                return result;
            } else {
                return pat;
            }
        },
        p);
}

Search clone_search(const Search& s) {
    return std::visit(
        [](const auto& search) -> Search {
            using T = std::decay_t<decltype(search)>;
            if constexpr (std::is_same_v<T, SearchRegex>) {
                SearchRegex result;
                result.pattern = search.pattern;
                result.ignore_case = search.ignore_case;
                auto flags = std::regex::ECMAScript;
                if (search.ignore_case) {
                    flags |= std::regex::icase;
                }
                result.regex = std::regex(search.pattern, flags);
                return result;
            } else {
                return search;
            }
        },
        s);
}

}  // namespace

Expression clone(const Expression& expr) {
    return std::visit(
        [](const auto& e) -> Expression {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, ExprBooleanGroup>) {
                ExprBooleanGroup result;
                result.op = e.op;
                for (const auto& child : e.expressions) {
                    result.expressions.push_back(clone(child));
                }
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprBooleanExpression>) {
                ExprBooleanExpression result;
                result.left = std::make_unique<Expression>(clone(*e.left));
                result.op = e.op;
                result.right = std::make_unique<Expression>(clone(*e.right));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprNegate>) {
                ExprNegate result;
                result.inner = std::make_unique<Expression>(clone(*e.inner));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprNested>) {
                ExprNested result;
                result.field = e.field;
                result.inner = std::make_unique<Expression>(clone(*e.inner));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                ExprMatch result;
                result.pattern = clone_pattern(e.pattern);
                result.inner = std::make_unique<Expression>(clone(*e.inner));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprSearch>) {
                ExprSearch result;
                result.search = clone_search(e.search);
                result.field = e.field;
                result.cast_to_str = e.cast_to_str;
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprMatrix>) {
                ExprMatrix result;
                result.fields = e.fields;
                for (const auto& [patterns, flag] : e.rows) {
                    std::vector<Pattern> cloned_patterns;
                    for (const auto& p : patterns) {
                        cloned_patterns.push_back(clone_pattern(p));
                    }
                    result.rows.emplace_back(std::move(cloned_patterns), flag);
                }
                return Expression(std::move(result));
            } else {
                // Simple types: Field, Cast, Boolean, Float, Integer, Null, Identifier
                return Expression(e);
            }
        },
        expr.data);
}

// ============================================================================
// ValueDocument
// ============================================================================

std::optional<Value> ValueDocument::find(std::string_view key) const {
    if (!value_.is_object()) {
        return std::nullopt;
    }

    // Поддержка dot-notation: a.b.c
    std::string_view remaining = key;
    const Value* current = &value_;

    while (!remaining.empty()) {
        auto dot_pos = remaining.find('.');
        std::string_view part;
        if (dot_pos == std::string_view::npos) {
            part = remaining;
            remaining = {};
        } else {
            part = remaining.substr(0, dot_pos);
            remaining = remaining.substr(dot_pos + 1);
        }

        if (!current->is_object()) {
            return std::nullopt;
        }

        std::string key_str(part);
        const Value* found = current->get(key_str);
        if (!found) {
            return std::nullopt;
        }

        if (remaining.empty()) {
            return *found;
        }

        current = found;
    }

    return std::nullopt;
}

// ============================================================================
// Solver - Pattern matching
// ============================================================================

namespace {

// Преобразование Value в строку
std::string value_to_string(const Value& v) {
    if (v.is_string()) {
        return v.as_string();
    } else if (v.is_bool()) {
        return v.as_bool() ? "true" : "false";
    } else if (v.is_int()) {
        return std::to_string(v.as_int());
    } else if (v.is_uint()) {
        return std::to_string(v.as_uint());
    } else if (v.is_double()) {
        std::ostringstream oss;
        oss << v.as_double();
        return oss.str();
    } else if (v.is_null()) {
        return "null";
    }
    return "";
}

// Преобразование Value в int64
std::optional<std::int64_t> value_to_int(const Value& v) {
    if (v.is_int()) {
        return v.as_int();
    } else if (v.is_uint()) {
        auto u = v.as_uint();
        if (u <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return static_cast<std::int64_t>(u);
        }
    } else if (v.is_string()) {
        std::int64_t result;
        auto [ptr, ec] = std::from_chars(v.as_string().data(),
                                         v.as_string().data() + v.as_string().size(), result);
        if (ec == std::errc{} && ptr == v.as_string().data() + v.as_string().size()) {
            return result;
        }
    }
    return std::nullopt;
}

// Преобразование Value в double
std::optional<double> value_to_double(const Value& v) {
    if (v.is_double()) {
        return v.as_double();
    } else if (v.is_int()) {
        return static_cast<double>(v.as_int());
    } else if (v.is_uint()) {
        return static_cast<double>(v.as_uint());
    } else if (v.is_string()) {
        try {
            return std::stod(v.as_string());
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// Применить Pattern к Value
bool match_pattern(const Pattern& pattern, const Value& value) {
    return std::visit(
        [&value](const auto& pat) -> bool {
            using T = std::decay_t<decltype(pat)>;

            if constexpr (std::is_same_v<T, PatternEqual>) {
                auto v = value_to_int(value);
                return v && *v == pat.value;
            } else if constexpr (std::is_same_v<T, PatternGreaterThan>) {
                auto v = value_to_int(value);
                return v && *v > pat.value;
            } else if constexpr (std::is_same_v<T, PatternGreaterThanOrEqual>) {
                auto v = value_to_int(value);
                return v && *v >= pat.value;
            } else if constexpr (std::is_same_v<T, PatternLessThan>) {
                auto v = value_to_int(value);
                return v && *v < pat.value;
            } else if constexpr (std::is_same_v<T, PatternLessThanOrEqual>) {
                auto v = value_to_int(value);
                return v && *v <= pat.value;
            } else if constexpr (std::is_same_v<T, PatternFEqual>) {
                auto v = value_to_double(value);
                return v && *v == pat.value;
            } else if constexpr (std::is_same_v<T, PatternFGreaterThan>) {
                auto v = value_to_double(value);
                return v && *v > pat.value;
            } else if constexpr (std::is_same_v<T, PatternFGreaterThanOrEqual>) {
                auto v = value_to_double(value);
                return v && *v >= pat.value;
            } else if constexpr (std::is_same_v<T, PatternFLessThan>) {
                auto v = value_to_double(value);
                return v && *v < pat.value;
            } else if constexpr (std::is_same_v<T, PatternFLessThanOrEqual>) {
                auto v = value_to_double(value);
                return v && *v <= pat.value;
            } else if constexpr (std::is_same_v<T, PatternAny>) {
                return !value.is_null();
            } else if constexpr (std::is_same_v<T, PatternRegex>) {
                std::string str = value_to_string(value);
                return std::regex_search(str, pat.regex);
            } else if constexpr (std::is_same_v<T, PatternContains>) {
                std::string str = value_to_string(value);
                return str.find(pat.value) != std::string::npos;
            } else if constexpr (std::is_same_v<T, PatternEndsWith>) {
                std::string str = value_to_string(value);
                if (str.size() < pat.value.size())
                    return false;
                return str.compare(str.size() - pat.value.size(), pat.value.size(), pat.value) == 0;
            } else if constexpr (std::is_same_v<T, PatternExact>) {
                std::string str = value_to_string(value);
                return str == pat.value;
            } else if constexpr (std::is_same_v<T, PatternStartsWith>) {
                std::string str = value_to_string(value);
                return str.compare(0, pat.value.size(), pat.value) == 0;
            } else {
                return false;
            }
        },
        pattern);
}

// Применить Search к строке
bool match_search(const Search& search, std::string_view value_str) {
    return std::visit(
        [&value_str](const auto& s) -> bool {
            using T = std::decay_t<decltype(s)>;

            if constexpr (std::is_same_v<T, SearchAny>) {
                return true;  // поле существует
            } else if constexpr (std::is_same_v<T, SearchRegex>) {
                return std::regex_search(value_str.begin(), value_str.end(), s.regex);
            } else if constexpr (std::is_same_v<T, SearchAhoCorasick>) {
                // Реализация через простые string операции с case folding
                for (const auto& mt : s.match_types) {
                    bool matched = false;
                    switch (mt.type) {
                    case MatchType::Contains:
                        matched = s.ignore_case
                                      ? icontains(value_str, mt.value)
                                      : (value_str.find(mt.value) != std::string_view::npos);
                        break;
                    case MatchType::EndsWith:
                        matched = s.ignore_case
                                      ? iends_with(value_str, mt.value)
                                      : (value_str.size() >= mt.value.size() &&
                                         value_str.compare(value_str.size() - mt.value.size(),
                                                           mt.value.size(), mt.value) == 0);
                        break;
                    case MatchType::Exact:
                        matched =
                            s.ignore_case ? iequals(value_str, mt.value) : (value_str == mt.value);
                        break;
                    case MatchType::StartsWith:
                        matched = s.ignore_case
                                      ? istarts_with(value_str, mt.value)
                                      : (value_str.compare(0, mt.value.size(), mt.value) == 0);
                        break;
                    }
                    if (matched)
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<T, SearchContains>) {
                return value_str.find(s.value) != std::string_view::npos;
            } else if constexpr (std::is_same_v<T, SearchEndsWith>) {
                if (value_str.size() < s.value.size())
                    return false;
                return value_str.compare(value_str.size() - s.value.size(), s.value.size(),
                                         s.value) == 0;
            } else if constexpr (std::is_same_v<T, SearchExact>) {
                return value_str == s.value;
            } else if constexpr (std::is_same_v<T, SearchStartsWith>) {
                return value_str.compare(0, s.value.size(), s.value) == 0;
            } else {
                return false;
            }
        },
        search);
}

// Forward declaration
bool solve_expr(const Expression& expr, const Document& doc);

// Solve для одного Value (используется для array iteration)
// Зарезервировано для будущего использования
[[maybe_unused]] bool solve_value(const Expression& expr, const Value& value) {
    ValueDocument doc(value);
    return solve_expr(expr, doc);
}

// Solve для поля, включая iteration по массиву
bool solve_field_value(const Search& search, const Value& value, bool cast_to_str) {
    std::string str;
    if (cast_to_str || value.is_string()) {
        str = value_to_string(value);
    } else if (value.is_string()) {
        str = value.as_string();
    } else {
        str = value_to_string(value);
    }
    return match_search(search, str);
}

bool solve_search_on_value(const Search& search, const Value& value, bool cast_to_str) {
    // Если это массив, итерируем
    if (value.is_array()) {
        for (const auto& elem : value.as_array()) {
            if (solve_search_on_value(search, elem, cast_to_str)) {
                return true;
            }
        }
        return false;
    }

    return solve_field_value(search, value, cast_to_str);
}

// Основная рекурсивная функция solve
bool solve_expr(const Expression& expr, const Document& doc) {
    return std::visit(
        [&doc](const auto& e) -> bool {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, ExprBooleanGroup>) {
                if (e.op == BoolSym::And) {
                    for (const auto& child : e.expressions) {
                        if (!solve_expr(child, doc)) {
                            return false;
                        }
                    }
                    return true;
                } else if (e.op == BoolSym::Or) {
                    for (const auto& child : e.expressions) {
                        if (solve_expr(child, doc)) {
                            return true;
                        }
                    }
                    return false;
                }
                return false;
            } else if constexpr (std::is_same_v<T, ExprBooleanExpression>) {
                // Вычислить left и right, сравнить
                // Для сравнения нужно извлечь числовые значения
                auto get_number = [&doc](const Expression& exp) -> std::optional<double> {
                    if (auto* i = exp.get_int()) {
                        return static_cast<double>(i->value);
                    }
                    if (auto* f = exp.get_float()) {
                        return f->value;
                    }
                    if (auto* field = exp.get_field()) {
                        auto val = doc.find(field->name);
                        if (val) {
                            return value_to_double(*val);
                        }
                    }
                    if (auto* cast = std::get_if<ExprCast>(&exp.data)) {
                        auto val = doc.find(cast->field);
                        if (val) {
                            if (cast->mod == ModSym::Int) {
                                auto i = value_to_int(*val);
                                if (i)
                                    return static_cast<double>(*i);
                            } else if (cast->mod == ModSym::Flt) {
                                return value_to_double(*val);
                            }
                        }
                    }
                    return std::nullopt;
                };

                auto left_val = get_number(*e.left);
                auto right_val = get_number(*e.right);

                if (!left_val || !right_val) {
                    return false;
                }

                switch (e.op) {
                case BoolSym::Equal:
                    return *left_val == *right_val;
                case BoolSym::GreaterThan:
                    return *left_val > *right_val;
                case BoolSym::GreaterThanOrEqual:
                    return *left_val >= *right_val;
                case BoolSym::LessThan:
                    return *left_val < *right_val;
                case BoolSym::LessThanOrEqual:
                    return *left_val <= *right_val;
                default:
                    return false;
                }
            } else if constexpr (std::is_same_v<T, ExprNegate>) {
                return !solve_expr(*e.inner, doc);
            } else if constexpr (std::is_same_v<T, ExprField>) {
                auto val = doc.find(e.name);
                return val.has_value() && !val->is_null();
            } else if constexpr (std::is_same_v<T, ExprCast>) {
                auto val = doc.find(e.field);
                if (!val)
                    return false;

                switch (e.mod) {
                case ModSym::Int: {
                    auto i = value_to_int(*val);
                    return i.has_value();
                }
                case ModSym::Str:
                    return true;  // всё можно привести к строке
                case ModSym::Flt: {
                    auto f = value_to_double(*val);
                    return f.has_value();
                }
                }
                return false;
            } else if constexpr (std::is_same_v<T, ExprNested>) {
                auto val = doc.find(e.field);
                if (!val || !val->is_object())
                    return false;
                ValueDocument nested_doc(*val);
                return solve_expr(*e.inner, nested_doc);
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                // Вычислить inner и применить pattern
                if (auto* field = e.inner->get_field()) {
                    auto val = doc.find(field->name);
                    if (!val)
                        return false;

                    // Array iteration
                    if (val->is_array()) {
                        for (const auto& elem : val->as_array()) {
                            if (match_pattern(e.pattern, elem)) {
                                return true;
                            }
                        }
                        return false;
                    }
                    return match_pattern(e.pattern, *val);
                }
                return false;
            } else if constexpr (std::is_same_v<T, ExprSearch>) {
                auto val = doc.find(e.field);
                if (!val) {
                    // SearchAny требует существования поля
                    if (std::holds_alternative<SearchAny>(e.search)) {
                        return false;
                    }
                    return false;
                }
                return solve_search_on_value(e.search, *val, e.cast_to_str);
            } else if constexpr (std::is_same_v<T, ExprMatrix>) {
                // Matrix matching - все поля должны соответствовать одной из строк
                for (const auto& [patterns, match_all] : e.rows) {
                    if (patterns.size() != e.fields.size())
                        continue;

                    bool row_matched = true;
                    for (std::size_t i = 0; i < e.fields.size(); ++i) {
                        auto val = doc.find(e.fields[i]);
                        if (!val) {
                            row_matched = false;
                            break;
                        }
                        if (!match_pattern(patterns[i], *val)) {
                            row_matched = false;
                            break;
                        }
                    }
                    if (row_matched)
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<T, ExprBoolean>) {
                return e.value;
            } else if constexpr (std::is_same_v<T, ExprFloat>) {
                return true;  // literals are truthy
            } else if constexpr (std::is_same_v<T, ExprInteger>) {
                return true;  // literals are truthy
            } else if constexpr (std::is_same_v<T, ExprNull>) {
                return false;
            } else if constexpr (std::is_same_v<T, ExprIdentifier>) {
                // Identifiers должны быть разрешены через coalesce
                return false;
            } else {
                return false;
            }
        },
        expr.data);
}

}  // namespace

// ============================================================================
// Public Solver API
// ============================================================================

bool solve(const Detection& detection, const Document& document) {
    // Если есть identifiers, подставляем через coalesce
    if (!detection.identifiers.empty()) {
        Expression resolved = coalesce(clone(detection.expression), detection.identifiers);
        return solve_expr(resolved, document);
    }
    return solve_expr(detection.expression, document);
}

bool solve(const Expression& expression, const Document& document) {
    return solve_expr(expression, document);
}

// ============================================================================
// Parser
// ============================================================================

std::optional<IdentifierResult> parse_identifier_string(std::string_view str) {
    if (str.empty()) {
        return IdentifierResult{PatternExact{""}, false};
    }

    bool ignore_case = false;
    std::string_view remaining = str;

    // Проверка prefix 'i' для ignore_case
    if (remaining.size() >= 1 && remaining[0] == 'i') {
        // Проверяем, что это не просто строка начинающаяся с 'i'
        // 'i' prefix означает ignore_case если за ним следует wildcard или строка
        if (remaining.size() >= 2 && (remaining[1] == '*' || remaining[1] == '?' ||
                                      std::isalnum(static_cast<unsigned char>(remaining[1])))) {
            ignore_case = true;
            remaining = remaining.substr(1);
        }
    }

    // Regex pattern: ?...
    if (!remaining.empty() && remaining[0] == '?') {
        std::string pattern(remaining.substr(1));
        PatternRegex result;
        result.pattern = pattern;
        try {
            auto flags = std::regex::ECMAScript;
            if (ignore_case) {
                flags |= std::regex::icase;
            }
            result.regex = std::regex(pattern, flags);
        } catch (...) {
            return std::nullopt;
        }
        return IdentifierResult{std::move(result), ignore_case};
    }

    // Wildcard patterns
    bool starts_wild = !remaining.empty() && remaining[0] == '*';
    bool ends_wild = !remaining.empty() && remaining.back() == '*';

    if (starts_wild && ends_wild && remaining.size() >= 2) {
        // *value* -> Contains
        std::string value(remaining.substr(1, remaining.size() - 2));
        return IdentifierResult{PatternContains{std::move(value)}, ignore_case};
    } else if (starts_wild) {
        // *value -> EndsWith
        std::string value(remaining.substr(1));
        return IdentifierResult{PatternEndsWith{std::move(value)}, ignore_case};
    } else if (ends_wild) {
        // value* -> StartsWith
        std::string value(remaining.substr(0, remaining.size() - 1));
        return IdentifierResult{PatternStartsWith{std::move(value)}, ignore_case};
    } else {
        // Exact match
        std::string value(remaining);

        // Попробовать распарсить как число
        std::int64_t int_val;
        auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), int_val);
        if (ec == std::errc{} && ptr == value.data() + value.size()) {
            return IdentifierResult{PatternEqual{int_val}, ignore_case};
        }

        return IdentifierResult{PatternExact{std::move(value)}, ignore_case};
    }
}

std::optional<Pattern> parse_numeric(std::string_view str) {
    if (str.empty())
        return std::nullopt;

    // Проверка префиксов сравнения
    if (str.size() >= 2 && str[0] == '>') {
        if (str[1] == '=') {
            std::int64_t val;
            auto [ptr, ec] = std::from_chars(str.data() + 2, str.data() + str.size(), val);
            if (ec == std::errc{}) {
                return PatternGreaterThanOrEqual{val};
            }
        } else {
            std::int64_t val;
            auto [ptr, ec] = std::from_chars(str.data() + 1, str.data() + str.size(), val);
            if (ec == std::errc{}) {
                return PatternGreaterThan{val};
            }
        }
    } else if (str.size() >= 2 && str[0] == '<') {
        if (str[1] == '=') {
            std::int64_t val;
            auto [ptr, ec] = std::from_chars(str.data() + 2, str.data() + str.size(), val);
            if (ec == std::errc{}) {
                return PatternLessThanOrEqual{val};
            }
        } else {
            std::int64_t val;
            auto [ptr, ec] = std::from_chars(str.data() + 1, str.data() + str.size(), val);
            if (ec == std::errc{}) {
                return PatternLessThan{val};
            }
        }
    }

    // Простое число
    std::int64_t val;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), val);
    if (ec == std::errc{} && ptr == str.data() + str.size()) {
        return PatternEqual{val};
    }

    return std::nullopt;
}

Expression parse_field(std::string_view key) {
    // int(field)
    if (key.size() > 5 && key.substr(0, 4) == "int(" && key.back() == ')') {
        std::string field(key.substr(4, key.size() - 5));
        return Expression(ExprCast{std::move(field), ModSym::Int});
    }
    // str(field)
    if (key.size() > 5 && key.substr(0, 4) == "str(" && key.back() == ')') {
        std::string field(key.substr(4, key.size() - 5));
        return Expression(ExprCast{std::move(field), ModSym::Str});
    }
    // flt(field)
    if (key.size() > 5 && key.substr(0, 4) == "flt(" && key.back() == ')') {
        std::string field(key.substr(4, key.size() - 5));
        return Expression(ExprCast{std::move(field), ModSym::Flt});
    }

    return Expression(ExprField{std::string(key)});
}

std::optional<Expression> parse_kv(std::string_view kv) {
    auto colon_pos = kv.find(": ");
    if (colon_pos == std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view key = kv.substr(0, colon_pos);
    std::string_view value = kv.substr(colon_pos + 2);

    bool is_negated = false;
    bool cast_to_str = false;
    std::string field_name;

    // Парсинг ключа
    if (key.size() > 5 && key.substr(0, 4) == "int(" && key.back() == ')') {
        field_name = std::string(key.substr(4, key.size() - 5));
    } else if (key.size() > 5 && key.substr(0, 4) == "not(" && key.back() == ')') {
        is_negated = true;
        field_name = std::string(key.substr(4, key.size() - 5));
    } else if (key.size() > 5 && key.substr(0, 4) == "str(" && key.back() == ')') {
        cast_to_str = true;
        field_name = std::string(key.substr(4, key.size() - 5));
    } else {
        field_name = std::string(key);
    }

    // Парсинг значения
    std::string_view val_to_parse = value;
    if (!val_to_parse.empty() && val_to_parse[0] == '!') {
        is_negated = true;
        val_to_parse = val_to_parse.substr(1);
    }

    auto id_result = parse_identifier_string(val_to_parse);
    if (!id_result) {
        return std::nullopt;
    }

    Expression field_expr = parse_field(key);

    // Создаём Expression на основе Pattern
    Expression result = std::visit(
        [&](const auto& pat) -> Expression {
            using T = std::decay_t<decltype(pat)>;

            if constexpr (std::is_same_v<T, PatternEqual>) {
                ExprBooleanExpression be;
                be.left = std::make_unique<Expression>(std::move(field_expr));
                be.op = BoolSym::Equal;
                be.right = std::make_unique<Expression>(ExprInteger{pat.value});
                return Expression(std::move(be));
            } else if constexpr (std::is_same_v<T, PatternGreaterThan>) {
                ExprBooleanExpression be;
                be.left = std::make_unique<Expression>(std::move(field_expr));
                be.op = BoolSym::GreaterThan;
                be.right = std::make_unique<Expression>(ExprInteger{pat.value});
                return Expression(std::move(be));
            } else if constexpr (std::is_same_v<T, PatternGreaterThanOrEqual>) {
                ExprBooleanExpression be;
                be.left = std::make_unique<Expression>(std::move(field_expr));
                be.op = BoolSym::GreaterThanOrEqual;
                be.right = std::make_unique<Expression>(ExprInteger{pat.value});
                return Expression(std::move(be));
            } else if constexpr (std::is_same_v<T, PatternLessThan>) {
                ExprBooleanExpression be;
                be.left = std::make_unique<Expression>(std::move(field_expr));
                be.op = BoolSym::LessThan;
                be.right = std::make_unique<Expression>(ExprInteger{pat.value});
                return Expression(std::move(be));
            } else if constexpr (std::is_same_v<T, PatternLessThanOrEqual>) {
                ExprBooleanExpression be;
                be.left = std::make_unique<Expression>(std::move(field_expr));
                be.op = BoolSym::LessThanOrEqual;
                be.right = std::make_unique<Expression>(ExprInteger{pat.value});
                return Expression(std::move(be));
            } else if constexpr (std::is_same_v<T, PatternAny>) {
                ExprSearch es;
                es.search = SearchAny{};
                es.field = field_name;
                es.cast_to_str = cast_to_str;
                return Expression(std::move(es));
            } else if constexpr (std::is_same_v<T, PatternRegex>) {
                ExprSearch es;
                SearchRegex sr;
                sr.pattern = pat.pattern;
                sr.ignore_case = id_result->ignore_case;
                auto flags = std::regex::ECMAScript;
                if (id_result->ignore_case) {
                    flags |= std::regex::icase;
                }
                sr.regex = std::regex(pat.pattern, flags);
                es.search = std::move(sr);
                es.field = field_name;
                es.cast_to_str = cast_to_str;
                return Expression(std::move(es));
            } else if constexpr (std::is_same_v<T, PatternContains> ||
                                 std::is_same_v<T, PatternEndsWith> ||
                                 std::is_same_v<T, PatternExact> ||
                                 std::is_same_v<T, PatternStartsWith>) {
                ExprSearch es;
                if (id_result->ignore_case) {
                    // Используем AhoCorasick для case-insensitive
                    SearchAhoCorasick ac;
                    MatchTypeEntry entry;
                    if constexpr (std::is_same_v<T, PatternContains>) {
                        entry.type = MatchType::Contains;
                        entry.value = pat.value;
                    } else if constexpr (std::is_same_v<T, PatternEndsWith>) {
                        entry.type = MatchType::EndsWith;
                        entry.value = pat.value;
                    } else if constexpr (std::is_same_v<T, PatternExact>) {
                        entry.type = MatchType::Exact;
                        entry.value = pat.value;
                    } else {
                        entry.type = MatchType::StartsWith;
                        entry.value = pat.value;
                    }
                    ac.match_types.push_back(std::move(entry));
                    ac.ignore_case = true;
                    es.search = std::move(ac);
                } else {
                    // Case-sensitive
                    if constexpr (std::is_same_v<T, PatternContains>) {
                        es.search = SearchContains{pat.value};
                    } else if constexpr (std::is_same_v<T, PatternEndsWith>) {
                        es.search = SearchEndsWith{pat.value};
                    } else if constexpr (std::is_same_v<T, PatternExact>) {
                        es.search = SearchExact{pat.value};
                    } else {
                        es.search = SearchStartsWith{pat.value};
                    }
                }
                es.field = field_name;
                es.cast_to_str = cast_to_str;
                return Expression(std::move(es));
            } else {
                return Expression();
            }
        },
        id_result->pattern);

    if (is_negated) {
        ExprNegate neg;
        neg.inner = std::make_unique<Expression>(std::move(result));
        return Expression(std::move(neg));
    }

    return result;
}

// ============================================================================
// Optimiser
// ============================================================================

Expression coalesce(Expression expr,
                    const std::unordered_map<std::string, Expression>& identifiers) {
    return std::visit(
        [&identifiers](auto& e) -> Expression {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, ExprIdentifier>) {
                auto it = identifiers.find(e.name);
                if (it != identifiers.end()) {
                    // Рекурсивно разрешаем идентификатор
                    return coalesce(clone(it->second), identifiers);
                }
                return Expression(std::move(e));
            } else if constexpr (std::is_same_v<T, ExprBooleanGroup>) {
                ExprBooleanGroup result;
                result.op = e.op;
                for (auto& child : e.expressions) {
                    result.expressions.push_back(coalesce(std::move(child), identifiers));
                }
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprBooleanExpression>) {
                ExprBooleanExpression result;
                result.left =
                    std::make_unique<Expression>(coalesce(std::move(*e.left), identifiers));
                result.op = e.op;
                result.right =
                    std::make_unique<Expression>(coalesce(std::move(*e.right), identifiers));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprNegate>) {
                ExprNegate result;
                result.inner =
                    std::make_unique<Expression>(coalesce(std::move(*e.inner), identifiers));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprNested>) {
                ExprNested result;
                result.field = std::move(e.field);
                result.inner =
                    std::make_unique<Expression>(coalesce(std::move(*e.inner), identifiers));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                ExprMatch result;
                result.pattern = std::move(e.pattern);
                result.inner =
                    std::make_unique<Expression>(coalesce(std::move(*e.inner), identifiers));
                return Expression(std::move(result));
            } else {
                return Expression(std::move(e));
            }
        },
        expr.data);
}

Expression shake(Expression expr) {
    return std::visit(
        [](auto& e) -> Expression {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, ExprNegate>) {
                auto shaken = shake(std::move(*e.inner));

                // NOT NOT x -> x
                if (auto* neg = std::get_if<ExprNegate>(&shaken.data)) {
                    return shake(std::move(*neg->inner));
                }

                // NOT true -> false, NOT false -> true
                if (auto* b = std::get_if<ExprBoolean>(&shaken.data)) {
                    return Expression(ExprBoolean{!b->value});
                }

                ExprNegate result;
                result.inner = std::make_unique<Expression>(std::move(shaken));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprBooleanGroup>) {
                ExpressionVec new_exprs;
                for (auto& child : e.expressions) {
                    auto shaken = shake(std::move(child));

                    // Check for constant true/false
                    if (auto* b = std::get_if<ExprBoolean>(&shaken.data)) {
                        if (e.op == BoolSym::And) {
                            if (b->value)
                                continue;  // true AND x -> x
                            else
                                return Expression(ExprBoolean{false});  // false AND x -> false
                        } else if (e.op == BoolSym::Or) {
                            if (b->value)
                                return Expression(ExprBoolean{true});  // true OR x -> true
                            else
                                continue;  // false OR x -> x
                        }
                    }

                    // Flatten nested groups with same op
                    if (auto* nested = std::get_if<ExprBooleanGroup>(&shaken.data)) {
                        if (nested->op == e.op) {
                            for (auto& nested_child : nested->expressions) {
                                new_exprs.push_back(std::move(nested_child));
                            }
                            continue;
                        }
                    }

                    new_exprs.push_back(std::move(shaken));
                }

                // Single element -> unwrap
                if (new_exprs.size() == 1) {
                    return std::move(new_exprs[0]);
                }

                // Empty group
                if (new_exprs.empty()) {
                    return Expression(ExprBoolean{e.op == BoolSym::And});
                }

                ExprBooleanGroup result;
                result.op = e.op;
                result.expressions = std::move(new_exprs);
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprBooleanExpression>) {
                ExprBooleanExpression result;
                result.left = std::make_unique<Expression>(shake(std::move(*e.left)));
                result.op = e.op;
                result.right = std::make_unique<Expression>(shake(std::move(*e.right)));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprNested>) {
                ExprNested result;
                result.field = std::move(e.field);
                result.inner = std::make_unique<Expression>(shake(std::move(*e.inner)));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                ExprMatch result;
                result.pattern = std::move(e.pattern);
                result.inner = std::make_unique<Expression>(shake(std::move(*e.inner)));
                return Expression(std::move(result));
            } else {
                return Expression(std::move(e));
            }
        },
        expr.data);
}

Expression rewrite(Expression expr) {
    return std::visit(
        [](auto& e) -> Expression {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, ExprBooleanGroup>) {
                ExpressionVec new_exprs;
                for (auto& child : e.expressions) {
                    new_exprs.push_back(rewrite(std::move(child)));
                }

                // Sort for stability (by type index, then by content)
                std::sort(new_exprs.begin(), new_exprs.end(),
                          [](const Expression& a, const Expression& b) {
                              return a.data.index() < b.data.index();
                          });

                ExprBooleanGroup result;
                result.op = e.op;
                result.expressions = std::move(new_exprs);
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprBooleanExpression>) {
                ExprBooleanExpression result;
                result.left = std::make_unique<Expression>(rewrite(std::move(*e.left)));
                result.op = e.op;
                result.right = std::make_unique<Expression>(rewrite(std::move(*e.right)));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprNegate>) {
                ExprNegate result;
                result.inner = std::make_unique<Expression>(rewrite(std::move(*e.inner)));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprNested>) {
                ExprNested result;
                result.field = std::move(e.field);
                result.inner = std::make_unique<Expression>(rewrite(std::move(*e.inner)));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                ExprMatch result;
                result.pattern = std::move(e.pattern);
                result.inner = std::make_unique<Expression>(rewrite(std::move(*e.inner)));
                return Expression(std::move(result));
            } else {
                return Expression(std::move(e));
            }
        },
        expr.data);
}

Expression matrix(Expression expr) {
    // Matrix оптимизация: объединение нескольких Search на разных полях в Matrix
    // Для MVP просто возвращаем как есть
    return std::visit(
        [](auto& e) -> Expression {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, ExprBooleanGroup>) {
                ExpressionVec new_exprs;
                for (auto& child : e.expressions) {
                    new_exprs.push_back(matrix(std::move(child)));
                }

                ExprBooleanGroup result;
                result.op = e.op;
                result.expressions = std::move(new_exprs);
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprBooleanExpression>) {
                ExprBooleanExpression result;
                result.left = std::make_unique<Expression>(matrix(std::move(*e.left)));
                result.op = e.op;
                result.right = std::make_unique<Expression>(matrix(std::move(*e.right)));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprNegate>) {
                ExprNegate result;
                result.inner = std::make_unique<Expression>(matrix(std::move(*e.inner)));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprNested>) {
                ExprNested result;
                result.field = std::move(e.field);
                result.inner = std::make_unique<Expression>(matrix(std::move(*e.inner)));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                ExprMatch result;
                result.pattern = std::move(e.pattern);
                result.inner = std::make_unique<Expression>(matrix(std::move(*e.inner)));
                return Expression(std::move(result));
            } else {
                return Expression(std::move(e));
            }
        },
        expr.data);
}

// ============================================================================
// Field utilities
// ============================================================================

std::unordered_set<std::string> extract_fields(const Expression& expr) {
    std::unordered_set<std::string> result;

    std::visit(
        [&result](const auto& e) {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, ExprBooleanGroup>) {
                for (const auto& child : e.expressions) {
                    auto child_fields = extract_fields(child);
                    result.insert(child_fields.begin(), child_fields.end());
                }
            } else if constexpr (std::is_same_v<T, ExprBooleanExpression>) {
                auto left_fields = extract_fields(*e.left);
                auto right_fields = extract_fields(*e.right);
                result.insert(left_fields.begin(), left_fields.end());
                result.insert(right_fields.begin(), right_fields.end());
            } else if constexpr (std::is_same_v<T, ExprNegate>) {
                auto inner_fields = extract_fields(*e.inner);
                result.insert(inner_fields.begin(), inner_fields.end());
            } else if constexpr (std::is_same_v<T, ExprField>) {
                result.insert(e.name);
            } else if constexpr (std::is_same_v<T, ExprCast>) {
                result.insert(e.field);
            } else if constexpr (std::is_same_v<T, ExprNested>) {
                result.insert(e.field);
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                auto inner_fields = extract_fields(*e.inner);
                result.insert(inner_fields.begin(), inner_fields.end());
            } else if constexpr (std::is_same_v<T, ExprSearch>) {
                result.insert(e.field);
            } else if constexpr (std::is_same_v<T, ExprMatrix>) {
                result.insert(e.fields.begin(), e.fields.end());
            }
        },
        expr.data);

    return result;
}

Expression update_fields(Expression expr,
                         const std::unordered_map<std::string, std::string>& lookup) {
    return std::visit(
        [&lookup](auto& e) -> Expression {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, ExprBooleanGroup>) {
                ExpressionVec new_exprs;
                for (auto& child : e.expressions) {
                    new_exprs.push_back(update_fields(std::move(child), lookup));
                }
                ExprBooleanGroup result;
                result.op = e.op;
                result.expressions = std::move(new_exprs);
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprBooleanExpression>) {
                ExprBooleanExpression result;
                result.left =
                    std::make_unique<Expression>(update_fields(std::move(*e.left), lookup));
                result.op = e.op;
                result.right =
                    std::make_unique<Expression>(update_fields(std::move(*e.right), lookup));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprNegate>) {
                ExprNegate result;
                result.inner =
                    std::make_unique<Expression>(update_fields(std::move(*e.inner), lookup));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprField>) {
                auto it = lookup.find(e.name);
                if (it != lookup.end()) {
                    return Expression(ExprField{it->second});
                }
                return Expression(std::move(e));
            } else if constexpr (std::is_same_v<T, ExprCast>) {
                auto it = lookup.find(e.field);
                if (it != lookup.end()) {
                    return Expression(ExprCast{it->second, e.mod});
                }
                return Expression(std::move(e));
            } else if constexpr (std::is_same_v<T, ExprNested>) {
                auto it = lookup.find(e.field);
                std::string new_field = (it != lookup.end()) ? it->second : e.field;
                ExprNested result;
                result.field = std::move(new_field);
                result.inner =
                    std::make_unique<Expression>(update_fields(std::move(*e.inner), lookup));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                ExprMatch result;
                result.pattern = std::move(e.pattern);
                result.inner =
                    std::make_unique<Expression>(update_fields(std::move(*e.inner), lookup));
                return Expression(std::move(result));
            } else if constexpr (std::is_same_v<T, ExprSearch>) {
                auto it = lookup.find(e.field);
                if (it != lookup.end()) {
                    ExprSearch result;
                    result.search = std::move(e.search);
                    result.field = it->second;
                    result.cast_to_str = e.cast_to_str;
                    return Expression(std::move(result));
                }
                return Expression(std::move(e));
            } else if constexpr (std::is_same_v<T, ExprMatrix>) {
                std::vector<std::string> new_fields;
                for (const auto& f : e.fields) {
                    auto it = lookup.find(f);
                    new_fields.push_back(it != lookup.end() ? it->second : f);
                }
                ExprMatrix result;
                result.fields = std::move(new_fields);
                result.rows = std::move(e.rows);
                return Expression(std::move(result));
            } else {
                return Expression(std::move(e));
            }
        },
        expr.data);
}

// ============================================================================
// YAML Serialization
// ============================================================================

std::string pattern_to_string(const Pattern& pattern) {
    return std::visit(
        [](const auto& p) -> std::string {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, PatternEqual>) {
                return std::to_string(p.value);
            } else if constexpr (std::is_same_v<T, PatternGreaterThan>) {
                return ">" + std::to_string(p.value);
            } else if constexpr (std::is_same_v<T, PatternGreaterThanOrEqual>) {
                return ">=" + std::to_string(p.value);
            } else if constexpr (std::is_same_v<T, PatternLessThan>) {
                return "<" + std::to_string(p.value);
            } else if constexpr (std::is_same_v<T, PatternLessThanOrEqual>) {
                return "<=" + std::to_string(p.value);
            } else if constexpr (std::is_same_v<T, PatternFEqual>) {
                return std::to_string(p.value);
            } else if constexpr (std::is_same_v<T, PatternFGreaterThan>) {
                return ">" + std::to_string(p.value);
            } else if constexpr (std::is_same_v<T, PatternFGreaterThanOrEqual>) {
                return ">=" + std::to_string(p.value);
            } else if constexpr (std::is_same_v<T, PatternFLessThan>) {
                return "<" + std::to_string(p.value);
            } else if constexpr (std::is_same_v<T, PatternFLessThanOrEqual>) {
                return "<=" + std::to_string(p.value);
            } else if constexpr (std::is_same_v<T, PatternAny>) {
                return "*";
            } else if constexpr (std::is_same_v<T, PatternRegex>) {
                return "?" + p.pattern;
            } else if constexpr (std::is_same_v<T, PatternContains>) {
                return "*" + p.value + "*";
            } else if constexpr (std::is_same_v<T, PatternEndsWith>) {
                return "*" + p.value;
            } else if constexpr (std::is_same_v<T, PatternExact>) {
                return p.value;
            } else if constexpr (std::is_same_v<T, PatternStartsWith>) {
                return p.value + "*";
            } else {
                return "<unknown pattern>";
            }
        },
        pattern);
}

namespace {
std::string make_indent(int level) {
    return std::string(static_cast<std::size_t>(level) * 2, ' ');
}

// Forward declaration
void serialize_expr_to_stream(std::ostream& os, const Expression& expr, int indent);

void serialize_search_to_stream(std::ostream& os, const ExprSearch& search, int indent_level) {
    os << make_indent(indent_level) << search.field << ": ";

    std::visit(
        [&os, indent_level](const auto& s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, SearchAny>) {
                os << "*";
            } else if constexpr (std::is_same_v<T, SearchRegex>) {
                os << "?" << s.pattern;
            } else if constexpr (std::is_same_v<T, SearchAhoCorasick>) {
                // AhoCorasick with multiple patterns stored in match_types
                if (s.match_types.size() == 1) {
                    // Single pattern - inline
                    const auto& entry = s.match_types[0];
                    std::string prefix, suffix;
                    switch (entry.type) {
                    case MatchType::Contains:
                        prefix = "*";
                        suffix = "*";
                        break;
                    case MatchType::EndsWith:
                        prefix = "*";
                        suffix = "";
                        break;
                    case MatchType::Exact:
                        break;
                    case MatchType::StartsWith:
                        suffix = "*";
                        break;
                    }
                    if (s.ignore_case)
                        prefix = "i" + prefix;
                    os << prefix << entry.value << suffix;
                } else {
                    os << "\n";
                    for (const auto& entry : s.match_types) {
                        os << make_indent(indent_level + 1) << "- ";
                        std::string prefix, suffix;
                        switch (entry.type) {
                        case MatchType::Contains:
                            prefix = "*";
                            suffix = "*";
                            break;
                        case MatchType::EndsWith:
                            prefix = "*";
                            suffix = "";
                            break;
                        case MatchType::Exact:
                            break;
                        case MatchType::StartsWith:
                            suffix = "*";
                            break;
                        }
                        if (s.ignore_case)
                            prefix = "i" + prefix;
                        os << prefix << entry.value << suffix << "\n";
                    }
                    return;  // Already added newline
                }
            } else if constexpr (std::is_same_v<T, SearchContains>) {
                os << "*" << s.value << "*";
            } else if constexpr (std::is_same_v<T, SearchEndsWith>) {
                os << "*" << s.value;
            } else if constexpr (std::is_same_v<T, SearchExact>) {
                os << s.value;
            } else if constexpr (std::is_same_v<T, SearchStartsWith>) {
                os << s.value << "*";
            }
        },
        search.search);

    os << "\n";
}

void serialize_boolean_group(std::ostream& os, const ExprBooleanGroup& group, int indent) {
    // Collect all field:value pairs from AND groups for Sigma-like output
    if (group.op == BoolSym::And) {
        for (const auto& child : group.expressions) {
            serialize_expr_to_stream(os, child, indent);
        }
    } else {
        // OR group - more complex
        os << make_indent(indent) << "# OR group\n";
        for (const auto& child : group.expressions) {
            serialize_expr_to_stream(os, child, indent);
        }
    }
}

void serialize_expr_to_stream(std::ostream& os, const Expression& expr, int indent) {
    std::visit(
        [&os, indent](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, ExprBooleanGroup>) {
                serialize_boolean_group(os, e, indent);
            } else if constexpr (std::is_same_v<T, ExprSearch>) {
                serialize_search_to_stream(os, e, indent);
            } else if constexpr (std::is_same_v<T, ExprNegate>) {
                os << make_indent(indent) << "# NOT\n";
                serialize_expr_to_stream(os, *e.inner, indent + 1);
            } else if constexpr (std::is_same_v<T, ExprMatch>) {
                // Pattern match on inner expression
                serialize_expr_to_stream(os, *e.inner, indent);
            } else if constexpr (std::is_same_v<T, ExprField>) {
                os << make_indent(indent) << e.name << ": *\n";
            } else if constexpr (std::is_same_v<T, ExprBoolean>) {
                os << make_indent(indent) << (e.value ? "true" : "false") << "\n";
            } else if constexpr (std::is_same_v<T, ExprInteger>) {
                os << make_indent(indent) << e.value << "\n";
            } else if constexpr (std::is_same_v<T, ExprFloat>) {
                os << make_indent(indent) << e.value << "\n";
            } else if constexpr (std::is_same_v<T, ExprNull>) {
                os << make_indent(indent) << "null\n";
            } else if constexpr (std::is_same_v<T, ExprIdentifier>) {
                os << make_indent(indent) << "# identifier: " << e.name << "\n";
            } else if constexpr (std::is_same_v<T, ExprBooleanExpression>) {
                // Binary boolean expression
                os << make_indent(indent) << "# boolean expression\n";
                serialize_expr_to_stream(os, *e.left, indent + 1);
                serialize_expr_to_stream(os, *e.right, indent + 1);
            } else if constexpr (std::is_same_v<T, ExprCast>) {
                std::string cast_name = (e.mod == ModSym::Int)   ? "int"
                                        : (e.mod == ModSym::Str) ? "str"
                                                                 : "flt";
                os << make_indent(indent) << cast_name << "(" << e.field << ")\n";
            } else if constexpr (std::is_same_v<T, ExprNested>) {
                os << make_indent(indent) << e.field << ":\n";
                serialize_expr_to_stream(os, *e.inner, indent + 1);
            } else if constexpr (std::is_same_v<T, ExprMatrix>) {
                // Matrix - multi-field match
                os << make_indent(indent) << "# matrix (" << e.fields.size() << " fields)\n";
                for (size_t i = 0; i < e.rows.size(); ++i) {
                    const auto& [patterns, ignore_case] = e.rows[i];
                    os << make_indent(indent) << "- ";
                    for (size_t j = 0; j < patterns.size() && j < e.fields.size(); ++j) {
                        if (j > 0)
                            os << ", ";
                        os << e.fields[j] << "=" << pattern_to_string(patterns[j]);
                    }
                    os << "\n";
                }
            }
        },
        expr.data);
}
}  // namespace

std::string expression_to_yaml(const Expression& expr, int indent) {
    std::ostringstream os;
    serialize_expr_to_stream(os, expr, indent);
    return os.str();
}

std::string detection_to_yaml(const Detection& detection) {
    std::ostringstream os;

    // Output in Sigma-like format
    // After optimization, identifiers are empty and expression contains all logic

    if (!detection.identifiers.empty()) {
        // If identifiers present, output condition + identifiers
        os << "condition: ";

        // Build condition string from identifier names
        bool first = true;
        for (const auto& [name, _] : detection.identifiers) {
            if (!first)
                os << " and ";
            first = false;
            os << name;
        }
        os << "\n";

        // Output each identifier
        for (const auto& [name, expr] : detection.identifiers) {
            os << name << ":\n";
            os << expression_to_yaml(expr, 1);
        }
    } else {
        // After optimization - output expression as selection
        os << "condition: selection\n";
        os << "selection:\n";
        os << expression_to_yaml(detection.expression, 1);
    }

    return os.str();
}

}  // namespace chainsaw::tau
