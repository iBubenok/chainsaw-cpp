// ==============================================================================
// test_platform_basic.cpp - Базовые тесты платформы ()
// ==============================================================================
//
// TST-PLAT-0001: Базовые тесты MOD-0004 platform
// GUIDE-0001 G-071: TST-* в имени теста
//
// ==============================================================================

#include "chainsaw/platform.hpp"

#include <cassert>
#include <iostream>

namespace {

// TST-PLAT-0001: Тест path_from_utf8 и path_to_utf8
void test_path_roundtrip() {
    std::cout << "TST-PLAT-0001: Testing path UTF-8 roundtrip... ";

    std::string original = "/some/path/to/file.evtx";
    auto path = chainsaw::platform::path_from_utf8(original);
    std::string result = chainsaw::platform::path_to_utf8(path);

    // На Unix пути должны совпадать точно
    // На Windows могут быть разные разделители, но содержимое то же
#ifndef _WIN32
    assert(result == original && "Path roundtrip should preserve string on Unix");
#else
    // На Windows проверяем, что путь содержит нужные компоненты
    assert(result.find("file.evtx") != std::string::npos && "Path should contain filename");
#endif

    std::cout << "PASS\n";
}

// TST-PLAT-0002: Тест пустого пути
void test_empty_path() {
    std::cout << "TST-PLAT-0002: Testing empty path... ";

    auto path = chainsaw::platform::path_from_utf8("");
    std::string result = chainsaw::platform::path_to_utf8(path);

    assert(result.empty() && "Empty path should remain empty");

    std::cout << "PASS\n";
}

// TST-PLAT-0003: Тест os_name
void test_os_name() {
    std::cout << "TST-PLAT-0003: Testing os_name... ";

    std::string os = chainsaw::platform::os_name();

    assert(!os.empty() && "OS name should not be empty");

#ifdef _WIN32
    assert(os == "Windows" && "OS should be Windows");
#elif defined(__APPLE__)
    assert(os == "macOS" && "OS should be macOS");
#elif defined(__linux__)
    assert(os == "Linux" && "OS should be Linux");
#endif

    std::cout << "PASS (OS: " << os << ")\n";
}

// TST-PLAT-0004: Тест rule_prefix
void test_rule_prefix() {
    std::cout << "TST-PLAT-0004: Testing rule_prefix... ";

    const char* prefix = chainsaw::platform::rule_prefix();

    assert(prefix != nullptr && "Prefix should not be null");
    assert(prefix[0] != '\0' && "Prefix should not be empty");

#ifdef _WIN32
    assert(std::strcmp(prefix, "+") == 0 && "Windows prefix should be '+'");
#else
    // Non-Windows: UTF-8 bullet point "‣"
    assert(prefix[0] != '\0' && "Prefix should exist");
#endif

    std::cout << "PASS (prefix: " << prefix << ")\n";
}

// TST-PLAT-0005: Тест TTY detection (просто проверяем, что не падает)
void test_tty_detection() {
    std::cout << "TST-PLAT-0005: Testing TTY detection... ";

    // Просто вызываем функции - они не должны падать
    bool stdout_tty = chainsaw::platform::is_tty_stdout();
    bool stderr_tty = chainsaw::platform::is_tty_stderr();

    // В тестовом окружении обычно не TTY
    (void)stdout_tty;
    (void)stderr_tty;

    std::cout << "PASS (stdout_tty=" << stdout_tty << ", stderr_tty=" << stderr_tty << ")\n";
}

}  // anonymous namespace

int main() {
    std::cout << "=== Chainsaw Platform Basic Tests ===\n\n";

    test_path_roundtrip();
    test_empty_path();
    test_os_name();
    test_rule_prefix();
    test_tty_detection();

    std::cout << "\nAll tests passed!\n";
    return 0;
}
