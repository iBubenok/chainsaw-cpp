// ==============================================================================
// cli.cpp - MOD-0002: CLI парсинг
// ==============================================================================
//
// MOD-0002 cli
// ADR-0006: собственный слой CLI для byte-to-byte совпадения
// CLI-0001: CLI-контракт Chainsaw (baseline)
//
// STUB: Это заглушка для . Полная реализация парсинга будет
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
// render_version: byte-to-byte)
// ----------------------------------------------------------------------------

std::string render_version() {
    // FACT-CLI-009: формат "chainsaw 2.13.1\n"
    return "chainsaw 2.13.1\n";
}

// ----------------------------------------------------------------------------
// render_help,: byte-to-byte)
// ----------------------------------------------------------------------------

std::string render_help(const std::optional<std::string>& command) {
    // Форматирование соответствует golden runs Rust clap v4
    if (!command.has_value()) {
        //: главная справка
        // Точное воспроизведение вывода clap
        return "Rapidly work with Forensic Artefacts\n"
               "\n"
               "Usage: chainsaw [OPTIONS] <COMMAND>\n"
               "\n"
               "Commands:\n"
               "  dump     Dump artefacts into a different format\n"
               "  hunt     Hunt through artefacts using detection rules for threat detection\n"
               "  lint     Lint provided rules to ensure that they load correctly\n"
               "  search   Search through forensic artefacts for keywords or patterns\n"
               "  analyse  Perform various analyses on artefacts\n"
               "  help     Print this message or the help of the given subcommand(s)\n"
               "\n"
               "Options:\n"
               "      --no-banner                  Hide Chainsaw's banner\n"
               "      --num-threads <NUM_THREADS>  Limit the thread number (default: num of CPUs)\n"
               "  -v...                            Print verbose output\n"
               "  -h, --help                       Print help\n"
               "  -V, --version                    Print version\n"
               "\n"
               "Examples:\n"
               "\n"
               "    Hunt with Sigma and Chainsaw Rules:\n"
               "        ./chainsaw hunt evtx_attack_samples/ -s sigma/ --mapping "
               "mappings/sigma-event-logs-all.yml -r rules/\n"
               "\n"
               "    Hunt with Sigma rules and output in JSON:\n"
               "        ./chainsaw hunt evtx_attack_samples/ -s sigma/ --mapping "
               "mappings/sigma-event-logs-all.yml --json\n"
               "\n"
               "    Search for the case-insensitive word 'mimikatz':\n"
               "        ./chainsaw search mimikatz -i evtx_attack_samples/\n"
               "\n"
               "    Search for Powershell Script Block Events (EventID 4014):\n"
               "        ./chainsaw search -t 'Event.System.EventID: =4104' evtx_attack_samples/\n";
    } else if (*command == "lint") {
        //: lint --help
        return "Lint provided rules to ensure that they load correctly\n"
               "\n"
               "Usage: chainsaw lint [OPTIONS] --kind <KIND> <PATH>\n"
               "\n"
               "Arguments:\n"
               "  <PATH>  The path to a collection of rules\n"
               "\n"
               "Options:\n"
               "      --kind <KIND>  The kind of rule to lint: chainsaw, sigma or stalker\n"
               "  -t, --tau          Output tau logic\n"
               "  -h, --help         Print help\n";
    } else if (*command == "dump") {
        return "Dump artefacts into a different format\n"
               "\n"
               "Usage: chainsaw dump [OPTIONS] <PATH>...\n"
               "\n"
               "Arguments:\n"
               "  <PATH>...  Paths containing artefacts to dump\n"
               "\n"
               "Options:\n"
               "  -j, --json                   Output as JSON\n"
               "      --jsonl                  Output as JSON lines\n"
               "      --load-unknown           Load files with an unknown extension\n"
               "      --extension <EXTENSION>  Only load files with this extension\n"
               "  -o, --output <OUTPUT>        Save output to a file\n"
               "      --skip-errors            Skip errors and continue processing\n"
               "  -h, --help                   Print help\n";
    } else if (*command == "hunt") {
        return "Hunt through artefacts using detection rules for threat detection\n"
               "\n"
               "Usage: chainsaw hunt [OPTIONS] [RULES] <PATH>...\n"
               "\n"
               "Arguments:\n"
               "  [RULES]    Path to chainsaw rules\n"
               "  <PATH>...  Paths containing artefacts to hunt\n"
               "\n"
               "Options:\n"
               "  -m, --mapping <MAPPING>  A mapping file to use with the sigma rules\n"
               "  -r, --rule <RULE>        Additional rule to hunt with\n"
               "  -s, --sigma <SIGMA>      Sigma rules to hunt with\n"
               "      --json               Output as JSON\n"
               "      --jsonl              Output as JSON lines\n"
               "      --csv                Output as CSV\n"
               "      --log                Output as text log\n"
               "  -o, --output <OUTPUT>    Save output to a file or directory\n"
               "  -c, --cache-to-disk      Cache results to disk\n"
               "  -h, --help               Print help\n";
    } else if (*command == "search") {
        return "Search through forensic artefacts for keywords or patterns\n"
               "\n"
               "Usage: chainsaw search [OPTIONS] [PATTERN] [PATH]...\n"
               "\n"
               "Arguments:\n"
               "  [PATTERN]  Search pattern\n"
               "  [PATH]...  Paths containing artefacts to search\n"
               "\n"
               "Options:\n"
               "  -e, --regex <REGEX>    A regular expression for searching\n"
               "  -t, --tau <TAU>        A tau expression for searching\n"
               "  -i, --ignore-case      Ignore case when searching\n"
               "      --match-any        Match any search (OR instead of AND)\n"
               "  -j, --json             Output as JSON\n"
               "      --jsonl            Output as JSON lines\n"
               "  -o, --output <OUTPUT>  Save output to a file\n"
               "      --skip-errors      Skip errors and continue processing\n"
               "  -h, --help             Print help\n";
    } else if (*command == "analyse") {
        return "Perform various analyses on artefacts\n"
               "\n"
               "Usage: chainsaw analyse <COMMAND>\n"
               "\n"
               "Commands:\n"
               "  shimcache  Create an execution timeline from Shimcache artefacts\n"
               "  srum       Parse SRUM database and output contents\n"
               "  help       Print this message or the help of the given subcommand(s)\n"
               "\n"
               "Options:\n"
               "  -h, --help  Print help\n";
    } else {
        // Неизвестная подкоманда для help
        return "error: unrecognized subcommand '" + *command + "'\n";
    }
}

// ----------------------------------------------------------------------------
// render_usage_error — формирует сообщение об ошибке парсинга в стиле clap
// ----------------------------------------------------------------------------

std::string render_usage_error(const std::string& error_msg) {
    // Формат clap: error + "\n\n" + Usage + "\n\n" + hint
    return error_msg + "\n\n"
                       "Usage: chainsaw [OPTIONS] <COMMAND>\n\n"
                       "For more information, try '--help'.\n";
}

// ----------------------------------------------------------------------------
// parse
// ----------------------------------------------------------------------------

ParseResult parse(int argc, char** argv) {
    ParseResult result;
    result.ok = false;
    result.command = HelpCommand{};

    if (argc < 2) {
        //: без аргументов - stderr=help, exit code=2
        result.ok = false;
        result.diagnostic.exit_code = 2;
        result.diagnostic.stderr_message = render_help(std::nullopt);
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
        // SPEC-SLICE-013: парсинг аргументов dump
        // Собираем пути после команды
        for (int i = cmd_idx + 1; i < argc; ++i) {
            const char* arg = argv[i];
            if (str_eq(arg, "-j") || str_eq(arg, "--json")) {
                dump_cmd.json = true;
            } else if (str_eq(arg, "--jsonl")) {
                dump_cmd.jsonl = true;
            } else if (str_eq(arg, "--skip-errors")) {
                dump_cmd.skip_errors = true;
            } else if (str_eq(arg, "--load-unknown")) {
                dump_cmd.load_unknown = true;
            } else if (str_eq(arg, "--extension") || str_eq(arg, "-e")) {
                // --extension требует следующий аргумент
                if (i + 1 < argc) {
                    ++i;
                    dump_cmd.extension = argv[i];
                }
            } else if (str_eq(arg, "-o") || str_eq(arg, "--output")) {
                // -o требует следующий аргумент
                if (i + 1 < argc) {
                    ++i;
                    dump_cmd.output = platform::path_from_utf8(argv[i]);
                }
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
            //: dump без path - это runtime error, не parsing error
            // exit code 1, но с ASCII banner + сообщением
            result.diagnostic.exit_code = 1;
            result.diagnostic.stderr_message =
                "[x] No compatible files were found in the provided paths";
            return result;
        }

        result.ok = true;
        result.command = dump_cmd;
    } else if (str_eq(cmd, "hunt")) {
        HuntCommand hunt_cmd;
        // SPEC-SLICE-012: полный парсинг аргументов hunt
        for (int i = cmd_idx + 1; i < argc; ++i) {
            const char* arg = argv[i];
            if (str_eq(arg, "-h") || str_eq(arg, "--help")) {
                result.ok = true;
                result.command = HelpCommand{"hunt"};
                return result;
            } else if (str_eq(arg, "-r") || str_eq(arg, "--rule")) {
                // -r, --rule <RULE> - Chainsaw rules
                if (i + 1 < argc) {
                    ++i;
                    hunt_cmd.rules.push_back(platform::path_from_utf8(argv[i]));
                }
            } else if (str_eq(arg, "-s") || str_eq(arg, "--sigma")) {
                // -s, --sigma <SIGMA> - Sigma rules
                if (i + 1 < argc) {
                    ++i;
                    hunt_cmd.sigma.push_back(platform::path_from_utf8(argv[i]));
                }
            } else if (str_eq(arg, "-m") || str_eq(arg, "--mapping")) {
                // -m, --mapping <MAPPING>
                if (i + 1 < argc) {
                    ++i;
                    hunt_cmd.mapping.push_back(platform::path_from_utf8(argv[i]));
                }
            } else if (str_eq(arg, "-o") || str_eq(arg, "--output")) {
                if (i + 1 < argc) {
                    ++i;
                    hunt_cmd.output = platform::path_from_utf8(argv[i]);
                }
            } else if (str_eq(arg, "--json")) {
                hunt_cmd.json = true;
            } else if (str_eq(arg, "--jsonl")) {
                hunt_cmd.jsonl = true;
            } else if (str_eq(arg, "--csv")) {
                hunt_cmd.csv = true;
            } else if (str_eq(arg, "--log")) {
                hunt_cmd.log = true;
            } else if (str_eq(arg, "-c") || str_eq(arg, "--cache-to-disk")) {
                hunt_cmd.cache_to_disk = true;
            } else if (str_eq(arg, "--skip-errors")) {
                hunt_cmd.skip_errors = true;
            } else if (str_eq(arg, "--load-unknown")) {
                hunt_cmd.load_unknown = true;
            } else if (str_eq(arg, "--from")) {
                if (i + 1 < argc) {
                    ++i;
                    hunt_cmd.from = argv[i];
                }
            } else if (str_eq(arg, "--to")) {
                if (i + 1 < argc) {
                    ++i;
                    hunt_cmd.to = argv[i];
                }
            } else if (str_eq(arg, "--timezone")) {
                if (i + 1 < argc) {
                    ++i;
                    hunt_cmd.timezone = argv[i];
                }
            } else if (str_eq(arg, "-l") || str_eq(arg, "--local")) {
                hunt_cmd.local = true;
            } else if (str_eq(arg, "-w") || str_eq(arg, "--column-width")) {
                if (i + 1 < argc) {
                    ++i;
                    hunt_cmd.column_width = static_cast<std::uint32_t>(std::stoul(argv[i]));
                }
            } else if (str_eq(arg, "-F") || str_eq(arg, "--full")) {
                hunt_cmd.full = true;
            } else if (str_eq(arg, "-M") || str_eq(arg, "--metadata")) {
                hunt_cmd.metadata = true;
            } else if (str_eq(arg, "-q")) {
                result.global.quiet = true;
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
            } else if (str_eq(arg, "--kind")) {
                // --kind <KIND> - следующий аргумент
                if (i + 1 < argc) {
                    ++i;
                    const char* kind_val = argv[i];
                    //: валидация значения kind
                    if (str_eq(kind_val, "chainsaw") || str_eq(kind_val, "sigma")) {
                        lint_cmd.kind = kind_val;
                    } else {
                        // Невалидное значение - exit code 2
                        result.diagnostic.exit_code = 2;
                        result.diagnostic.stderr_message =
                            std::string("error: invalid value '") + kind_val +
                            "' for '--kind <KIND>': unknown kind, must be: chainsaw, or sigma\n\n"
                            "For more information, try '--help'.\n";
                        return result;
                    }
                }
            } else if (starts_with(arg, "--kind=")) {
                // --kind=<value> format
                const char* kind_val = arg + 7;  // strlen("--kind=")
                if (str_eq(kind_val, "chainsaw") || str_eq(kind_val, "sigma")) {
                    lint_cmd.kind = kind_val;
                } else {
                    result.diagnostic.exit_code = 2;
                    result.diagnostic.stderr_message =
                        std::string("error: invalid value '") + kind_val +
                        "' for '--kind <KIND>': unknown kind, must be: chainsaw, or sigma\n\n"
                        "For more information, try '--help'.\n";
                    return result;
                }
            } else if (arg[0] != '-') {
                lint_cmd.path = platform::path_from_utf8(arg);
            }
        }

        if (lint_cmd.path.empty()) {
            result.diagnostic.exit_code = 2;
            result.diagnostic.stderr_message =
                "error: the following required arguments were not provided:\n"
                "  <PATH>\n\n"
                "Usage: chainsaw lint [OPTIONS] --kind <KIND> <PATH>\n\n"
                "For more information, try '--help'.\n";
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
            } else if (str_eq(arg, "--match-any")) {
                search_cmd.match_any = true;
            } else if (str_eq(arg, "--skip-errors")) {
                search_cmd.skip_errors = true;
            } else if (str_eq(arg, "--load-unknown")) {
                search_cmd.load_unknown = true;
            } else if (str_eq(arg, "-e") || str_eq(arg, "--regex")) {
                if (i + 1 < argc) {
                    ++i;
                    search_cmd.regex_patterns.push_back(argv[i]);
                }
            } else if (str_eq(arg, "-t") || str_eq(arg, "--tau")) {
                if (i + 1 < argc) {
                    ++i;
                    search_cmd.tau_exprs.push_back(argv[i]);
                }
            } else if (str_eq(arg, "-o") || str_eq(arg, "--output")) {
                if (i + 1 < argc) {
                    ++i;
                    search_cmd.output = platform::path_from_utf8(argv[i]);
                }
            } else if (str_eq(arg, "--from")) {
                if (i + 1 < argc) {
                    ++i;
                    search_cmd.from = argv[i];
                }
            } else if (str_eq(arg, "--to")) {
                if (i + 1 < argc) {
                    ++i;
                    search_cmd.to = argv[i];
                }
            } else if (str_eq(arg, "--timestamp")) {
                if (i + 1 < argc) {
                    ++i;
                    search_cmd.timestamp = argv[i];
                }
            } else if (str_eq(arg, "--extension")) {
                if (i + 1 < argc) {
                    ++i;
                    search_cmd.extensions.push_back(argv[i]);
                }
            } else if (arg[0] != '-') {
                // SPEC-SLICE-011 FACT-011/012: pattern vs path logic
                // Если есть -e/--regex или -t/--tau, первый positional = path
                // Иначе первый positional = pattern, остальные = paths
                if (!search_cmd.regex_patterns.empty() || !search_cmd.tau_exprs.empty()) {
                    search_cmd.paths.push_back(platform::path_from_utf8(arg));
                } else if (!search_cmd.pattern.has_value()) {
                    search_cmd.pattern = arg;
                } else {
                    search_cmd.paths.push_back(platform::path_from_utf8(arg));
                }
            }
        }
        // SPEC-SLICE-011 FACT-013: если path пуст, используем cwd
        if (search_cmd.paths.empty()) {
            search_cmd.paths.push_back(std::filesystem::current_path());
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
            // Неизвестная подкоманда analyse - exit code 2
            result.diagnostic.exit_code = 2;
            result.diagnostic.stderr_message = std::string("error: unrecognized subcommand '") +
                                               subcmd +
                                               "'\n\n"
                                               "Usage: chainsaw analyse <COMMAND>\n\n"
                                               "For more information, try '--help'.\n";
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
        //: Неизвестная команда - exit code 2
        result.diagnostic.exit_code = 2;
        result.diagnostic.stderr_message =
            render_usage_error(std::string("error: unrecognized subcommand '") + cmd + "'");
        return result;
    }

    return result;
}

}  // namespace chainsaw::cli
