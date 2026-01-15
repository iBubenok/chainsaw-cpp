// ==============================================================================
// sigma.cpp - MOD-0008: Sigma Rules Loader Implementation
// ==============================================================================
//
// SLICE-010: Sigma Rules Loader + Conversion
// SPEC-SLICE-010: micro-spec поведения
//
// Реализация загрузки и конвертации правил в формате Sigma YAML.
//
// ==============================================================================

#include <algorithm>
#include <cctype>
#include <chainsaw/rule.hpp>
#include <chainsaw/sigma.hpp>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

// GCC 13 generates false positives for -Wnull-dereference when using
// istreambuf_iterator to read files into strings at high optimization levels.
// See: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=106199
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif

namespace chainsaw::rule::sigma {

// ============================================================================
// Error formatting
// ============================================================================

std::string Error::format() const {
    std::ostringstream oss;
    if (!context.empty()) {
        oss << context << ": ";
    }
    oss << message;
    return oss.str();
}

// ============================================================================
// Base64 encoding (standalone implementation without OpenSSL)
// ============================================================================

namespace {

const std::string BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // anonymous namespace

std::string base64_encode(const std::string& value) {
    std::string result;
    result.reserve(((value.size() + 2) / 3) * 4);

    unsigned int val = 0;
    int valb = -6;

    for (char ch : value) {
        unsigned char c = static_cast<unsigned char>(ch);
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(BASE64_CHARS[static_cast<size_t>((val >> valb) & 0x3F)]);
            valb -= 6;
        }
    }

    if (valb > -6) {
        result.push_back(BASE64_CHARS[static_cast<size_t>(((val << 8) >> (valb + 8)) & 0x3F)]);
    }

    while (result.size() % 4) {
        result.push_back('=');
    }

    return result;
}

std::vector<std::string> base64_offset_encode(const std::string& value) {
    // SPEC-SLICE-010 FACT-008: base64offset генерирует 3 варианта
    std::vector<std::string> result;
    result.reserve(3);

    // offset 0
    result.push_back(base64_encode(value));

    // offset 1: добавляем 1 пробел перед значением
    std::string offset1 = " " + value;
    std::string encoded1 = base64_encode(offset1);
    // Обрезаем: начало S[1]=2, конец E[len%3]
    size_t len1 = offset1.size();
    size_t end_trim1 = (len1 % 3 == 0) ? 0 : (len1 % 3 == 1) ? 3 : 2;
    if (encoded1.size() > 2 + end_trim1) {
        result.push_back(encoded1.substr(2, encoded1.size() - 2 - end_trim1));
    } else {
        result.push_back(encoded1);
    }

    // offset 2: добавляем 2 пробела перед значением
    std::string offset2 = "  " + value;
    std::string encoded2 = base64_encode(offset2);
    // Обрезаем: начало S[2]=3, конец E[len%3]
    size_t len2 = offset2.size();
    size_t end_trim2 = (len2 % 3 == 0) ? 0 : (len2 % 3 == 1) ? 3 : 2;
    if (encoded2.size() > 3 + end_trim2) {
        result.push_back(encoded2.substr(3, encoded2.size() - 3 - end_trim2));
    } else {
        result.push_back(encoded2);
    }

    return result;
}

// ============================================================================
// Match trait functions
// ============================================================================

std::string as_contains(const std::string& value) {
    return "i*" + value + "*";
}

std::string as_endswith(const std::string& value) {
    return "i*" + value;
}

std::string as_startswith(const std::string& value) {
    return "i" + value + "*";
}

std::optional<std::string> as_match(const std::string& value) {
    // SPEC-SLICE-010 FACT-006: as_match() возвращает nullopt для nested wildcards
    size_t len = value.size();
    if (len > 1) {
        size_t start = 0;
        size_t end_pos = len;

        if (value[0] == '*') {
            start = 1;
        }
        if (value.back() == '*') {
            end_pos = len - 1;
        }

        // Проверяем nested wildcards
        std::string_view middle(value.data() + start, end_pos - start);
        if (middle.find('*') != std::string_view::npos ||
            middle.find('?') != std::string_view::npos) {
            return std::nullopt;
        }
    }

    return "i" + value;
}

std::optional<std::string> as_regex(const std::string& value, bool convert) {
    if (convert) {
        // Конвертируем wildcard в regex
        std::string result;
        result.reserve(value.size() * 2);

        bool escaped = false;
        for (char c : value) {
            // Escape special regex characters (кроме * и ?)
            if (!escaped && (c == '.' || c == '+' || c == '^' || c == '$' || c == '(' || c == ')' ||
                             c == '[' || c == ']' || c == '{' || c == '}' || c == '|')) {
                result += '\\';
            }

            if (c == '*' || c == '?') {
                if (!escaped) {
                    result += '.';
                }
            } else if (c == '\\') {
                escaped = !escaped;
                result += c;
                continue;
            }

            escaped = false;
            result += c;
        }

        return "?" + result;
    } else {
        // Проверяем валидность regex
        try {
            std::regex re(value);
            (void)re;  // Suppress unused variable warning
            return "?" + value;
        } catch (const std::regex_error&) {
            return std::nullopt;
        }
    }
}

// ============================================================================
// Modifier support
// ============================================================================

const std::unordered_set<std::string> SUPPORTED_MODIFIERS = {
    "all", "base64", "base64offset", "contains", "endswith", "startswith", "re"};

bool is_modifier_supported(const std::string& modifier) {
    return SUPPORTED_MODIFIERS.count(modifier) > 0;
}

std::vector<std::string> get_unsupported_modifiers(
    const std::unordered_set<std::string>& modifiers) {
    std::vector<std::string> unsupported;
    for (const auto& m : modifiers) {
        if (!is_modifier_supported(m)) {
            unsupported.push_back(m);
        }
    }
    std::sort(unsupported.begin(), unsupported.end());
    return unsupported;
}

// ============================================================================
// Condition checking
// ============================================================================

bool is_condition_unsupported(const std::string& condition) {
    // SPEC-SLICE-010 FACT-014: Unsupported conditions
    return condition.find(" | ") != std::string::npos || condition.find('*') != std::string::npos ||
           condition.find(" avg ") != std::string::npos ||
           condition.find(" of ") != std::string::npos ||
           condition.find(" max ") != std::string::npos ||
           condition.find(" min ") != std::string::npos ||
           condition.find(" near ") != std::string::npos ||
           condition.find(" sum ") != std::string::npos;
}

// ============================================================================
// Internal parsing helpers
// ============================================================================

namespace {

// Detection wrapper for internal use
struct Detection {
    std::optional<YAML::Node> condition;
    YAML::Node identifiers;
};

// Internal result type
template <typename T>
struct Result {
    bool ok = false;
    T value;
    Error error;

    static Result success(T v) {
        Result r;
        r.ok = true;
        r.value = std::move(v);
        return r;
    }

    static Result failure(const std::string& msg, const std::string& ctx = "") {
        Result r;
        r.ok = false;
        r.error = Error{msg, ctx};
        return r;
    }
};

// Parse identifier with modifiers
Result<YAML::Node> parse_identifier(const YAML::Node& value,
                                    const std::unordered_set<std::string>& modifiers) {
    // Проверяем на неподдерживаемые модификаторы
    auto unsupported = get_unsupported_modifiers(modifiers);
    if (!unsupported.empty()) {
        std::string msg;
        for (size_t i = 0; i < unsupported.size(); ++i) {
            if (i > 0)
                msg += ", ";
            msg += unsupported[i];
        }
        return Result<YAML::Node>::failure(msg, "unsupported modifiers");
    }

    if (value.IsMap()) {
        YAML::Node result;
        for (const auto& kv : value) {
            auto parsed = parse_identifier(kv.second, modifiers);
            if (!parsed.ok) {
                return parsed;
            }
            result[kv.first] = parsed.value;
        }
        return Result<YAML::Node>::success(result);
    }

    if (value.IsSequence()) {
        YAML::Node result;
        for (const auto& item : value) {
            auto parsed = parse_identifier(item, modifiers);
            if (!parsed.ok) {
                return parsed;
            }
            // Если результат — последовательность, расширяем
            if (parsed.value.IsSequence()) {
                for (const auto& sub : parsed.value) {
                    result.push_back(sub);
                }
            } else {
                result.push_back(parsed.value);
            }
        }
        return Result<YAML::Node>::success(result);
    }

    if (value.IsScalar()) {
        std::string s = value.as<std::string>();

        // SPEC-SLICE-010 FACT-007: base64 modifier
        if (modifiers.count("base64")) {
            auto remaining = modifiers;
            remaining.erase("base64");
            std::string encoded = base64_encode(s);
            return parse_identifier(YAML::Node(encoded), remaining);
        }

        // SPEC-SLICE-010 FACT-008: base64offset modifier
        if (modifiers.count("base64offset")) {
            auto remaining = modifiers;
            remaining.erase("base64offset");

            auto offsets = base64_offset_encode(s);
            YAML::Node result;
            for (const auto& offset : offsets) {
                auto parsed = parse_identifier(YAML::Node(offset), remaining);
                if (!parsed.ok) {
                    return parsed;
                }
                result.push_back(parsed.value);
            }
            return Result<YAML::Node>::success(result);
        }

        // SPEC-SLICE-010 FACT-005: contains → "i*value*"
        if (modifiers.count("contains")) {
            return Result<YAML::Node>::success(YAML::Node(as_contains(s)));
        }

        // endswith → "i*value"
        if (modifiers.count("endswith")) {
            return Result<YAML::Node>::success(YAML::Node(as_endswith(s)));
        }

        // re → "?pattern"
        if (modifiers.count("re")) {
            auto regex = as_regex(s, false);
            if (!regex) {
                return Result<YAML::Node>::failure(s, "unsupported regex");
            }
            return Result<YAML::Node>::success(YAML::Node(*regex));
        }

        // startswith → "ivalue*"
        if (modifiers.count("startswith")) {
            return Result<YAML::Node>::success(YAML::Node(as_startswith(s)));
        }

        // Default: as_match или as_regex(convert=true)
        auto match = as_match(s);
        if (match) {
            return Result<YAML::Node>::success(YAML::Node(*match));
        }

        auto regex = as_regex(s, true);
        if (regex) {
            return Result<YAML::Node>::success(YAML::Node(*regex));
        }

        return Result<YAML::Node>::failure(s, "unsupported match");
    }

    // Для других типов (null, bool, int) — возвращаем как есть
    return Result<YAML::Node>::success(YAML::Clone(value));
}

// Prepare condition (extract aggregation)
Result<std::pair<std::string, std::optional<SigmaAggregate>>> prepare_condition(
    const std::string& condition) {
    // SPEC-SLICE-010 FACT-015: Aggregation in Condition
    auto pipe_pos = condition.find(" | ");
    if (pipe_pos != std::string::npos) {
        std::string cond_part = condition.substr(0, pipe_pos);
        std::string agg_part = condition.substr(pipe_pos + 3);

        SigmaAggregate agg;

        // Parse aggregation: count(field) [ by group-field ] comparison-op value
        std::istringstream iss(agg_part);
        std::string kind;
        iss >> kind;

        if (kind.substr(0, 6) == "count(") {
            auto close = kind.find(')');
            if (close == std::string::npos) {
                return Result<std::pair<std::string, std::optional<SigmaAggregate>>>::failure(
                    "invalid agg function");
            }
            std::string field = kind.substr(6, close - 6);
            if (!field.empty()) {
                agg.fields.push_back(field);
            }
        } else {
            return Result<std::pair<std::string, std::optional<SigmaAggregate>>>::failure(
                "unsupported agg function - " + kind);
        }

        std::string next;
        if (!(iss >> next)) {
            return Result<std::pair<std::string, std::optional<SigmaAggregate>>>::failure(
                "invalid aggregation");
        }

        if (next == "by") {
            std::string group_field;
            if (!(iss >> group_field)) {
                return Result<std::pair<std::string, std::optional<SigmaAggregate>>>::failure(
                    "missing group field");
            }
            agg.fields.push_back(group_field);
            if (!(iss >> next)) {
                return Result<std::pair<std::string, std::optional<SigmaAggregate>>>::failure(
                    "invalid aggregation");
            }
        }

        std::string number;
        if (!(iss >> number)) {
            return Result<std::pair<std::string, std::optional<SigmaAggregate>>>::failure(
                "invalid aggregation");
        }

        agg.count = next + number;

        return Result<std::pair<std::string, std::optional<SigmaAggregate>>>::success(
            std::make_pair(cond_part, std::optional<SigmaAggregate>(agg)));
    }

    return Result<std::pair<std::string, std::optional<SigmaAggregate>>>::success(
        std::make_pair(condition, std::nullopt));
}

// Prepare detection (merge with extension for Rule Collections)
Result<std::pair<Detection, std::optional<SigmaAggregate>>> prepare(
    Detection detection, std::optional<Detection> extra) {
    std::optional<SigmaAggregate> aggregate;

    // Получаем condition
    YAML::Node condition_node =
        extra && extra->condition.has_value()
            ? *extra->condition
            : (detection.condition.has_value() ? *detection.condition : YAML::Node());

    if (condition_node.IsDefined() && !condition_node.IsNull()) {
        std::string condition_str;

        if (condition_node.IsScalar()) {
            condition_str = condition_node.as<std::string>();
        } else if (condition_node.IsSequence() && condition_node.size() == 1) {
            condition_str = condition_node[0].as<std::string>();
        } else {
            return Result<std::pair<Detection, std::optional<SigmaAggregate>>>::failure(
                "condition must be a string");
        }

        // Parse condition
        auto prep_result = prepare_condition(condition_str);
        if (!prep_result.ok) {
            return Result<std::pair<Detection, std::optional<SigmaAggregate>>>::failure(
                prep_result.error.message, prep_result.error.context);
        }
        condition_str = prep_result.value.first;
        aggregate = prep_result.value.second;

        // Merge identifiers
        YAML::Node identifiers = YAML::Clone(detection.identifiers);

        if (extra && extra->identifiers.IsDefined()) {
            for (const auto& kv : extra->identifiers) {
                std::string key = kv.first.as<std::string>();

                if (identifiers[key]) {
                    // Merge existing identifier
                    YAML::Node existing = identifiers[key];
                    YAML::Node new_val = kv.second;

                    if (existing.IsMap() && new_val.IsMap()) {
                        for (const auto& inner : new_val) {
                            existing[inner.first] = inner.second;
                        }
                        identifiers[key] = existing;
                    } else if (existing.IsSequence() && new_val.IsMap()) {
                        YAML::Node merged;
                        for (size_t i = 0; i < existing.size(); ++i) {
                            YAML::Node item = YAML::Clone(existing[i]);
                            if (item.IsMap()) {
                                for (const auto& inner : new_val) {
                                    item[inner.first] = inner.second;
                                }
                            }
                            merged.push_back(item);
                        }
                        identifiers[key] = merged;
                    } else {
                        return Result<std::pair<Detection, std::optional<SigmaAggregate>>>::failure(
                            "unsupported rule collection format");
                    }
                } else {
                    identifiers[key] = kv.second;
                }
            }
        }

        Detection result;
        result.condition = YAML::Node(condition_str);
        result.identifiers = identifiers;
        return Result<std::pair<Detection, std::optional<SigmaAggregate>>>::success(
            std::make_pair(result, aggregate));
    }

    return Result<std::pair<Detection, std::optional<SigmaAggregate>>>::success(
        std::make_pair(detection, std::nullopt));
}

// Convert detection to tau format
Result<YAML::Node> detections_to_tau(const Detection& detection) {
    YAML::Node tau;
    YAML::Node det;

    // Получаем condition
    std::string condition;
    if (!detection.condition || !detection.condition->IsDefined()) {
        return Result<YAML::Node>::failure("missing condition");
    }

    if (detection.condition->IsScalar()) {
        condition = detection.condition->as<std::string>();
    } else {
        return Result<YAML::Node>::failure("unsupported condition format");
    }

    // Обработка identifiers
    std::unordered_map<std::string, std::string> patches;

    for (const auto& kv : detection.identifiers) {
        std::string k = kv.first.as<std::string>();

        // Пропускаем timeframe
        if (k == "timeframe") {
            continue;
        }

        YAML::Node v = kv.second;

        if (v.IsSequence()) {
            // Sequence → split into K_0, K_1, ...
            std::vector<std::pair<std::string, YAML::Node>> blocks;

            for (size_t index = 0; index < v.size(); ++index) {
                YAML::Node entry = v[index];
                if (!entry.IsMap()) {
                    return Result<YAML::Node>::failure("keyless identifiers cannot be converted");
                }

                bool collect = true;
                std::unordered_set<std::string> seen;
                std::vector<YAML::Node> maps;

                for (const auto& field_kv : entry) {
                    std::string f = field_kv.first.as<std::string>();

                    // Parse modifiers
                    std::vector<std::string> parts;
                    std::istringstream iss(f);
                    std::string part;
                    while (std::getline(iss, part, '|')) {
                        parts.push_back(part);
                    }

                    if (parts.empty() || parts[0].empty()) {
                        return Result<YAML::Node>::failure(
                            "keyless identifiers cannot be converted");
                    }

                    std::string field_name = parts[0];

                    if (seen.count(field_name)) {
                        collect = false;
                    }
                    seen.insert(field_name);

                    std::unordered_set<std::string> modifiers(parts.begin() + 1, parts.end());

                    // all modifier
                    if (modifiers.count("all")) {
                        field_name = "all(" + field_name + ")";
                    }

                    auto parsed = parse_identifier(field_kv.second, modifiers);
                    if (!parsed.ok) {
                        return Result<YAML::Node>::failure(parsed.error.message,
                                                           parsed.error.context);
                    }

                    YAML::Node map;
                    map[field_name] = parsed.value;
                    maps.push_back(map);
                }

                std::string ident = k + "_" + std::to_string(index);
                if (collect) {
                    YAML::Node merged;
                    for (const auto& m : maps) {
                        for (const auto& inner : m) {
                            merged[inner.first] = inner.second;
                        }
                    }
                    blocks.emplace_back(ident, merged);
                } else {
                    std::string all_ident = "all(" + ident + ")";
                    YAML::Node seq;
                    for (const auto& m : maps) {
                        seq.push_back(m);
                    }
                    blocks.emplace_back(all_ident, seq);
                    ident = all_ident;
                }
            }

            // Create patch
            std::string patch = "(";
            for (size_t i = 0; i < blocks.size(); ++i) {
                if (i > 0)
                    patch += " or ";
                patch += blocks[i].first;
            }
            patch += ")";
            patches[k] = patch;

            for (const auto& block : blocks) {
                det[block.first] = block.second;
            }
        } else if (v.IsMap()) {
            // Mapping → AND of all fields
            bool collect = true;
            std::unordered_set<std::string> seen;
            std::vector<YAML::Node> maps;

            for (const auto& field_kv : v) {
                std::string f = field_kv.first.as<std::string>();

                // Parse modifiers
                std::vector<std::string> parts;
                std::istringstream iss(f);
                std::string part;
                while (std::getline(iss, part, '|')) {
                    parts.push_back(part);
                }

                if (parts.empty() || parts[0].empty()) {
                    return Result<YAML::Node>::failure("keyless identifiers cannot be converted");
                }

                std::string field_name = parts[0];

                if (seen.count(field_name)) {
                    collect = false;
                }
                seen.insert(field_name);

                std::unordered_set<std::string> modifiers(parts.begin() + 1, parts.end());

                // all modifier
                if (modifiers.count("all")) {
                    field_name = "all(" + field_name + ")";
                }

                auto parsed = parse_identifier(field_kv.second, modifiers);
                if (!parsed.ok) {
                    return Result<YAML::Node>::failure(parsed.error.message, parsed.error.context);
                }

                YAML::Node map;
                map[field_name] = parsed.value;
                maps.push_back(map);
            }

            if (collect) {
                YAML::Node merged;
                for (const auto& m : maps) {
                    for (const auto& inner : m) {
                        merged[inner.first] = inner.second;
                    }
                }
                det[k] = merged;
            } else {
                std::string all_ident = "all(" + k + ")";
                YAML::Node seq;
                for (const auto& m : maps) {
                    seq.push_back(m);
                }
                det[k] = seq;
                patches[k] = all_ident;
            }
        } else {
            return Result<YAML::Node>::failure(
                "identifier blocks must be a mapping or a sequence of mappings");
        }
    }

    // Нормализация condition
    std::string normalized;
    {
        std::string temp = condition;

        // Replace uppercase keywords
        auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
            size_t pos = 0;
            while ((pos = str.find(from, pos)) != std::string::npos) {
                str.replace(pos, from.length(), to);
                pos += to.length();
            }
        };

        replace_all(temp, " AND ", " and ");
        replace_all(temp, " NOT ", " not ");
        replace_all(temp, " OR ", " or ");

        // Tokenize and apply patches
        std::istringstream iss(temp);
        std::string token;
        std::vector<std::string> tokens;
        while (iss >> token) {
            // Strip parentheses for lookup
            std::string key = token;
            size_t start_parens = 0;
            size_t end_parens = 0;

            while (!key.empty() && key.front() == '(') {
                start_parens++;
                key = key.substr(1);
            }
            while (!key.empty() && key.back() == ')') {
                end_parens++;
                key = key.substr(0, key.size() - 1);
            }

            auto it = patches.find(key);
            if (it != patches.end()) {
                std::string result;
                for (size_t i = 0; i < start_parens; ++i)
                    result += '(';
                result += it->second;
                for (size_t i = 0; i < end_parens; ++i)
                    result += ')';
                tokens.push_back(result);
            } else {
                tokens.push_back(token);
            }
        }

        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0)
                normalized += ' ';
            normalized += tokens[i];
        }
    }

    // Handle "all of them" / "1 of them"
    if (normalized == "all of them") {
        std::vector<std::string> identifiers;
        for (const auto& kv : det) {
            std::string key = kv.first.as<std::string>();
            auto it = patches.find(key);
            if (it != patches.end()) {
                identifiers.push_back(it->second);
            } else {
                identifiers.push_back(key);
            }
        }
        normalized.clear();
        for (size_t i = 0; i < identifiers.size(); ++i) {
            if (i > 0)
                normalized += " and ";
            normalized += identifiers[i];
        }
    } else if (normalized == "1 of them") {
        std::vector<std::string> identifiers;
        for (const auto& kv : det) {
            std::string key = kv.first.as<std::string>();
            auto it = patches.find(key);
            if (it != patches.end()) {
                identifiers.push_back(it->second);
            } else {
                identifiers.push_back(key);
            }
        }
        normalized.clear();
        for (size_t i = 0; i < identifiers.size(); ++i) {
            if (i > 0)
                normalized += " or ";
            normalized += identifiers[i];
        }
    } else {
        // Handle "all of prefix*" / "1 of prefix*"
        std::vector<std::string> mutated;
        std::istringstream iss(normalized);
        std::string part;

        while (iss >> part) {
            // Handle leading parentheses
            while (!part.empty() && part.front() == '(') {
                mutated.push_back("(");
                part = part.substr(1);
            }

            if (part == "all" || part == "1") {
                std::string next;
                if (iss >> next) {
                    if (next != "of") {
                        mutated.push_back(part);
                        mutated.push_back(next);
                        continue;
                    }

                    std::string identifier;
                    if (iss >> identifier) {
                        // Handle trailing parentheses
                        std::vector<std::string> brackets;
                        while (!identifier.empty() && identifier.back() == ')') {
                            brackets.push_back(")");
                            identifier = identifier.substr(0, identifier.size() - 1);
                        }

                        // Check for wildcard
                        if (!identifier.empty() && identifier.back() == '*') {
                            std::string ident_prefix = identifier.substr(0, identifier.size() - 1);

                            std::vector<std::string> keys;
                            for (const auto& kv : det) {
                                std::string key = kv.first.as<std::string>();
                                if (key.substr(0, ident_prefix.size()) == ident_prefix) {
                                    auto it = patches.find(key);
                                    if (it != patches.end()) {
                                        keys.push_back(it->second);
                                    } else {
                                        keys.push_back(key);
                                    }
                                }
                            }

                            if (keys.empty()) {
                                return Result<YAML::Node>::failure(
                                    "could not find any applicable identifiers");
                            }

                            std::string expr = "(";
                            for (size_t i = 0; i < keys.size(); ++i) {
                                if (i > 0) {
                                    if (part == "all") {
                                        expr += " and ";
                                    } else {
                                        expr += " or ";
                                    }
                                }
                                expr += keys[i];
                            }
                            expr += ")";
                            mutated.push_back(expr);
                        } else {
                            // Single identifier
                            auto it = patches.find(identifier);
                            std::string key = (it != patches.end()) ? it->second : identifier;

                            if (part == "all") {
                                mutated.push_back("all(" + key + ")");
                            } else {
                                mutated.push_back("of(" + key + ", 1)");
                            }
                        }

                        for (const auto& b : brackets) {
                            mutated.push_back(b);
                        }
                        continue;
                    }
                }
            }

            mutated.push_back(part);
        }

        normalized.clear();
        for (const auto& t : mutated) {
            normalized += t;
        }
        // Clean up parentheses spacing
        auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
            size_t pos = 0;
            while ((pos = str.find(from, pos)) != std::string::npos) {
                str.replace(pos, from.length(), to);
                pos += to.length();
            }
        };
        replace_all(normalized, "( ", "(");
        replace_all(normalized, " )", ")");
    }

    // Check for unsupported conditions
    if (is_condition_unsupported(normalized)) {
        return Result<YAML::Node>::failure(normalized, "unsupported condition");
    }

    det["condition"] = normalized;

    tau["detection"] = det;
    tau["true_positives"] = YAML::Node(YAML::NodeType::Sequence);
    tau["true_negatives"] = YAML::Node(YAML::NodeType::Sequence);

    return Result<YAML::Node>::success(tau);
}

// Splits YAML multi-doc by "---\n"
std::vector<std::string> split_yaml_docs(const std::string& content) {
    std::vector<std::string> docs;
    std::regex doc_regex(R"(---\s*\n)");

    std::sregex_token_iterator it(content.begin(), content.end(), doc_regex, -1);
    std::sregex_token_iterator end;

    for (; it != end; ++it) {
        std::string doc = it->str();
        // Trim whitespace
        size_t start = doc.find_first_not_of(" \t\n\r");
        if (start != std::string::npos) {
            size_t end_pos = doc.find_last_not_of(" \t\n\r");
            docs.push_back(doc.substr(start, end_pos - start + 1));
        }
    }

    return docs;
}

// Parse Header from YAML
struct Header {
    std::string title;
    std::string description;
    std::optional<std::string> action;
    std::optional<std::string> author;
    std::optional<std::vector<std::string>> falsepositives;
    std::optional<std::string> id;
    std::optional<LogSource> logsource;
    std::optional<std::vector<std::string>> references;
    std::optional<std::string> status;
    std::optional<std::vector<std::string>> tags;
};

std::optional<Header> parse_header(const YAML::Node& node) {
    if (!node["title"]) {
        return std::nullopt;
    }

    Header h;
    h.title = node["title"].as<std::string>();
    h.description = node["description"].as<std::string>("");

    if (node["action"]) {
        h.action = node["action"].as<std::string>();
    }
    if (node["author"]) {
        h.author = node["author"].as<std::string>();
    }
    if (node["falsepositives"] && node["falsepositives"].IsSequence()) {
        std::vector<std::string> fps;
        for (const auto& fp : node["falsepositives"]) {
            fps.push_back(fp.as<std::string>());
        }
        h.falsepositives = fps;
    }
    if (node["id"]) {
        h.id = node["id"].as<std::string>();
    }
    if (node["logsource"]) {
        LogSource ls;
        if (node["logsource"]["category"]) {
            ls.category = node["logsource"]["category"].as<std::string>();
        }
        if (node["logsource"]["definition"]) {
            ls.definition = node["logsource"]["definition"].as<std::string>();
        }
        if (node["logsource"]["product"]) {
            ls.product = node["logsource"]["product"].as<std::string>();
        }
        if (node["logsource"]["service"]) {
            ls.service = node["logsource"]["service"].as<std::string>();
        }
        h.logsource = ls;
    }
    if (node["references"] && node["references"].IsSequence()) {
        std::vector<std::string> refs;
        for (const auto& r : node["references"]) {
            refs.push_back(r.as<std::string>());
        }
        h.references = refs;
    }
    if (node["status"]) {
        h.status = node["status"].as<std::string>();
    }
    if (node["tags"] && node["tags"].IsSequence()) {
        std::vector<std::string> ts;
        for (const auto& t : node["tags"]) {
            ts.push_back(t.as<std::string>());
        }
        h.tags = ts;
    }

    return h;
}

// Create base rule data from header
SigmaRuleData create_base_rule(const Header& header) {
    SigmaRuleData rule;

    rule.title = header.title;
    rule.description = header.description;

    // Status normalization
    if (header.status) {
        rule.status = (*header.status == "stable") ? "stable" : "experimental";
    } else {
        rule.status = "experimental";
    }

    rule.falsepositives = header.falsepositives;
    rule.id = header.id;
    rule.logsource = header.logsource;
    rule.references = header.references;
    rule.tags = header.tags;

    // Author parsing: split by comma
    if (header.author) {
        std::istringstream iss(*header.author);
        std::string author;
        while (std::getline(iss, author, ',')) {
            // Trim whitespace
            size_t start = author.find_first_not_of(" \t");
            size_t end = author.find_last_not_of(" \t");
            if (start != std::string::npos) {
                rule.authors.push_back(author.substr(start, end - start + 1));
            }
        }
    }
    if (rule.authors.empty()) {
        rule.authors.push_back("unknown");
    }

    return rule;
}

// Parse Detection from YAML
std::optional<Detection> parse_detection(const YAML::Node& node) {
    if (!node["detection"]) {
        return std::nullopt;
    }

    Detection d;
    YAML::Node det = node["detection"];

    if (det["condition"]) {
        d.condition = det["condition"];
    }

    // Get all identifiers (everything except 'condition')
    for (const auto& kv : det) {
        std::string key = kv.first.as<std::string>();
        if (key != "condition") {
            d.identifiers[key] = kv.second;
        }
    }

    return d;
}

}  // anonymous namespace

// ============================================================================
// Load function — main entry point
// ============================================================================

LoadResult load(const std::filesystem::path& path) {
    LoadResult result;

    // Read file
    std::ifstream file(path);
    if (!file) {
        result.error = Error{"could not open file", path.string()};
        return result;
    }

    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Split into documents
    auto docs = split_yaml_docs(contents);

    if (docs.empty()) {
        result.ok = true;
        return result;
    }

    // Parse first document
    YAML::Node first_doc;
    try {
        first_doc = YAML::Load(docs[0]);
    } catch (const YAML::Exception& e) {
        result.error = Error{e.what(), path.string()};
        return result;
    }

    auto header = parse_header(first_doc);
    if (!header) {
        result.error = Error{"failed to parse sigma rule", path.string()};
        return result;
    }

    SigmaRuleData base = create_base_rule(*header);

    // Parse main detection
    auto main_detection = parse_detection(first_doc);

    // Get level from main document
    std::string main_level = "info";
    if (first_doc["level"]) {
        std::string level = first_doc["level"].as<std::string>();
        if (level == "critical" || level == "high" || level == "medium" || level == "low") {
            main_level = level;
        }
    }

    // Check for Rule Collection (action: global)
    bool is_collection = header->action.has_value();
    bool single = false;

    if (is_collection) {
        // Process subsequent documents
        for (size_t i = 1; i < docs.size(); ++i) {
            YAML::Node doc;
            try {
                doc = YAML::Load(docs[i]);
            } catch (const YAML::Exception&) {
                continue;
            }

            auto ext_detection = parse_detection(doc);
            if (ext_detection) {
                // Prepare merged detection
                auto prep_result = main_detection ? prepare(*main_detection, ext_detection)
                                                  : prepare(*ext_detection, std::nullopt);

                if (!prep_result.ok) {
                    result.error = Error{prep_result.error.message, prep_result.error.context};
                    return result;
                }

                auto& [detection, agg] = prep_result.value;

                // Convert to tau
                auto tau_result = detections_to_tau(detection);
                if (!tau_result.ok) {
                    result.error = Error{tau_result.error.message, tau_result.error.context};
                    return result;
                }

                // Create rule
                SigmaRuleData rule = base;
                rule.level = main_level;
                rule.aggregate = agg;

                // Serialize detection to YAML string
                std::stringstream ss;
                ss << tau_result.value["detection"];
                rule.detection_yaml = ss.str();

                result.rules.push_back(rule);
            } else {
                single = true;
            }
        }
    } else {
        single = true;
    }

    if (single && main_detection) {
        // Single rule
        auto prep_result = prepare(*main_detection, std::nullopt);
        if (!prep_result.ok) {
            result.error = Error{prep_result.error.message, prep_result.error.context};
            return result;
        }

        auto& [detection, agg] = prep_result.value;

        // Convert to tau
        auto tau_result = detections_to_tau(detection);
        if (!tau_result.ok) {
            result.error = Error{tau_result.error.message, tau_result.error.context};
            return result;
        }

        // Create rule
        SigmaRuleData rule = base;
        rule.level = main_level;
        rule.aggregate = agg;

        // Serialize detection to YAML string
        std::stringstream ss;
        ss << tau_result.value["detection"];
        rule.detection_yaml = ss.str();

        result.rules.push_back(rule);
    }

    result.ok = true;
    return result;
}

}  // namespace chainsaw::rule::sigma

// ============================================================================
// SigmaRule::find implementation (Document interface)
// ============================================================================

namespace chainsaw::rule {

std::optional<std::string> SigmaRule::find(const std::string& key) const {
    // SPEC-SLICE-010 FACT-003: Document trait implementation
    if (key == "title") {
        return name;
    }
    if (key == "level") {
        return to_string(level);
    }
    if (key == "status") {
        return to_string(status);
    }
    if (key == "id") {
        return id;
    }
    if (key == "logsource.category" && logsource) {
        return logsource->category;
    }
    if (key == "logsource.definition" && logsource) {
        return logsource->definition;
    }
    if (key == "logsource.product" && logsource) {
        return logsource->product;
    }
    if (key == "logsource.service" && logsource) {
        return logsource->service;
    }
    return std::nullopt;
}

}  // namespace chainsaw::rule

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
