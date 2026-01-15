// ==============================================================================
// search.cpp - MOD-0003: Search Command Pipeline Implementation
// ==============================================================================
//
// SLICE-011: Search Command Implementation
// SPEC-SLICE-011: micro-spec поведения
//
// ==============================================================================

#include <algorithm>
#include <cctype>
#include <chainsaw/platform.hpp>
#include <chainsaw/search.hpp>
#include <cstring>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sstream>

namespace chainsaw::search {

// ============================================================================
// DateTime implementation
// ============================================================================

std::optional<DateTime> DateTime::parse(std::string_view str) {
    DateTime dt;

    // Поддерживаемые форматы (SPEC-SLICE-011 FACT-009):
    // 1. %Y-%m-%dT%H:%M:%S%.fZ  (ISO 8601 с микросекундами и Z)
    // 2. %Y-%m-%dT%H:%M:%S%.f   (ISO 8601 с микросекундами без Z)
    // 3. %Y-%m-%dT%H:%M:%S      (ISO 8601 без микросекунд)

    // Минимальная длина: YYYY-MM-DDTHH:MM:SS = 19 символов
    if (str.size() < 19) {
        return std::nullopt;
    }

    // Парсим базовую часть: YYYY-MM-DDTHH:MM:SS
    auto parse_int = [](std::string_view s, int& out) -> bool {
        if (s.empty())
            return false;
        out = 0;
        for (char c : s) {
            if (c < '0' || c > '9')
                return false;
            out = out * 10 + (c - '0');
        }
        return true;
    };

    // YYYY
    if (!parse_int(str.substr(0, 4), dt.year))
        return std::nullopt;
    if (str[4] != '-')
        return std::nullopt;

    // MM
    if (!parse_int(str.substr(5, 2), dt.month))
        return std::nullopt;
    if (dt.month < 1 || dt.month > 12)
        return std::nullopt;
    if (str[7] != '-')
        return std::nullopt;

    // DD
    if (!parse_int(str.substr(8, 2), dt.day))
        return std::nullopt;
    if (dt.day < 1 || dt.day > 31)
        return std::nullopt;
    if (str[10] != 'T')
        return std::nullopt;

    // HH
    if (!parse_int(str.substr(11, 2), dt.hour))
        return std::nullopt;
    if (dt.hour < 0 || dt.hour > 23)
        return std::nullopt;
    if (str[13] != ':')
        return std::nullopt;

    // MM
    if (!parse_int(str.substr(14, 2), dt.minute))
        return std::nullopt;
    if (dt.minute < 0 || dt.minute > 59)
        return std::nullopt;
    if (str[16] != ':')
        return std::nullopt;

    // SS
    if (!parse_int(str.substr(17, 2), dt.second))
        return std::nullopt;
    if (dt.second < 0 || dt.second > 59)
        return std::nullopt;

    // Опциональная часть: микросекунды и/или Z
    std::size_t pos = 19;
    if (pos < str.size() && str[pos] == '.') {
        // Парсим микросекунды
        ++pos;
        std::size_t frac_start = pos;
        while (pos < str.size() && str[pos] >= '0' && str[pos] <= '9') {
            ++pos;
        }
        if (pos > frac_start) {
            std::string_view frac = str.substr(frac_start, pos - frac_start);
            int frac_val = 0;
            for (char c : frac) {
                frac_val = frac_val * 10 + (c - '0');
            }
            // Нормализуем к микросекундам (6 цифр)
            std::size_t frac_len = frac.size();
            if (frac_len < 6) {
                for (std::size_t i = frac_len; i < 6; ++i) {
                    frac_val *= 10;
                }
            } else if (frac_len > 6) {
                for (std::size_t i = 6; i < frac_len; ++i) {
                    frac_val /= 10;
                }
            }
            dt.microsecond = frac_val;
        }
    }

    // Опциональный Z в конце
    if (pos < str.size() && str[pos] == 'Z') {
        ++pos;
    }

    return dt;
}

bool DateTime::operator<(const DateTime& other) const {
    if (year != other.year)
        return year < other.year;
    if (month != other.month)
        return month < other.month;
    if (day != other.day)
        return day < other.day;
    if (hour != other.hour)
        return hour < other.hour;
    if (minute != other.minute)
        return minute < other.minute;
    if (second != other.second)
        return second < other.second;
    return microsecond < other.microsecond;
}

bool DateTime::operator<=(const DateTime& other) const {
    return !(other < *this);
}

bool DateTime::operator>(const DateTime& other) const {
    return other < *this;
}

bool DateTime::operator>=(const DateTime& other) const {
    return !(*this < other);
}

bool DateTime::operator==(const DateTime& other) const {
    return year == other.year && month == other.month && day == other.day && hour == other.hour &&
           minute == other.minute && second == other.second && microsecond == other.microsecond;
}

std::string DateTime::to_string() const {
    char buf[64];
    if (microsecond > 0) {
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%06dZ", year, month, day,
                      hour, minute, second, microsecond);
    } else {
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", year, month, day, hour,
                      minute, second);
    }
    return buf;
}

// ============================================================================
// JSON serialization helper
// ============================================================================

namespace {

/// Сериализовать Value в JSON строку
std::string value_to_json_string(const Value& value) {
    rapidjson::Document doc;
    value.to_rapidjson(doc, doc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return std::string(buffer.GetString(), buffer.GetSize());
}

}  // anonymous namespace

// ============================================================================
// Вспомогательные функции
// ============================================================================

std::string normalize_json_for_search(const Value& value) {
    // SPEC-SLICE-011 FACT-004: JSON.to_string() with backslash normalization
    std::string json_str = value_to_json_string(value);

    // Replace quadruple backslash with double backslash
    // In JSON serialization: literal backslash = escaped backslash
    // Rust does: .replace("\\\\\\\\", "\\\\")
    // This replaces 4-char sequence with 2-char sequence
    std::string result;
    result.reserve(json_str.size());

    for (std::size_t i = 0; i < json_str.size(); ++i) {
        if (i + 3 < json_str.size() && json_str[i] == '\\' && json_str[i + 1] == '\\' &&
            json_str[i + 2] == '\\' && json_str[i + 3] == '\\') {
            // Replace 4 backslashes with 2
            result += '\\';
            result += '\\';
            i += 3;  // +1 from loop
        } else {
            result += json_str[i];
        }
    }

    return result;
}

bool value_matches_patterns(const Value& value, const std::vector<std::regex>& patterns,
                            bool match_any) {
    if (patterns.empty()) {
        return true;  // Нет паттернов = всё совпадает
    }

    std::string json = normalize_json_for_search(value);

    if (match_any) {
        // OR семантика: любой паттерн совпал = true
        for (const auto& pattern : patterns) {
            if (std::regex_search(json, pattern)) {
                return true;
            }
        }
        return false;
    } else {
        // AND семантика: все паттерны должны совпасть
        for (const auto& pattern : patterns) {
            if (!std::regex_search(json, pattern)) {
                return false;
            }
        }
        return true;
    }
}

std::optional<DateTime> extract_timestamp(const Value& value, std::string_view field) {
    // Поддержка dot-notation для вложенных полей
    // Например: "Event.System.TimeCreated"

    const Value* current = &value;

    std::size_t start = 0;
    while (start < field.size()) {
        std::size_t dot_pos = field.find('.', start);
        std::string_view part;
        if (dot_pos == std::string_view::npos) {
            part = field.substr(start);
            start = field.size();
        } else {
            part = field.substr(start, dot_pos - start);
            start = dot_pos + 1;
        }

        if (!current->is_object()) {
            return std::nullopt;
        }

        const Value* next = current->get(std::string(part));
        if (!next) {
            return std::nullopt;
        }
        current = next;
    }

    // Получили значение, пробуем как строку
    if (!current->is_string()) {
        return std::nullopt;
    }

    return DateTime::parse(current->as_string());
}

// ============================================================================
// SearcherBuilder implementation
// ============================================================================

SearcherBuilder SearcherBuilder::create() {
    return SearcherBuilder();
}

SearcherBuilder& SearcherBuilder::patterns(std::vector<std::string> patterns) {
    patterns_ = std::move(patterns);
    return *this;
}

SearcherBuilder& SearcherBuilder::tau(std::vector<std::string> expressions) {
    tau_exprs_ = std::move(expressions);
    return *this;
}

SearcherBuilder& SearcherBuilder::ignore_case(bool ignore) {
    ignore_case_ = ignore;
    return *this;
}

SearcherBuilder& SearcherBuilder::match_any(bool any) {
    match_any_ = any;
    return *this;
}

SearcherBuilder& SearcherBuilder::from(DateTime datetime) {
    from_ = datetime;
    return *this;
}

SearcherBuilder& SearcherBuilder::to(DateTime datetime) {
    to_ = datetime;
    return *this;
}

SearcherBuilder& SearcherBuilder::timestamp(std::string field) {
    timestamp_ = std::move(field);
    return *this;
}

SearcherBuilder& SearcherBuilder::load_unknown(bool load) {
    load_unknown_ = load;
    return *this;
}

SearcherBuilder& SearcherBuilder::skip_errors(bool skip) {
    skip_errors_ = skip;
    return *this;
}

SearcherBuilder::BuildResult SearcherBuilder::build() {
    BuildResult result;
    result.ok = false;

    auto searcher = std::unique_ptr<Searcher>(new Searcher());

    // Компилируем regex паттерны
    // SPEC-SLICE-011 FACT-002: case-insensitive через (?i) prefix
    for (const auto& pattern : patterns_) {
        try {
            std::regex::flag_type flags = std::regex::ECMAScript;
            if (ignore_case_) {
                flags |= std::regex::icase;
            }
            searcher->regex_patterns_.emplace_back(pattern, flags);
        } catch (const std::regex_error& e) {
            result.error = std::string("invalid regex pattern '") + pattern + "': " + e.what();
            return result;
        }
    }

    // Парсим tau выражения
    // SPEC-SLICE-011 FACT-003: парсятся через tau::parse_kv
    if (!tau_exprs_.empty()) {
        std::vector<tau::Expression> exprs;
        exprs.reserve(tau_exprs_.size());

        for (const auto& kv : tau_exprs_) {
            auto parsed = tau::parse_kv(kv);
            if (!parsed) {
                result.error = std::string("invalid tau expression: ") + kv;
                return result;
            }
            exprs.push_back(std::move(*parsed));
        }

        // Объединяем в AND/OR группу
        if (exprs.size() == 1) {
            searcher->tau_expression_ = std::move(exprs[0]);
        } else {
            // SPEC-SLICE-011 FACT-006/007: match_any определяет AND/OR
            tau::BoolSym op = match_any_ ? tau::BoolSym::Or : tau::BoolSym::And;
            tau::ExprBooleanGroup group;
            group.op = op;
            group.expressions = std::move(exprs);
            searcher->tau_expression_ = tau::Expression(std::move(group));
        }
    }

    // Копируем параметры
    searcher->timestamp_ = timestamp_;
    searcher->from_ = from_;
    searcher->to_ = to_;
    searcher->match_any_ = match_any_;
    searcher->load_unknown_ = load_unknown_;
    searcher->skip_errors_ = skip_errors_;

    result.ok = true;
    result.searcher = std::move(searcher);
    return result;
}

// ============================================================================
// Searcher implementation
// ============================================================================

std::vector<SearchResult> Searcher::search(const std::filesystem::path& path) const {
    std::vector<SearchResult> results;

    // Открываем файл через Reader
    auto reader_result = io::Reader::open(path, load_unknown_, skip_errors_);
    if (!reader_result) {
        // Ошибка открытия - если skip_errors, молча пропускаем
        return results;
    }

    // Итерируем по документам
    io::Document doc;
    while (reader_result.reader->next(doc)) {
        if (matches(doc)) {
            SearchResult hit;
            hit.data = std::move(doc.data);
            hit.source = std::move(doc.source);
            hit.record_id = doc.record_id;
            hit.timestamp = doc.timestamp;
            results.push_back(std::move(hit));
        }
    }

    return results;
}

bool Searcher::matches(const io::Document& doc) const {
    // SPEC-SLICE-011: порядок проверок:
    // 1. Time filtering (если есть)
    // 2. Tau expression (если есть)
    // 3. Regex patterns (если есть)

    // 1. Time filtering
    if (has_time_filter()) {
        if (!matches_time_filter(doc.data)) {
            return false;
        }
    }

    // 2. Tau expression
    if (has_tau()) {
        if (!matches_tau(doc.data)) {
            return false;
        }
        // Если есть tau и нет patterns, документ совпал
        if (!has_patterns()) {
            return true;
        }
    }

    // 3. Regex patterns
    if (has_patterns()) {
        return matches_patterns(doc.data);
    }

    // Нет ни tau ни patterns - всё совпадает
    return true;
}

bool Searcher::matches_patterns(const Value& value) const {
    return value_matches_patterns(value, regex_patterns_, match_any_);
}

bool Searcher::matches_tau(const Value& value) const {
    if (!tau_expression_) {
        return true;
    }

    // Используем tau::solve через ValueDocument
    tau::ValueDocument doc(value);
    return tau::solve(*tau_expression_, doc);
}

bool Searcher::matches_time_filter(const Value& value) const {
    if (!timestamp_) {
        return true;  // Нет поля timestamp = не фильтруем
    }

    auto ts = extract_timestamp(value, *timestamp_);
    if (!ts) {
        // Не удалось извлечь timestamp - пропускаем документ
        return false;
    }

    // SPEC-SLICE-011 FACT-010: документы вне диапазона [from, to] исключаются
    // Rust: if ts < *from || ts > *to { return false; }
    // Но на самом деле в Rust используется <= и >= (см. search.rs:97-107)
    if (from_ && *ts <= *from_) {
        return false;
    }
    if (to_ && *ts >= *to_) {
        return false;
    }

    return true;
}

}  // namespace chainsaw::search
