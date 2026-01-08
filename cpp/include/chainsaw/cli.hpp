// ==============================================================================
// chainsaw/cli.hpp - MOD-0002: CLI парсинг и команды
// ==============================================================================
//
// MOD-0002 cli
// ADR-0006: собственный слой CLI для byte-to-byte совпадения help/errors
// CLI-0001: CLI-контракт Chainsaw (baseline)
//
// Назначение:
// - Парсинг argv
// - Генерация --help / --version
// - Диспетчеризация подкоманд
// - Диагностические ошибки CLI
//
// ==============================================================================

#ifndef CHAINSAW_CLI_HPP
#define CHAINSAW_CLI_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace chainsaw::cli {

// ----------------------------------------------------------------------------
// Глобальные опции (CLI-0001 1.2)
// ----------------------------------------------------------------------------

struct GlobalOptions {
    bool no_banner = false;  // --no-banner
    int num_threads = 0;     // --num-threads (0 = default = CPU count)
    int verbose = 0;         // -v (repeatable)
    bool quiet = false;      // -q
};

// ----------------------------------------------------------------------------
// Подкоманды (CLI-0001 2.x)
// ----------------------------------------------------------------------------

/// dump - дамп артефактов (CLI-0001 2.1)
struct DumpCommand {
    std::vector<std::filesystem::path> paths;
    bool json = false;                            // -j, --json
    bool jsonl = false;                           // --jsonl
    bool load_unknown = false;                    // --load-unknown
    std::optional<std::string> extension;         // --extension
    std::optional<std::filesystem::path> output;  // -o, --output
    bool skip_errors = false;                     // --skip-errors
};

/// hunt - применение правил детекта (CLI-0001 2.2)
struct HuntCommand {
    std::optional<std::filesystem::path> rules;
    std::vector<std::filesystem::path> paths;
    std::vector<std::filesystem::path> mappings;     // -m, --mapping
    std::vector<std::filesystem::path> rule_files;   // -r, --rule
    std::vector<std::filesystem::path> sigma_files;  // -s, --sigma
    // Форматы вывода
    bool json = false;
    bool jsonl = false;
    bool csv = false;
    bool log = false;
    std::optional<std::filesystem::path> output;
    bool cache_to_disk = false;  // -c, --cache-to-disk
};

/// lint - проверка правил (CLI-0001 2.3)
struct LintCommand {
    std::filesystem::path path;
    std::optional<std::string> kind;  // --kind (chainsaw|sigma)
    bool tau = false;                 // -t, --tau
};

/// search - поиск по артефактам (CLI-0001 2.4)
struct SearchCommand {
    std::optional<std::string> pattern;
    std::vector<std::filesystem::path> paths;
    std::vector<std::string> regex_patterns;  // -e, --regex
    std::vector<std::string> tau_exprs;       // -t, --tau
    bool ignore_case = false;                 // -i, --ignore-case
    bool match_any = false;                   // --match-any
    bool json = false;
    bool jsonl = false;
    std::optional<std::filesystem::path> output;
    bool skip_errors = false;
};

/// analyse shimcache (CLI-0001 2.5)
struct AnalyseShimcacheCommand {
    std::filesystem::path shimcache_path;
    std::vector<std::string> regex_patterns;          // -e, --regex
    std::optional<std::filesystem::path> regex_file;  // -r, --regexfile
    std::optional<std::filesystem::path> output;      // -o, --output
    std::optional<std::filesystem::path> amcache;     // -a, --amcache
    bool tspair = false;                              // -p, --tspair
};

/// analyse srum (CLI-0001 2.6)
struct AnalyseSrumCommand {
    std::filesystem::path srum_path;
    std::filesystem::path software_path;          // -s, --software (required)
    bool stats_only = false;                      // --stats-only
    std::optional<std::filesystem::path> output;  // -o, --output
};

/// help - показать справку
struct HelpCommand {
    std::optional<std::string> command;  // опциональная подкоманда для справки
};

/// version - показать версию
struct VersionCommand {};

// ----------------------------------------------------------------------------
// Command - вариант команды
// ----------------------------------------------------------------------------

using Command =
    std::variant<DumpCommand, HuntCommand, LintCommand, SearchCommand, AnalyseShimcacheCommand,
                 AnalyseSrumCommand, HelpCommand, VersionCommand>;

// ----------------------------------------------------------------------------
// Диагностика CLI
// ----------------------------------------------------------------------------

struct CliDiagnostic {
    int exit_code = 1;
    std::string stderr_message;
};

// ----------------------------------------------------------------------------
// Результат парсинга
// ----------------------------------------------------------------------------

struct ParseResult {
    bool ok = false;
    GlobalOptions global;
    Command command;
    CliDiagnostic diagnostic;
};

// ----------------------------------------------------------------------------
// API парсинга (TOBE-0001 4.2.2)
// ----------------------------------------------------------------------------

/// Парсить аргументы командной строки
ParseResult parse(int argc, char** argv);

/// Генерировать текст --help (для конкретной команды или общий)
std::string render_help(const std::optional<std::string>& command = std::nullopt);

/// Генерировать текст --version
std::string render_version();

// ----------------------------------------------------------------------------
// Константы
// ----------------------------------------------------------------------------

/// Версия программы
constexpr const char* VERSION = "0.1.0";

/// Описание программы (CLI-0001 1.1)
constexpr const char* ABOUT = "Rapidly work with Forensic Artefacts";

}  // namespace chainsaw::cli

#endif  // CHAINSAW_CLI_HPP
