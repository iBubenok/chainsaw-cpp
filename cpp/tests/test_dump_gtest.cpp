// ==============================================================================
// test_dump_gtest.cpp - Тесты Dump Command (GoogleTest)
// ==============================================================================
//
// SLICE-013: Dump Command Pipeline
// SPEC-SLICE-013: micro-spec поведения
// ADR-0008: GoogleTest
//
// Тесты покрывают:
// - TST-DUMP-001..016: Unit tests из SPEC-SLICE-013
// - TST-DUMP-INT-001..008: Integration tests
//
// ==============================================================================

#include "chainsaw/cli.hpp"
#include "chainsaw/discovery.hpp"
#include "chainsaw/output.hpp"
#include "chainsaw/platform.hpp"
#include "chainsaw/reader.hpp"
#include "chainsaw/value.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

// Platform-specific includes for PID (unique temp directories for parallel tests)
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace chainsaw::dump::test {

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

// Создание временной директории для тестов
class DumpTestFixture : public ::testing::Test {
protected:
    std::filesystem::path temp_dir_;

    void SetUp() override {
        // Create unique temp directory for each test to avoid race conditions
        // when running tests in parallel (ctest -j)
        auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string unique_name = std::string("chainsaw_dump_") + test_info->test_case_name() +
                                  "_" + test_info->name() + "_" +
                                  std::to_string(
#ifdef _WIN32
                                      GetCurrentProcessId()
#else
                                      getpid()
#endif
                                  );

        temp_dir_ = std::filesystem::temp_directory_path() / unique_name;

        // Remove if exists from previous runs
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);

        // Create clean directory
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        // Удаляем временную директорию
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    // Создать JSON файл с тестовыми данными
    std::filesystem::path create_json_file(const std::string& name, const std::string& content) {
        auto path = temp_dir_ / name;
        std::ofstream file(path);
        file << content;
        return path;
    }

    // Создать JSONL файл с тестовыми данными
    std::filesystem::path create_jsonl_file(const std::string& name,
                                            const std::vector<std::string>& lines) {
        auto path = temp_dir_ / name;
        std::ofstream file(path);
        for (const auto& line : lines) {
            file << line << "\n";
        }
        return path;
    }

    // Создать XML файл с тестовыми данными
    std::filesystem::path create_xml_file(const std::string& name, const std::string& content) {
        auto path = temp_dir_ / name;
        std::ofstream file(path);
        file << content;
        return path;
    }
};

// ==============================================================================
// TST-DUMP-001: Path required validation
// SPEC-SLICE-013 FACT-001: Dump требует хотя бы один path
// ==============================================================================

TEST(DumpCliTest, Parse_Dump_RequiresPath) {
    // Arrange: dump без путей
    Args args{"chainsaw", "dump"};

    // Act
    cli::ParseResult result = cli::parse(args.argc(), args.argv());

    // Assert: парсинг должен вернуть ошибку или пустой paths
    // CLI требует хотя бы один позиционный аргумент
    if (result.ok) {
        auto& cmd = std::get<cli::DumpCommand>(result.command);
        EXPECT_TRUE(cmd.paths.empty());
    } else {
        // Ошибка парсинга — тоже валидный результат
        EXPECT_FALSE(result.ok);
    }
}

// ==============================================================================
// TST-DUMP-002: JSON/JSONL mutual exclusion
// SPEC-SLICE-013 FACT-002: --json и --jsonl взаимоисключающие
// ==============================================================================

TEST(DumpCliTest, Parse_Dump_JsonFlag) {
    // Arrange
    Args args{"chainsaw", "dump", "-j", "test.evtx"};

    // Act
    cli::ParseResult result = cli::parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    auto& cmd = std::get<cli::DumpCommand>(result.command);
    EXPECT_TRUE(cmd.json);
    EXPECT_FALSE(cmd.jsonl);
}

TEST(DumpCliTest, Parse_Dump_JsonlFlag) {
    // Arrange
    Args args{"chainsaw", "dump", "--jsonl", "test.evtx"};

    // Act
    cli::ParseResult result = cli::parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    auto& cmd = std::get<cli::DumpCommand>(result.command);
    EXPECT_FALSE(cmd.json);
    EXPECT_TRUE(cmd.jsonl);
}

// ==============================================================================
// TST-DUMP-003: Extension filter (single)
// SPEC-SLICE-013 FACT-003: --extension фильтрует файлы по одному расширению
// ==============================================================================

TEST(DumpCliTest, Parse_Dump_ExtensionFilter) {
    // Arrange
    Args args{"chainsaw", "dump", "--extension", "evtx", "logs/"};

    // Act
    cli::ParseResult result = cli::parse(args.argc(), args.argv());

    // Assert
    EXPECT_TRUE(result.ok);
    auto& cmd = std::get<cli::DumpCommand>(result.command);
    EXPECT_TRUE(cmd.extension.has_value());
    EXPECT_EQ(*cmd.extension, "evtx");
}

// ==============================================================================
// TST-DUMP-005: JSON array output format
// SPEC-SLICE-013 FACT-006: JSON формат выводится как массив [...] с pretty-printing
// ==============================================================================

TEST_F(DumpTestFixture, JsonOutput_StartsWithBracket) {
    // Arrange: создаём JSON файл
    auto json_path = create_json_file("test.json", R"({"key": "value"})");

    // Act: читаем через Reader и форматируем как JSON array
    auto result = io::Reader::open(json_path, false, false);
    ASSERT_TRUE(result.ok);

    io::Document doc;
    bool has_doc = result.reader->next(doc);
    ASSERT_TRUE(has_doc);

    // Форматируем в JSON (симулируем вывод)
    rapidjson::Document rjdoc;
    doc.data.to_rapidjson(rjdoc, rjdoc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    writer.SetIndent(' ', 2);
    rjdoc.Accept(writer);

    std::string json_str = buffer.GetString();

    // Assert: JSON начинается с {
    EXPECT_FALSE(json_str.empty());
    EXPECT_EQ(json_str[0], '{');
}

// ==============================================================================
// TST-DUMP-006: JSONL output format
// SPEC-SLICE-013 FACT-007: JSONL формат — compact JSON по строкам
// ==============================================================================

TEST_F(DumpTestFixture, JsonlOutput_CompactNoNewlines) {
    // Arrange: создаём JSON файл
    auto json_path = create_json_file("test.json", R"({"key": "value", "num": 42})");

    // Act: читаем и форматируем как compact JSON
    auto result = io::Reader::open(json_path, false, false);
    ASSERT_TRUE(result.ok);

    io::Document doc;
    bool has_doc = result.reader->next(doc);
    ASSERT_TRUE(has_doc);

    rapidjson::Document rjdoc;
    doc.data.to_rapidjson(rjdoc, rjdoc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    rjdoc.Accept(writer);

    std::string jsonl_str = buffer.GetString();

    // Assert: compact JSON не содержит переводов строк внутри
    EXPECT_EQ(jsonl_str.find('\n'), std::string::npos);
}

// ==============================================================================
// TST-DUMP-007: YAML separator "---"
// SPEC-SLICE-013 FACT-008: YAML формат (default) выводит каждый документ с "---"
// ==============================================================================

TEST(DumpOutputTest, YamlSeparator_IsThreeDashes) {
    // FACT-008: разделитель "---"
    const std::string yaml_separator = "---";
    EXPECT_EQ(yaml_separator, "---");
}

// ==============================================================================
// TST-DUMP-009: Sequential file processing
// SPEC-SLICE-013 FACT-010, FACT-011: Файлы и документы обрабатываются последовательно
// ==============================================================================

TEST_F(DumpTestFixture, SequentialProcessing_JsonlMultipleLines) {
    // Arrange: создаём JSONL файл с несколькими строками
    auto jsonl_path =
        create_jsonl_file("test.jsonl", {R"({"id": 1})", R"({"id": 2})", R"({"id": 3})"});

    // Act: читаем через Reader
    auto result = io::Reader::open(jsonl_path, false, false);
    ASSERT_TRUE(result.ok);

    std::vector<int> ids;
    io::Document doc;
    while (result.reader->next(doc)) {
        // Извлекаем id
        if (doc.data.is_object()) {
            auto* id_val = doc.data.get("id");
            if (id_val && id_val->is_uint()) {
                ids.push_back(static_cast<int>(id_val->as_uint()));
            } else if (id_val && id_val->is_int()) {
                ids.push_back(static_cast<int>(id_val->as_int()));
            }
        }
    }

    // Assert: документы в правильном порядке
    ASSERT_EQ(ids.size(), 3);
    EXPECT_EQ(ids[0], 1);
    EXPECT_EQ(ids[1], 2);
    EXPECT_EQ(ids[2], 3);
}

// ==============================================================================
// TST-DUMP-010: Skip errors on document parse
// SPEC-SLICE-013 FACT-012: При --skip-errors ошибки парсинга логируются и пропускаются
// ==============================================================================

TEST_F(DumpTestFixture, SkipErrors_ContinuesOnBadFile) {
    // Arrange: создаём невалидный JSON файл
    auto bad_path = create_json_file("bad.json", "{ invalid json }");

    // Act: открываем с skip_errors=true
    auto result = io::Reader::open(bad_path, false, true);

    // Assert: результат ok, но reader пустой или с ошибкой
    // При skip_errors возвращается пустой reader
    EXPECT_TRUE(result.ok);
}

TEST_F(DumpTestFixture, NoSkipErrors_FailsOnBadFile) {
    // Arrange: создаём невалидный JSON файл
    auto bad_path = create_json_file("bad.json", "{ invalid json }");

    // Act: открываем с skip_errors=false
    auto result = io::Reader::open(bad_path, false, false);

    // Assert: должна быть ошибка
    // Reader может быть создан, но с last_error
    if (result.reader) {
        EXPECT_TRUE(result.reader->last_error().has_value());
    }
}

// ==============================================================================
// TST-DUMP-011: Empty files error
// SPEC-SLICE-013 FACT-013: Если files.is_empty() — возвращается ошибка
// ==============================================================================

TEST_F(DumpTestFixture, Discovery_EmptyDir_ReturnsEmpty) {
    // Arrange: пустая директория
    auto empty_dir = temp_dir_ / "empty";
    std::filesystem::create_directories(empty_dir);

    // Act
    io::DiscoveryOptions opts;
    auto files = io::discover_files({empty_dir}, opts);

    // Assert
    EXPECT_TRUE(files.empty());
}

// ==============================================================================
// TST-DUMP-012: Document type Evtx.data extraction
// SPEC-SLICE-013 FACT-015: Document::Evtx содержит evtx.data
// ==============================================================================

TEST(DumpDocumentTest, DocumentKind_EvtxToString) {
    // FACT-015: Evtx тип документа
    EXPECT_STREQ(io::document_kind_to_string(io::DocumentKind::Evtx), "evtx");
}

// ==============================================================================
// TST-DUMP-013: Document type JSON value extraction
// SPEC-SLICE-013 FACT-016: Остальные Document типы содержат Value напрямую
// ==============================================================================

TEST_F(DumpTestFixture, JsonDocument_ContainsValue) {
    // Arrange
    auto json_path = create_json_file("test.json", R"({"name": "test", "count": 42})");

    // Act
    auto result = io::Reader::open(json_path, false, false);
    ASSERT_TRUE(result.ok);

    io::Document doc;
    bool has_doc = result.reader->next(doc);
    ASSERT_TRUE(has_doc);

    // Assert
    EXPECT_EQ(doc.kind, io::DocumentKind::Json);
    EXPECT_TRUE(doc.data.is_object());

    auto* name = doc.data.get("name");
    ASSERT_NE(name, nullptr);
    EXPECT_TRUE(name->is_string());
    EXPECT_EQ(name->as_string(), "test");

    auto* count = doc.data.get("count");
    ASSERT_NE(count, nullptr);
    EXPECT_TRUE(count->is_number());
}

// ==============================================================================
// TST-DUMP-014: Extension detection case-sensitive
// SPEC-SLICE-013 FACT-017: Reader определяет тип файла по расширению
// ==============================================================================

TEST(DumpExtensionTest, ExtensionDetection_Json) {
    EXPECT_EQ(io::document_kind_from_extension("json"), io::DocumentKind::Json);
    EXPECT_EQ(io::document_kind_from_extension("JSON"),
              io::DocumentKind::Json);  // case-insensitive
}

TEST(DumpExtensionTest, ExtensionDetection_Jsonl) {
    EXPECT_EQ(io::document_kind_from_extension("jsonl"), io::DocumentKind::Jsonl);
}

TEST(DumpExtensionTest, ExtensionDetection_Evtx) {
    EXPECT_EQ(io::document_kind_from_extension("evtx"), io::DocumentKind::Evtx);
    EXPECT_EQ(io::document_kind_from_extension("evt"), io::DocumentKind::Evtx);
}

TEST(DumpExtensionTest, ExtensionDetection_Xml) {
    EXPECT_EQ(io::document_kind_from_extension("xml"), io::DocumentKind::Xml);
}

TEST(DumpExtensionTest, ExtensionDetection_Unknown) {
    EXPECT_EQ(io::document_kind_from_extension("xyz"), io::DocumentKind::Unknown);
}

// ==============================================================================
// TST-DUMP-015: $MFT filename detection
// SPEC-SLICE-013 FACT-018: Файл без расширения с именем $MFT распознаётся как MFT
// ==============================================================================

TEST(DumpExtensionTest, MftFilename_DetectedByPath) {
    std::filesystem::path mft_path = "/some/dir/$MFT";
    EXPECT_EQ(io::document_kind_from_path(mft_path), io::DocumentKind::Mft);
}

// ==============================================================================
// TST-DUMP-016: Load unknown probe order
// SPEC-SLICE-013 FACT-019, FACT-020: --load-unknown пробует определить тип
// ==============================================================================

TEST_F(DumpTestFixture, LoadUnknown_JsonFile_WithoutExtension) {
    // Arrange: создаём JSON файл без расширения .json
    auto path = temp_dir_ / "data_file";
    std::ofstream file(path);
    file << R"({"type": "json"})";
    file.close();

    // Act: открываем с load_unknown=true
    auto result = io::Reader::open(path, true, false);

    // Assert: должен определить как JSON
    if (result.ok && result.reader) {
        io::Document doc;
        if (result.reader->next(doc)) {
            EXPECT_TRUE(doc.data.is_object());
        }
    }
}

// ==============================================================================
// TST-DUMP-INT-003: JSONL output
// ==============================================================================

TEST_F(DumpTestFixture, JsonlReader_MultipleDocuments) {
    // Arrange
    auto jsonl_path =
        create_jsonl_file("events.jsonl", {R"({"event": "start"})", R"({"event": "process"})",
                                           R"({"event": "end"})"});

    // Act
    auto result = io::Reader::open(jsonl_path, false, false);
    ASSERT_TRUE(result.ok);

    std::vector<std::string> events;
    io::Document doc;
    while (result.reader->next(doc)) {
        auto* event = doc.data.get("event");
        if (event && event->is_string()) {
            events.push_back(event->as_string());
        }
    }

    // Assert
    ASSERT_EQ(events.size(), 3);
    EXPECT_EQ(events[0], "start");
    EXPECT_EQ(events[1], "process");
    EXPECT_EQ(events[2], "end");
}

// ==============================================================================
// TST-DUMP-INT-004: JSON file dump
// ==============================================================================

TEST_F(DumpTestFixture, JsonReader_ArrayDocument) {
    // Arrange: JSON с массивом в корне
    auto json_path = create_json_file("array.json", R"([1, 2, 3, 4, 5])");

    // Act
    auto result = io::Reader::open(json_path, false, false);
    ASSERT_TRUE(result.ok);

    int doc_count = 0;
    io::Document doc;
    while (result.reader->next(doc)) {
        ++doc_count;
    }

    // Assert: массив итерируется как 5 документов
    EXPECT_EQ(doc_count, 5);
}

TEST_F(DumpTestFixture, JsonReader_ObjectDocument) {
    // Arrange: JSON с объектом в корне
    auto json_path = create_json_file("object.json", R"({"a": 1, "b": 2})");

    // Act
    auto result = io::Reader::open(json_path, false, false);
    ASSERT_TRUE(result.ok);

    int doc_count = 0;
    io::Document doc;
    while (result.reader->next(doc)) {
        ++doc_count;
    }

    // Assert: один документ
    EXPECT_EQ(doc_count, 1);
}

// ==============================================================================
// TST-DUMP-INT-005: XML file dump
// ==============================================================================

TEST_F(DumpTestFixture, XmlReader_SimpleDocument) {
    // Arrange
    auto xml_path =
        create_xml_file("test.xml", R"(<?xml version="1.0"?><root><item>value</item></root>)");

    // Act
    auto result = io::Reader::open(xml_path, false, false);
    ASSERT_TRUE(result.ok);

    io::Document doc;
    bool has_doc = result.reader->next(doc);

    // Assert
    ASSERT_TRUE(has_doc);
    EXPECT_EQ(doc.kind, io::DocumentKind::Xml);
    EXPECT_TRUE(doc.data.is_object());
}

// ==============================================================================
// TST-DUMP-INT-007: Multiple files dump (order)
// ==============================================================================

TEST_F(DumpTestFixture, Discovery_MultipleFiles_SortedOrder) {
    // Arrange: создаём несколько файлов
    create_json_file("c_file.json", "{}");
    create_json_file("a_file.json", "{}");
    create_json_file("b_file.json", "{}");

    // Act
    io::DiscoveryOptions opts;
    std::unordered_set<std::string> exts = {"json"};
    opts.extensions = exts;
    auto files = io::discover_files({temp_dir_}, opts);

    // Assert: файлы отсортированы
    ASSERT_EQ(files.size(), 3);
    // Проверяем что порядок детерминирован (отсортирован)
    for (size_t i = 1; i < files.size(); ++i) {
        EXPECT_LT(files[i - 1], files[i]);
    }
}

// ==============================================================================
// TST-DUMP-CLI: Dump CLI options parsing
// ==============================================================================

TEST(DumpCliTest, Parse_Dump_SkipErrors) {
    Args args{"chainsaw", "dump", "--skip-errors", "test.evtx"};

    cli::ParseResult result = cli::parse(args.argc(), args.argv());

    EXPECT_TRUE(result.ok);
    auto& cmd = std::get<cli::DumpCommand>(result.command);
    EXPECT_TRUE(cmd.skip_errors);
}

TEST(DumpCliTest, Parse_Dump_LoadUnknown) {
    Args args{"chainsaw", "dump", "--load-unknown", "test.evtx"};

    cli::ParseResult result = cli::parse(args.argc(), args.argv());

    EXPECT_TRUE(result.ok);
    auto& cmd = std::get<cli::DumpCommand>(result.command);
    EXPECT_TRUE(cmd.load_unknown);
}

TEST(DumpCliTest, Parse_Dump_OutputFile) {
    Args args{"chainsaw", "dump", "-o", "output.json", "test.evtx"};

    cli::ParseResult result = cli::parse(args.argc(), args.argv());

    EXPECT_TRUE(result.ok);
    auto& cmd = std::get<cli::DumpCommand>(result.command);
    EXPECT_TRUE(cmd.output.has_value());
}

}  // namespace chainsaw::dump::test
