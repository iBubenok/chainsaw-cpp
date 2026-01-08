// ==============================================================================
// test_platform_gtest.cpp - Тесты платформенного модуля (GoogleTest)
// ==============================================================================
//
// MOD-0004: platform
// ADR-0008: GoogleTest
// ADR-0010: пути и кодировки
// GUIDE-0001 G-040..G-042: единый тип путей, преобразования, безопасность
// Step 25: интеграция quality gates
//
// ==============================================================================

#include "chainsaw/platform.hpp"

#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace chainsaw::platform::test {

// ==============================================================================
// TST-PLATFORM-001: Идентификация платформы (os_name)
// ==============================================================================

TEST(PlatformTest, OsName_ReturnsNonEmpty) {
    // Arrange & Act
    std::string name = os_name();

    // Assert
    EXPECT_FALSE(name.empty());
    EXPECT_TRUE(name == "Windows" || name == "Linux" || name == "macOS");
}

TEST(PlatformTest, OsName_IsConsistent) {
    // Arrange & Act
    std::string name1 = os_name();
    std::string name2 = os_name();

    // Assert
    EXPECT_EQ(name1, name2);
}

// ==============================================================================
// TST-PLATFORM-002: Преобразование путей UTF-8 <-> path
// ==============================================================================

TEST(PlatformTest, PathToUtf8_BasicPath) {
    // Arrange
    std::filesystem::path p = "test/path/file.txt";

    // Act
    std::string utf8 = path_to_utf8(p);

    // Assert
    EXPECT_FALSE(utf8.empty());
    EXPECT_NE(utf8.find("test"), std::string::npos);
    EXPECT_NE(utf8.find("file.txt"), std::string::npos);
}

TEST(PlatformTest, PathFromUtf8_BasicPath) {
    // Arrange
    std::string utf8 = "test/path/file.txt";

    // Act
    std::filesystem::path p = path_from_utf8(utf8);

    // Assert
    EXPECT_FALSE(p.empty());
    EXPECT_EQ(p.filename(), "file.txt");
}

TEST(PlatformTest, PathConversion_Roundtrip) {
    // Arrange
    std::filesystem::path original = "test/path/file.txt";

    // Act
    std::string utf8 = path_to_utf8(original);
    std::filesystem::path roundtrip = path_from_utf8(utf8);

    // Assert
    // Нормализуем для сравнения (разные платформы могут использовать разные разделители)
    EXPECT_EQ(roundtrip.filename(), original.filename());
}

TEST(PlatformTest, PathToUtf8_EmptyPath) {
    // Arrange
    std::filesystem::path p;

    // Act
    std::string utf8 = path_to_utf8(p);

    // Assert
    EXPECT_TRUE(utf8.empty());
}

TEST(PlatformTest, PathFromUtf8_EmptyString) {
    // Arrange
    std::string utf8;

    // Act
    std::filesystem::path p = path_from_utf8(utf8);

    // Assert
    EXPECT_TRUE(p.empty());
}

// ==============================================================================
// TST-PLATFORM-003: TTY detection
// ==============================================================================

TEST(PlatformTest, IsTtyStdout_ReturnsBoolean) {
    // Act
    bool result = is_tty_stdout();

    // Assert - в тестах обычно не TTY, но функция должна работать
    // Просто проверяем, что не падает
    EXPECT_TRUE(result == true || result == false);
}

TEST(PlatformTest, IsTtyStderr_ReturnsBoolean) {
    // Act
    bool result = is_tty_stderr();

    // Assert
    EXPECT_TRUE(result == true || result == false);
}

// ==============================================================================
// TST-PLATFORM-004: Платформенные константы
// ==============================================================================

TEST(PlatformTest, RulePrefix_ReturnsNonNull) {
    // Act
    const char* prefix = rule_prefix();

    // Assert
    EXPECT_NE(prefix, nullptr);
    EXPECT_GT(std::strlen(prefix), 0);
}

TEST(PlatformTest, RulePrefix_IsConsistent) {
    // Act
    const char* prefix1 = rule_prefix();
    const char* prefix2 = rule_prefix();

    // Assert
    EXPECT_EQ(prefix1, prefix2);  // Должны возвращать один и тот же указатель
}

}  // namespace chainsaw::platform::test
