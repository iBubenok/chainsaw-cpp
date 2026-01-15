// ==============================================================================
// test_discovery_gtest.cpp - Тесты модуля File Discovery (GoogleTest)
// ==============================================================================
//
// MOD-0005: io::discovery
// SLICE-004: File Discovery (get_files -> discover_files)
// SPEC-SLICE-004: micro-spec поведения
// ADR-0008: GoogleTest
// ADR-0010: пути и кодировки
// ADR-0011: детерминизм
//
// Тесты: TST-DISC-001..TST-DISC-012
//
// ==============================================================================

#include "chainsaw/discovery.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

// Platform-specific includes для PID (уникальные temp директории при параллельных тестах)
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace chainsaw::io::test {

// ==============================================================================
// Test Fixture: создаёт временную структуру директорий для тестов
// ==============================================================================

class DiscoveryTest : public ::testing::Test {
protected:
    std::filesystem::path test_dir_;

    void SetUp() override {
        // Создаём уникальную временную директорию для каждого теста
        // Используем имя теста + PID + случайное число для избежания race condition
        // при параллельном запуске тестов (ctest -j)
        auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string unique_name = std::string("chainsaw_discovery_") + test_info->test_case_name() +
                                  "_" + test_info->name() + "_" +
                                  std::to_string(
#ifdef _WIN32
                                      GetCurrentProcessId()
#else
                                      getpid()
#endif
                                  );

        test_dir_ = std::filesystem::temp_directory_path() / unique_name;

        // Удаляем если существует от предыдущих запусков
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);

        // Создаём чистую директорию
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        // Очистка после тестов
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    // Создаёт пустой файл
    void create_file(const std::filesystem::path& path) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << "test content";
    }

    // Создаёт директорию
    void create_directory(const std::filesystem::path& path) {
        std::filesystem::create_directories(path);
    }
};

// ==============================================================================
// TST-DISC-001: Единичный файл с совпадающим расширением
// ==============================================================================

TEST_F(DiscoveryTest, TST_DISC_001_SingleFile_MatchingExtension) {
    // Arrange - создаём файл с .evtx расширением
    std::filesystem::path file_path = test_dir_ / "test.evtx";
    create_file(file_path);

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act
    auto result = discover_files({file_path}, opt);

    // Assert
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], file_path);
}

// ==============================================================================
// TST-DISC-002: Единичный файл без совпадающего расширения
// ==============================================================================

TEST_F(DiscoveryTest, TST_DISC_002_SingleFile_NonMatchingExtension) {
    // Arrange - создаём файл с .txt расширением, ищем .evtx
    std::filesystem::path file_path = test_dir_ / "test.txt";
    create_file(file_path);

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act
    auto result = discover_files({file_path}, opt);

    // Assert - файл не должен быть найден
    EXPECT_TRUE(result.empty());
}

// ==============================================================================
// TST-DISC-003: Пустая директория
// ==============================================================================

TEST_F(DiscoveryTest, TST_DISC_003_EmptyDirectory) {
    // Arrange - создаём пустую директорию
    std::filesystem::path empty_dir = test_dir_ / "empty";
    create_directory(empty_dir);

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act
    auto result = discover_files({empty_dir}, opt);

    // Assert - INV-006: пустой результат не ошибка
    EXPECT_TRUE(result.empty());
}

// ==============================================================================
// TST-DISC-004: Директория с файлами разных расширений
// ==============================================================================

TEST_F(DiscoveryTest, TST_DISC_004_Directory_MixedExtensions) {
    // Arrange - создаём файлы с разными расширениями
    create_file(test_dir_ / "file1.evtx");
    create_file(test_dir_ / "file2.evtx");
    create_file(test_dir_ / "file3.txt");
    create_file(test_dir_ / "file4.log");
    create_file(test_dir_ / "file5.evtx");

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act
    auto result = discover_files({test_dir_}, opt);

    // Assert - должны найти только .evtx файлы
    ASSERT_EQ(result.size(), 3);
    for (const auto& path : result) {
        EXPECT_EQ(path.extension(), ".evtx");
    }
}

// ==============================================================================
// TST-DISC-005: Рекурсивный обход поддиректорий
// ==============================================================================

TEST_F(DiscoveryTest, TST_DISC_005_RecursiveTraversal) {
    // Arrange - создаём вложенную структуру директорий
    create_file(test_dir_ / "root.evtx");
    create_file(test_dir_ / "level1" / "sub1.evtx");
    create_file(test_dir_ / "level1" / "level2" / "sub2.evtx");
    create_file(test_dir_ / "level1" / "level2" / "level3" / "sub3.evtx");
    create_file(test_dir_ / "other" / "sub4.evtx");

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act
    auto result = discover_files({test_dir_}, opt);

    // Assert - INV-004: depth-first обход, все файлы найдены
    EXPECT_EQ(result.size(), 5);
}

// ==============================================================================
// TST-DISC-006: extensions=None (все файлы)
// ==============================================================================

TEST_F(DiscoveryTest, TST_DISC_006_NoExtensionFilter) {
    // Arrange - создаём файлы с разными расширениями
    create_file(test_dir_ / "file1.evtx");
    create_file(test_dir_ / "file2.txt");
    create_file(test_dir_ / "file3.json");
    create_file(test_dir_ / "file4");  // файл без расширения

    DiscoveryOptions opt;
    // extensions = nullopt (по умолчанию)

    // Act
    auto result = discover_files({test_dir_}, opt);

    // Assert - все файлы должны быть найдены
    EXPECT_EQ(result.size(), 4);
}

// ==============================================================================
// TST-DISC-007: Несуществующий путь, skip_errors=true
// ==============================================================================

TEST_F(DiscoveryTest, TST_DISC_007_NonExistentPath_SkipErrors) {
    // Arrange
    std::filesystem::path nonexistent = test_dir_ / "does_not_exist";

    DiscoveryOptions opt;
    opt.skip_errors = true;

    // Act - не должно выбросить исключение
    // Перенаправляем stderr для проверки вывода
    std::stringstream stderr_capture;
    auto old_buf = std::cerr.rdbuf(stderr_capture.rdbuf());

    auto result = discover_files({nonexistent}, opt);

    std::cerr.rdbuf(old_buf);

    // Assert - INV-005: пустой результат, предупреждение в stderr
    EXPECT_TRUE(result.empty());
    EXPECT_NE(stderr_capture.str().find("[!]"), std::string::npos);
}

// ==============================================================================
// TST-DISC-008: Несуществующий путь, skip_errors=false
// ==============================================================================

TEST_F(DiscoveryTest, TST_DISC_008_NonExistentPath_ThrowsError) {
    // Arrange
    std::filesystem::path nonexistent = test_dir_ / "does_not_exist";

    DiscoveryOptions opt;
    opt.skip_errors = false;

    // Act & Assert - должно выбросить исключение
    EXPECT_THROW(discover_files({nonexistent}, opt), std::runtime_error);
}

// ==============================================================================
// TST-DISC-009: Специальный случай $MFT
// ==============================================================================

TEST_F(DiscoveryTest, TST_DISC_009_SpecialCase_MFT) {
    // Arrange - INV-002: файл без расширения с именем $MFT
    std::filesystem::path mft_file = test_dir_ / "$MFT";
    create_file(mft_file);
    create_file(test_dir_ / "regular.mft");

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"$MFT", "mft"};

    // Act
    auto result = discover_files({test_dir_}, opt);

    // Assert - оба файла должны быть найдены
    EXPECT_EQ(result.size(), 2);
}

TEST_F(DiscoveryTest, TST_DISC_009_MFT_NotMatchedWithoutExtension) {
    // Arrange - $MFT не должен быть найден, если "$MFT" не в extensions
    std::filesystem::path mft_file = test_dir_ / "$MFT";
    create_file(mft_file);

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act
    auto result = discover_files({test_dir_}, opt);

    // Assert
    EXPECT_TRUE(result.empty());
}

// ==============================================================================
// TST-DISC-010: Путь с пробелами
// ==============================================================================

TEST_F(DiscoveryTest, TST_DISC_010_PathWithSpaces) {
    // Arrange - RISK-0007: пути с пробелами
    std::filesystem::path dir_with_spaces = test_dir_ / "path with spaces";
    std::filesystem::path file_path = dir_with_spaces / "my file.evtx";
    create_file(file_path);

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act
    auto result = discover_files({dir_with_spaces}, opt);

    // Assert
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], file_path);
}

TEST_F(DiscoveryTest, TST_DISC_010_MultipleSpaces) {
    // Arrange - множественные пробелы
    std::filesystem::path complex_path = test_dir_ / "dir  with   multiple" / "spaces in name";
    std::filesystem::path file_path = complex_path / "test  file.evtx";
    create_file(file_path);

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act
    auto result = discover_files({complex_path}, opt);

    // Assert
    ASSERT_EQ(result.size(), 1);
}

// ==============================================================================
// TST-DISC-011: Детерминированный порядок (сортировка) - ADR-0011
// ==============================================================================

TEST_F(DiscoveryTest, TST_DISC_011_DeterministicOrder) {
    // Arrange - создаём файлы в "случайном" порядке
    create_file(test_dir_ / "z_file.evtx");
    create_file(test_dir_ / "a_file.evtx");
    create_file(test_dir_ / "m_file.evtx");
    create_file(test_dir_ / "subdir" / "b_file.evtx");

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act - вызываем несколько раз
    auto result1 = discover_files({test_dir_}, opt);
    auto result2 = discover_files({test_dir_}, opt);

    // Assert - ADR-0011: результат должен быть отсортирован и детерминирован
    EXPECT_EQ(result1, result2);

    // Проверяем, что результат отсортирован
    auto sorted = result1;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(result1, sorted);
}

TEST_F(DiscoveryTest, TST_DISC_011_SortedAcrossDirectories) {
    // Arrange - файлы в разных директориях
    create_file(test_dir_ / "z" / "file.evtx");
    create_file(test_dir_ / "a" / "file.evtx");
    create_file(test_dir_ / "m" / "file.evtx");

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act
    auto result = discover_files({test_dir_}, opt);

    // Assert - сортировка по полному пути
    ASSERT_EQ(result.size(), 3);
    for (size_t i = 1; i < result.size(); ++i) {
        EXPECT_LT(result[i - 1], result[i]);
    }
}

// ==============================================================================
// TST-DISC-012: Регистр расширений (case-sensitive) - INV-001
// ==============================================================================

TEST_F(DiscoveryTest, TST_DISC_012_ExtensionCaseSensitive) {
    // Arrange - INV-001: case-sensitive сравнение
    create_file(test_dir_ / "file1.evtx");
    create_file(test_dir_ / "file2.EVTX");
    create_file(test_dir_ / "file3.Evtx");

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};  // только нижний регистр

    // Act
    auto result = discover_files({test_dir_}, opt);

    // Assert - только file1.evtx должен быть найден (case-sensitive)
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].filename(), "file1.evtx");
}

TEST_F(DiscoveryTest, TST_DISC_012_MultipleExtensionCases) {
    // Arrange - поиск с разными регистрами в extensions
    create_file(test_dir_ / "file1.evtx");
    create_file(test_dir_ / "file2.EVTX");
    create_file(test_dir_ / "file3.Evtx");

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx", "EVTX"};

    // Act
    auto result = discover_files({test_dir_}, opt);

    // Assert - file1.evtx и file2.EVTX должны быть найдены
    EXPECT_EQ(result.size(), 2);
}

// ==============================================================================
// Дополнительные тесты
// ==============================================================================

TEST_F(DiscoveryTest, MultipleInputPaths) {
    // Arrange - несколько входных путей
    std::filesystem::path dir1 = test_dir_ / "dir1";
    std::filesystem::path dir2 = test_dir_ / "dir2";

    create_file(dir1 / "file1.evtx");
    create_file(dir2 / "file2.evtx");

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act
    auto result = discover_files({dir1, dir2}, opt);

    // Assert
    EXPECT_EQ(result.size(), 2);
}

TEST_F(DiscoveryTest, MultipleExtensions) {
    // Arrange - поиск нескольких расширений
    create_file(test_dir_ / "file1.evtx");
    create_file(test_dir_ / "file2.json");
    create_file(test_dir_ / "file3.xml");
    create_file(test_dir_ / "file4.txt");

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx", "json", "xml"};

    // Act
    auto result = discover_files({test_dir_}, opt);

    // Assert
    EXPECT_EQ(result.size(), 3);
}

TEST_F(DiscoveryTest, FileWithNoExtension) {
    // Arrange - файл без расширения
    create_file(test_dir_ / "noextension");

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act
    auto result = discover_files({test_dir_}, opt);

    // Assert - файл без расширения не должен быть найден
    EXPECT_TRUE(result.empty());
}

TEST_F(DiscoveryTest, EmptyInputVector) {
    // Arrange - пустой вектор входов
    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act
    auto result = discover_files({}, opt);

    // Assert
    EXPECT_TRUE(result.empty());
}

TEST_F(DiscoveryTest, SingleFileAsInput) {
    // Arrange - одиночный файл как вход (не директория)
    std::filesystem::path file_path = test_dir_ / "single.evtx";
    create_file(file_path);

    DiscoveryOptions opt;
    opt.extensions = std::unordered_set<std::string>{"evtx"};

    // Act
    auto result = discover_files({file_path}, opt);

    // Assert
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], file_path);
}

}  // namespace chainsaw::io::test
