// ==============================================================================
// main.cpp - MOD-0001: Точка входа приложения
// ==============================================================================
//
// MOD-0001 app
// CLI-0001: CLI-контракт Chainsaw (baseline)
// TOBE-0001: архитектура порта
// GUIDE-0001 G-024: перехват исключений на границе app
//
// Точка входа:
// 1. Парсинг argv через MOD-0002 cli
// 2. Создание Writer (MOD-0003 output)
// 3. Dispatch команды
// 4. Возврат exit code
//
// ==============================================================================

#include "chainsaw/cli.hpp"
#include "chainsaw/discovery.hpp"
#include "chainsaw/hunt.hpp"
#include "chainsaw/output.hpp"
#include "chainsaw/platform.hpp"
#include "chainsaw/reader.hpp"
#include "chainsaw/rule.hpp"
#include "chainsaw/search.hpp"
#include "chainsaw/shimcache.hpp"
#include "chainsaw/srum.hpp"
#include "chainsaw/tau.hpp"

#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <unordered_set>

namespace {

// ----------------------------------------------------------------------------
// ASCII Banner (CLI-0001 1.2: --no-banner)
// ----------------------------------------------------------------------------

constexpr const char* BANNER = R"(
     ██████╗██╗  ██╗ █████╗ ██╗███╗   ██╗███████╗ █████╗ ██╗    ██╗
    ██╔════╝██║  ██║██╔══██╗██║████╗  ██║██╔════╝██╔══██╗██║    ██║
    ██║     ███████║███████║██║██╔██╗ ██║███████╗███████║██║ █╗ ██║
    ██║     ██╔══██║██╔══██║██║██║╚██╗██║╚════██║██╔══██║██║███╗██║
    ╚██████╗██║  ██║██║  ██║██║██║ ╚████║███████║██║  ██║╚███╔███╔╝
     ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝ ╚══╝╚══╝
)";

// ----------------------------------------------------------------------------
// Вывод баннера
// ----------------------------------------------------------------------------

void print_banner(chainsaw::output::Writer& writer, bool no_banner, bool quiet) {
    if (no_banner || quiet) {
        return;
    }
    writer.write(chainsaw::output::Stream::Stderr, BANNER);
    writer.write_line(chainsaw::output::Stream::Stderr, "");
}

// ----------------------------------------------------------------------------
// Выполнение команд (заглушки для )
// ----------------------------------------------------------------------------

int run_dump(const chainsaw::cli::DumpCommand& cmd, const chainsaw::cli::GlobalOptions& global,
             chainsaw::output::Writer& writer) {
    (void)global;
    using namespace chainsaw;

    // SPEC-SLICE-013 FACT-001: Dump требует хотя бы один path
    if (cmd.paths.empty()) {
        writer.error("dump command requires at least one path");
        return 1;
    }

    // SPEC-SLICE-013: Если указан output file, создаём новый Writer
    std::unique_ptr<output::Writer> file_writer;
    output::Writer* out = &writer;
    if (cmd.output.has_value()) {
        output::OutputConfig out_cfg = writer.config();
        out_cfg.output_path = cmd.output;
        file_writer = std::make_unique<output::Writer>(out_cfg);
        out = file_writer.get();
    }

    // SPEC-SLICE-013 FACT-005: Информационное сообщение о путях и расширениях
    std::string paths_str;
    for (size_t i = 0; i < cmd.paths.size(); ++i) {
        if (i > 0)
            paths_str += ", ";
        paths_str += platform::path_to_utf8(cmd.paths[i]);
    }
    std::string ext_str = cmd.extension.has_value() ? *cmd.extension : "*";
    writer.info("Dumping the contents of forensic artefacts from: " + paths_str +
                " (extensions: " + ext_str + ")");

    // SPEC-SLICE-013 FACT-006: JSON формат начинается с "["
    if (cmd.json) {
        out->write(output::Stream::Stdout, "[");
    }

    // Собираем расширения для discovery
    io::DiscoveryOptions disc_opt;
    disc_opt.skip_errors = cmd.skip_errors;
    if (cmd.extension.has_value()) {
        std::unordered_set<std::string> exts;
        exts.insert(*cmd.extension);
        disc_opt.extensions = std::move(exts);
    }

    // Находим файлы
    auto files = io::discover_files(cmd.paths, disc_opt);

    // SPEC-SLICE-013 FACT-013: Если файлы не найдены — ошибка
    if (files.empty()) {
        writer.error("No compatible files were found in the provided paths");
        return 1;
    }

    // Подсчёт общего размера для информационного сообщения
    std::uint64_t total_size = 0;
    for (const auto& file : files) {
        std::error_code ec;
        auto size = std::filesystem::file_size(file, ec);
        if (!ec) {
            total_size += size;
        }
    }

    // Форматирование размера
    std::string size_str;
    constexpr std::uint64_t KB = 1024ULL;
    constexpr std::uint64_t MB = 1024ULL * 1024ULL;
    constexpr std::uint64_t GB = 1024ULL * 1024ULL * 1024ULL;
    if (total_size >= GB) {
        size_str = std::to_string(total_size / GB) + " GB";
    } else if (total_size >= MB) {
        size_str = std::to_string(total_size / MB) + " MB";
    } else if (total_size >= KB) {
        size_str = std::to_string(total_size / KB) + " KB";
    } else {
        size_str = std::to_string(total_size) + " B";
    }
    writer.info("Loaded " + std::to_string(files.size()) + " forensic artefacts (" + size_str +
                ")");

    // SPEC-SLICE-013 FACT-009: Флаг для запятой между элементами JSON
    bool first = true;

    // SPEC-SLICE-013 FACT-010, FACT-011: Последовательная обработка файлов и документов
    for (const auto& file : files) {
        // Открываем Reader
        auto result = io::Reader::open(file, cmd.load_unknown, cmd.skip_errors);
        if (!result.ok) {
            if (cmd.skip_errors) {
                writer.warn("failed to load file '" + platform::path_to_utf8(file) + "' - " +
                            result.error.message);
                continue;
            }
            writer.error(result.error.format());
            return 1;
        }

        if (!result.reader) {
            continue;
        }

        auto& reader = *result.reader;

        // Итерация по документам
        io::Document doc;
        while (reader.next(doc)) {
            // SPEC-SLICE-013 FACT-015, FACT-016: Извлечение данных из Document
            // Для всех типов Document данные находятся в doc.data

            // Конвертируем Value в RapidJSON для вывода
            rapidjson::Document rjdoc;
            doc.data.to_rapidjson(rjdoc, rjdoc.GetAllocator());

            if (cmd.json) {
                // SPEC-SLICE-013 FACT-006, FACT-009: JSON массив с pretty-printing
                if (first) {
                    first = false;
                } else {
                    out->write_line(output::Stream::Stdout, ",");
                }

                // Pretty JSON
                rapidjson::StringBuffer buffer;
                rapidjson::PrettyWriter<rapidjson::StringBuffer> rj_writer(buffer);
                rj_writer.SetIndent(' ', 2);
                rjdoc.Accept(rj_writer);
                out->write(output::Stream::Stdout,
                           std::string_view(buffer.GetString(), buffer.GetSize()));
            } else if (cmd.jsonl) {
                // SPEC-SLICE-013 FACT-007: JSONL — compact JSON по строкам
                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> rj_writer(buffer);
                rjdoc.Accept(rj_writer);
                out->write_line(output::Stream::Stdout,
                                std::string_view(buffer.GetString(), buffer.GetSize()));
            } else {
                // SPEC-SLICE-013 FACT-008: YAML формат (default) — разделитель "---"
                out->write_line(output::Stream::Stdout, "---");

                // YAML-like pretty JSON output
                rapidjson::StringBuffer buffer;
                rapidjson::PrettyWriter<rapidjson::StringBuffer> rj_writer(buffer);
                rj_writer.SetIndent(' ', 2);
                rjdoc.Accept(rj_writer);
                out->write_line(output::Stream::Stdout,
                                std::string_view(buffer.GetString(), buffer.GetSize()));
            }
        }

        // SPEC-SLICE-013 FACT-012: Проверяем ошибки итерации
        const auto& err = reader.last_error();
        if (err.has_value()) {
            if (cmd.skip_errors) {
                writer.warn("failed to parse document '" + platform::path_to_utf8(file) + "' - " +
                            err->message);
            } else {
                writer.error(err->format());
                return 1;
            }
        }
    }

    // SPEC-SLICE-013 FACT-006: JSON формат заканчивается "]"
    if (cmd.json) {
        out->write_line(output::Stream::Stdout, "]");
    }

    writer.info("Done");
    return 0;
}

int run_hunt(const chainsaw::cli::HuntCommand& cmd, const chainsaw::cli::GlobalOptions& global,
             chainsaw::output::Writer& writer) {
    (void)global;
    using namespace chainsaw;

    // SPEC-SLICE-012: строим Hunter через builder
    auto builder = hunt::HunterBuilder::create();

    // Загружаем правила
    std::vector<rule::Rule> all_rules;

    // Chainsaw rules
    for (const auto& rule_path : cmd.rules) {
        auto result = rule::load(rule::Kind::Chainsaw, rule_path, {});
        if (!result.ok) {
            writer.error(result.error.format());
            return 1;
        }
        for (auto& r : result.rules) {
            all_rules.push_back(std::move(r));
        }
    }

    // Sigma rules
    for (const auto& sigma_path : cmd.sigma) {
        auto result = rule::load(rule::Kind::Sigma, sigma_path, {});
        if (!result.ok) {
            writer.error(result.error.format());
            return 1;
        }
        for (auto& r : result.rules) {
            all_rules.push_back(std::move(r));
        }
    }

    if (!all_rules.empty()) {
        builder.rules(std::move(all_rules));
    }

    // Mappings
    if (!cmd.mapping.empty()) {
        builder.mappings(cmd.mapping);
    }

    // Опции
    builder.load_unknown(cmd.load_unknown).skip_errors(cmd.skip_errors);

    // Time filtering
    if (cmd.from.has_value()) {
        auto dt = hunt::DateTime::parse(*cmd.from);
        if (!dt) {
            writer.error(std::string("invalid --from datetime: ") + *cmd.from);
            return 1;
        }
        builder.from(*dt);
    }
    if (cmd.to.has_value()) {
        auto dt = hunt::DateTime::parse(*cmd.to);
        if (!dt) {
            writer.error(std::string("invalid --to datetime: ") + *cmd.to);
            return 1;
        }
        builder.to(*dt);
    }

    // Строим Hunter
    auto build_result = builder.build();
    if (!build_result.ok) {
        writer.error(build_result.error);
        return 1;
    }
    auto& hunter = *build_result.hunter;

    // Собираем расширения для discovery
    io::DiscoveryOptions disc_opt;
    disc_opt.skip_errors = cmd.skip_errors;

    auto exts = hunter.extensions();
    if (!exts.empty()) {
        disc_opt.extensions = std::move(exts);
    }

    // Находим файлы
    auto files = io::discover_files(cmd.paths, disc_opt);

    // Статистика
    std::size_t total_detections = 0;
    std::size_t files_with_detections = 0;
    std::vector<hunt::Detections> all_detections;

    // Итерируем по файлам
    for (const auto& file : files) {
        auto hunt_result = hunter.hunt(file);
        if (!hunt_result.ok) {
            if (cmd.skip_errors) {
                continue;
            }
            writer.error(hunt_result.error);
            return 1;
        }

        if (hunt_result.detections.empty()) {
            continue;
        }

        ++files_with_detections;
        total_detections += hunt_result.detections.size();

        for (auto& det : hunt_result.detections) {
            if (cmd.json) {
                // JSON format
                std::string json_str =
                    hunt::detections_to_json(det, hunter.hunts(), hunter.rules(), cmd.local);
                writer.write_line(output::Stream::Stdout, json_str);
            } else if (cmd.jsonl) {
                // JSONL format
                std::string jsonl_str =
                    hunt::detections_to_jsonl(det, hunter.hunts(), hunter.rules(), cmd.local);
                writer.write(output::Stream::Stdout, jsonl_str);
            } else {
                // Collect for table output
                all_detections.push_back(std::move(det));
            }
        }
    }

    // Table output (default)
    if (!cmd.json && !cmd.jsonl && !all_detections.empty()) {
        std::string table =
            hunt::format_table(all_detections, hunter.hunts(), hunter.rules(),
                               cmd.column_width.value_or(0), cmd.full, cmd.metadata, cmd.local);
        writer.write(output::Stream::Stdout, table);
    }

    // SPEC-SLICE-012 FACT-025: статистика в stderr
    writer.info(std::string("[+] ") + std::to_string(total_detections) + " detections in " +
                std::to_string(files_with_detections) + " files");

    return 0;
}

int run_lint(const chainsaw::cli::LintCommand& cmd, const chainsaw::cli::GlobalOptions& global,
             chainsaw::output::Writer& writer) {
    (void)global;
    using namespace chainsaw;

    // SPEC-SLICE-014 FACT-002: --kind обязательный аргумент
    // INV-001: Если --kind не указан — ошибка CLI
    if (!cmd.kind.has_value()) {
        writer.write(output::Stream::Stderr,
                     "error: the following required arguments were not provided:\n"
                     "  --kind <KIND>\n\n"
                     "Usage: chainsaw lint <PATH> --kind <KIND>\n\n"
                     "For more information, try '--help'.\n");
        return 2;
    }

    // Парсинг kind (chainsaw или sigma)
    rule::Kind kind = rule::Kind::Chainsaw;  // инициализация по умолчанию
    try {
        kind = rule::parse_kind(*cmd.kind);
    } catch (const std::invalid_argument& e) {
        writer.error(std::string("Invalid kind '") + *cmd.kind + "': " + e.what());
        return 2;
    }

    // SPEC-SLICE-014 FACT-005: Информационное сообщение
    // INV-002: Validating message всегда в stderr
    writer.info(std::string("Validating as ") + rule::to_string(kind) +
                " for supplied detection rules...");

    // SPEC-SLICE-014 FACT-012: get_files без фильтра расширений
    io::DiscoveryOptions disc_opt;
    disc_opt.skip_errors = false;
    // extensions не задаём — берём все файлы

    std::vector<std::filesystem::path> files;
    try {
        files = io::discover_files({cmd.path}, disc_opt);
    } catch (const std::exception& e) {
        writer.error(e.what());
        return 1;
    }

    // SPEC-SLICE-014 FACT-007: Счётчики count/failed
    std::size_t count = 0;
    std::size_t failed = 0;

    // SPEC-SLICE-014 FACT-006: Итерация по файлам
    for (const auto& file : files) {
        auto result = rule::lint(kind, file);

        if (result.ok) {
            // SPEC-SLICE-014 FACT-009: Tau output для Detection
            if (cmd.tau) {
                // Вывод [+] Rule <path>: в stderr (как в Rust версии)
                writer.info("Rule " + platform::path_to_utf8(file) + ":");

                for (auto& filter : result.filters) {
                    if (std::holds_alternative<tau::Detection>(filter)) {
                        auto& detection = std::get<tau::Detection>(filter);

                        // SPEC-SLICE-014 FACT-017: Optimizer passes в фиксированном порядке
                        // 1. coalesce (инлайн identifiers)
                        auto expression =
                            tau::coalesce(std::move(detection.expression), detection.identifiers);
                        detection.identifiers.clear();
                        // 2. shake (dead code elimination)
                        expression = tau::shake(std::move(expression));
                        // 3. rewrite (нормализация)
                        expression = tau::rewrite(std::move(expression));
                        // 4. matrix (multi-field optimization)
                        expression = tau::matrix(std::move(expression));

                        // Создаём новый Detection с optimized expression
                        tau::Detection optimized;
                        optimized.expression = std::move(expression);

                        // Вывод optimized expression в YAML формате
                        std::string yaml_output = tau::detection_to_yaml(optimized);
                        writer.write(output::Stream::Stdout, yaml_output);
                    } else {
                        // SPEC-SLICE-014 FACT-010: Expression warning
                        writer.warn("Tau does not support visual representation of expressions");
                    }
                }
            }
            ++count;
        } else {
            // SPEC-SLICE-014 FACT-008: Формат ошибки [!] filename: error
            // INV-003: Ошибки lint выводятся с форматом [!] filename: error
            // Примечание: writer.warn() добавляет [!] автоматически
            ++failed;
            writer.warn(file.filename().string() + ": " + result.error.message);
        }
    }

    // SPEC-SLICE-014 FACT-011: Summary message
    // INV-004: Summary message содержит count и total (count + failed)
    writer.info(std::string("Validated ") + std::to_string(count) + " detection rules out of " +
                std::to_string(count + failed));

    return 0;
}

int run_search(const chainsaw::cli::SearchCommand& cmd, const chainsaw::cli::GlobalOptions& global,
               chainsaw::output::Writer& writer) {
    (void)global;
    using namespace chainsaw;

    // SPEC-SLICE-011: строим Searcher через builder
    auto builder = search::SearcherBuilder::create();

    // Собираем все regex паттерны
    std::vector<std::string> all_patterns;
    if (cmd.pattern.has_value()) {
        all_patterns.push_back(*cmd.pattern);
    }
    for (const auto& p : cmd.regex_patterns) {
        all_patterns.push_back(p);
    }

    if (!all_patterns.empty()) {
        builder.patterns(std::move(all_patterns));
    }

    // Tau expressions
    if (!cmd.tau_exprs.empty()) {
        builder.tau(cmd.tau_exprs);
    }

    // Опции
    builder.ignore_case(cmd.ignore_case)
        .match_any(cmd.match_any)
        .load_unknown(cmd.load_unknown)
        .skip_errors(cmd.skip_errors);

    // Time filtering
    if (cmd.timestamp.has_value()) {
        builder.timestamp(*cmd.timestamp);
    }
    if (cmd.from.has_value()) {
        auto dt = search::DateTime::parse(*cmd.from);
        if (!dt) {
            writer.error(std::string("invalid --from datetime: ") + *cmd.from);
            return 1;
        }
        builder.from(*dt);
    }
    if (cmd.to.has_value()) {
        auto dt = search::DateTime::parse(*cmd.to);
        if (!dt) {
            writer.error(std::string("invalid --to datetime: ") + *cmd.to);
            return 1;
        }
        builder.to(*dt);
    }

    // Строим Searcher
    auto build_result = builder.build();
    if (!build_result.ok) {
        writer.error(build_result.error);
        return 1;
    }
    auto& searcher = *build_result.searcher;

    // Собираем расширения для discovery
    io::DiscoveryOptions disc_opt;
    disc_opt.skip_errors = cmd.skip_errors;
    if (!cmd.extensions.empty()) {
        std::unordered_set<std::string> exts(cmd.extensions.begin(), cmd.extensions.end());
        disc_opt.extensions = std::move(exts);
    }

    // Находим файлы
    auto files = io::discover_files(cmd.paths, disc_opt);

    // Статистика
    std::size_t total_hits = 0;
    std::size_t files_with_hits = 0;

    // Открываем output file если указан
    if (cmd.output.has_value()) {
        output::OutputConfig out_cfg = writer.config();
        out_cfg.output_path = cmd.output;
        // Пересоздаём writer с новым output
    }

    // JSON array для -j/--json
    bool first_json = true;
    if (cmd.json) {
        writer.write(output::Stream::Stdout, "[");
    }

    // Итерируем по файлам
    for (const auto& file : files) {
        auto hits = searcher.search(file);
        if (hits.empty()) {
            continue;
        }

        ++files_with_hits;
        total_hits += hits.size();

        for (auto& hit : hits) {
            if (cmd.json) {
                // JSON array format
                rapidjson::Document doc;
                hit.data.to_rapidjson(doc, doc.GetAllocator());

                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> rj_writer(buffer);
                doc.Accept(rj_writer);

                if (!first_json) {
                    writer.write(output::Stream::Stdout, ",");
                }
                first_json = false;
                writer.write(output::Stream::Stdout,
                             std::string_view(buffer.GetString(), buffer.GetSize()));
            } else if (cmd.jsonl) {
                // JSONL format: один объект на строку
                rapidjson::Document doc;
                hit.data.to_rapidjson(doc, doc.GetAllocator());

                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> rj_writer(buffer);
                doc.Accept(rj_writer);

                writer.write_line(output::Stream::Stdout,
                                  std::string_view(buffer.GetString(), buffer.GetSize()));
            } else {
                // YAML-like format (default)
                // SPEC-SLICE-011 FACT-014: YAML формат по умолчанию
                writer.write_line(output::Stream::Stdout, "---");

                // Сериализуем в JSON, затем выводим как YAML-like
                rapidjson::Document doc;
                hit.data.to_rapidjson(doc, doc.GetAllocator());

                rapidjson::StringBuffer buffer;
                rapidjson::PrettyWriter<rapidjson::StringBuffer> rj_writer(buffer);
                rj_writer.SetIndent(' ', 2);
                doc.Accept(rj_writer);

                writer.write_line(output::Stream::Stdout,
                                  std::string_view(buffer.GetString(), buffer.GetSize()));
            }
        }
    }

    if (cmd.json) {
        writer.write_line(output::Stream::Stdout, "]");
    }

    // SPEC-SLICE-011 FACT-015: статистика в stderr
    writer.info(std::string("[+] ") + std::to_string(total_hits) + " hits in " +
                std::to_string(files_with_hits) + " files");

    return 0;
}

int run_analyse_shimcache(const chainsaw::cli::AnalyseShimcacheCommand& cmd,
                          const chainsaw::cli::GlobalOptions& global,
                          chainsaw::output::Writer& writer) {
    (void)global;
    using namespace chainsaw;

    // SPEC-SLICE-019: Create ShimcacheAnalyser
    analyse::shimcache::ShimcacheAnalyser analyser(cmd.shimcache_path, cmd.amcache);

    writer.info("Loading shimcache from " + platform::path_to_utf8(cmd.shimcache_path));
    if (cmd.amcache.has_value()) {
        writer.info("Loading amcache from " + platform::path_to_utf8(*cmd.amcache));
    }

    // Load regex patterns
    std::vector<std::string> regex_patterns = cmd.regex_patterns;

    // Load patterns from file if specified
    if (cmd.regex_file.has_value()) {
        std::ifstream file(cmd.regex_file.value());
        if (!file.is_open()) {
            writer.error("Failed to open regex file: " +
                         platform::path_to_utf8(cmd.regex_file.value()));
            return 1;
        }
        std::string line;
        std::size_t count = 0;
        while (std::getline(file, line)) {
            if (!line.empty() && line[0] != '#') {
                regex_patterns.push_back(line);
                ++count;
            }
        }
        writer.info("[+] Regex file with " + std::to_string(count) + " pattern(s) loaded from " +
                    platform::path_to_utf8(cmd.regex_file.value()));
    }

    // Generate timeline
    writer.info("Analysing shimcache...");
    auto result = analyser.amcache_shimcache_timeline(regex_patterns, cmd.tspair);

    if (auto* error = std::get_if<analyse::shimcache::ShimcacheError>(&result)) {
        writer.error(error->format());
        return 1;
    }

    auto& timeline = std::get<std::vector<analyse::shimcache::TimelineEntity>>(result);

    // SPEC-SLICE-019: Output to file if specified, otherwise to stdout
    std::unique_ptr<output::Writer> file_writer;
    output::Writer* out = &writer;
    if (cmd.output.has_value()) {
        output::OutputConfig out_cfg = writer.config();
        out_cfg.output_path = cmd.output;
        file_writer = std::make_unique<output::Writer>(out_cfg);
        out = file_writer.get();
    }

    // SPEC-SLICE-019 FACT-026: Output CSV format
    // Write header
    out->write_line(output::Stream::Stdout, analyse::shimcache::get_csv_header());

    // Write entries
    std::size_t entry_number = 1;
    for (const auto& entity : timeline) {
        out->write_line(output::Stream::Stdout,
                        analyse::shimcache::format_timeline_entity_csv(entity, entry_number));
        ++entry_number;
    }

    // Summary
    writer.info("[+] " + std::to_string(timeline.size()) + " timeline entries generated");

    if (cmd.output.has_value()) {
        writer.info("[+] Saved output to " + platform::path_to_utf8(*cmd.output));
    }

    return 0;
}

int run_analyse_srum(const chainsaw::cli::AnalyseSrumCommand& cmd,
                     const chainsaw::cli::GlobalOptions& global, chainsaw::output::Writer& writer) {
    (void)global;
    using namespace chainsaw;

    // SPEC-SLICE-017: Create SrumAnalyser
    analyse::SrumAnalyser analyser(cmd.srum_path, cmd.software_path);

    writer.info("Loading SRUM database from " + platform::path_to_utf8(cmd.srum_path));
    writer.info("Loading SOFTWARE hive from " + platform::path_to_utf8(cmd.software_path));

    // Parse SRUM database
    auto result = analyser.parse_srum_database();
    if (!result) {
        const auto& err = analyser.last_error();
        writer.error(err.value_or("Failed to parse SRUM database"));
        return 1;
    }

    writer.info("Analysing the SRUM database...");

    // SPEC-SLICE-017: If output file specified, create new writer
    std::unique_ptr<output::Writer> file_writer;
    output::Writer* out = &writer;
    if (cmd.output.has_value()) {
        output::OutputConfig out_cfg = writer.config();
        out_cfg.output_path = cmd.output;
        file_writer = std::make_unique<output::Writer>(out_cfg);
        out = file_writer.get();
    }

    // SPEC-SLICE-017: Output table details
    if (!result->table_details.empty()) {
        // Header
        out->write_line(output::Stream::Stdout, "");
        out->write_line(output::Stream::Stdout, "SRUM Table Details:");
        out->write_line(
            output::Stream::Stdout,
            "+------------------------------------------+--------------------------------+---------"
            "------------------+---------------------------+------------------------+");
        out->write_line(
            output::Stream::Stdout,
            "| Table GUID                               | Table Name                     | DLL "
            "Path                  | Timeframe                 | Expected Retention     |");
        out->write_line(
            output::Stream::Stdout,
            "+------------------------------------------+--------------------------------+---------"
            "------------------+---------------------------+------------------------+");

        for (const auto& [guid, td] : result->table_details) {
            std::string row = "| ";
            // GUID (max 40 chars)
            std::string guid_col = guid;
            if (guid_col.size() > 40)
                guid_col = guid_col.substr(0, 37) + "...";
            row += guid_col;
            row += std::string(41 - guid_col.size(), ' ');
            row += "| ";

            // Table Name (max 30 chars)
            std::string name_col = td.table_name;
            if (name_col.size() > 30)
                name_col = name_col.substr(0, 27) + "...";
            row += name_col;
            row += std::string(31 - name_col.size(), ' ');
            row += "| ";

            // DLL Path (max 25 chars)
            std::string dll_col = td.dll_path.value_or("");
            if (dll_col.size() > 25)
                dll_col = dll_col.substr(0, 22) + "...";
            row += dll_col;
            row += std::string(26 - dll_col.size(), ' ');
            row += "| ";

            // Timeframe (max 25 chars)
            std::string tf_col;
            if (td.from && td.to) {
                tf_col = *td.from + " - " + *td.to;
            } else {
                tf_col = "No records";
            }
            if (tf_col.size() > 25)
                tf_col = tf_col.substr(0, 22) + "...";
            row += tf_col;
            row += std::string(26 - tf_col.size(), ' ');
            row += "| ";

            // Retention (max 22 chars)
            std::string ret_col =
                td.retention_time_days ? analyse::format_duration(*td.retention_time_days) : "";
            if (ret_col.size() > 22)
                ret_col = ret_col.substr(0, 19) + "...";
            row += ret_col;
            row += std::string(23 - ret_col.size(), ' ');
            row += "|";

            out->write_line(output::Stream::Stdout, row);
        }

        out->write_line(
            output::Stream::Stdout,
            "+------------------------------------------+--------------------------------+---------"
            "------------------+---------------------------+------------------------+");
        out->write_line(output::Stream::Stdout, "");
    }

    // SPEC-SLICE-017: If --stats-only, don't output JSON data
    if (cmd.stats_only) {
        writer.info("Done (stats only mode)");
        return 0;
    }

    // Output JSON data
    rapidjson::Document rjdoc;
    result->db_content.to_rapidjson(rjdoc, rjdoc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> rj_writer(buffer);
    rj_writer.SetIndent(' ', 2);
    rjdoc.Accept(rj_writer);

    out->write_line(output::Stream::Stdout, std::string_view(buffer.GetString(), buffer.GetSize()));

    // Count entries
    std::size_t entry_count = 0;
    if (auto* arr = result->db_content.get_array()) {
        entry_count = arr->size();
    }

    writer.info("Done. Processed " + std::to_string(entry_count) + " SRUM entries.");
    return 0;
}

// ----------------------------------------------------------------------------
// Главная функция выполнения (run)
// ----------------------------------------------------------------------------

int run(int argc, char** argv) {
    using namespace chainsaw;

    // 1. Парсинг argv
    cli::ParseResult parse_result = cli::parse(argc, argv);

    // 2. Создание Writer
    output::OutputConfig out_cfg;
    out_cfg.quiet = parse_result.global.quiet;
    out_cfg.verbose = parse_result.global.verbose;
    out_cfg.no_banner = parse_result.global.no_banner;
    output::Writer writer(out_cfg);

    // 3. Обработка ошибок парсинга,,
    // Вывод сообщения идёт без форматирования [x], напрямую в stderr
    if (!parse_result.ok) {
        writer.write(output::Stream::Stderr, parse_result.diagnostic.stderr_message);
        return parse_result.diagnostic.exit_code;
    }

    // 4. Dispatch команды
    return std::visit(
        [&](auto&& cmd) -> int {
            using T = std::decay_t<decltype(cmd)>;

            if constexpr (std::is_same_v<T, cli::HelpCommand>) {
                // --help или help subcommand
                // FACT-CLI-008: --help не выводит баннер,
                std::string help_text = cli::render_help(cmd.command);
                writer.write(output::Stream::Stdout, help_text);
                return 0;
            } else if constexpr (std::is_same_v<T, cli::VersionCommand>) {
                // --version
                std::string version_text = cli::render_version();
                writer.write(output::Stream::Stdout, version_text);
                return 0;
            } else if constexpr (std::is_same_v<T, cli::DumpCommand>) {
                print_banner(writer, out_cfg.no_banner, out_cfg.quiet);
                return run_dump(cmd, parse_result.global, writer);
            } else if constexpr (std::is_same_v<T, cli::HuntCommand>) {
                print_banner(writer, out_cfg.no_banner, out_cfg.quiet);
                return run_hunt(cmd, parse_result.global, writer);
            } else if constexpr (std::is_same_v<T, cli::LintCommand>) {
                print_banner(writer, out_cfg.no_banner, out_cfg.quiet);
                return run_lint(cmd, parse_result.global, writer);
            } else if constexpr (std::is_same_v<T, cli::SearchCommand>) {
                print_banner(writer, out_cfg.no_banner, out_cfg.quiet);
                return run_search(cmd, parse_result.global, writer);
            } else if constexpr (std::is_same_v<T, cli::AnalyseShimcacheCommand>) {
                print_banner(writer, out_cfg.no_banner, out_cfg.quiet);
                return run_analyse_shimcache(cmd, parse_result.global, writer);
            } else if constexpr (std::is_same_v<T, cli::AnalyseSrumCommand>) {
                print_banner(writer, out_cfg.no_banner, out_cfg.quiet);
                return run_analyse_srum(cmd, parse_result.global, writer);
            } else {
                // Unreachable
                return 1;
            }
        },
        parse_result.command);
}

}  // anonymous namespace

// ----------------------------------------------------------------------------
// main
// ----------------------------------------------------------------------------

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        // GUIDE-0001 G-024: перехват исключений на границе app
        // CLI-0001 1.6.1: формат ошибки "[x] <err>"
        std::cerr << "[x] " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "[x] Unknown error occurred\n";
        return 1;
    }
}
