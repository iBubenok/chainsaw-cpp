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
#include "chainsaw/output.hpp"
#include "chainsaw/platform.hpp"

#include <exception>
#include <iostream>

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
// Выполнение команд (заглушки для Step 24)
// ----------------------------------------------------------------------------

int run_dump(const chainsaw::cli::DumpCommand& cmd, const chainsaw::cli::GlobalOptions& global,
             chainsaw::output::Writer& writer) {
    (void)cmd;
    (void)global;
    writer.info("dump command invoked (not yet implemented)");
    writer.info("Paths provided:");
    for (const auto& p : cmd.paths) {
        writer.info(std::string("  - ") + chainsaw::platform::path_to_utf8(p));
    }
    return 0;
}

int run_hunt(const chainsaw::cli::HuntCommand& cmd, const chainsaw::cli::GlobalOptions& global,
             chainsaw::output::Writer& writer) {
    (void)cmd;
    (void)global;
    writer.info("hunt command invoked (not yet implemented)");
    return 0;
}

int run_lint(const chainsaw::cli::LintCommand& cmd, const chainsaw::cli::GlobalOptions& global,
             chainsaw::output::Writer& writer) {
    (void)global;
    writer.info("lint command invoked (not yet implemented)");
    writer.info(std::string("Path: ") + chainsaw::platform::path_to_utf8(cmd.path));
    return 0;
}

int run_search(const chainsaw::cli::SearchCommand& cmd, const chainsaw::cli::GlobalOptions& global,
               chainsaw::output::Writer& writer) {
    (void)global;
    writer.info("search command invoked (not yet implemented)");
    if (cmd.pattern.has_value()) {
        writer.info(std::string("Pattern: ") + *cmd.pattern);
    }
    return 0;
}

int run_analyse_shimcache(const chainsaw::cli::AnalyseShimcacheCommand& cmd,
                          const chainsaw::cli::GlobalOptions& global,
                          chainsaw::output::Writer& writer) {
    (void)global;
    writer.info("analyse shimcache command invoked (not yet implemented)");
    writer.info(std::string("Shimcache path: ") +
                chainsaw::platform::path_to_utf8(cmd.shimcache_path));
    return 0;
}

int run_analyse_srum(const chainsaw::cli::AnalyseSrumCommand& cmd,
                     const chainsaw::cli::GlobalOptions& global, chainsaw::output::Writer& writer) {
    (void)global;
    writer.info("analyse srum command invoked (not yet implemented)");
    writer.info(std::string("SRUM path: ") + chainsaw::platform::path_to_utf8(cmd.srum_path));
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

    // 3. Обработка ошибок парсинга
    if (!parse_result.ok) {
        writer.error(parse_result.diagnostic.stderr_message);
        return parse_result.diagnostic.exit_code;
    }

    // 4. Dispatch команды
    return std::visit(
        [&](auto&& cmd) -> int {
            using T = std::decay_t<decltype(cmd)>;

            if constexpr (std::is_same_v<T, cli::HelpCommand>) {
                // --help или help subcommand
                print_banner(writer, out_cfg.no_banner, out_cfg.quiet);
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
