// ==============================================================================
// cli.cpp - MOD-0002: CLI парсинг
// ==============================================================================
//
// MOD-0002 cli
// ADR-0006: собственный слой CLI для byte-to-byte совпадения
// CLI-0001: CLI-контракт Chainsaw (baseline)
//
// STUB: Это заглушка для Step 24. Полная реализация парсинга будет
// добавлена в последующих слайсах портирования.
//
// ==============================================================================

#include "chainsaw/cli.hpp"

#include "chainsaw/platform.hpp"

#include <cstring>
#include <sstream>

namespace chainsaw::cli {

// ----------------------------------------------------------------------------
// Вспомогательные функции
// ----------------------------------------------------------------------------

namespace {

bool str_eq(const char* a, const char* b) {
    return std::strcmp(a, b) == 0;
}

bool starts_with(const char* str, const char* prefix) {
    return std::strncmp(str, prefix, std::strlen(prefix)) == 0;
}

}  // anonymous namespace

// ----------------------------------------------------------------------------
// render_version
// ----------------------------------------------------------------------------

std::string render_version() {
    std::ostringstream oss;
    oss << "chainsaw " << VERSION << "\n";
    return oss.str();
}

// ----------------------------------------------------------------------------
// render_help
// ----------------------------------------------------------------------------

std::string render_help(const std::optional<std::string>& command) {
    std::ostringstream oss;

    if (!command.has_value()) {
        // Общая справка (CLI-0001 1.1)
        oss << "chainsaw " << VERSION << "\n";
        oss << ABOUT << "\n";
        oss << "\n";
        oss << "USAGE:\n";
        oss << "    chainsaw [OPTIONS] <COMMAND>\n";
        oss << "\n";
        oss << "OPTIONS:\n";
        oss << "    --no-banner           Hide the Chainsaw banner\n";
        oss << "    --num-threads <N>     Limit the thread count (default: num of CPUs)\n";
        oss << "    -v                    Increase logging verbosity\n";
        oss << "    -q                    Suppress informational output\n";
        oss << "    -h, --help            Print help information\n";
        oss << "    -V, --version         Print version information\n";
        oss << "\n";
        oss << "COMMANDS:\n";
        oss << "    dump       Dump the contents of forensic artefacts\n";
        oss << "    hunt       Hunt through artefacts using detection rules\n";
        oss << "    lint       Lint provided rules to ensure that they load correctly\n";
        oss << "    search     Search through artefacts using string/regex patterns or Tau "
               "expressions\n";
        oss << "    analyse    Perform analysis of forensic artefacts\n";
        oss << "    help       Print this message or help of the given subcommand\n";
    } else if (*command == "dump") {
        oss << "Dump the contents of forensic artefacts\n";
        oss << "\n";
        oss << "USAGE:\n";
        oss << "    chainsaw dump [OPTIONS] <PATH>...\n";
        oss << "\n";
        oss << "ARGS:\n";
        oss << "    <PATH>...    Path(s) to forensic artefacts\n";
        oss << "\n";
        oss << "OPTIONS:\n";
        oss << "    -j, --json           Output as JSON array\n";
        oss << "    --jsonl              Output as JSON Lines\n";
        oss << "    --load-unknown       Load unknown file types\n";
        oss << "    --extension <EXT>    Filter by file extension\n";
        oss << "    -o, --output <PATH>  Write output to file\n";
        oss << "    -q                   Suppress informational output\n";
        oss << "    --skip-errors        Continue on errors\n";
        oss << "    -h, --help           Print help information\n";
    } else if (*command == "hunt") {
        oss << "Hunt through artefacts using detection rules\n";
        oss << "\n";
        oss << "USAGE:\n";
        oss << "    chainsaw hunt [OPTIONS] [RULES] <PATH>...\n";
        oss << "\n";
        oss << "ARGS:\n";
        oss << "    [RULES]      Path to detection rules\n";
        oss << "    <PATH>...    Path(s) to forensic artefacts\n";
        oss << "\n";
        oss << "OPTIONS:\n";
        oss << "    -m, --mapping <PATH>     Path to field mappings (repeatable)\n";
        oss << "    -r, --rule <PATH>        Additional rule path (repeatable)\n";
        oss << "    -s, --sigma <PATH>       Sigma rules path (requires --mapping)\n";
        oss << "    --json                   Output as JSON\n";
        oss << "    --jsonl                  Output as JSON Lines\n";
        oss << "    --csv                    Output as CSV (requires --output)\n";
        oss << "    -o, --output <PATH>      Write output to file/directory\n";
        oss << "    -c, --cache-to-disk      Cache results to disk (requires --jsonl)\n";
        oss << "    -q                       Suppress informational output\n";
        oss << "    -h, --help               Print help information\n";
    } else if (*command == "lint") {
        oss << "Lint provided rules to ensure that they load correctly\n";
        oss << "\n";
        oss << "USAGE:\n";
        oss << "    chainsaw lint [OPTIONS] <PATH>\n";
        oss << "\n";
        oss << "ARGS:\n";
        oss << "    <PATH>    Path to detection rules\n";
        oss << "\n";
        oss << "OPTIONS:\n";
        oss << "    --kind <KIND>    Rule kind: chainsaw, sigma\n";
        oss << "    -t, --tau        Print tau representation\n";
        oss << "    -h, --help       Print help information\n";
    } else if (*command == "search") {
        oss << "Search through artefacts using string/regex patterns or Tau expressions\n";
        oss << "\n";
        oss << "USAGE:\n";
        oss << "    chainsaw search [OPTIONS] [PATTERN] [PATH]...\n";
        oss << "\n";
        oss << "ARGS:\n";
        oss << "    [PATTERN]    Search pattern (string or regex)\n";
        oss << "    [PATH]...    Path(s) to forensic artefacts\n";
        oss << "\n";
        oss << "OPTIONS:\n";
        oss << "    -e, --regex <PATTERN>    Additional regex pattern (repeatable)\n";
        oss << "    -t, --tau <EXPR>         Tau expression (repeatable)\n";
        oss << "    -i, --ignore-case        Case insensitive search\n";
        oss << "    --match-any              Match any pattern (OR logic)\n";
        oss << "    -j, --json               Output as JSON\n";
        oss << "    --jsonl                  Output as JSON Lines\n";
        oss << "    -o, --output <PATH>      Write output to file\n";
        oss << "    -q                       Suppress informational output\n";
        oss << "    -h, --help               Print help information\n";
    } else if (*command == "analyse") {
        oss << "Perform analysis of forensic artefacts\n";
        oss << "\n";
        oss << "USAGE:\n";
        oss << "    chainsaw analyse <COMMAND>\n";
        oss << "\n";
        oss << "COMMANDS:\n";
        oss << "    shimcache    Create an execution timeline from shimcache artefacts\n";
        oss << "    srum         Parse SRUM database and output contents\n";
        oss << "    help         Print this message or help of the given subcommand\n";
    } else {
        oss << "Unknown command: " << *command << "\n";
    }

    return oss.str();
}

// ----------------------------------------------------------------------------
// parse
// ----------------------------------------------------------------------------

ParseResult parse(int argc, char** argv) {
    ParseResult result;
    result.ok = false;
    result.command = HelpCommand{};

    if (argc < 2) {
        // Нет аргументов - показываем help
        result.ok = true;
        result.command = HelpCommand{};
        return result;
    }

    // Парсим глобальные опции и находим подкоманду
    int cmd_idx = 1;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (str_eq(arg, "--no-banner")) {
            result.global.no_banner = true;
        } else if (str_eq(arg, "-v")) {
            result.global.verbose++;
        } else if (str_eq(arg, "-q")) {
            result.global.quiet = true;
        } else if (starts_with(arg, "--num-threads")) {
            // STUB: парсинг значения
        } else if (str_eq(arg, "-h") || str_eq(arg, "--help")) {
            result.ok = true;
            result.command = HelpCommand{};
            return result;
        } else if (str_eq(arg, "-V") || str_eq(arg, "--version")) {
            result.ok = true;
            result.command = VersionCommand{};
            return result;
        } else if (arg[0] != '-') {
            // Нашли подкоманду
            cmd_idx = i;
            break;
        }
    }

    if (cmd_idx >= argc) {
        result.ok = true;
        result.command = HelpCommand{};
        return result;
    }

    const char* cmd = argv[cmd_idx];

    // Dispatch по подкомандам (CLI-0001 2.x)
    if (str_eq(cmd, "dump")) {
        DumpCommand dump_cmd;
        // STUB: парсинг аргументов dump
        // Собираем пути после команды
        for (int i = cmd_idx + 1; i < argc; ++i) {
            const char* arg = argv[i];
            if (str_eq(arg, "-j") || str_eq(arg, "--json")) {
                dump_cmd.json = true;
            } else if (str_eq(arg, "--jsonl")) {
                dump_cmd.jsonl = true;
            } else if (str_eq(arg, "-q")) {
                result.global.quiet = true;
            } else if (str_eq(arg, "-h") || str_eq(arg, "--help")) {
                result.ok = true;
                result.command = HelpCommand{"dump"};
                return result;
            } else if (arg[0] != '-') {
                dump_cmd.paths.push_back(platform::path_from_utf8(arg));
            }
        }

        if (dump_cmd.paths.empty()) {
            result.diagnostic.exit_code = 1;
            result.diagnostic.stderr_message =
                "error: The following required arguments were not provided:\n"
                "    <PATH>...\n";
            return result;
        }

        result.ok = true;
        result.command = dump_cmd;
    } else if (str_eq(cmd, "hunt")) {
        HuntCommand hunt_cmd;
        // STUB: парсинг аргументов hunt
        for (int i = cmd_idx + 1; i < argc; ++i) {
            const char* arg = argv[i];
            if (str_eq(arg, "-h") || str_eq(arg, "--help")) {
                result.ok = true;
                result.command = HelpCommand{"hunt"};
                return result;
            } else if (arg[0] != '-') {
                hunt_cmd.paths.push_back(platform::path_from_utf8(arg));
            }
        }
        result.ok = true;
        result.command = hunt_cmd;
    } else if (str_eq(cmd, "lint")) {
        LintCommand lint_cmd;
        for (int i = cmd_idx + 1; i < argc; ++i) {
            const char* arg = argv[i];
            if (str_eq(arg, "-h") || str_eq(arg, "--help")) {
                result.ok = true;
                result.command = HelpCommand{"lint"};
                return result;
            } else if (str_eq(arg, "-t") || str_eq(arg, "--tau")) {
                lint_cmd.tau = true;
            } else if (arg[0] != '-') {
                lint_cmd.path = platform::path_from_utf8(arg);
            }
        }

        if (lint_cmd.path.empty()) {
            result.diagnostic.exit_code = 1;
            result.diagnostic.stderr_message =
                "error: The following required arguments were not provided:\n"
                "    <PATH>\n";
            return result;
        }

        result.ok = true;
        result.command = lint_cmd;
    } else if (str_eq(cmd, "search")) {
        SearchCommand search_cmd;
        for (int i = cmd_idx + 1; i < argc; ++i) {
            const char* arg = argv[i];
            if (str_eq(arg, "-h") || str_eq(arg, "--help")) {
                result.ok = true;
                result.command = HelpCommand{"search"};
                return result;
            } else if (str_eq(arg, "-j") || str_eq(arg, "--json")) {
                search_cmd.json = true;
            } else if (str_eq(arg, "--jsonl")) {
                search_cmd.jsonl = true;
            } else if (str_eq(arg, "-i") || str_eq(arg, "--ignore-case")) {
                search_cmd.ignore_case = true;
            } else if (arg[0] != '-') {
                if (!search_cmd.pattern.has_value()) {
                    search_cmd.pattern = arg;
                } else {
                    search_cmd.paths.push_back(platform::path_from_utf8(arg));
                }
            }
        }
        result.ok = true;
        result.command = search_cmd;
    } else if (str_eq(cmd, "analyse")) {
        // Подкоманда analyse требует вторую подкоманду
        if (cmd_idx + 1 >= argc) {
            result.ok = true;
            result.command = HelpCommand{"analyse"};
            return result;
        }

        const char* subcmd = argv[cmd_idx + 1];

        if (str_eq(subcmd, "shimcache")) {
            AnalyseShimcacheCommand shimcache_cmd;
            for (int i = cmd_idx + 2; i < argc; ++i) {
                const char* arg = argv[i];
                if (str_eq(arg, "-h") || str_eq(arg, "--help")) {
                    result.ok = true;
                    // STUB: help для shimcache
                    result.command = HelpCommand{"analyse"};
                    return result;
                } else if (arg[0] != '-') {
                    shimcache_cmd.shimcache_path = platform::path_from_utf8(arg);
                }
            }
            result.ok = true;
            result.command = shimcache_cmd;
        } else if (str_eq(subcmd, "srum")) {
            AnalyseSrumCommand srum_cmd;
            for (int i = cmd_idx + 2; i < argc; ++i) {
                const char* arg = argv[i];
                if (str_eq(arg, "-h") || str_eq(arg, "--help")) {
                    result.ok = true;
                    result.command = HelpCommand{"analyse"};
                    return result;
                } else if (arg[0] != '-') {
                    if (srum_cmd.srum_path.empty()) {
                        srum_cmd.srum_path = platform::path_from_utf8(arg);
                    }
                }
            }
            result.ok = true;
            result.command = srum_cmd;
        } else if (str_eq(subcmd, "help") || str_eq(subcmd, "-h") || str_eq(subcmd, "--help")) {
            result.ok = true;
            result.command = HelpCommand{"analyse"};
        } else {
            result.diagnostic.exit_code = 1;
            result.diagnostic.stderr_message = "error: unrecognized subcommand '";
            result.diagnostic.stderr_message += subcmd;
            result.diagnostic.stderr_message += "'\n";
            return result;
        }
    } else if (str_eq(cmd, "help")) {
        result.ok = true;
        if (cmd_idx + 1 < argc) {
            result.command = HelpCommand{argv[cmd_idx + 1]};
        } else {
            result.command = HelpCommand{};
        }
    } else {
        // Неизвестная команда
        result.diagnostic.exit_code = 1;
        result.diagnostic.stderr_message = "error: unrecognized subcommand '";
        result.diagnostic.stderr_message += cmd;
        result.diagnostic.stderr_message += "'\n\n";
        result.diagnostic.stderr_message += "For more information try --help\n";
        return result;
    }

    return result;
}

}  // namespace chainsaw::cli
