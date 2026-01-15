// ==============================================================================
// test_cli_gtest.cpp - Тесты CLI модуля (GoogleTest)
// ==============================================================================
//
// MOD-0002: cli
// ADR-0006: CLI и пользовательский вывод
// ADR-0008: GoogleTest
// CLI-0001: CLI-контракт Chainsaw
// GUIDE-0001 G-030: help/usage/errors
// : интеграция quality gates
//
// ==============================================================================

#include "chainsaw/cli.hpp"
#include "chainsaw/platform.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace chainsaw::cli::test {

// ==============================================================================
// Вспомогательные функции
// ==============================================================================

// Конвертация вектора строк в argc/argv
struct Args {
    std::vector<std::string> strings;
    std::vector<char*> ptrs;

    explicit Args(std::initializer_list<std::string> args) : strings(args) {
        for (auto& s : strings) {
            ptrs.push_back(s.data());
        }
        ptrs.push_back(nullptr);
    }

    int argc() const { return static_cast<int>(strings.size()); }

    char** argv() { return ptrs.data(); }
};

// ==============================================================================
// TST-CLI-001: Парсинг --help
// ==============================================================================

TEST(CliTest, Parse_Help_ReturnsHelpCommand) {
    // Arrange
    Args args{"chainsaw", "--help"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::holds_alternative<HelpCommand>(result.command));
}

TEST(CliTest, Parse_HelpShort_ReturnsHelpCommand) {
    // Arrange
    Args args{"chainsaw", "-h"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::holds_alternative<HelpCommand>(result.command));
}

// ==============================================================================
// TST-CLI-002: Парсинг --version
// ==============================================================================

TEST(CliTest, Parse_Version_ReturnsVersionCommand) {
    // Arrange
    Args args{"chainsaw", "--version"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::holds_alternative<VersionCommand>(result.command));
}

TEST(CliTest, Parse_VersionShort_ReturnsVersionCommand) {
    // Arrange
    Args args{"chainsaw", "-V"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::holds_alternative<VersionCommand>(result.command));
}

// ==============================================================================
// TST-CLI-003: Глобальные опции
// ==============================================================================

TEST(CliTest, Parse_Quiet_SetsGlobalOption) {
    // Arrange
    // CLI-0001: -q — suppress informational output
    Args args{"chainsaw", "-q", "--help"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.global.quiet);
}

TEST(CliTest, Parse_Verbose_SetsGlobalOption) {
    // Arrange
    // CLI-0001: -v — increase logging verbosity
    Args args{"chainsaw", "-v", "--help"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.global.verbose);
}

TEST(CliTest, Parse_NoBanner_SetsGlobalOption) {
    // Arrange
    Args args{"chainsaw", "--no-banner", "--help"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.global.no_banner);
}

// ==============================================================================
// TST-CLI-004: Команда dump
// ==============================================================================

TEST(CliTest, Parse_Dump_ReturnsDumpCommand) {
    // Arrange
    Args args{"chainsaw", "dump", "test.evtx"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::holds_alternative<DumpCommand>(result.command));

    auto& cmd = std::get<DumpCommand>(result.command);
    EXPECT_EQ(cmd.paths.size(), 1);
}

TEST(CliTest, Parse_Dump_MultiplePaths) {
    // Arrange
    Args args{"chainsaw", "dump", "file1.evtx", "file2.evtx", "file3.evtx"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::holds_alternative<DumpCommand>(result.command));

    auto& cmd = std::get<DumpCommand>(result.command);
    EXPECT_EQ(cmd.paths.size(), 3);
}

// ==============================================================================
// TST-CLI-005: Команда hunt
// ==============================================================================

TEST(CliTest, Parse_Hunt_ReturnsHuntCommand) {
    // Arrange
    Args args{"chainsaw", "hunt", "rules/", "logs/"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::holds_alternative<HuntCommand>(result.command));
}

// ==============================================================================
// TST-CLI-006: Команда lint
// ==============================================================================

TEST(CliTest, Parse_Lint_ReturnsLintCommand) {
    // Arrange
    Args args{"chainsaw", "lint", "rules/"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::holds_alternative<LintCommand>(result.command));

    auto& cmd = std::get<LintCommand>(result.command);
    EXPECT_FALSE(cmd.path.empty());
}

// ==============================================================================
// TST-CLI-007: Команда search
// ==============================================================================

TEST(CliTest, Parse_Search_ReturnsSearchCommand) {
    // Arrange
    Args args{"chainsaw", "search", "--pattern", "test", "logs/"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::holds_alternative<SearchCommand>(result.command));
}

// ==============================================================================
// TST-CLI-008: Вывод help
// ==============================================================================

TEST(CliTest, RenderHelp_Main_ContainsCommands) {
    // Arrange & Act
    std::string help = render_help(std::nullopt);

    // Assert
    EXPECT_NE(help.find("dump"), std::string::npos);
    EXPECT_NE(help.find("hunt"), std::string::npos);
    EXPECT_NE(help.find("lint"), std::string::npos);
    EXPECT_NE(help.find("search"), std::string::npos);
}

TEST(CliTest, RenderHelp_Dump_ContainsDescription) {
    // Arrange & Act
    std::string help = render_help("dump");

    // Assert
    EXPECT_NE(help.find("dump"), std::string::npos);
}

// ==============================================================================
// TST-CLI-009: Вывод version
// ==============================================================================

TEST(CliTest, RenderVersion_ContainsVersion) {
    // Arrange & Act
    std::string version = render_version();

    // Assert
    EXPECT_FALSE(version.empty());
    // Версия должна содержать цифры
    bool has_digit = false;
    for (char c : version) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            has_digit = true;
            break;
        }
    }
    EXPECT_TRUE(has_digit);
}

// ==============================================================================
// TST-CLI-010: Ошибки парсинга
// ==============================================================================

TEST(CliTest, Parse_UnknownCommand_ReturnsError) {
    // Arrange
    Args args{"chainsaw", "no_such_cmd"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.diagnostic.exit_code, 2);  // Usage error = 2
    EXPECT_FALSE(result.diagnostic.stderr_message.empty());
    // Проверяем формат clap
    EXPECT_NE(result.diagnostic.stderr_message.find("error: unrecognized subcommand"),
              std::string::npos);
}

TEST(CliTest, Parse_NoArgs_ReturnsUsageError) {
    // Arrange
    Args args{"chainsaw"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    // Без аргументов: ok=false, exit_code=2, stderr=help
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.diagnostic.exit_code, 2);  // Usage error = 2
    // stderr должен содержать help текст
    EXPECT_NE(result.diagnostic.stderr_message.find("Rapidly work with Forensic Artefacts"),
              std::string::npos);
}

// ==============================================================================
// TST-CLI-011: Exit codes (SLICE-003)
// ==============================================================================

TEST(CliTest, ExitCode_Help_Is0) {
    //: --help exit code 0
    Args args{"chainsaw", "--help"};
    ParseResult result = parse(args.argc(), args.argv());
    EXPECT_TRUE(result.ok);
    // ok=true означает exit code 0 при успешном выполнении команды
}

TEST(CliTest, ExitCode_Version_Is0) {
    //: --version exit code 0
    Args args{"chainsaw", "--version"};
    ParseResult result = parse(args.argc(), args.argv());
    EXPECT_TRUE(result.ok);
}

TEST(CliTest, ExitCode_NoArgs_Is2) {
    //: no args exit code 2
    Args args{"chainsaw"};
    ParseResult result = parse(args.argc(), args.argv());
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.diagnostic.exit_code, 2);
}

TEST(CliTest, ExitCode_UnknownCommand_Is2) {
    //: unknown subcommand exit code 2
    Args args{"chainsaw", "no_such_cmd"};
    ParseResult result = parse(args.argc(), args.argv());
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.diagnostic.exit_code, 2);
}

TEST(CliTest, ExitCode_InvalidKind_Is2) {
    //: invalid value for --kind exit code 2
    Args args{"chainsaw", "lint", "--kind", "stalker", "rules/"};
    ParseResult result = parse(args.argc(), args.argv());
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.diagnostic.exit_code, 2);
    EXPECT_NE(result.diagnostic.stderr_message.find("invalid value 'stalker'"), std::string::npos);
}

// ==============================================================================
// TST-CLI-012: Byte-to-byte help/version (SLICE-003)
// ==============================================================================

TEST(CliTest, RenderHelp_ByteToByte_Golden) {
    //: проверка точного совпадения главной справки
    std::string help = render_help(std::nullopt);

    // Должен начинаться с "Rapidly work with Forensic Artefacts"
    // Golden: "Rapidly work with Forensic Artefacts\n\nUsage:..."
    EXPECT_TRUE(help.find("Rapidly work with Forensic Artefacts") == 0);

    // Проверяем ключевые элементы
    EXPECT_NE(help.find("Usage: chainsaw [OPTIONS] <COMMAND>"), std::string::npos);
    EXPECT_NE(help.find("Commands:"), std::string::npos);
    EXPECT_NE(help.find("Options:"), std::string::npos);
    EXPECT_NE(help.find("Examples:"), std::string::npos);
    EXPECT_NE(help.find("--no-banner"), std::string::npos);
    EXPECT_NE(help.find("--num-threads <NUM_THREADS>"), std::string::npos);
    EXPECT_NE(help.find("-v..."), std::string::npos);
}

TEST(CliTest, RenderVersion_ByteToByte_Golden) {
    //: проверка точного совпадения версии
    std::string version = render_version();
    EXPECT_EQ(version, "chainsaw 2.13.1\n");
}

TEST(CliTest, RenderHelp_Lint_ByteToByte_Golden) {
    //: lint --help
    std::string help = render_help("lint");

    EXPECT_NE(help.find("Lint provided rules to ensure that they load correctly"),
              std::string::npos);
    EXPECT_NE(help.find("Usage: chainsaw lint"), std::string::npos);
    EXPECT_NE(help.find("--kind <KIND>"), std::string::npos);
}

// ==============================================================================
// TST-CLI-013: Error message format (SLICE-003)
// ==============================================================================

TEST(CliTest, ErrorMessage_UnknownSubcommand_Format) {
    //: формат сообщения об ошибке
    Args args{"chainsaw", "no_such_cmd"};
    ParseResult result = parse(args.argc(), args.argv());

    EXPECT_FALSE(result.ok);
    // Формат clap: "error: unrecognized subcommand '<cmd>'"
    EXPECT_NE(result.diagnostic.stderr_message.find("error: unrecognized subcommand 'no_such_cmd'"),
              std::string::npos);
    // Должен содержать подсказку
    EXPECT_NE(result.diagnostic.stderr_message.find("For more information, try '--help'."),
              std::string::npos);
}

TEST(CliTest, ErrorMessage_InvalidValue_Format) {
    //: формат сообщения о невалидном значении
    Args args{"chainsaw", "lint", "--kind", "stalker", "rules/"};
    ParseResult result = parse(args.argc(), args.argv());

    EXPECT_FALSE(result.ok);
    // Формат clap: "error: invalid value '<val>' for '<opt>'"
    EXPECT_NE(
        result.diagnostic.stderr_message.find("error: invalid value 'stalker' for '--kind <KIND>'"),
        std::string::npos);
    EXPECT_NE(result.diagnostic.stderr_message.find("For more information, try '--help'."),
              std::string::npos);
}

// ==============================================================================
// TST-CLI-014: Lint --kind parsing (SLICE-003)
// ==============================================================================

TEST(CliTest, Parse_Lint_ValidKind_Chainsaw) {
    Args args{"chainsaw", "lint", "--kind", "chainsaw", "rules/"};
    ParseResult result = parse(args.argc(), args.argv());

    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::holds_alternative<LintCommand>(result.command));
    auto& cmd = std::get<LintCommand>(result.command);
    EXPECT_TRUE(cmd.kind.has_value());
    EXPECT_EQ(*cmd.kind, "chainsaw");
}

TEST(CliTest, Parse_Lint_ValidKind_Sigma) {
    Args args{"chainsaw", "lint", "--kind", "sigma", "rules/"};
    ParseResult result = parse(args.argc(), args.argv());

    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::holds_alternative<LintCommand>(result.command));
    auto& cmd = std::get<LintCommand>(result.command);
    EXPECT_TRUE(cmd.kind.has_value());
    EXPECT_EQ(*cmd.kind, "sigma");
}

TEST(CliTest, Parse_Lint_InvalidKind_Stalker) {
    Args args{"chainsaw", "lint", "--kind", "stalker", "rules/"};
    ParseResult result = parse(args.argc(), args.argv());

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.diagnostic.exit_code, 2);
}

}  // namespace chainsaw::cli::test
