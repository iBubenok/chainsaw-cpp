// ==============================================================================
// hunt.cpp - MOD-0012: Hunt Command Pipeline Implementation
// ==============================================================================
//
// SLICE-012: Hunt Command Implementation
// SPEC-SLICE-012: micro-spec поведения
//
// ==============================================================================

#include <algorithm>
#include <chainsaw/hunt.hpp>
#include <chainsaw/platform.hpp>
#include <chainsaw/sigma.hpp>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <random>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sstream>
#include <yaml-cpp/yaml.h>

namespace chainsaw::hunt {

// ============================================================================
// UUID implementation
// ============================================================================

UUID UUID::generate() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<std::uint64_t> dis;

    UUID id;
    id.high = dis(gen);
    id.low = dis(gen);

    // Set version 4 (random) bits
    id.high = (id.high & 0xFFFFFFFFFFFF0FFFUL) | 0x0000000000004000UL;
    // Set variant bits
    id.high = (id.high & 0x3FFFFFFFFFFFFFFFUL) | 0x8000000000000000UL;

    return id;
}

// ============================================================================
// Mapping loading
// ============================================================================

namespace {

/// Парсить expression из YAML node
std::optional<tau::Expression> parse_expression_from_yaml(const YAML::Node& node) {
    if (!node.IsDefined() || node.IsNull()) {
        return std::nullopt;
    }

    // Конвертируем YAML в строку для разбора через tau
    std::string yaml_str;
    if (node.IsScalar()) {
        yaml_str = node.as<std::string>();
    } else {
        std::stringstream ss;
        ss << node;
        yaml_str = ss.str();
    }

    // Простой случай: key: value
    auto kv = tau::parse_kv(yaml_str);
    if (kv) {
        return kv;
    }

    // Для сложных случаев возвращаем true expression (заглушка)
    return tau::Expression::make_bool(true);
}

/// Парсить DocumentKind из строки
std::optional<io::DocumentKind> parse_document_kind(const std::string& s) {
    if (s == "evtx")
        return io::DocumentKind::Evtx;
    if (s == "json")
        return io::DocumentKind::Json;
    if (s == "jsonl")
        return io::DocumentKind::Jsonl;
    if (s == "xml")
        return io::DocumentKind::Xml;
    if (s == "mft")
        return io::DocumentKind::Mft;
    if (s == "hve")
        return io::DocumentKind::Hve;
    if (s == "esedb")
        return io::DocumentKind::Esedb;
    return std::nullopt;
}

/// Парсить RuleKind из строки
std::optional<rule::Kind> parse_rule_kind(const std::string& s) {
    if (s == "chainsaw")
        return rule::Kind::Chainsaw;
    if (s == "sigma")
        return rule::Kind::Sigma;
    return std::nullopt;
}

/// Парсить Field из YAML node
std::optional<rule::Field> parse_field(const YAML::Node& node) {
    rule::Field field;

    if (node["name"]) {
        field.name = node["name"].as<std::string>();
    }
    if (node["from"]) {
        field.from = node["from"].as<std::string>();
    } else {
        return std::nullopt;  // from is required
    }
    if (node["to"]) {
        field.to = node["to"].as<std::string>();
    } else {
        field.to = field.from;  // default to same as from
    }
    if (node["visible"]) {
        field.visible = node["visible"].as<bool>();
    }

    // Parse cast
    if (node["cast"]) {
        std::string cast_str = node["cast"].as<std::string>();
        if (cast_str == "int") {
            field.cast = tau::ModSym::Int;
        } else if (cast_str == "str") {
            field.cast = tau::ModSym::Str;
        } else if (cast_str == "flt") {
            field.cast = tau::ModSym::Flt;
        }
    }

    // Parse container
    if (node["container"]) {
        rule::Container container;
        container.field = node["container"]["field"].as<std::string>();
        std::string format = node["container"]["format"].as<std::string>("json");
        if (format == "json") {
            container.format = rule::ContainerFormat::Json;
        } else if (format == "kv") {
            container.format = rule::ContainerFormat::Kv;
            if (node["container"]["delimiter"]) {
                rule::KvFormat kv;
                kv.delimiter = node["container"]["delimiter"].as<std::string>();
                kv.separator = node["container"]["separator"].as<std::string>("=");
                kv.trim = node["container"]["trim"].as<bool>(false);
                container.kv_params = kv;
            }
        }
        field.container = container;
    }

    return field;
}

/// Парсить Group из YAML node
std::optional<Group> parse_group(const YAML::Node& node) {
    Group group;
    group.id = UUID::generate();

    if (!node["name"])
        return std::nullopt;
    group.name = node["name"].as<std::string>();

    if (!node["timestamp"])
        return std::nullopt;
    group.timestamp = node["timestamp"].as<std::string>();

    // Parse filter
    if (node["filter"]) {
        auto expr = parse_expression_from_yaml(node["filter"]);
        if (expr) {
            group.filter = std::move(*expr);
        } else {
            group.filter = tau::Expression::make_bool(true);
        }
    } else {
        group.filter = tau::Expression::make_bool(true);
    }

    // Parse fields
    if (node["fields"] && node["fields"].IsSequence()) {
        for (const auto& field_node : node["fields"]) {
            auto field = parse_field(field_node);
            if (field) {
                group.fields.push_back(std::move(*field));
            }
        }
    }

    return group;
}

/// Парсить Precondition из YAML node
std::optional<Precondition> parse_precondition(const YAML::Node& node) {
    Precondition precond;

    if (node["for"] && node["for"].IsMap()) {
        for (const auto& kv : node["for"]) {
            precond.for_[kv.first.as<std::string>()] = kv.second.as<std::string>();
        }
    }

    if (node["filter"]) {
        auto expr = parse_expression_from_yaml(node["filter"]);
        if (expr) {
            precond.filter = std::move(*expr);
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    return precond;
}

}  // anonymous namespace

MappingResult load_mapping(const std::filesystem::path& path) {
    MappingResult result;
    result.ok = false;

    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            result.error = "cannot open mapping file: " + platform::path_to_utf8(path);
            return result;
        }

        YAML::Node root = YAML::Load(file);

        // Parse kind (required)
        if (!root["kind"]) {
            result.error = "mapping file missing 'kind' field";
            return result;
        }
        auto kind = parse_document_kind(root["kind"].as<std::string>());
        if (!kind) {
            result.error = "invalid document kind in mapping";
            return result;
        }
        result.mapping.kind = *kind;

        // Parse rules (required)
        if (!root["rules"]) {
            result.error = "mapping file missing 'rules' field";
            return result;
        }
        auto rules_kind = parse_rule_kind(root["rules"].as<std::string>());
        if (!rules_kind) {
            result.error = "invalid rules kind in mapping";
            return result;
        }
        result.mapping.rules = *rules_kind;

        // Parse exclusions (optional)
        if (root["exclusions"] && root["exclusions"].IsSequence()) {
            for (const auto& excl : root["exclusions"]) {
                result.mapping.exclusions.insert(excl.as<std::string>());
            }
        }

        // Parse extensions (optional)
        if (root["extensions"]) {
            Extensions ext;
            if (root["extensions"]["preconditions"] &&
                root["extensions"]["preconditions"].IsSequence()) {
                std::vector<Precondition> preconds;
                for (const auto& p : root["extensions"]["preconditions"]) {
                    auto precond = parse_precondition(p);
                    if (precond) {
                        preconds.push_back(std::move(*precond));
                    }
                }
                if (!preconds.empty()) {
                    ext.preconditions = std::move(preconds);
                }
            }
            result.mapping.extensions = std::move(ext);
        }

        // Parse groups (required)
        if (!root["groups"] || !root["groups"].IsSequence()) {
            result.error = "mapping file missing 'groups' field";
            return result;
        }
        for (const auto& g : root["groups"]) {
            auto group = parse_group(g);
            if (group) {
                result.mapping.groups.push_back(std::move(*group));
            }
        }

        // Sort groups by name (SPEC-SLICE-012 FACT-005)
        std::sort(result.mapping.groups.begin(), result.mapping.groups.end(),
                  [](const Group& a, const Group& b) { return a.name < b.name; });

        result.ok = true;
        return result;

    } catch (const YAML::Exception& e) {
        result.error = std::string("YAML parse error: ") + e.what();
        return result;
    } catch (const std::exception& e) {
        result.error = std::string("error loading mapping: ") + e.what();
        return result;
    }
}

// ============================================================================
// Mapper implementation
// ============================================================================

Mapper Mapper::from(std::vector<rule::Field> fields) {
    Mapper mapper;
    mapper.fields_ = std::move(fields);

    // Determine mode based on fields
    bool has_full = false;
    bool has_fast = false;

    for (const auto& field : mapper.fields_) {
        if (field.cast.has_value() || field.container.has_value()) {
            has_full = true;
            break;
        }
        if (field.from != field.to) {
            has_fast = true;
        }
    }

    if (has_full) {
        mapper.mode_ = MapperMode::Full;
        for (const auto& field : mapper.fields_) {
            FullEntry entry;
            entry.to = field.to;
            entry.container = field.container;
            entry.cast = field.cast;
            mapper.full_map_[field.from] = std::move(entry);
        }
    } else if (has_fast) {
        mapper.mode_ = MapperMode::Fast;
        for (const auto& field : mapper.fields_) {
            mapper.fast_map_[field.from] = field.to;
        }
    } else {
        mapper.mode_ = MapperMode::None;
    }

    return mapper;
}

std::optional<Value> Mapper::find(const tau::Document& doc, std::string_view key) const {
    std::string key_str(key);

    switch (mode_) {
    case MapperMode::None:
        return doc.find(key);

    case MapperMode::Fast: {
        auto it = fast_map_.find(key_str);
        if (it != fast_map_.end()) {
            return doc.find(it->second);
        }
        return doc.find(key);
    }

    case MapperMode::Full: {
        auto it = full_map_.find(key_str);
        if (it == full_map_.end()) {
            return doc.find(key);
        }

        const auto& entry = it->second;

        // Handle container
        if (entry.container.has_value()) {
            // Get the container field value
            auto container_val = doc.find(entry.container->field);
            if (!container_val || !container_val->is_string()) {
                return std::nullopt;
            }

            // Parse container based on format
            if (entry.container->format == rule::ContainerFormat::Json) {
                // Parse JSON
                rapidjson::Document json_doc;
                json_doc.Parse(container_val->as_string().data());
                if (json_doc.HasParseError()) {
                    return std::nullopt;
                }
                // Look up the target field in parsed JSON
                Value json_val = Value::from_rapidjson(json_doc);
                const Value* result = json_val.get(entry.to);
                if (result) {
                    return *result;
                }
                return std::nullopt;
            } else if (entry.container->format == rule::ContainerFormat::Kv) {
                // Parse key-value pairs
                if (!entry.container->kv_params) {
                    return std::nullopt;
                }
                const auto& kv = *entry.container->kv_params;
                std::string_view str = container_val->as_string();

                // Split by delimiter
                std::size_t pos = 0;
                while (pos < str.size()) {
                    std::size_t delim_pos = str.find(kv.delimiter, pos);
                    std::string_view item;
                    if (delim_pos == std::string_view::npos) {
                        item = str.substr(pos);
                        pos = str.size();
                    } else {
                        item = str.substr(pos, delim_pos - pos);
                        pos = delim_pos + kv.delimiter.size();
                    }

                    // Trim if needed
                    if (kv.trim) {
                        while (!item.empty() &&
                               std::isspace(static_cast<unsigned char>(item.front()))) {
                            item.remove_prefix(1);
                        }
                        while (!item.empty() &&
                               std::isspace(static_cast<unsigned char>(item.back()))) {
                            item.remove_suffix(1);
                        }
                    }

                    // Split by separator
                    std::size_t sep_pos = item.find(kv.separator);
                    if (sep_pos != std::string_view::npos) {
                        std::string_view k = item.substr(0, sep_pos);
                        std::string_view v = item.substr(sep_pos + kv.separator.size());
                        if (k == entry.to) {
                            return Value(std::string(v));
                        }
                    }
                }
                return std::nullopt;
            }
        }

        // Handle cast
        auto val = doc.find(entry.to);
        if (!val) {
            return std::nullopt;
        }

        if (entry.cast.has_value()) {
            switch (*entry.cast) {
            case tau::ModSym::Int:
                if (val->is_string()) {
                    try {
                        std::int64_t i = std::stoll(std::string(val->as_string()));
                        return Value(i);
                    } catch (...) {
                        return val;
                    }
                }
                return val;

            case tau::ModSym::Str:
                // Convert to string
                if (val->is_string()) {
                    return val;
                } else if (val->is_int()) {
                    return Value(std::to_string(val->as_int()));
                } else if (val->is_double()) {
                    return Value(std::to_string(val->as_double()));
                } else if (val->is_bool()) {
                    return Value(val->as_bool() ? "true" : "false");
                }
                return val;

            case tau::ModSym::Flt:
                if (val->is_string()) {
                    try {
                        double d = std::stod(std::string(val->as_string()));
                        return Value(d);
                    } catch (...) {
                        return val;
                    }
                }
                return val;
            }
        }

        return val;
    }
    }

    return std::nullopt;
}

// ============================================================================
// MappedDocument implementation
// ============================================================================

std::optional<Value> MappedDocument::find(std::string_view key) const {
    return mapper_.find(doc_, key);
}

// ============================================================================
// Hunt implementation
// ============================================================================

bool Hunt::is_aggregation() const {
    if (std::holds_alternative<HuntKindGroup>(kind)) {
        return true;
    }
    const auto& rule_kind = std::get<HuntKindRule>(kind);
    return rule_kind.aggregate.has_value();
}

// ============================================================================
// HunterBuilder implementation
// ============================================================================

HunterBuilder HunterBuilder::create() {
    return HunterBuilder();
}

HunterBuilder& HunterBuilder::mappings(std::vector<std::filesystem::path> paths) {
    mappings_ = std::move(paths);
    return *this;
}

HunterBuilder& HunterBuilder::rules(std::vector<rule::Rule> rules) {
    rules_ = std::move(rules);
    return *this;
}

HunterBuilder& HunterBuilder::load_unknown(bool load) {
    load_unknown_ = load;
    return *this;
}

HunterBuilder& HunterBuilder::local(bool local) {
    local_ = local;
    return *this;
}

HunterBuilder& HunterBuilder::preprocess(bool preprocess) {
    preprocess_ = preprocess;
    return *this;
}

HunterBuilder& HunterBuilder::from(DateTime datetime) {
    from_ = datetime;
    return *this;
}

HunterBuilder& HunterBuilder::to(DateTime datetime) {
    to_ = datetime;
    return *this;
}

HunterBuilder& HunterBuilder::skip_errors(bool skip) {
    skip_errors_ = skip;
    return *this;
}

HunterBuilder& HunterBuilder::timezone(std::string tz) {
    timezone_ = std::move(tz);
    return *this;
}

HunterBuilder::BuildResult HunterBuilder::build() {
    BuildResult result;
    result.ok = false;

    auto hunter = std::unique_ptr<Hunter>(new Hunter());

    // Process rules
    if (rules_.has_value()) {
        auto& rules = *rules_;

        // Sort rules by name (SPEC-SLICE-012 FACT-002)
        std::sort(rules.begin(), rules.end(), [](const rule::Rule& a, const rule::Rule& b) {
            return rule::rule_name(a) < rule::rule_name(b);
        });

        for (auto& rule : rules) {
            UUID uuid = UUID::generate();

            // Create Hunt for Chainsaw rules (SPEC-SLICE-012 FACT-003)
            if (std::holds_alternative<rule::ChainsawRule>(rule)) {
                auto& chainsaw = std::get<rule::ChainsawRule>(rule);

                Mapper mapper = Mapper::from(chainsaw.fields);

                HuntKindRule hunt_kind;
                hunt_kind.aggregate = chainsaw.aggregate;

                // Copy filter properly using clone
                if (std::holds_alternative<tau::Detection>(chainsaw.filter)) {
                    const auto& det = std::get<tau::Detection>(chainsaw.filter);
                    tau::Detection det_copy;
                    det_copy.expression = tau::clone(det.expression);
                    // Clone each identifier
                    for (const auto& [key, expr] : det.identifiers) {
                        det_copy.identifiers.emplace(key, tau::clone(expr));
                    }
                    hunt_kind.filter = std::move(det_copy);
                } else {
                    hunt_kind.filter = tau::clone(std::get<tau::Expression>(chainsaw.filter));
                }

                Hunt hunt;
                hunt.id = uuid;
                hunt.group = chainsaw.group;
                hunt.kind = std::move(hunt_kind);
                hunt.mapper = std::move(mapper);
                hunt.timestamp = chainsaw.timestamp;
                hunt.file = chainsaw.kind;

                hunter->hunts_.push_back(std::move(hunt));
            }

            hunter->rules_.insert({uuid, std::move(rule)});
        }
    }

    // Process mappings
    if (mappings_.has_value()) {
        auto& mapping_paths = *mappings_;

        // Sort mappings by path (SPEC-SLICE-012 FACT-005)
        std::sort(mapping_paths.begin(), mapping_paths.end());

        for (const auto& path : mapping_paths) {
            auto mapping_result = load_mapping(path);
            if (!mapping_result.ok) {
                result.error = mapping_result.error;
                return result;
            }

            auto& mapping = mapping_result.mapping;

            // SPEC-SLICE-012 FACT-008: Chainsaw rules don't support mappings
            if (mapping.rules == rule::Kind::Chainsaw) {
                result.error = "Chainsaw rules do not support mappings";
                return result;
            }

            // Create hunts for each group
            for (auto& group : mapping.groups) {
                // Build exclusions set
                std::unordered_set<UUID, UUID::Hash> exclusions;
                for (const auto& [rid, rule] : hunter->rules_) {
                    if (mapping.exclusions.count(rule::rule_name(rule)) > 0) {
                        exclusions.insert(rid);
                    }
                }

                // Build preconditions map for this group (clone expressions)
                std::unordered_map<UUID, tau::Expression, UUID::Hash> preconds;
                if (mapping.extensions.has_value() &&
                    mapping.extensions->preconditions.has_value()) {
                    for (const auto& precond : *mapping.extensions->preconditions) {
                        // Match precondition to rules
                        for (const auto& [rid, rule] : hunter->rules_) {
                            if (std::holds_alternative<rule::SigmaRule>(rule)) {
                                const auto& sigma = std::get<rule::SigmaRule>(rule);
                                bool matched = true;

                                for (const auto& [field, value] : precond.for_) {
                                    auto found = sigma.find(field);
                                    if (!found || *found != value) {
                                        matched = false;
                                        break;
                                    }
                                }

                                if (matched) {
                                    preconds.emplace(rid, tau::clone(precond.filter));
                                }
                            }
                        }
                    }
                }

                Mapper mapper = Mapper::from(std::move(group.fields));

                HuntKindGroup hunt_kind;
                hunt_kind.exclusions = std::move(exclusions);
                hunt_kind.filter = std::move(group.filter);
                hunt_kind.kind = mapping.rules;
                hunt_kind.preconditions = std::move(preconds);

                Hunt hunt;
                hunt.id = group.id;
                hunt.group = std::move(group.name);
                hunt.kind = std::move(hunt_kind);
                hunt.mapper = std::move(mapper);
                hunt.timestamp = std::move(group.timestamp);

                // Handle document kind (SPEC-SLICE-012: jsonl -> json internally)
                if (mapping.kind == io::DocumentKind::Jsonl) {
                    hunt.file = io::DocumentKind::Json;
                } else {
                    hunt.file = mapping.kind;
                }

                hunter->hunts_.push_back(std::move(hunt));
            }
        }
    }

    // Copy settings
    hunter->load_unknown_ = load_unknown_.value_or(false);
    hunter->preprocess_ = preprocess_.value_or(false);
    hunter->skip_errors_ = skip_errors_.value_or(false);
    hunter->from_ = from_;
    hunter->to_ = to_;

    result.ok = true;
    result.hunter = std::move(hunter);
    return result;
}

// ============================================================================
// Hunter implementation
// ============================================================================

bool Hunter::should_skip(const DateTime& timestamp) const {
    // SPEC-SLICE-012 FACT-013: документы вне диапазона [from, to] пропускаются
    if (from_.has_value() && timestamp <= *from_) {
        return true;
    }
    if (to_.has_value() && timestamp >= *to_) {
        return true;
    }
    return false;
}

Hunter::HuntResult Hunter::hunt(const std::filesystem::path& path, std::FILE* cache_file) const {
    HuntResult result;
    result.ok = false;

    // Open file using Reader
    auto reader_result = io::Reader::open(path, load_unknown_, skip_errors_);
    if (!reader_result) {
        if (skip_errors_) {
            result.ok = true;
            return result;
        }
        result.error = reader_result.error.message;
        return result;
    }

    auto& reader = *reader_result.reader;
    io::DocumentKind file_kind = reader.kind();

    // Aggregation state
    struct AggregateState {
        const rule::Aggregate* aggregate = nullptr;
        std::unordered_map<std::size_t, std::vector<UUID>> docs;  // hash -> doc IDs
    };

    // Custom hash for pair<UUID, UUID>
    auto pair_hash = [](const std::pair<UUID, UUID>& p) -> std::size_t {
        return UUID::Hash{}(p.first) ^ (UUID::Hash{}(p.second) << 1);
    };
    std::unordered_map<std::pair<UUID, UUID>, AggregateState, decltype(pair_hash)> aggregates(
        16, pair_hash);

    std::unordered_map<UUID, std::pair<Value, DateTime>, UUID::Hash> stored_docs;
    std::size_t cache_offset = 0;

    // Iterate through documents
    io::Document doc;
    while (reader.next(doc)) {
        UUID document_id = UUID::generate();

        // Check document kind matches hunt
        std::vector<Hit> hits;

        for (const auto& hunt : hunts_) {
            // SPEC-SLICE-012 FACT-011: проверка hunt.file == document.kind
            if (hunt.file != file_kind) {
                continue;
            }

            // Create mapped document
            tau::ValueDocument value_doc(doc.data);
            MappedDocument mapped(value_doc, hunt.mapper);

            // Extract timestamp (SPEC-SLICE-012 FACT-012)
            auto ts_val = mapped.find(hunt.timestamp);
            if (!ts_val || !ts_val->is_string()) {
                continue;
            }

            auto timestamp = DateTime::parse(ts_val->as_string());
            if (!timestamp) {
                if (skip_errors_) {
                    continue;
                }
                result.error =
                    std::string("failed to parse timestamp: ") + std::string(ts_val->as_string());
                return result;
            }

            // Time filtering (SPEC-SLICE-012 FACT-013)
            if (should_skip(*timestamp)) {
                continue;
            }

            // Match based on hunt kind
            if (std::holds_alternative<HuntKindGroup>(hunt.kind)) {
                const auto& group_kind = std::get<HuntKindGroup>(hunt.kind);

                // SPEC-SLICE-012 FACT-020: сначала проверяем group filter
                if (!tau::solve(group_kind.filter, mapped)) {
                    continue;
                }

                // Check all matching rules
                for (const auto& [rid, rule] : rules_) {
                    // SPEC-SLICE-012: проверка типа правила
                    if (!rule::rule_is_kind(rule, group_kind.kind)) {
                        continue;
                    }

                    // SPEC-SLICE-012 FACT-006: проверка exclusions
                    if (group_kind.exclusions.count(rid) > 0) {
                        continue;
                    }

                    // SPEC-SLICE-012 FACT-007: проверка preconditions
                    auto precond_it = group_kind.preconditions.find(rid);
                    if (precond_it != group_kind.preconditions.end()) {
                        if (!tau::solve(precond_it->second, mapped)) {
                            continue;
                        }
                    }

                    // Check rule
                    if (!rule::rule_solve(rule, mapped)) {
                        continue;
                    }

                    // Check for aggregation
                    const auto& agg = rule::rule_aggregate(rule);
                    if (agg.has_value()) {
                        // Store document for aggregation
                        stored_docs[document_id] = {doc.data, *timestamp};

                        // Compute hash of aggregate fields
                        std::size_t hash = 0;
                        bool skip = false;
                        for (const auto& field : agg->fields) {
                            auto val = mapped.find(field);
                            if (val && val->is_string()) {
                                hash ^= std::hash<std::string>{}(std::string(val->as_string()));
                            } else {
                                skip = true;
                                break;
                            }
                        }
                        if (skip)
                            continue;

                        auto key = std::make_pair(hunt.id, rid);
                        auto& state = aggregates[key];
                        state.aggregate = &(*agg);
                        state.docs[hash].push_back(document_id);
                    } else {
                        hits.push_back(Hit{hunt.id, rid, *timestamp});
                    }
                }
            } else {
                // HuntKindRule
                const auto& rule_kind = std::get<HuntKindRule>(hunt.kind);

                // SPEC-SLICE-012 FACT-019: проверка фильтра правила
                bool hit = false;
                if (std::holds_alternative<tau::Detection>(rule_kind.filter)) {
                    hit = tau::solve(std::get<tau::Detection>(rule_kind.filter), mapped);
                } else {
                    hit = tau::solve(std::get<tau::Expression>(rule_kind.filter), mapped);
                }

                if (hit) {
                    if (rule_kind.aggregate.has_value()) {
                        // Store document for aggregation
                        stored_docs[document_id] = {doc.data, *timestamp};

                        // Compute hash of aggregate fields
                        std::size_t hash = 0;
                        bool skip = false;
                        for (const auto& field : rule_kind.aggregate->fields) {
                            auto val = mapped.find(field);
                            if (val && val->is_string()) {
                                hash ^= std::hash<std::string>{}(std::string(val->as_string()));
                            } else {
                                skip = true;
                                break;
                            }
                        }
                        if (!skip) {
                            auto key = std::make_pair(hunt.id, hunt.id);
                            auto& state = aggregates[key];
                            state.aggregate = &(*rule_kind.aggregate);
                            state.docs[hash].push_back(document_id);
                        }
                    } else {
                        hits.push_back(Hit{hunt.id, hunt.id, *timestamp});
                    }
                }
            }
        }

        // Add detection if we have hits
        if (!hits.empty()) {
            Detections det;
            det.hits = std::move(hits);

            if (cache_file) {
                // Cache-to-disk mode
                rapidjson::Document json_doc;
                doc.data.to_rapidjson(json_doc, json_doc.GetAllocator());

                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                json_doc.Accept(writer);

                std::fwrite(buffer.GetString(), 1, buffer.GetSize(), cache_file);

                KindCached cached;
                cached.kind = file_kind;
                cached.path = platform::path_to_utf8(path);
                cached.offset = cache_offset;
                cached.size = buffer.GetSize();
                cache_offset += buffer.GetSize();

                det.kind = std::move(cached);
            } else {
                KindIndividual ind;
                ind.document.kind = file_kind;
                ind.document.path = platform::path_to_utf8(path);
                ind.document.data = std::move(doc.data);
                det.kind = std::move(ind);
            }

            result.detections.push_back(std::move(det));
        }
    }

    // Process aggregations (SPEC-SLICE-012 FACT-022-024)
    for (const auto& [key, state] : aggregates) {
        const auto& [hid, rid] = key;

        for (const auto& [hash, doc_ids] : state.docs) {
            // Check count pattern (SPEC-SLICE-012 FACT-023)
            bool hit = false;
            std::size_t count = doc_ids.size();

            if (std::holds_alternative<tau::PatternEqual>(state.aggregate->count)) {
                auto i = std::get<tau::PatternEqual>(state.aggregate->count).value;
                hit = count == static_cast<std::size_t>(i);
            } else if (std::holds_alternative<tau::PatternGreaterThan>(state.aggregate->count)) {
                auto i = std::get<tau::PatternGreaterThan>(state.aggregate->count).value;
                hit = count > static_cast<std::size_t>(i);
            } else if (std::holds_alternative<tau::PatternGreaterThanOrEqual>(
                           state.aggregate->count)) {
                auto i = std::get<tau::PatternGreaterThanOrEqual>(state.aggregate->count).value;
                hit = count >= static_cast<std::size_t>(i);
            } else if (std::holds_alternative<tau::PatternLessThan>(state.aggregate->count)) {
                auto i = std::get<tau::PatternLessThan>(state.aggregate->count).value;
                hit = count < static_cast<std::size_t>(i);
            } else if (std::holds_alternative<tau::PatternLessThanOrEqual>(
                           state.aggregate->count)) {
                auto i = std::get<tau::PatternLessThanOrEqual>(state.aggregate->count).value;
                hit = count <= static_cast<std::size_t>(i);
            }

            if (hit) {
                std::vector<Document> documents;
                std::vector<DateTime> timestamps;

                for (const auto& doc_id : doc_ids) {
                    auto it = stored_docs.find(doc_id);
                    if (it != stored_docs.end()) {
                        Document d;
                        d.kind = file_kind;
                        d.path = platform::path_to_utf8(path);
                        d.data = it->second.first;
                        documents.push_back(std::move(d));
                        timestamps.push_back(it->second.second);
                    }
                }

                // Sort timestamps
                std::sort(timestamps.begin(), timestamps.end());

                Detections det;
                det.hits.push_back(Hit{hid, rid, timestamps.front()});

                KindAggregate agg;
                agg.documents = std::move(documents);
                det.kind = std::move(agg);

                result.detections.push_back(std::move(det));
            }
        }
    }

    result.ok = true;
    return result;
}

std::unordered_set<std::string> Hunter::extensions() const {
    std::unordered_set<std::string> exts;

    for (const auto& [_, rule] : rules_) {
        io::DocumentKind kind = rule::rule_types(rule);
        auto rule_exts = io::document_kind_extensions(kind);
        exts.insert(rule_exts.begin(), rule_exts.end());
    }

    for (const auto& hunt : hunts_) {
        auto hunt_exts = io::document_kind_extensions(hunt.file);
        exts.insert(hunt_exts.begin(), hunt_exts.end());
    }

    return exts;
}

// ============================================================================
// Output formatting
// ============================================================================

namespace {

/// Найти hunt по ID
const Hunt* find_hunt(const std::vector<Hunt>& hunts, const UUID& id) {
    for (const auto& hunt : hunts) {
        if (hunt.id == id) {
            return &hunt;
        }
    }
    return nullptr;
}

/// Найти rule по ID
const rule::Rule* find_rule(const std::unordered_map<UUID, rule::Rule, UUID::Hash>& rules,
                            const UUID& id) {
    auto it = rules.find(id);
    if (it != rules.end()) {
        return &it->second;
    }
    return nullptr;
}

}  // anonymous namespace

std::string detections_to_json(const Detections& det, const std::vector<Hunt>& hunts,
                               const std::unordered_map<UUID, rule::Rule, UUID::Hash>& rules,
                               bool local_time) {
    (void)local_time;  // TODO: implement timezone handling

    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    // Get first hit for metadata
    if (det.hits.empty()) {
        return "{}";
    }

    const auto& hit = det.hits.front();
    const Hunt* hunt = find_hunt(hunts, hit.hunt);
    const rule::Rule* rule_ptr = find_rule(rules, hit.rule);

    if (hunt) {
        doc.AddMember("group", rapidjson::Value(hunt->group.c_str(), alloc), alloc);
    }

    doc.AddMember("timestamp", rapidjson::Value(hit.timestamp.to_string().c_str(), alloc), alloc);

    if (rule_ptr) {
        doc.AddMember("name", rapidjson::Value(rule::rule_name(*rule_ptr).c_str(), alloc), alloc);

        // Authors
        rapidjson::Value authors_arr(rapidjson::kArrayType);
        // TODO: extract authors from rule
        doc.AddMember("authors", authors_arr, alloc);

        doc.AddMember("level",
                      rapidjson::Value(rule::to_string(rule::rule_level(*rule_ptr)).c_str(), alloc),
                      alloc);
        doc.AddMember(
            "status",
            rapidjson::Value(rule::to_string(rule::rule_status(*rule_ptr)).c_str(), alloc), alloc);
    }

    // Add document data
    if (std::holds_alternative<KindIndividual>(det.kind)) {
        const auto& ind = std::get<KindIndividual>(det.kind);
        rapidjson::Document data_doc;
        ind.document.data.to_rapidjson(data_doc, alloc);
        doc.AddMember("document", data_doc, alloc);
        doc.AddMember("kind", "individual", alloc);
    } else if (std::holds_alternative<KindAggregate>(det.kind)) {
        const auto& agg = std::get<KindAggregate>(det.kind);
        rapidjson::Value docs_arr(rapidjson::kArrayType);
        for (const auto& d : agg.documents) {
            rapidjson::Document data_doc;
            d.data.to_rapidjson(data_doc, alloc);
            docs_arr.PushBack(data_doc, alloc);
        }
        doc.AddMember("documents", docs_arr, alloc);
        doc.AddMember("kind", "aggregate", alloc);
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return std::string(buffer.GetString(), buffer.GetSize());
}

std::string detections_to_jsonl(const Detections& det, const std::vector<Hunt>& hunts,
                                const std::unordered_map<UUID, rule::Rule, UUID::Hash>& rules,
                                bool local_time) {
    // JSONL is just JSON without pretty printing, one per line
    return detections_to_json(det, hunts, rules, local_time) + "\n";
}

std::string format_table(const std::vector<Detections>& detections, const std::vector<Hunt>& hunts,
                         const std::unordered_map<UUID, rule::Rule, UUID::Hash>& rules,
                         std::uint32_t column_width, bool full, bool metadata, bool local_time) {
    (void)column_width;
    (void)full;
    (void)metadata;
    (void)local_time;

    if (detections.empty()) {
        return "";
    }

    std::stringstream ss;

    // Header
    ss << "\n";

    // Unicode box-drawing characters
    const char* top_left = "\xe2\x94\x8c";      // ┌
    const char* top_right = "\xe2\x94\x90";     // ┐
    const char* bottom_left = "\xe2\x94\x94";   // └
    const char* bottom_right = "\xe2\x94\x98";  // ┘
    const char* horizontal = "\xe2\x94\x80";    // ─
    const char* vertical = "\xe2\x94\x82";      // │
    const char* t_down = "\xe2\x94\xac";        // ┬
    const char* t_up = "\xe2\x94\xb4";          // ┴
    const char* t_right = "\xe2\x94\x9c";       // ├
    const char* t_left = "\xe2\x94\xa4";        // ┤
    const char* cross = "\xe2\x94\xbc";         // ┼

    // Column widths
    constexpr int col_timestamp = 24;
    constexpr int col_detections = 50;
    constexpr int col_data = 40;

    // Draw top border
    ss << top_left;
    for (int i = 0; i < col_timestamp; ++i)
        ss << horizontal;
    ss << t_down;
    for (int i = 0; i < col_detections; ++i)
        ss << horizontal;
    ss << t_down;
    for (int i = 0; i < col_data; ++i)
        ss << horizontal;
    ss << top_right << "\n";

    // Header row
    ss << vertical << " Timestamp              " << vertical
       << " Detections                                       " << vertical
       << " Data                                   " << vertical << "\n";

    // Header separator
    ss << t_right;
    for (int i = 0; i < col_timestamp; ++i)
        ss << horizontal;
    ss << cross;
    for (int i = 0; i < col_detections; ++i)
        ss << horizontal;
    ss << cross;
    for (int i = 0; i < col_data; ++i)
        ss << horizontal;
    ss << t_left << "\n";

    // Data rows
    for (const auto& det : detections) {
        if (det.hits.empty())
            continue;

        const auto& hit = det.hits.front();
        const Hunt* hunt = find_hunt(hunts, hit.hunt);
        const rule::Rule* rule_ptr = find_rule(rules, hit.rule);

        // Timestamp
        ss << vertical << " " << hit.timestamp.to_string();

        // Pad timestamp
        int ts_len = static_cast<int>(hit.timestamp.to_string().size());
        for (int i = ts_len + 1; i < col_timestamp; ++i)
            ss << " ";
        ss << vertical;

        // Detections column
        std::string det_str;
        if (hunt) {
            // ANSI green for group name
            det_str = "\033[32m" + hunt->group + "\033[0m";
        }
        if (rule_ptr) {
            det_str += " " + std::string(RULE_PREFIX) + " " + rule::rule_name(*rule_ptr);
        }

        ss << " " << det_str;

        // Pad detections (approximation, ANSI codes make this tricky)
        int det_visible_len = hunt ? static_cast<int>(hunt->group.size()) : 0;
        if (rule_ptr) {
            det_visible_len += 3 + static_cast<int>(rule::rule_name(*rule_ptr).size());
        }
        for (int i = det_visible_len + 1; i < col_detections; ++i)
            ss << " ";
        ss << vertical;

        // Data column (abbreviated)
        ss << " ...";
        for (int i = 4; i < col_data; ++i)
            ss << " ";
        ss << vertical << "\n";
    }

    // Bottom border
    ss << bottom_left;
    for (int i = 0; i < col_timestamp; ++i)
        ss << horizontal;
    ss << t_up;
    for (int i = 0; i < col_detections; ++i)
        ss << horizontal;
    ss << t_up;
    for (int i = 0; i < col_data; ++i)
        ss << horizontal;
    ss << bottom_right << "\n";

    return ss.str();
}

}  // namespace chainsaw::hunt
