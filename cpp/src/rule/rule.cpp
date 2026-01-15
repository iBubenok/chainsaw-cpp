// ==============================================================================
// rule.cpp - MOD-0008: Chainsaw Rules Loader Implementation
// ==============================================================================
//
// SLICE-009: Chainsaw Rules Loader
// SPEC-SLICE-009: micro-spec поведения
//
// Реализация загрузки и парсинга правил в формате Chainsaw YAML.
//
// ==============================================================================

#include <algorithm>
#include <cctype>
#include <chainsaw/rule.hpp>
#include <chainsaw/sigma.hpp>
#include <fstream>
#include <sstream>
#include <yaml-cpp/yaml.h>

// GCC 13 generates false positives for -Wnull-dereference when using
// std::visit on std::variant at high optimization levels.
// See: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=108842
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif

namespace chainsaw::rule {

// ============================================================================
// Error formatting
// ============================================================================

std::string Error::format() const {
    std::ostringstream oss;
    oss << "rule error";
    if (!path.empty()) {
        oss << " [" << path << "]";
    }
    oss << ": " << message;
    return oss.str();
}

// ============================================================================
// Kind/Level/Status string conversion
// ============================================================================

std::string to_string(Kind k) {
    switch (k) {
    case Kind::Chainsaw:
        return "chainsaw";
    case Kind::Sigma:
        return "sigma";
    }
    return "unknown";
}

std::string to_string(Level l) {
    switch (l) {
    case Level::Critical:
        return "critical";
    case Level::High:
        return "high";
    case Level::Medium:
        return "medium";
    case Level::Low:
        return "low";
    case Level::Info:
        return "info";
    }
    return "unknown";
}

std::string to_string(Status s) {
    switch (s) {
    case Status::Stable:
        return "stable";
    case Status::Experimental:
        return "experimental";
    }
    return "unknown";
}

Kind parse_kind(std::string_view s) {
    if (s == "chainsaw")
        return Kind::Chainsaw;
    if (s == "sigma")
        return Kind::Sigma;
    throw std::invalid_argument("unknown kind, must be: chainsaw, or sigma");
}

Level parse_level(std::string_view s) {
    if (s == "critical")
        return Level::Critical;
    if (s == "high")
        return Level::High;
    if (s == "medium")
        return Level::Medium;
    if (s == "low")
        return Level::Low;
    if (s == "info")
        return Level::Info;
    throw std::invalid_argument("unknown level, must be: critical, high, medium, low or info");
}

Status parse_status(std::string_view s) {
    if (s == "stable")
        return Status::Stable;
    if (s == "experimental")
        return Status::Experimental;
    throw std::invalid_argument("unknown status, must be: stable or experimental");
}

// ============================================================================
// Parse helpers
// ============================================================================

tau::Expression parse_field(std::string_view key) {
    // SPEC-SLICE-009 FACT-002: int(field) -> Cast(Int), str(field) -> Cast(Str)
    const std::string_view int_prefix = "int(";
    const std::string_view str_prefix = "str(";
    const std::string_view flt_prefix = "flt(";

    if (key.size() > 5 && key.substr(0, 4) == int_prefix && key.back() == ')') {
        std::string field_name(key.substr(4, key.size() - 5));
        return tau::Expression(tau::ExprCast{field_name, tau::ModSym::Int});
    }
    if (key.size() > 5 && key.substr(0, 4) == str_prefix && key.back() == ')') {
        std::string field_name(key.substr(4, key.size() - 5));
        return tau::Expression(tau::ExprCast{field_name, tau::ModSym::Str});
    }
    if (key.size() > 5 && key.substr(0, 4) == flt_prefix && key.back() == ')') {
        std::string field_name(key.substr(4, key.size() - 5));
        return tau::Expression(tau::ExprCast{field_name, tau::ModSym::Flt});
    }

    return tau::Expression::make_field(std::string(key));
}

std::optional<tau::Pattern> parse_numeric(std::string_view str) {
    // SPEC-SLICE-009: соответствует ext/tau.rs:18-36
    // Форматы: "100", ">100", ">=100", "<100", "<=100"

    if (str.empty())
        return std::nullopt;

    auto try_parse_int = [](std::string_view s) -> std::optional<std::int64_t> {
        if (s.empty())
            return std::nullopt;
        try {
            std::size_t pos = 0;
            std::int64_t val = std::stoll(std::string(s), &pos);
            if (pos == s.size())
                return val;
        } catch (...) {
            // ignore
        }
        return std::nullopt;
    };

    // Exact number
    if (auto val = try_parse_int(str)) {
        return tau::PatternEqual{*val};
    }

    // >=
    if (str.size() > 2 && str.substr(0, 2) == ">=") {
        if (auto val = try_parse_int(str.substr(2))) {
            return tau::PatternGreaterThanOrEqual{*val};
        }
    }

    // <=
    if (str.size() > 2 && str.substr(0, 2) == "<=") {
        if (auto val = try_parse_int(str.substr(2))) {
            return tau::PatternLessThanOrEqual{*val};
        }
    }

    // >
    if (str.size() > 1 && str[0] == '>') {
        if (auto val = try_parse_int(str.substr(1))) {
            return tau::PatternGreaterThan{*val};
        }
    }

    // <
    if (str.size() > 1 && str[0] == '<') {
        if (auto val = try_parse_int(str.substr(1))) {
            return tau::PatternLessThan{*val};
        }
    }

    return std::nullopt;
}

// ============================================================================
// YAML parsing helpers
// ============================================================================

namespace {

io::DocumentKind parse_document_kind(const std::string& s) {
    if (s == "evtx")
        return io::DocumentKind::Evtx;
    if (s == "mft")
        return io::DocumentKind::Mft;
    if (s == "json")
        return io::DocumentKind::Json;
    if (s == "jsonl")
        return io::DocumentKind::Jsonl;
    if (s == "xml")
        return io::DocumentKind::Xml;
    if (s == "hve")
        return io::DocumentKind::Hve;
    if (s == "esedb")
        return io::DocumentKind::Esedb;
    return io::DocumentKind::Unknown;
}

// Parse Container from YAML node
std::optional<Container> parse_container(const YAML::Node& node) {
    if (!node.IsDefined() || node.IsNull()) {
        return std::nullopt;
    }

    Container container;
    container.field = node["field"].as<std::string>("");

    std::string format = node["format"].as<std::string>("json");
    if (format == "json") {
        container.format = ContainerFormat::Json;
    } else if (format == "kv") {
        container.format = ContainerFormat::Kv;
        KvFormat kv;
        kv.delimiter = node["delimiter"].as<std::string>("=");
        kv.separator = node["separator"].as<std::string>(",");
        kv.trim = node["trim"].as<bool>(false);
        container.kv_params = kv;
    }

    return container;
}

// Parse Field from YAML node
// SPEC-SLICE-009 FACT-002: Field deserialization
Field parse_field_yaml(const YAML::Node& node) {
    Field field;

    std::optional<std::string> name_opt;
    std::optional<std::string> from_opt;
    std::optional<std::string> to_opt;
    bool visible = true;
    std::optional<Container> container_opt;
    std::optional<tau::ModSym> cast_opt;

    if (node["name"]) {
        name_opt = node["name"].as<std::string>();
    }
    if (node["from"]) {
        from_opt = node["from"].as<std::string>();
    }
    if (node["to"]) {
        std::string to_str = node["to"].as<std::string>();

        // Parse cast from 'to' field: int(field), str(field)
        auto expr = parse_field(to_str);
        if (auto* cast_expr = std::get_if<tau::ExprCast>(&expr.data)) {
            to_opt = cast_expr->field;
            cast_opt = cast_expr->mod;
        } else if (auto* field_expr = std::get_if<tau::ExprField>(&expr.data)) {
            to_opt = field_expr->name;
        }
    }
    if (node["visible"]) {
        visible = node["visible"].as<bool>();
    }
    if (node["container"]) {
        container_opt = parse_container(node["container"]);
    }

    // SPEC-SLICE-009 FACT-002: cast and container are mutually exclusive
    if (cast_opt && container_opt) {
        throw std::runtime_error("cast and container are mutually exclusive");
    }

    // Determine name/from/to based on what's provided
    if (!from_opt && !to_opt) {
        // Only name specified: from = to = name
        if (!name_opt) {
            throw std::runtime_error("field must have at least 'name' or 'to'");
        }
        field.name = *name_opt;
        field.from = *name_opt;
        field.to = *name_opt;
    } else {
        // to is required
        if (!to_opt) {
            throw std::runtime_error("field must have 'to' when 'from' is specified");
        }
        field.to = *to_opt;
        field.name = name_opt.value_or(*to_opt);
        field.from = from_opt.value_or(*to_opt);
    }

    field.cast = cast_opt;
    field.container = container_opt;
    field.visible = visible;

    return field;
}

// Parse Aggregate from YAML node
std::optional<Aggregate> parse_aggregate(const YAML::Node& node) {
    if (!node.IsDefined() || node.IsNull()) {
        return std::nullopt;
    }

    Aggregate agg;

    // Parse count pattern
    std::string count_str = node["count"].as<std::string>("");
    if (auto pattern = parse_numeric(count_str)) {
        agg.count = *pattern;
    } else {
        agg.count = tau::PatternEqual{1};  // default
    }

    // Parse fields
    if (node["fields"] && node["fields"].IsSequence()) {
        for (const auto& f : node["fields"]) {
            agg.fields.push_back(f.as<std::string>());
        }
    }

    return agg;
}

// Forward declaration for Filter parsing
tau::Detection parse_yaml_detection(const YAML::Node& node);

// Parse identifier pattern from YAML value
// SPEC-SLICE-009: соответствует parse_identifier в tau-engine
tau::Expression parse_yaml_identifier_value(const YAML::Node& value, const std::string& field) {
    if (value.IsNull()) {
        // Empty string match
        return tau::Expression(tau::ExprSearch{tau::SearchExact{""}, field, false});
    }

    if (value.IsScalar()) {
        std::string val = value.as<std::string>();

        // Check for numeric patterns
        if (auto pattern = parse_numeric(val)) {
            // Create comparison expression
            return std::visit(
                [&field](auto&& p) -> tau::Expression {
                    using T = std::decay_t<decltype(p)>;
                    if constexpr (std::is_same_v<T, tau::PatternEqual>) {
                        return tau::Expression(tau::ExprBooleanExpression{
                            std::make_unique<tau::Expression>(tau::ExprField{field}),
                            tau::BoolSym::Equal,
                            std::make_unique<tau::Expression>(tau::ExprInteger{p.value})});
                    } else if constexpr (std::is_same_v<T, tau::PatternGreaterThan>) {
                        return tau::Expression(tau::ExprBooleanExpression{
                            std::make_unique<tau::Expression>(tau::ExprField{field}),
                            tau::BoolSym::GreaterThan,
                            std::make_unique<tau::Expression>(tau::ExprInteger{p.value})});
                    } else if constexpr (std::is_same_v<T, tau::PatternGreaterThanOrEqual>) {
                        return tau::Expression(tau::ExprBooleanExpression{
                            std::make_unique<tau::Expression>(tau::ExprField{field}),
                            tau::BoolSym::GreaterThanOrEqual,
                            std::make_unique<tau::Expression>(tau::ExprInteger{p.value})});
                    } else if constexpr (std::is_same_v<T, tau::PatternLessThan>) {
                        return tau::Expression(tau::ExprBooleanExpression{
                            std::make_unique<tau::Expression>(tau::ExprField{field}),
                            tau::BoolSym::LessThan,
                            std::make_unique<tau::Expression>(tau::ExprInteger{p.value})});
                    } else if constexpr (std::is_same_v<T, tau::PatternLessThanOrEqual>) {
                        return tau::Expression(tau::ExprBooleanExpression{
                            std::make_unique<tau::Expression>(tau::ExprField{field}),
                            tau::BoolSym::LessThanOrEqual,
                            std::make_unique<tau::Expression>(tau::ExprInteger{p.value})});
                    } else {
                        return tau::Expression(tau::ExprSearch{tau::SearchAny{}, field, false});
                    }
                },
                *pattern);
        }

        // Parse string pattern: "i*value*" for case-insensitive, "*value*" for contains, etc.
        bool ignore_case = false;
        std::string_view sv = val;

        if (!sv.empty() && sv[0] == 'i') {
            ignore_case = true;
            sv = sv.substr(1);
        }

        // Check for wildcards
        bool starts_wild = !sv.empty() && sv[0] == '*';
        bool ends_wild = !sv.empty() && sv.back() == '*';

        std::string pattern_str;
        if (starts_wild && ends_wild && sv.size() >= 2) {
            pattern_str = std::string(sv.substr(1, sv.size() - 2));
        } else if (starts_wild) {
            pattern_str = std::string(sv.substr(1));
        } else if (ends_wild) {
            pattern_str = std::string(sv.substr(0, sv.size() - 1));
        } else {
            pattern_str = std::string(sv);
        }

        tau::Search search;
        if (starts_wild && ends_wild) {
            // Contains
            if (ignore_case) {
                search = tau::SearchAhoCorasick{{{tau::MatchType::Contains, pattern_str}}, true};
            } else {
                search = tau::SearchContains{pattern_str};
            }
        } else if (starts_wild) {
            // EndsWith
            if (ignore_case) {
                search = tau::SearchAhoCorasick{{{tau::MatchType::EndsWith, pattern_str}}, true};
            } else {
                search = tau::SearchEndsWith{pattern_str};
            }
        } else if (ends_wild) {
            // StartsWith
            if (ignore_case) {
                search = tau::SearchAhoCorasick{{{tau::MatchType::StartsWith, pattern_str}}, true};
            } else {
                search = tau::SearchStartsWith{pattern_str};
            }
        } else {
            // Exact match
            if (ignore_case) {
                search = tau::SearchAhoCorasick{{{tau::MatchType::Exact, pattern_str}}, true};
            } else {
                search = tau::SearchExact{pattern_str};
            }
        }

        return tau::Expression(tau::ExprSearch{std::move(search), field, false});
    }

    if (value.IsSequence()) {
        // OR of multiple values
        tau::ExpressionVec exprs;
        for (const auto& item : value) {
            exprs.push_back(parse_yaml_identifier_value(item, field));
        }
        if (exprs.size() == 1) {
            return std::move(exprs[0]);
        }
        return tau::Expression(tau::ExprBooleanGroup{tau::BoolSym::Or, std::move(exprs)});
    }

    // Default: any
    return tau::Expression(tau::ExprSearch{tau::SearchAny{}, field, false});
}

// Parse detection block (identifier) from YAML
tau::Expression parse_yaml_identifier_block(const YAML::Node& node) {
    if (!node.IsMap()) {
        throw std::runtime_error("identifier must be a map");
    }

    tau::ExpressionVec exprs;

    for (const auto& kv : node) {
        std::string field = kv.first.as<std::string>();
        tau::Expression expr = parse_yaml_identifier_value(kv.second, field);
        exprs.push_back(std::move(expr));
    }

    if (exprs.size() == 1) {
        return std::move(exprs[0]);
    }

    // AND of all conditions
    return tau::Expression(tau::ExprBooleanGroup{tau::BoolSym::And, std::move(exprs)});
}

// Parse condition expression (tokenize and build AST)
tau::Expression parse_condition_string(
    const std::string& condition,
    const std::unordered_map<std::string, tau::Expression>& identifiers) {
    // Simple condition parser
    // Supports: identifier, "and", "or", "not", parentheses

    // Tokenize
    std::vector<std::string> tokens;
    std::string current;

    for (size_t i = 0; i < condition.size(); ++i) {
        char c = condition[i];

        if (c == '(') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            tokens.emplace_back("(");
        } else if (c == ')') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            tokens.emplace_back(")");
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }

    // Recursive descent parser
    std::function<tau::Expression(size_t&)> parse_or;
    std::function<tau::Expression(size_t&)> parse_and;
    std::function<tau::Expression(size_t&)> parse_unary;
    std::function<tau::Expression(size_t&)> parse_primary;

    parse_primary = [&](size_t& pos) -> tau::Expression {
        if (pos >= tokens.size()) {
            return tau::Expression::make_bool(true);
        }

        const std::string& tok = tokens[pos];

        if (tok == "(") {
            pos++;
            auto expr = parse_or(pos);
            if (pos < tokens.size() && tokens[pos] == ")") {
                pos++;
            }
            return expr;
        }

        // Identifier
        pos++;
        auto it = identifiers.find(tok);
        if (it != identifiers.end()) {
            return tau::clone(it->second);
        }

        // Return identifier reference (will be resolved later)
        return tau::Expression::make_identifier(tok);
    };

    parse_unary = [&](size_t& pos) -> tau::Expression {
        if (pos < tokens.size() && tokens[pos] == "not") {
            pos++;
            auto expr = parse_unary(pos);
            return tau::Expression(
                tau::ExprNegate{std::make_unique<tau::Expression>(std::move(expr))});
        }
        return parse_primary(pos);
    };

    parse_and = [&](size_t& pos) -> tau::Expression {
        auto left = parse_unary(pos);
        while (pos < tokens.size() && tokens[pos] == "and") {
            pos++;
            auto right = parse_unary(pos);
            tau::ExpressionVec exprs;
            if (auto* grp = std::get_if<tau::ExprBooleanGroup>(&left.data)) {
                if (grp->op == tau::BoolSym::And) {
                    exprs = std::move(grp->expressions);
                } else {
                    exprs.push_back(std::move(left));
                }
            } else {
                exprs.push_back(std::move(left));
            }
            exprs.push_back(std::move(right));
            left = tau::Expression(tau::ExprBooleanGroup{tau::BoolSym::And, std::move(exprs)});
        }
        return left;
    };

    parse_or = [&](size_t& pos) -> tau::Expression {
        auto left = parse_and(pos);
        while (pos < tokens.size() && tokens[pos] == "or") {
            pos++;
            auto right = parse_and(pos);
            tau::ExpressionVec exprs;
            if (auto* grp = std::get_if<tau::ExprBooleanGroup>(&left.data)) {
                if (grp->op == tau::BoolSym::Or) {
                    exprs = std::move(grp->expressions);
                } else {
                    exprs.push_back(std::move(left));
                }
            } else {
                exprs.push_back(std::move(left));
            }
            exprs.push_back(std::move(right));
            left = tau::Expression(tau::ExprBooleanGroup{tau::BoolSym::Or, std::move(exprs)});
        }
        return left;
    };

    size_t pos = 0;
    return parse_or(pos);
}

// Parse Detection from YAML (filter block with condition)
tau::Detection parse_yaml_detection(const YAML::Node& node) {
    tau::Detection detection;

    std::string condition;
    if (node["condition"]) {
        condition = node["condition"].as<std::string>();
    }

    // Parse all identifier blocks
    for (const auto& kv : node) {
        std::string key = kv.first.as<std::string>();
        if (key == "condition")
            continue;

        tau::Expression expr = parse_yaml_identifier_block(kv.second);
        detection.identifiers[key] = std::move(expr);
    }

    // Parse condition
    detection.expression = parse_condition_string(condition, detection.identifiers);

    return detection;
}

// Parse Filter from YAML - either Detection or Expression
Filter parse_yaml_filter(const YAML::Node& node) {
    // SPEC-SLICE-009 FACT-003: untagged enum
    // Detection has 'condition' key, Expression is just a string

    if (node.IsScalar()) {
        // Expression string - parse as tau expression
        std::string expr_str = node.as<std::string>();
        // For now, return simple expression
        // TODO: implement full tau expression parser
        return tau::Expression::make_bool(true);
    }

    if (node.IsMap() && node["condition"]) {
        // Detection
        return parse_yaml_detection(node);
    }

    // Default to empty detection
    return tau::Detection{};
}

// Parse ChainsawRule from YAML
ChainsawRule parse_chainsaw_rule(const YAML::Node& root) {
    ChainsawRule rule;

    // SPEC-SLICE-009 FACT-001: title is alias for name
    if (root["title"]) {
        rule.name = root["title"].as<std::string>();
    } else if (root["name"]) {
        rule.name = root["name"].as<std::string>();
    }

    rule.group = root["group"].as<std::string>("");
    rule.description = root["description"].as<std::string>("");

    // Authors
    if (root["authors"] && root["authors"].IsSequence()) {
        for (const auto& a : root["authors"]) {
            rule.authors.push_back(a.as<std::string>());
        }
    }

    // Kind/Level/Status
    rule.kind = parse_document_kind(root["kind"].as<std::string>("evtx"));
    rule.level = parse_level(root["level"].as<std::string>("info"));
    rule.status = parse_status(root["status"].as<std::string>("experimental"));
    rule.timestamp = root["timestamp"].as<std::string>("");

    // Fields
    if (root["fields"] && root["fields"].IsSequence()) {
        for (const auto& f : root["fields"]) {
            rule.fields.push_back(parse_field_yaml(f));
        }
    }

    // Filter
    if (root["filter"]) {
        rule.filter = parse_yaml_filter(root["filter"]);
    }

    // Aggregate
    rule.aggregate = parse_aggregate(root["aggregate"]);

    return rule;
}

// Apply optimisation pipeline to filter
// SPEC-SLICE-009 FACT-011
Filter optimize_filter(Filter filter) {
    if (auto* detection = std::get_if<tau::Detection>(&filter)) {
        tau::Detection d = std::move(*detection);

        // 1. Coalesce identifiers into expression
        d.expression = tau::coalesce(std::move(d.expression), d.identifiers);

        // 2. Clear identifiers
        d.identifiers.clear();

        // 3. Shake (dead code elimination)
        d.expression = tau::shake(std::move(d.expression));

        // 4. Rewrite (normalization)
        d.expression = tau::rewrite(std::move(d.expression));

        // 5. Matrix (multi-field optimization)
        d.expression = tau::matrix(std::move(d.expression));

        return d;
    }

    if (auto* expression = std::get_if<tau::Expression>(&filter)) {
        tau::Expression e = std::move(*expression);

        // No coalesce for Expression (no identifiers)
        e = tau::shake(std::move(e));
        e = tau::rewrite(std::move(e));
        e = tau::matrix(std::move(e));

        return e;
    }

    return filter;
}

// Check file extension
bool is_yaml_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    // Convert to lowercase for comparison
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".yml" || ext == ".yaml";
}

}  // anonymous namespace

// ============================================================================
// Rule interface functions
// ============================================================================

const std::optional<Aggregate>& rule_aggregate(const Rule& r) {
    return std::visit(
        [](const auto& rule) -> const std::optional<Aggregate>& { return rule.aggregate; }, r);
}

bool rule_is_kind(const Rule& r, Kind kind) {
    return std::visit(
        [kind](const auto& rule) -> bool {
            using T = std::decay_t<decltype(rule)>;
            if constexpr (std::is_same_v<T, ChainsawRule>) {
                return kind == Kind::Chainsaw;
            } else {
                static_assert(std::is_same_v<T, SigmaRule>, "Unhandled rule type");
                return kind == Kind::Sigma;
            }
        },
        r);
}

Level rule_level(const Rule& r) {
    return std::visit([](const auto& rule) -> Level { return rule.level; }, r);
}

io::DocumentKind rule_types(const Rule& r) {
    return std::visit(
        [](const auto& rule) -> io::DocumentKind {
            using T = std::decay_t<decltype(rule)>;
            if constexpr (std::is_same_v<T, ChainsawRule>) {
                return rule.kind;
            } else {
                static_assert(std::is_same_v<T, SigmaRule>, "Unhandled rule type");
                return io::DocumentKind::Unknown;
            }
        },
        r);
}

const std::string& rule_name(const Rule& r) {
    return std::visit([](const auto& rule) -> const std::string& { return rule.name; }, r);
}

bool rule_solve(const Rule& r, const tau::Document& doc) {
    return std::visit(
        [&doc](const auto& rule) -> bool {
            using T = std::decay_t<decltype(rule)>;

            if constexpr (std::is_same_v<T, ChainsawRule>) {
                return std::visit(
                    [&doc](const auto& filter) -> bool {
                        using F = std::decay_t<decltype(filter)>;
                        if constexpr (std::is_same_v<F, tau::Detection>) {
                            return tau::solve(filter, doc);
                        } else {
                            static_assert(std::is_same_v<F, tau::Expression>,
                                          "Unhandled filter type");
                            return tau::solve(filter, doc);
                        }
                    },
                    rule.filter);
            } else {
                static_assert(std::is_same_v<T, SigmaRule>, "Unhandled rule type");
                return tau::solve(rule.detection, doc);
            }
        },
        r);
}

Status rule_status(const Rule& r) {
    return std::visit([](const auto& rule) -> Status { return rule.status; }, r);
}

// ============================================================================
// Load/Lint functions
// ============================================================================

LoadResult load(Kind kind, const std::filesystem::path& path, const LoadOptions& options) {
    LoadResult result;

    // SPEC-SLICE-009 FACT-007: check extension
    if (!is_yaml_extension(path)) {
        result.error = Error{"rule must have a yaml file extension", path.string()};
        return result;
    }

    // SPEC-SLICE-009 FACT-008: check kind filter
    if (options.kinds && options.kinds->find(kind) == options.kinds->end()) {
        result.ok = true;
        return result;  // Empty result, not an error
    }

    try {
        switch (kind) {
        case Kind::Chainsaw: {
            YAML::Node root = YAML::LoadFile(path.string());
            ChainsawRule rule = parse_chainsaw_rule(root);

            // SPEC-SLICE-009 FACT-011: optimize filter
            rule.filter = optimize_filter(std::move(rule.filter));

            // Apply level/status filters
            if (options.levels && options.levels->find(rule.level) == options.levels->end()) {
                result.ok = true;
                return result;
            }
            if (options.statuses &&
                options.statuses->find(rule.status) == options.statuses->end()) {
                result.ok = true;
                return result;
            }

            result.rules.push_back(std::move(rule));
            break;
        }
        case Kind::Sigma: {
            // SLICE-010: Sigma rules loader
            auto sigma_result = sigma::load(path);
            if (!sigma_result.ok) {
                result.error = Error{sigma_result.error.format(), path.string()};
                return result;
            }

            for (const auto& data : sigma_result.rules) {
                SigmaRule rule;

                // Copy metadata
                rule.name = data.title;
                rule.description = data.description;
                rule.authors = data.authors;
                rule.level = parse_level(data.level);
                rule.status = (data.status == "stable") ? Status::Stable : Status::Experimental;

                // Optional fields
                rule.id = data.id;
                if (data.logsource) {
                    LogSource ls;
                    ls.category = data.logsource->category;
                    ls.definition = data.logsource->definition;
                    ls.product = data.logsource->product;
                    ls.service = data.logsource->service;
                    rule.logsource = ls;
                }
                rule.references = data.references;
                rule.tags = data.tags;
                rule.falsepositives = data.falsepositives;

                // Parse detection from YAML string
                if (!data.detection_yaml.empty()) {
                    try {
                        YAML::Node det_node = YAML::Load(data.detection_yaml);
                        rule.detection = parse_yaml_detection(det_node);
                    } catch (const YAML::Exception&) {
                        // Skip invalid detection
                    }
                }

                // Aggregate
                if (data.aggregate) {
                    Aggregate agg;
                    if (auto pattern = parse_numeric(data.aggregate->count)) {
                        agg.count = *pattern;
                    } else {
                        agg.count = tau::PatternEqual{1};
                    }
                    agg.fields = data.aggregate->fields;
                    rule.aggregate = agg;
                }

                // Optimize detection
                rule.detection.expression =
                    tau::coalesce(std::move(rule.detection.expression), rule.detection.identifiers);
                rule.detection.identifiers.clear();
                rule.detection.expression = tau::shake(std::move(rule.detection.expression));
                rule.detection.expression = tau::rewrite(std::move(rule.detection.expression));
                rule.detection.expression = tau::matrix(std::move(rule.detection.expression));

                // Apply level/status filters
                if (options.levels && options.levels->find(rule.level) == options.levels->end()) {
                    continue;
                }
                if (options.statuses &&
                    options.statuses->find(rule.status) == options.statuses->end()) {
                    continue;
                }

                result.rules.push_back(std::move(rule));
            }
            break;
        }
        }

        result.ok = true;
    } catch (const YAML::Exception& e) {
        result.error = Error{e.what(), path.string()};
    } catch (const std::exception& e) {
        result.error = Error{e.what(), path.string()};
    }

    return result;
}

LintResult lint(Kind kind, const std::filesystem::path& path) {
    LintResult result;

    // SPEC-SLICE-009 FACT-007: check extension
    if (!is_yaml_extension(path)) {
        result.error = Error{"rule must have a yaml file extension", path.string()};
        return result;
    }

    try {
        switch (kind) {
        case Kind::Chainsaw: {
            YAML::Node root = YAML::LoadFile(path.string());
            ChainsawRule rule = parse_chainsaw_rule(root);
            result.filters.push_back(std::move(rule.filter));
            break;
        }
        case Kind::Sigma: {
            // SLICE-010: Sigma rules lint
            auto sigma_result = sigma::load(path);
            if (!sigma_result.ok) {
                result.error = Error{sigma_result.error.format(), path.string()};
                return result;
            }

            for (const auto& data : sigma_result.rules) {
                if (!data.detection_yaml.empty()) {
                    try {
                        YAML::Node det_node = YAML::Load(data.detection_yaml);
                        tau::Detection detection = parse_yaml_detection(det_node);
                        result.filters.push_back(std::move(detection));
                    } catch (const YAML::Exception&) {
                        // Skip invalid detection
                    }
                }
            }
            break;
        }
        }

        result.ok = true;
    } catch (const YAML::Exception& e) {
        result.error = Error{e.what(), path.string()};
    } catch (const std::exception& e) {
        result.error = Error{e.what(), path.string()};
    }

    return result;
}

}  // namespace chainsaw::rule

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
