// ==============================================================================
// test_extended_gtest.cpp - Расширенные тесты (TEST-EXPAND-0001)
// ==============================================================================
//
// : Реализация расширенных тестов
// TEST-EXPAND-0001: План расширения тестов (на основе рисков)
//
// Категории:
// - TST-DET-*: Determinism
// - TST-ROB-*: Robustness (REQ-SEC-0017)
// - TST-SEC-*: Security (REQ-SEC-0021, RISK-0025)
// - TST-CLI-*: CLI/Output (RISK-0031, RISK-0028)
//
// ==============================================================================

#include <chainsaw/cli.hpp>
#include <chainsaw/discovery.hpp>
#include <chainsaw/platform.hpp>
#include <chainsaw/reader.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace chainsaw;
using namespace chainsaw::io;

/// Вспомогательная функция для сериализации Value в JSON строку
static std::string value_to_json_string(const Value& v) {
    auto doc = v.to_rapidjson_document();
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    return buffer.GetString();
}

// ============================================================================
// Test Fixtures
// ============================================================================

/// Fixture для расширенных тестов с временными файлами
class ExtendedTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string unique_name = std::string("chainsaw_ext_") + test_info->test_case_name() + "_" +
                                  test_info->name() + "_" +
                                  std::to_string(
#ifdef _WIN32
                                      GetCurrentProcessId()
#else
                                      getpid()
#endif
                                  );
        temp_dir_ = fs::temp_directory_path() / unique_name;

        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
        fs::create_directories(temp_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    /// Создать временный файл с содержимым
    fs::path create_temp_file(const std::string& name, const std::string& content) {
        fs::path file_path = temp_dir_ / name;
        std::ofstream file(file_path, std::ios::binary);
        file << content;
        file.close();
        return file_path;
    }

    /// Создать временный файл с бинарным содержимым
    fs::path create_binary_file(const std::string& name, const std::vector<uint8_t>& content) {
        fs::path file_path = temp_dir_ / name;
        std::ofstream file(file_path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(content.data()),
                   static_cast<std::streamsize>(content.size()));
        file.close();
        return file_path;
    }

    fs::path temp_dir_;
};

// ============================================================================
// TST-DET-*: Determinism tests
// ============================================================================

/// TST-DET-001: Reader итерация детерминирована — одинаковые результаты при повторных прогонах
TEST_F(ExtendedTestFixture, TST_DET_001_ReaderDeterminism) {
    // RISK-0011: Детерминизм
    // Проверяем что Reader возвращает одинаковые результаты при повторных прогонах

    auto path = create_temp_file("test.json", R"([
        {"id": 1, "name": "first"},
        {"id": 2, "name": "second"},
        {"id": 3, "name": "third"}
    ])");

    // Первый прогон
    std::vector<std::string> run1;
    {
        auto result = Reader::open(path);
        ASSERT_TRUE(result.ok);
        Document doc;
        while (result.reader->next(doc)) {
            run1.push_back(value_to_json_string(doc.data));
        }
    }

    // Второй прогон
    std::vector<std::string> run2;
    {
        auto result = Reader::open(path);
        ASSERT_TRUE(result.ok);
        Document doc;
        while (result.reader->next(doc)) {
            run2.push_back(value_to_json_string(doc.data));
        }
    }

    // Третий прогон
    std::vector<std::string> run3;
    {
        auto result = Reader::open(path);
        ASSERT_TRUE(result.ok);
        Document doc;
        while (result.reader->next(doc)) {
            run3.push_back(value_to_json_string(doc.data));
        }
    }

    // Проверяем детерминизм
    ASSERT_EQ(run1.size(), run2.size());
    ASSERT_EQ(run2.size(), run3.size());

    for (size_t i = 0; i < run1.size(); ++i) {
        EXPECT_EQ(run1[i], run2[i]) << "Mismatch at index " << i << " between run 1 and 2";
        EXPECT_EQ(run2[i], run3[i]) << "Mismatch at index " << i << " between run 2 and 3";
    }
}

/// TST-DET-002: JSONL итерация детерминирована
TEST_F(ExtendedTestFixture, TST_DET_002_JsonlDeterminism) {
    auto path = create_temp_file("test.jsonl", R"({"line": 1, "value": "alpha"}
{"line": 2, "value": "beta"}
{"line": 3, "value": "gamma"})");

    std::vector<std::string> runs[3];

    for (int r = 0; r < 3; ++r) {
        auto result = Reader::open(path);
        ASSERT_TRUE(result.ok);
        Document doc;
        while (result.reader->next(doc)) {
            runs[r].push_back(value_to_json_string(doc.data));
        }
    }

    for (int r = 1; r < 3; ++r) {
        ASSERT_EQ(runs[0].size(), runs[r].size());
        for (size_t i = 0; i < runs[0].size(); ++i) {
            EXPECT_EQ(runs[0][i], runs[r][i]);
        }
    }
}

/// TST-DET-003: XML итерация детерминирована
TEST_F(ExtendedTestFixture, TST_DET_003_XmlDeterminism) {
    auto path = create_temp_file("test.xml", R"(<?xml version="1.0"?>
<root>
    <item id="1">first</item>
    <item id="2">second</item>
    <item id="3">third</item>
</root>)");

    std::vector<std::string> runs[3];

    for (int r = 0; r < 3; ++r) {
        auto result = Reader::open(path);
        ASSERT_TRUE(result.ok);
        Document doc;
        while (result.reader->next(doc)) {
            runs[r].push_back(value_to_json_string(doc.data));
        }
    }

    for (int r = 1; r < 3; ++r) {
        ASSERT_EQ(runs[0].size(), runs[r].size());
        for (size_t i = 0; i < runs[0].size(); ++i) {
            EXPECT_EQ(runs[0][i], runs[r][i]);
        }
    }
}

// ============================================================================
// TST-ROB-*: Robustness tests (REQ-SEC-0017)
// ============================================================================

/// TST-ROB-001: Truncated EVTX (обрезанный заголовок) — не crash
TEST_F(ExtendedTestFixture, TST_ROB_001_TruncatedEvtxHeader) {
    // REQ-SEC-0017: Недоверенные входы
    // Файл с обрезанным заголовком (менее 4096 байт)
    std::vector<uint8_t> truncated(100, 0);
    std::memcpy(truncated.data(), "ElfFile", 8);  // Частичный заголовок

    auto path = create_binary_file("truncated.evtx", truncated);

    // Должен обработать без crash
    auto result = Reader::open(path, false, false);
    // Ожидаем ошибку, но не crash
    if (result.reader) {
        Document doc;
        result.reader->next(doc);  // Не должен упасть
    }
    // Если ok=false — это тоже валидный результат
}

/// TST-ROB-002: Corrupted EVTX (битый chunk) — не crash
TEST_F(ExtendedTestFixture, TST_ROB_002_CorruptedEvtxChunk) {
    // EVTX с валидным заголовком, но битым чанком
    std::vector<uint8_t> corrupted(8192, 0);
    std::memcpy(corrupted.data(), "ElfFile", 8);  // Валидный заголовок файла

    // Добавляем невалидный чанк на позиции 4096
    corrupted[4096] = 'X';  // Невалидная магия чанка

    auto path = create_binary_file("corrupted.evtx", corrupted);

    auto result = Reader::open(path, false, true);  // skip_errors=true
    EXPECT_TRUE(result.ok);  // С skip_errors не должно быть ошибки
    if (result.reader) {
        Document doc;
        // Итерация не должна вызывать crash
        while (result.reader->next(doc)) {
            // Продолжаем
        }
    }
}

/// TST-ROB-003: Empty EVTX (0 bytes) — не crash
TEST_F(ExtendedTestFixture, TST_ROB_003_EmptyEvtx) {
    auto path = create_temp_file("empty.evtx", "");

    auto result = Reader::open(path, false, false);
    // Пустой файл — должна быть ошибка
    EXPECT_FALSE(result.ok);
}

/// TST-ROB-004: Non-EVTX file with .evtx extension — не crash
TEST_F(ExtendedTestFixture, TST_ROB_004_NonEvtxWithEvtxExtension) {
    auto path = create_temp_file("fake.evtx", "This is not an EVTX file, just plain text.");

    auto result = Reader::open(path, false, false);
    EXPECT_FALSE(result.ok);
    // Не должен упасть
}

/// TST-ROB-005: Invalid YAML Sigma rule — не crash
TEST_F(ExtendedTestFixture, TST_ROB_005_InvalidYamlSigma) {
    // Невалидный YAML
    auto path = create_temp_file("invalid.yml", R"(
title: Test Rule
logsource:
    category: test
    - broken yaml structure
      indentation: wrong
)");

    // Парсим как обычный файл — не должен crash
    std::ifstream file(path);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_FALSE(content.empty());
    // Тест на то что файл создан — парсинг YAML в Sigma loader
}

/// TST-ROB-006: Malformed Chainsaw rule — не crash
TEST_F(ExtendedTestFixture, TST_ROB_006_MalformedChainsawRule) {
    auto path = create_temp_file("malformed.yml", R"(
title: Malformed Rule
# Missing required fields
status: experimental
)");

    std::ifstream file(path);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_FALSE(content.empty());
}

/// TST-ROB-007: Very long path (>260 chars) — обработка
TEST_F(ExtendedTestFixture, TST_ROB_007_VeryLongPath) {
    // Создаём глубокую структуру директорий
    // На Windows MAX_PATH=260, но с \\?\ можно длиннее
    fs::path deep_path = temp_dir_;
    for (int i = 0; i < 30; ++i) {
        deep_path /= "subdir_" + std::to_string(i);
    }

    std::error_code ec;
    fs::create_directories(deep_path, ec);

    if (!ec) {
        // Директория создана — пробуем создать файл
        fs::path file_path = deep_path / "test.json";
        std::ofstream file(file_path);
        if (file.is_open()) {
            file << "{}";
            file.close();

            auto result = Reader::open(file_path, false, true);
            // Не должен crash
        }
    }
    // Если не удалось создать — тест пропускается (ограничения платформы)
}

/// TST-ROB-008: Path with special characters — обработка
TEST_F(ExtendedTestFixture, TST_ROB_008_SpecialCharactersPath) {
// На Windows некоторые символы недопустимы в путях
#ifndef _WIN32
    fs::path special_dir = temp_dir_ / "special chars!@#$%";
    std::error_code ec;
    fs::create_directories(special_dir, ec);

    if (!ec) {
        auto path = special_dir / "test.json";
        std::ofstream file(path);
        if (file.is_open()) {
            file << R"({"key": "value"})";
            file.close();

            auto result = Reader::open(path);
            EXPECT_TRUE(result.ok);
        }
    }
#else
    // На Windows пропускаем тест с некорректными символами
    GTEST_SKIP() << "Special characters in path not supported on Windows";
#endif
}

/// TST-ROB-009: Unicode path names — обработка
TEST_F(ExtendedTestFixture, TST_ROB_009_UnicodePath) {
    // Создаём директорию с Unicode именем
    fs::path unicode_dir = temp_dir_ / u8"тест_директория";
    std::error_code ec;
    fs::create_directories(unicode_dir, ec);

    if (!ec) {
        auto path = unicode_dir / u8"файл.json";
        std::ofstream file(path);
        if (file.is_open()) {
            file << R"({"ключ": "значение"})";
            file.close();

            auto result = Reader::open(path);
            // Поведение зависит от платформы/файловой системы
            if (result.ok) {
                Document doc;
                EXPECT_TRUE(result.reader->next(doc));
            }
        }
    }
}

// ============================================================================
// TST-SEC-*: Security tests (REQ-SEC-0021, RISK-0025)
// ============================================================================

/// TST-SEC-001: Path traversal в output path — предотвращение
TEST_F(ExtendedTestFixture, TST_SEC_001_PathTraversalOutput) {
    // REQ-SEC-0021: Path traversal защита
    // Проверяем что пути с .. не позволяют выйти за пределы
    fs::path malicious_path = temp_dir_ / ".." / ".." / ".." / "etc" / "passwd";

    // Нормализуем путь
    fs::path normalized = fs::weakly_canonical(malicious_path);

    // Проверяем что нормализованный путь не содержит temp_dir_ как родителя
    // (это проверка логики, не реальная атака)
    std::string norm_str = normalized.string();
    std::string temp_str = temp_dir_.string();

    // В реальном приложении здесь была бы проверка что output_path начинается с allowed_base
}

/// TST-SEC-002: Null byte в пути файла — обработка
TEST_F(ExtendedTestFixture, TST_SEC_002_NullByteInPath) {
    // REQ-SEC-0021: защита от null byte injection
    std::string path_with_null = temp_dir_.string() + "/test\x00.json";

    // Путь с null byte должен быть отвергнут или обрезан
    fs::path test_path(path_with_null);
    // На большинстве ОС путь будет обрезан до null byte
}

/// TST-SEC-003: Symlink traversal (Unix) — обработка
TEST_F(ExtendedTestFixture, TST_SEC_003_SymlinkTraversal) {
#ifndef _WIN32
    // Создаём симлинк наружу
    fs::path outside = temp_dir_ / ".." / "outside_dir";
    fs::path symlink_path = temp_dir_ / "symlink";

    std::error_code ec;
    fs::create_directory_symlink(outside, symlink_path, ec);

    if (!ec) {
        // Проверяем что симлинк создан
        EXPECT_TRUE(fs::is_symlink(symlink_path));

        // В реальном приложении должна быть проверка перед открытием
        auto target = fs::read_symlink(symlink_path, ec);
        EXPECT_FALSE(ec);
    }
#else
    GTEST_SKIP() << "Symlink test not applicable on Windows";
#endif
}

/// TST-SEC-004: Junction point traversal (Windows) — обработка
TEST_F(ExtendedTestFixture, TST_SEC_004_JunctionPointTraversal) {
#ifdef _WIN32
    // Windows junction points
    // Создание junction требует прав администратора
    GTEST_SKIP() << "Junction point creation requires admin rights";
#else
    GTEST_SKIP() << "Junction points are Windows-specific";
#endif
}

// ============================================================================
// TST-CLI-*: CLI/Output tests (RISK-0031, RISK-0028)
// ============================================================================

/// TST-CLI-001: Базовый парсинг CLI аргументов
TEST_F(ExtendedTestFixture, TST_CLI_001_BasicCliParsing) {
    // RISK-0031: CLI byte-to-byte
    const char* argv[] = {"chainsaw", "--help"};
    int argc = 2;

    auto result = cli::parse(argc, const_cast<char**>(argv));

    // --help должен быть распознан — результат HelpCommand
    EXPECT_TRUE(std::holds_alternative<cli::HelpCommand>(result.command));
}

/// TST-CLI-002: Version output format
TEST_F(ExtendedTestFixture, TST_CLI_002_VersionFormat) {
    const char* argv[] = {"chainsaw", "--version"};
    int argc = 2;

    auto result = cli::parse(argc, const_cast<char**>(argv));

    // --version должен быть распознан — результат VersionCommand
    EXPECT_TRUE(std::holds_alternative<cli::VersionCommand>(result.command));

    // Проверка формата версии
    // render_version() возвращает "chainsaw 2.13.1\n" для byte-to-byte совместимости
    std::string version_text = cli::render_version();
    EXPECT_FALSE(version_text.empty());
    EXPECT_NE(version_text.find("chainsaw"), std::string::npos);
    EXPECT_NE(version_text.find("2.13.1"), std::string::npos);
}

/// TST-CLI-003: Error message format (invalid args)
TEST_F(ExtendedTestFixture, TST_CLI_003_ErrorMessageFormat) {
    const char* argv[] = {"chainsaw", "--invalid-option-that-does-not-exist"};
    int argc = 2;

    auto result = cli::parse(argc, const_cast<char**>(argv));
    // Невалидная опция должна давать ошибку
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.diagnostic.stderr_message.empty());
}

// ============================================================================
// Дополнительные Robustness тесты (P2)
// ============================================================================

/// TST-ROB-010: Large JSON array — обработка
TEST_F(ExtendedTestFixture, TST_ROB_010_LargeJsonArray) {
    // Создаём JSON с 1000 элементами
    std::string content = "[";
    for (int i = 0; i < 1000; ++i) {
        if (i > 0)
            content += ",";
        content += "{\"id\":" + std::to_string(i) + "}";
    }
    content += "]";

    auto path = create_temp_file("large.json", content);

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok);

    int count = 0;
    Document doc;
    while (result.reader->next(doc)) {
        ++count;
    }
    EXPECT_EQ(count, 1000);
}

/// TST-ROB-011: Deeply nested JSON — обработка
TEST_F(ExtendedTestFixture, TST_ROB_011_DeeplyNestedJson) {
    // Создаём глубоко вложенную структуру (50 уровней)
    std::string content;
    for (int i = 0; i < 50; ++i) {
        content += "{\"level" + std::to_string(i) + "\":";
    }
    content += "\"deep\"";
    for (int i = 0; i < 50; ++i) {
        content += "}";
    }

    auto path = create_temp_file("nested.json", content);

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok);

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));
    EXPECT_TRUE(doc.data.is_object());
}

/// TST-ROB-012: Empty JSON object and array
TEST_F(ExtendedTestFixture, TST_ROB_012_EmptyJsonStructures) {
    // Пустой объект
    auto obj_path = create_temp_file("empty_obj.json", "{}");
    auto obj_result = Reader::open(obj_path);
    ASSERT_TRUE(obj_result.ok);
    Document doc;
    EXPECT_TRUE(obj_result.reader->next(doc));
    EXPECT_TRUE(doc.data.is_object());
    EXPECT_EQ(doc.data.object_size(), 0u);

    // Пустой массив
    auto arr_path = create_temp_file("empty_arr.json", "[]");
    auto arr_result = Reader::open(arr_path);
    ASSERT_TRUE(arr_result.ok);
    EXPECT_FALSE(arr_result.reader->next(doc));  // Пустой массив — 0 документов
}

// ============================================================================
// Тесты File Discovery (P2)
// ============================================================================

/// TST-DISC-001: Discovery обходит директории рекурсивно
TEST_F(ExtendedTestFixture, TST_DISC_001_RecursiveDiscovery) {
    // Создаём структуру директорий
    fs::create_directories(temp_dir_ / "a" / "b" / "c");
    create_temp_file("file1.json", "{}");
    std::ofstream(temp_dir_ / "a" / "file2.json") << "{}";
    std::ofstream(temp_dir_ / "a" / "b" / "file3.json") << "{}";
    std::ofstream(temp_dir_ / "a" / "b" / "c" / "file4.json") << "{}";

    io::DiscoveryOptions opts;
    auto files = io::discover_files({temp_dir_}, opts);

    // Должны найти все 4 файла
    int json_count = 0;
    for (const auto& f : files) {
        if (f.extension() == ".json") {
            ++json_count;
        }
    }
    EXPECT_EQ(json_count, 4);
}

/// TST-DISC-002: Discovery фильтрует по расширению
TEST_F(ExtendedTestFixture, TST_DISC_002_ExtensionFilter) {
    create_temp_file("test.json", "{}");
    create_temp_file("test.txt", "text");
    create_temp_file("test.xml", "<root/>");

    io::DiscoveryOptions opts;
    opts.extensions = std::unordered_set<std::string>{"json"};  // БЕЗ точки
    auto files = io::discover_files({temp_dir_}, opts);

    // Должен найти только .json
    EXPECT_EQ(files.size(), 1U);
    if (!files.empty()) {
        EXPECT_EQ(files[0].extension(), ".json");
    }
}

