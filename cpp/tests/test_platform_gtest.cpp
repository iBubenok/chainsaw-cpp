// ==============================================================================
// test_platform_gtest.cpp - Тесты платформенного модуля (GoogleTest)
// ==============================================================================
//
// MOD-0004: platform
// ADR-0008: GoogleTest
// ADR-0010: пути и кодировки
// GUIDE-0001 G-040..G-042: единый тип путей, преобразования, безопасность
// : интеграция quality gates
//
// ==============================================================================

#include "chainsaw/platform.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
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

// ==============================================================================
// TST-PLATFORM-005: Тесты путей с пробелами
// ==============================================================================

TEST(PlatformTest, PathFromUtf8_WithSpaces) {
    // Arrange - путь с пробелами
    std::string utf8 = "path with spaces/my file.txt";

    // Act
    std::filesystem::path p = path_from_utf8(utf8);

    // Assert
    EXPECT_FALSE(p.empty());
    EXPECT_EQ(p.filename(), "my file.txt");
}

TEST(PlatformTest, PathToUtf8_WithSpaces) {
    // Arrange
    std::filesystem::path p = "path with spaces/my file.txt";

    // Act
    std::string utf8 = path_to_utf8(p);

    // Assert
    EXPECT_NE(utf8.find("spaces"), std::string::npos);
    EXPECT_NE(utf8.find("my file.txt"), std::string::npos);
}

TEST(PlatformTest, PathConversion_RoundtripWithSpaces) {
    // Arrange - путь с множественными пробелами
    std::filesystem::path original = "some path/with  multiple/spaces in name.log";

    // Act
    std::string utf8 = path_to_utf8(original);
    std::filesystem::path roundtrip = path_from_utf8(utf8);

    // Assert
    EXPECT_EQ(roundtrip.filename(), original.filename());
}

// ==============================================================================
// TST-PLATFORM-006: Тесты путей с Unicode (FACT-003, FACT-004)
// ==============================================================================

TEST(PlatformTest, PathFromUtf8_WithUnicode) {
    // Arrange - китайские иероглифы (FACT-004: Chinese characters)
    std::string utf8 = "folder/文件.txt";

    // Act
    std::filesystem::path p = path_from_utf8(utf8);

    // Assert
    EXPECT_FALSE(p.empty());
    // filename должен содержать Unicode символы
    std::string filename = path_to_utf8(p);
    EXPECT_NE(filename.find("文件"), std::string::npos);
}

TEST(PlatformTest, PathFromUtf8_WithCyrillic) {
    // Arrange - кириллица
    std::string utf8 = "папка/файл.txt";

    // Act
    std::filesystem::path p = path_from_utf8(utf8);

    // Assert
    EXPECT_FALSE(p.empty());
}

TEST(PlatformTest, PathFromUtf8_WithEmoji) {
    // Arrange - эмодзи (расширенный Unicode)
    std::string utf8 = "folder/file_🔥.txt";

    // Act
    std::filesystem::path p = path_from_utf8(utf8);

    // Assert
    EXPECT_FALSE(p.empty());
}

TEST(PlatformTest, PathConversion_RoundtripWithUnicode) {
    // Arrange
    std::string original_utf8 = "путь/к/файлу.txt";

    // Act
    std::filesystem::path p = path_from_utf8(original_utf8);
    std::string roundtrip = path_to_utf8(p);

    // Assert - проверяем, что кириллица сохранилась
    EXPECT_NE(roundtrip.find("путь"), std::string::npos);
}

// ==============================================================================
// TST-PLATFORM-007: Тесты временных файлов (FACT-011, FACT-012)
// ==============================================================================

TEST(PlatformTest, MakeTempFile_CreatesFile) {
    // Arrange
    std::string prefix = "chainsaw_test";

    // Act
    std::filesystem::path temp_path = make_temp_file(prefix);

    // Assert
    EXPECT_FALSE(temp_path.empty());
    EXPECT_TRUE(std::filesystem::exists(temp_path));

    // Cleanup
    std::filesystem::remove(temp_path);
}

TEST(PlatformTest, MakeTempFile_ContainsPrefix) {
    // Arrange
    std::string prefix = "myprefix";

    // Act
    std::filesystem::path temp_path = make_temp_file(prefix);

    // Assert
    std::string filename = temp_path.filename().string();
    EXPECT_NE(filename.find(prefix), std::string::npos);

    // Cleanup
    std::filesystem::remove(temp_path);
}

TEST(PlatformTest, MakeTempFile_UniqueFiles) {
    // Arrange & Act - создаём два файла с одинаковым префиксом
    std::filesystem::path path1 = make_temp_file("test");
    std::filesystem::path path2 = make_temp_file("test");

    // Assert - пути должны быть разными
    EXPECT_NE(path1, path2);
    EXPECT_TRUE(std::filesystem::exists(path1));
    EXPECT_TRUE(std::filesystem::exists(path2));

    // Cleanup
    std::filesystem::remove(path1);
    std::filesystem::remove(path2);
}

TEST(PlatformTest, MakeTempFile_IsWritable) {
    // Arrange
    std::filesystem::path temp_path = make_temp_file("write_test");

    // Act - пробуем записать в файл
    {
        std::ofstream out(temp_path);
        out << "test content";
    }

    // Assert - читаем обратно
    std::string content;
    {
        std::ifstream in(temp_path);
        std::getline(in, content);
    }  // Закрываем поток перед удалением (Windows требует)
    EXPECT_EQ(content, "test content");

    // Cleanup
    std::filesystem::remove(temp_path);
}

TEST(PlatformTest, MakeTempFile_EmptyPrefix) {
    // Arrange & Act - пустой префикс должен работать
    std::filesystem::path temp_path = make_temp_file("");

    // Assert
    EXPECT_FALSE(temp_path.empty());
    EXPECT_TRUE(std::filesystem::exists(temp_path));

    // Cleanup
    std::filesystem::remove(temp_path);
}

}  // namespace chainsaw::platform::test
