// ==============================================================================
// test_cli_basic.cpp - Базовые тесты CLI ()
// ==============================================================================
//
// TST-CLI-0001: Базовый тест парсинга CLI
// GUIDE-0001 G-071: TST-* в имени теста
//
// Это базовый тест для проверки компиляции и минимальной работоспособности.
// Полноценные тесты с GoogleTest будут добавлены в +.
//
// ==============================================================================

#include "chainsaw/cli.hpp"

#include <cassert>
#include <cstring>
#include <iostream>

namespace {

// Вспомогательная функция для создания argv
std::vector<char*> make_argv(std::initializer_list<const char*> args) {
    std::vector<char*> result;
    for (const char* arg : args) {
        result.push_back(const_cast<char*>(arg));
    }
    return result;
}

// TST-CLI-0001: Тест парсинга --help
void test_help_command() {
    std::cout << "TST-CLI-0001: Testing --help parsing... ";

    auto argv = make_argv({"chainsaw", "--help"});
    auto result = chainsaw::cli::parse(static_cast<int>(argv.size()), argv.data());

    assert(result.ok && "Parse should succeed");
    assert(std::holds_alternative<chainsaw::cli::HelpCommand>(result.command) &&
           "Command should be HelpCommand");

    std::cout << "PASS\n";
}

// TST-CLI-0002: Тест парсинга --version
void test_version_command() {
    std::cout << "TST-CLI-0002: Testing --version parsing... ";

    auto argv = make_argv({"chainsaw", "--version"});
    auto result = chainsaw::cli::parse(static_cast<int>(argv.size()), argv.data());

    assert(result.ok && "Parse should succeed");
    assert(std::holds_alternative<chainsaw::cli::VersionCommand>(result.command) &&
           "Command should be VersionCommand");

    std::cout << "PASS\n";
}

// TST-CLI-0003: Тест парсинга dump с путём
void test_dump_command() {
    std::cout << "TST-CLI-0003: Testing dump command parsing... ";

    auto argv = make_argv({"chainsaw", "dump", "/path/to/file.evtx"});
    auto result = chainsaw::cli::parse(static_cast<int>(argv.size()), argv.data());

    assert(result.ok && "Parse should succeed");
    assert(std::holds_alternative<chainsaw::cli::DumpCommand>(result.command) &&
           "Command should be DumpCommand");

    [[maybe_unused]] const auto& cmd = std::get<chainsaw::cli::DumpCommand>(result.command);
    assert(!cmd.paths.empty() && "Paths should not be empty");
    assert(cmd.paths[0] == "/path/to/file.evtx" && "Path should match input");

    std::cout << "PASS\n";
}

// TST-CLI-0004: Тест неизвестной команды
void test_unknown_command() {
    std::cout << "TST-CLI-0004: Testing unknown command error... ";

    auto argv = make_argv({"chainsaw", "unknown_cmd"});
    auto result = chainsaw::cli::parse(static_cast<int>(argv.size()), argv.data());

    assert(!result.ok && "Parse should fail for unknown command");
    //: Неизвестная команда возвращает exit code 2
    assert(result.diagnostic.exit_code == 2 && "Exit code should be 2 for unknown command");

    std::cout << "PASS\n";
}

// TST-CLI-0005: Тест render_help
void test_render_help() {
    std::cout << "TST-CLI-0005: Testing render_help... ";

    std::string help = chainsaw::cli::render_help();

    assert(!help.empty() && "Help should not be empty");
    assert(help.find("chainsaw") != std::string::npos && "Help should contain 'chainsaw'");
    assert(help.find("dump") != std::string::npos && "Help should mention dump command");
    assert(help.find("hunt") != std::string::npos && "Help should mention hunt command");
    assert(help.find("search") != std::string::npos && "Help should mention search command");

    std::cout << "PASS\n";
}

// TST-CLI-0006: Тест render_version
void test_render_version() {
    std::cout << "TST-CLI-0006: Testing render_version... ";

    std::string version = chainsaw::cli::render_version();

    assert(!version.empty() && "Version should not be empty");
    assert(version.find("chainsaw") != std::string::npos && "Version should contain 'chainsaw'");

    std::cout << "PASS\n";
}

}  // anonymous namespace

int main() {
    std::cout << "=== Chainsaw CLI Basic Tests ===\n\n";

    test_help_command();
    test_version_command();
    test_dump_command();
    test_unknown_command();
    test_render_help();
    test_render_version();

    std::cout << "\nAll tests passed!\n";
    return 0;
}
