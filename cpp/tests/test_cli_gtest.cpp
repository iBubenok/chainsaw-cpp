// ==============================================================================
// test_cli_gtest.cpp - Тесты CLI модуля (GoogleTest)
// ==============================================================================
//
// MOD-0002: cli
// ADR-0006: CLI и пользовательский вывод
// ADR-0008: GoogleTest
// CLI-0001: CLI-контракт Chainsaw
// GUIDE-0001 G-030: help/usage/errors
// Step 25: интеграция quality gates
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
    Args args{"chainsaw", "unknown_command"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.diagnostic.exit_code, 0);
    EXPECT_FALSE(result.diagnostic.stderr_message.empty());
}

TEST(CliTest, Parse_NoArgs_ShowsHelp) {
    // Arrange
    Args args{"chainsaw"};

    // Act
    ParseResult result = parse(args.argc(), args.argv());

    // Assert
    // Без аргументов должен показывать help
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(std::holds_alternative<HelpCommand>(result.command));
}

}  // namespace chainsaw::cli::test
