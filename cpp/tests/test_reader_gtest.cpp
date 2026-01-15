// ==============================================================================
// test_reader_gtest.cpp - Тесты Reader Framework (SLICE-005)
// ==============================================================================
//
// MOD-0006 io::reader
// MOD-0007 formats (JSON/JSONL)
// SLICE-005: Reader Framework + JSON Parser
// SPEC-SLICE-005: unit-тесты по micro-spec
//
// Тесты из SPEC-SLICE-005:
// - TST-RDR-001..009: Reader API
// - TST-JSON-001..004: JSON parsing
// - TST-JSONL-001..003: JSONL parsing
// - TST-VALUE-001..004: Value conversion
//
// ==============================================================================

#include <algorithm>
#include <chainsaw/platform.hpp>
#include <chainsaw/reader.hpp>
#include <chainsaw/value.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace chainsaw;
using namespace chainsaw::io;

// ============================================================================
// Test Fixtures
// ============================================================================

/// Базовый fixture для тестов с временными файлами
class ReaderTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // Создаём уникальную временную директорию для каждого теста
        // Используем имя теста + PID для избежания race condition при параллельном запуске
        auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string unique_name = std::string("chainsaw_reader_") + test_info->test_case_name() +
                                  "_" + test_info->name() + "_" +
                                  std::to_string(
#ifdef _WIN32
                                      GetCurrentProcessId()
#else
                                      getpid()
#endif
                                  );
        temp_dir_ = fs::temp_directory_path() / unique_name;

        // Удаляем если существует от предыдущих запусков
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);

        fs::create_directories(temp_dir_);
    }

    void TearDown() override {
        // Удаляем временные файлы
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

    fs::path temp_dir_;
};

// ============================================================================
// TST-VALUE-*: Value conversion tests
// ============================================================================

/// TST-VALUE-001: Value::from(Json) — числа UInt
TEST(ValueTest, TST_VALUE_001_UIntConversion) {
    // SPEC-SLICE-005 FACT-027: Number → UInt приоритет
    rapidjson::Document doc;
    doc.Parse("42");
    ASSERT_FALSE(doc.HasParseError());

    Value v = Value::from_rapidjson(doc);
    EXPECT_TRUE(v.is_uint());
    EXPECT_EQ(v.as_uint(), 42u);
}

/// TST-VALUE-001b: Большие UInt64
TEST(ValueTest, TST_VALUE_001b_LargeUInt) {
    rapidjson::Document doc;
    doc.Parse("18446744073709551615");  // UINT64_MAX
    ASSERT_FALSE(doc.HasParseError());

    Value v = Value::from_rapidjson(doc);
    EXPECT_TRUE(v.is_uint());
    EXPECT_EQ(v.as_uint(), 18446744073709551615ULL);
}

/// TST-VALUE-002: Value::from(Json) — числа Int (отрицательные)
TEST(ValueTest, TST_VALUE_002_IntConversion) {
    rapidjson::Document doc;
    doc.Parse("-42");
    ASSERT_FALSE(doc.HasParseError());

    Value v = Value::from_rapidjson(doc);
    EXPECT_TRUE(v.is_int());
    EXPECT_EQ(v.as_int(), -42);
}

/// TST-VALUE-003: Value::from(Json) — Float
TEST(ValueTest, TST_VALUE_003_FloatConversion) {
    rapidjson::Document doc;
    doc.Parse("3.14159");
    ASSERT_FALSE(doc.HasParseError());

    Value v = Value::from_rapidjson(doc);
    EXPECT_TRUE(v.is_double());
    EXPECT_DOUBLE_EQ(v.as_double(), 3.14159);
}

/// TST-VALUE-004: Value to Json round-trip
TEST(ValueTest, TST_VALUE_004_RoundTrip) {
    // Создаём сложную структуру
    Value::Object obj;
    obj["name"] = Value("test");
    obj["count"] = Value(static_cast<std::int64_t>(42));
    obj["enabled"] = Value(true);
    obj["score"] = Value(3.14);
    obj["tags"] = Value(Value::Array{Value("a"), Value("b")});

    Value original(std::move(obj));

    // Конвертируем в RapidJSON и обратно
    auto doc = original.to_rapidjson_document();
    Value restored = Value::from_rapidjson(doc);

    // Проверяем структуру
    ASSERT_TRUE(restored.is_object());
    EXPECT_TRUE(restored.has("name"));
    EXPECT_TRUE(restored.has("count"));
    EXPECT_TRUE(restored.has("enabled"));
    EXPECT_TRUE(restored.has("score"));
    EXPECT_TRUE(restored.has("tags"));

    EXPECT_EQ(restored.get("name")->as_string(), "test");
    // Примечание: положительные числа конвертируются в UInt при round-trip
    EXPECT_TRUE(restored.get("count")->is_number());
    EXPECT_TRUE(restored.get("enabled")->as_bool());
    EXPECT_DOUBLE_EQ(restored.get("score")->as_double(), 3.14);

    const auto* tags = restored.get("tags");
    ASSERT_NE(tags, nullptr);
    ASSERT_TRUE(tags->is_array());
    EXPECT_EQ(tags->array_size(), 2u);
}

/// Дополнительный тест Value типов
TEST(ValueTest, AllTypes) {
    // Null
    Value null_val;
    EXPECT_TRUE(null_val.is_null());

    // Bool
    Value bool_val(true);
    EXPECT_TRUE(bool_val.is_bool());
    EXPECT_TRUE(bool_val.as_bool());

    // Int64
    Value int_val(static_cast<std::int64_t>(-123));
    EXPECT_TRUE(int_val.is_int());
    EXPECT_EQ(int_val.as_int(), -123);

    // UInt64
    Value uint_val(static_cast<std::uint64_t>(456));
    EXPECT_TRUE(uint_val.is_uint());
    EXPECT_EQ(uint_val.as_uint(), 456u);

    // Double
    Value double_val(2.718);
    EXPECT_TRUE(double_val.is_double());
    EXPECT_DOUBLE_EQ(double_val.as_double(), 2.718);

    // String
    Value str_val(std::string("hello"));
    EXPECT_TRUE(str_val.is_string());
    EXPECT_EQ(str_val.as_string(), "hello");

    // Array
    Value arr_val(
        Value::Array{Value(static_cast<std::int64_t>(1)), Value(static_cast<std::int64_t>(2))});
    EXPECT_TRUE(arr_val.is_array());
    EXPECT_EQ(arr_val.array_size(), 2u);

    // Object
    Value::Object obj;
    obj["key"] = Value("value");
    Value obj_val(std::move(obj));
    EXPECT_TRUE(obj_val.is_object());
    EXPECT_EQ(obj_val.object_size(), 1u);
}

// ============================================================================
// TST-RDR-*: Reader API tests
// ============================================================================

/// TST-RDR-001: DocumentKind enum соответствует upstream
TEST(ReaderTest, TST_RDR_001_DocumentKindEnum) {
    // SPEC-SLICE-005 FACT-001, FACT-003
    EXPECT_STREQ(document_kind_to_string(DocumentKind::Evtx), "evtx");
    EXPECT_STREQ(document_kind_to_string(DocumentKind::Hve), "hve");
    EXPECT_STREQ(document_kind_to_string(DocumentKind::Json), "json");
    EXPECT_STREQ(document_kind_to_string(DocumentKind::Jsonl), "jsonl");
    EXPECT_STREQ(document_kind_to_string(DocumentKind::Mft), "mft");
    EXPECT_STREQ(document_kind_to_string(DocumentKind::Xml), "xml");
    EXPECT_STREQ(document_kind_to_string(DocumentKind::Esedb), "esedb");
    EXPECT_STREQ(document_kind_to_string(DocumentKind::Unknown), "unknown");
}

/// TST-RDR-002: Kind::extensions() возвращает правильные расширения
TEST(ReaderTest, TST_RDR_002_KindExtensions) {
    // SPEC-SLICE-005 FACT-005: расширения БЕЗ точки
    auto evtx_ext = document_kind_extensions(DocumentKind::Evtx);
    EXPECT_EQ(evtx_ext.size(), 2u);
    EXPECT_NE(std::find(evtx_ext.begin(), evtx_ext.end(), "evt"), evtx_ext.end());
    EXPECT_NE(std::find(evtx_ext.begin(), evtx_ext.end(), "evtx"), evtx_ext.end());

    auto json_ext = document_kind_extensions(DocumentKind::Json);
    EXPECT_EQ(json_ext.size(), 1u);
    EXPECT_EQ(json_ext[0], "json");

    auto jsonl_ext = document_kind_extensions(DocumentKind::Jsonl);
    EXPECT_EQ(jsonl_ext.size(), 1u);
    EXPECT_EQ(jsonl_ext[0], "jsonl");

    // FACT-006: $MFT — специальный случай
    auto mft_ext = document_kind_extensions(DocumentKind::Mft);
    EXPECT_NE(std::find(mft_ext.begin(), mft_ext.end(), "$MFT"), mft_ext.end());
}

/// TST-RDR-003: Reader::open() выбирает JSON по расширению
TEST_F(ReaderTestFixture, TST_RDR_003_JsonByExtension) {
    // SPEC-SLICE-005 FACT-007: extension-first
    auto path = create_temp_file("test.json", R"({"key": "value"})");

    auto result = Reader::open(path, false, false);
    ASSERT_TRUE(result.ok) << result.error.format();
    ASSERT_NE(result.reader, nullptr);
    EXPECT_EQ(result.reader->kind(), DocumentKind::Json);
}

/// TST-RDR-004: Reader::open() выбирает JSONL по расширению
TEST_F(ReaderTestFixture, TST_RDR_004_JsonlByExtension) {
    auto path = create_temp_file("test.jsonl", R"({"line": 1}
{"line": 2})");

    auto result = Reader::open(path, false, false);
    ASSERT_TRUE(result.ok) << result.error.format();
    ASSERT_NE(result.reader, nullptr);
    EXPECT_EQ(result.reader->kind(), DocumentKind::Jsonl);
}

/// TST-RDR-005: Reader::kind() возвращает правильный Kind
TEST_F(ReaderTestFixture, TST_RDR_005_ReaderKind) {
    auto json_path = create_temp_file("test.json", "{}");
    auto jsonl_path = create_temp_file("test.jsonl", "{}");

    auto json_result = Reader::open(json_path);
    auto jsonl_result = Reader::open(jsonl_path);

    ASSERT_TRUE(json_result.ok);
    ASSERT_TRUE(jsonl_result.ok);

    EXPECT_EQ(json_result.reader->kind(), DocumentKind::Json);
    EXPECT_EQ(jsonl_result.reader->kind(), DocumentKind::Jsonl);
}

/// TST-RDR-006: skip_errors=true → пустой Reader
TEST_F(ReaderTestFixture, TST_RDR_006_SkipErrors) {
    // SPEC-SLICE-005 FACT-009
    auto path = create_temp_file("invalid.json", "not valid json {{{");

    auto result = Reader::open(path, false, true);  // skip_errors=true
    EXPECT_TRUE(result.ok);
    ASSERT_NE(result.reader, nullptr);

    Document doc;
    EXPECT_FALSE(result.reader->next(doc));  // Пустой итератор
}

/// TST-RDR-007: skip_errors=false → ошибка
TEST_F(ReaderTestFixture, TST_RDR_007_NoSkipErrors) {
    auto path = create_temp_file("invalid.json", "not valid json");

    auto result = Reader::open(path, false, false);  // skip_errors=false
    // Reader создаётся, но с ошибкой
    ASSERT_NE(result.reader, nullptr);
    EXPECT_TRUE(result.reader->last_error().has_value());
}

/// TST-RDR-008: load_unknown=true fallback (JSON)
TEST_F(ReaderTestFixture, TST_RDR_008_LoadUnknownFallback) {
    // SPEC-SLICE-005 FACT-010: fallback порядок
    auto path = create_temp_file("noext", R"({"key": "value"})");

    auto result = Reader::open(path, true, false);  // load_unknown=true
    ASSERT_TRUE(result.ok) << result.error.format();
    ASSERT_NE(result.reader, nullptr);

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));
    EXPECT_TRUE(doc.data.is_object());
}

/// TST-RDR-009: $MFT edge case (только проверка распознавания)
TEST(ReaderTest, TST_RDR_009_MftEdgeCase) {
    // SPEC-SLICE-005 FACT-012
    fs::path mft_path("$MFT");
    EXPECT_EQ(document_kind_from_path(mft_path), DocumentKind::Mft);

    fs::path mft_path2("/some/path/$MFT");
    EXPECT_EQ(document_kind_from_path(mft_path2), DocumentKind::Mft);
}

// ============================================================================
// TST-JSON-*: JSON parsing tests
// ============================================================================

/// TST-JSON-001: JSON массив → несколько документов
TEST_F(ReaderTestFixture, TST_JSON_001_ArrayMultipleDocs) {
    // SPEC-SLICE-005 FACT-018
    auto path = create_temp_file("array.json", R"([{"id": 1}, {"id": 2}, {"id": 3}])");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok);

    std::vector<Document> docs;
    Document doc;
    while (result.reader->next(doc)) {
        docs.push_back(std::move(doc));
    }

    EXPECT_EQ(docs.size(), 3u);
    for (size_t i = 0; i < docs.size(); ++i) {
        EXPECT_TRUE(docs[i].data.is_object());
        EXPECT_EQ(docs[i].record_id, i);  // record_id = array index
    }
}

/// TST-JSON-002: JSON объект → один документ
TEST_F(ReaderTestFixture, TST_JSON_002_ObjectSingleDoc) {
    // SPEC-SLICE-005 FACT-019
    auto path = create_temp_file("object.json", R"({"name": "test", "value": 42})");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok);

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));
    EXPECT_TRUE(doc.data.is_object());
    EXPECT_EQ(doc.data.get("name")->as_string(), "test");

    // Второй вызов должен вернуть false
    EXPECT_FALSE(result.reader->next(doc));
}

/// TST-JSON-003: JSON примитив → один документ
TEST_F(ReaderTestFixture, TST_JSON_003_PrimitiveSingleDoc) {
    auto path = create_temp_file("string.json", R"("hello world")");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok);

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));
    EXPECT_TRUE(doc.data.is_string());
    EXPECT_EQ(doc.data.as_string(), "hello world");

    EXPECT_FALSE(result.reader->next(doc));
}

/// TST-JSON-004: Невалидный JSON → ошибка
TEST_F(ReaderTestFixture, TST_JSON_004_InvalidJson) {
    // SPEC-SLICE-005 FACT-017
    auto path = create_temp_file("invalid.json", "{key: value}");  // Невалидный JSON

    auto result = Reader::open(path, false, false);
    ASSERT_NE(result.reader, nullptr);
    EXPECT_TRUE(result.reader->last_error().has_value());
    EXPECT_EQ(result.reader->last_error()->kind, ReaderErrorKind::ParseError);
}

// ============================================================================
// TST-JSONL-*: JSONL parsing tests
// ============================================================================

/// TST-JSONL-001: JSONL — построчная итерация
TEST_F(ReaderTestFixture, TST_JSONL_001_LineByLineIteration) {
    // SPEC-SLICE-005 FACT-023
    auto path = create_temp_file("lines.jsonl", R"({"line": 1}
{"line": 2}
{"line": 3})");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok);

    std::vector<Document> docs;
    Document doc;
    while (result.reader->next(doc)) {
        docs.push_back(std::move(doc));
    }

    EXPECT_EQ(docs.size(), 3u);
    for (size_t i = 0; i < docs.size(); ++i) {
        EXPECT_EQ(docs[i].kind, DocumentKind::Jsonl);
        EXPECT_TRUE(docs[i].data.is_object());
        EXPECT_EQ(docs[i].record_id, i + 1);  // Номер строки (1-based)
    }
}

/// TST-JSONL-002: JSONL первая строка невалидна → ошибка load
TEST_F(ReaderTestFixture, TST_JSONL_002_InvalidFirstLine) {
    // SPEC-SLICE-005 FACT-022
    auto path = create_temp_file("invalid_first.jsonl", R"(not json
{"line": 2})");

    auto result = Reader::open(path, false, false);
    ASSERT_NE(result.reader, nullptr);
    EXPECT_TRUE(result.reader->last_error().has_value());
}

/// TST-JSONL-003: JSONL строка посередине невалидна → ошибка next
TEST_F(ReaderTestFixture, TST_JSONL_003_InvalidMiddleLine) {
    // SPEC-SLICE-005 FACT-024
    auto path = create_temp_file("invalid_middle.jsonl", R"({"line": 1}
not json here
{"line": 3})");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok);

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));   // Первая строка OK
    EXPECT_FALSE(result.reader->next(doc));  // Вторая строка — ошибка
    EXPECT_TRUE(result.reader->last_error().has_value());
}

/// Дополнительный тест: JSONL с пустыми строками
TEST_F(ReaderTestFixture, TST_JSONL_EmptyLines) {
    auto path = create_temp_file("empty_lines.jsonl", R"({"line": 1}

{"line": 2}

{"line": 3})");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok);

    std::vector<Document> docs;
    Document doc;
    while (result.reader->next(doc)) {
        docs.push_back(std::move(doc));
    }

    // Пустые строки пропускаются
    EXPECT_EQ(docs.size(), 3u);
}

// ============================================================================
// Дополнительные тесты
// ============================================================================

/// Тест Document metadata
TEST_F(ReaderTestFixture, DocumentMetadata) {
    auto path = create_temp_file("meta.json", R"({"key": "value"})");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok);

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));

    EXPECT_EQ(doc.kind, DocumentKind::Json);
    EXPECT_FALSE(doc.source.empty());
    // source содержит путь к файлу
    EXPECT_NE(doc.source.find("meta.json"), std::string::npos);
}

/// Тест файл не найден
TEST_F(ReaderTestFixture, FileNotFound) {
    fs::path nonexistent = temp_dir_ / "nonexistent.json";

    auto result = Reader::open(nonexistent, false, false);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error.kind, ReaderErrorKind::FileNotFound);
}

/// Тест extension case-insensitive
TEST_F(ReaderTestFixture, ExtensionCaseInsensitive) {
    auto path1 = create_temp_file("test.JSON", "{}");
    auto path2 = create_temp_file("test.Json", "{}");
    auto path3 = create_temp_file("test.JSONL", "{}");

    auto result1 = Reader::open(path1);
    auto result2 = Reader::open(path2);
    auto result3 = Reader::open(path3);

    EXPECT_TRUE(result1.ok);
    EXPECT_TRUE(result2.ok);
    EXPECT_TRUE(result3.ok);

    EXPECT_EQ(result1.reader->kind(), DocumentKind::Json);
    EXPECT_EQ(result2.reader->kind(), DocumentKind::Json);
    EXPECT_EQ(result3.reader->kind(), DocumentKind::Jsonl);
}

/// Тест сложной JSON структуры
TEST_F(ReaderTestFixture, ComplexJsonStructure) {
    auto path = create_temp_file("complex.json", R"({
        "string": "hello",
        "number": 42,
        "float": 3.14,
        "bool": true,
        "null": null,
        "array": [1, 2, 3],
        "nested": {
            "deep": {
                "value": "found"
            }
        }
    })");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok);

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));

    EXPECT_TRUE(doc.data.is_object());
    EXPECT_EQ(doc.data.get("string")->as_string(), "hello");
    EXPECT_TRUE(doc.data.get("number")->is_number());
    EXPECT_TRUE(doc.data.get("float")->is_double());
    EXPECT_TRUE(doc.data.get("bool")->as_bool());
    EXPECT_TRUE(doc.data.get("null")->is_null());
    EXPECT_TRUE(doc.data.get("array")->is_array());
    EXPECT_TRUE(doc.data.get("nested")->is_object());

    // Глубокий доступ
    auto* nested = doc.data.get("nested");
    auto* deep = nested->get("deep");
    EXPECT_EQ(deep->get("value")->as_string(), "found");
}

/// Тест Error format
TEST(ReaderTest, ErrorFormat) {
    // SPEC-SLICE-005 FACT-008
    ReaderError err{ReaderErrorKind::ParseError, "test error", "/path/to/file.json"};

    std::string formatted = err.format();
    EXPECT_EQ(formatted, "[!] failed to load file '/path/to/file.json' - test error\n");
}

// ============================================================================
// TST-XML-*: XML parsing tests (SLICE-006)
// ============================================================================

/// TST-XML-001: Простой XML элемент → JSON объект
TEST_F(ReaderTestFixture, TST_XML_001_SimpleElement) {
    // SPEC-SLICE-006 FACT-001, FACT-002
    auto path = create_temp_file("simple.xml", R"(<?xml version="1.0"?>
<root>
    <item>value</item>
    <name>test</name>
</root>)");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();
    ASSERT_NE(result.reader, nullptr);
    EXPECT_EQ(result.reader->kind(), DocumentKind::Xml);

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));
    EXPECT_TRUE(doc.data.is_object());

    // Проверяем структуру: корень содержит объект "root"
    auto* root = doc.data.get("root");
    ASSERT_NE(root, nullptr);
    ASSERT_TRUE(root->is_object());

    // Проверяем дочерние элементы
    auto* item = root->get("item");
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->as_string(), "value");

    auto* name = root->get("name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->as_string(), "test");
}

/// TST-XML-002: XML с атрибутами → JSON с @-полями
TEST_F(ReaderTestFixture, TST_XML_002_Attributes) {
    // SPEC-SLICE-006: атрибуты конвертируются с префиксом '@'
    auto path = create_temp_file("attrs.xml", R"(<?xml version="1.0"?>
<root id="123" enabled="true">
    <item name="first"/>
</root>)");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));

    auto* root = doc.data.get("root");
    ASSERT_NE(root, nullptr);

    // Атрибуты с префиксом '@'
    auto* id = root->get("@id");
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(id->as_string(), "123");

    auto* enabled = root->get("@enabled");
    ASSERT_NE(enabled, nullptr);
    EXPECT_EQ(enabled->as_string(), "true");

    // Дочерний элемент с атрибутом
    auto* item = root->get("item");
    ASSERT_NE(item, nullptr);
    ASSERT_TRUE(item->is_object());

    auto* item_name = item->get("@name");
    ASSERT_NE(item_name, nullptr);
    EXPECT_EQ(item_name->as_string(), "first");
}

/// TST-XML-003: XML с вложенными элементами → вложенные объекты
TEST_F(ReaderTestFixture, TST_XML_003_NestedElements) {
    auto path = create_temp_file("nested.xml", R"(<?xml version="1.0"?>
<root>
    <level1>
        <level2>
            <level3>deep</level3>
        </level2>
    </level1>
</root>)");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));

    // Навигация: root -> level1 -> level2 -> level3
    auto* root = doc.data.get("root");
    ASSERT_NE(root, nullptr);

    auto* level1 = root->get("level1");
    ASSERT_NE(level1, nullptr);

    auto* level2 = level1->get("level2");
    ASSERT_NE(level2, nullptr);

    auto* level3 = level2->get("level3");
    ASSERT_NE(level3, nullptr);
    EXPECT_EQ(level3->as_string(), "deep");
}

/// TST-XML-004: XML с повторяющимися элементами → JSON массив
TEST_F(ReaderTestFixture, TST_XML_004_RepeatedElements) {
    auto path = create_temp_file("array.xml", R"(<?xml version="1.0"?>
<items>
    <item>alpha</item>
    <item>beta</item>
    <item>gamma</item>
</items>)");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));

    auto* items = doc.data.get("items");
    ASSERT_NE(items, nullptr);

    // Повторяющиеся элементы конвертируются в массив
    auto* item_array = items->get("item");
    ASSERT_NE(item_array, nullptr);
    ASSERT_TRUE(item_array->is_array());
    EXPECT_EQ(item_array->array_size(), 3u);

    EXPECT_EQ(item_array->at(0)->as_string(), "alpha");
    EXPECT_EQ(item_array->at(1)->as_string(), "beta");
    EXPECT_EQ(item_array->at(2)->as_string(), "gamma");
}

/// TST-XML-005: XML текстовое содержимое → $text поле
TEST_F(ReaderTestFixture, TST_XML_005_TextContent) {
    // Текст без дочерних элементов → просто строка
    auto path = create_temp_file("text.xml", R"(<?xml version="1.0"?>
<message>This is text content</message>)");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));

    auto* message = doc.data.get("message");
    ASSERT_NE(message, nullptr);
    // Элемент без атрибутов и без дочерних элементов → строка
    EXPECT_EQ(message->as_string(), "This is text content");
}

/// TST-XML-006: Невалидный XML → ошибка load
TEST_F(ReaderTestFixture, TST_XML_006_InvalidXml) {
    // SPEC-SLICE-006 FACT-007: ошибки парсинга в load()
    auto path = create_temp_file("invalid.xml", R"(<root><unclosed>)");

    auto result = Reader::open(path, false, false);
    ASSERT_NE(result.reader, nullptr);
    EXPECT_TRUE(result.reader->last_error().has_value());
    EXPECT_EQ(result.reader->last_error()->kind, ReaderErrorKind::ParseError);
}

/// TST-XML-007: Пустой XML файл → ошибка или пустой документ
TEST_F(ReaderTestFixture, TST_XML_007_EmptyXml) {
    // Пустой файл — ошибка парсинга (нет корневого элемента)
    auto path = create_temp_file("empty.xml", "");

    auto result = Reader::open(path, false, false);
    ASSERT_NE(result.reader, nullptr);
    // Пустой файл = ошибка парсинга
    EXPECT_TRUE(result.reader->last_error().has_value());
}

/// TST-XML-007b: XML с пустым корневым элементом
TEST_F(ReaderTestFixture, TST_XML_007b_EmptyRootElement) {
    auto path = create_temp_file("empty_root.xml", R"(<?xml version="1.0"?><root/>)");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));

    auto* root = doc.data.get("root");
    ASSERT_NE(root, nullptr);
    // Пустой элемент → пустая строка
    EXPECT_EQ(root->as_string(), "");
}

/// TST-XML-008: Reader::open() выбирает XML по расширению
TEST_F(ReaderTestFixture, TST_XML_008_XmlByExtension) {
    // SPEC-SLICE-006 FACT-013
    auto path = create_temp_file("test.xml", R"(<?xml version="1.0"?><root/>)");

    auto result = Reader::open(path, false, false);
    ASSERT_TRUE(result.ok) << result.error.format();
    ASSERT_NE(result.reader, nullptr);
    EXPECT_EQ(result.reader->kind(), DocumentKind::Xml);
}

/// TST-XML-009: Reader::kind() возвращает Xml
TEST_F(ReaderTestFixture, TST_XML_009_ReaderKind) {
    // SPEC-SLICE-006 FACT-018
    auto path = create_temp_file("kind.xml", R"(<?xml version="1.0"?><root/>)");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.reader->kind(), DocumentKind::Xml);
}

/// TST-XML-010: XML результат → один документ (no array splitting)
TEST_F(ReaderTestFixture, TST_XML_010_SingleDocument) {
    // SPEC-SLICE-006 FACT-009: не массив → один документ
    auto path =
        create_temp_file("single.xml", R"(<?xml version="1.0"?><root><a>1</a><b>2</b></root>)");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok);

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));
    // Второй вызов должен вернуть false
    EXPECT_FALSE(result.reader->next(doc));
}

/// TST-XML-011: XML результат-объект → один документ (take-семантика)
TEST_F(ReaderTestFixture, TST_XML_011_TakeSemantics) {
    // SPEC-SLICE-006 FACT-010: take-семантика
    auto path = create_temp_file("take.xml", R"(<?xml version="1.0"?><root/>)");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok);

    Document doc1;
    EXPECT_TRUE(result.reader->next(doc1));
    EXPECT_TRUE(doc1.data.is_object());

    Document doc2;
    EXPECT_FALSE(result.reader->next(doc2));  // Уже взят
}

/// TST-XML-012: skip_errors=true → пустой Reader при ошибке
TEST_F(ReaderTestFixture, TST_XML_012_SkipErrors) {
    // SPEC-SLICE-006 FACT-014, FACT-015
    auto path = create_temp_file("invalid_skip.xml", "<root><broken");

    auto result = Reader::open(path, false, true);  // skip_errors=true
    EXPECT_TRUE(result.ok);
    ASSERT_NE(result.reader, nullptr);

    Document doc;
    EXPECT_FALSE(result.reader->next(doc));  // Пустой итератор
}

/// TST-XML-013: load_unknown=true fallback (XML на позиции 4)
TEST_F(ReaderTestFixture, TST_XML_013_LoadUnknownFallback) {
    // SPEC-SLICE-006 FACT-016: XML в fallback после JSON
    // Файл без расширения, но с XML содержимым
    auto path = create_temp_file("noext", R"(<?xml version="1.0"?><root><key>value</key></root>)");

    auto result = Reader::open(path, true, false);  // load_unknown=true
    ASSERT_TRUE(result.ok) << result.error.format();
    ASSERT_NE(result.reader, nullptr);

    // Должен распознаться как XML через fallback
    EXPECT_EQ(result.reader->kind(), DocumentKind::Xml);

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));
    EXPECT_TRUE(doc.data.is_object());
}

/// Дополнительный тест: Complex XML (Windows Event)
TEST_F(ReaderTestFixture, TST_XML_ComplexEvent) {
    auto path = create_temp_file("event.xml", R"(<?xml version="1.0" encoding="UTF-8"?>
<Event xmlns="http://schemas.microsoft.com/win/2004/08/events/event">
    <System>
        <Provider Name="Security" Guid="{12345678}"/>
        <EventID>4624</EventID>
        <Computer>WORKSTATION01</Computer>
    </System>
    <EventData>
        <Data Name="SubjectUserSid">S-1-5-18</Data>
        <Data Name="TargetUserName">Admin</Data>
    </EventData>
</Event>)");

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();

    Document doc;
    EXPECT_TRUE(result.reader->next(doc));
    EXPECT_EQ(doc.kind, DocumentKind::Xml);

    auto* event = doc.data.get("Event");
    ASSERT_NE(event, nullptr);
    ASSERT_TRUE(event->is_object());

    // Проверяем namespace атрибут
    auto* xmlns = event->get("@xmlns");
    ASSERT_NE(xmlns, nullptr);
    EXPECT_NE(xmlns->as_string().find("microsoft.com"), std::string::npos);

    // Проверяем System
    auto* system = event->get("System");
    ASSERT_NE(system, nullptr);

    auto* provider = system->get("Provider");
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->get("@Name")->as_string(), "Security");

    auto* eventId = system->get("EventID");
    ASSERT_NE(eventId, nullptr);
    EXPECT_EQ(eventId->as_string(), "4624");
}

/// Дополнительный тест: XML extension case-insensitive
TEST_F(ReaderTestFixture, TST_XML_ExtensionCaseInsensitive) {
    auto path1 = create_temp_file("test.XML", R"(<?xml version="1.0"?><root/>)");
    auto path2 = create_temp_file("test.Xml", R"(<?xml version="1.0"?><root/>)");

    auto result1 = Reader::open(path1);
    auto result2 = Reader::open(path2);

    EXPECT_TRUE(result1.ok);
    EXPECT_TRUE(result2.ok);
    EXPECT_EQ(result1.reader->kind(), DocumentKind::Xml);
    EXPECT_EQ(result2.reader->kind(), DocumentKind::Xml);
}

// ============================================================================
// TST-EVTX-*: EVTX Parser tests (SLICE-007)
// ============================================================================
//
// SPEC-SLICE-007: EVTX Parser micro-spec
// Тесты основаны на security_sample.evtx фикстуре
//

/// Получить путь к EVTX фикстуре
static fs::path get_evtx_fixture_path() {
    // Ищем фикстуру относительно текущей директории или build директории
    std::vector<fs::path> search_paths = {
        fs::current_path() / "tests" / "fixtures" / "evtx" / "security_sample.evtx",
        fs::current_path() / ".." / "tests" / "fixtures" / "evtx" / "security_sample.evtx",
        fs::current_path() / ".." / ".." / "cpp" / "tests" / "fixtures" / "evtx" /
            "security_sample.evtx",
        fs::path(CMAKE_SOURCE_DIR) / "tests" / "fixtures" / "evtx" / "security_sample.evtx",
    };

    for (const auto& path : search_paths) {
        if (fs::exists(path)) {
            return fs::canonical(path);
        }
    }

    // Если не нашли, возвращаем первый путь (тест упадёт с понятным сообщением)
    return search_paths[0];
}

/// TST-EVTX-001: Reader::open() выбирает EVTX по расширению .evtx
TEST_F(ReaderTestFixture, TST_EVTX_001_OpenByExtension) {
    // SPEC-SLICE-007 FACT-007: расширения .evt, .evtx
    auto path = get_evtx_fixture_path();
    if (!fs::exists(path)) {
        GTEST_SKIP() << "EVTX fixture not found: " << path;
    }

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();
    ASSERT_NE(result.reader, nullptr);
    EXPECT_EQ(result.reader->kind(), DocumentKind::Evtx);
}

/// TST-EVTX-002: EVTX записи возвращаются с record_id
TEST_F(ReaderTestFixture, TST_EVTX_002_RecordId) {
    // SPEC-SLICE-007 FACT-002: каждая запись имеет record_id
    auto path = get_evtx_fixture_path();
    if (!fs::exists(path)) {
        GTEST_SKIP() << "EVTX fixture not found";
    }

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();

    Document doc;
    ASSERT_TRUE(result.reader->next(doc));

    // record_id должен быть установлен
    EXPECT_TRUE(doc.record_id.has_value());
    EXPECT_GT(*doc.record_id, 0u);
}

/// TST-EVTX-003: EVTX записи возвращаются с timestamp
TEST_F(ReaderTestFixture, TST_EVTX_003_Timestamp) {
    // SPEC-SLICE-007 INV-003: timestamp в ISO 8601
    auto path = get_evtx_fixture_path();
    if (!fs::exists(path)) {
        GTEST_SKIP() << "EVTX fixture not found";
    }

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();

    Document doc;
    ASSERT_TRUE(result.reader->next(doc));

    // timestamp должен быть установлен и содержать ISO 8601 формат
    EXPECT_TRUE(doc.timestamp.has_value());
    EXPECT_FALSE(doc.timestamp->empty());
    // ISO 8601: YYYY-MM-DDTHH:MM:SS
    EXPECT_NE(doc.timestamp->find('T'), std::string::npos);
    EXPECT_NE(doc.timestamp->find('Z'), std::string::npos);
}

/// TST-EVTX-004: EVTX данные содержат Event объект
TEST_F(ReaderTestFixture, TST_EVTX_004_EventObject) {
    // SPEC-SLICE-007 FACT-005: separate_json_attributes
    auto path = get_evtx_fixture_path();
    if (!fs::exists(path)) {
        GTEST_SKIP() << "EVTX fixture not found";
    }

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();

    Document doc;
    ASSERT_TRUE(result.reader->next(doc));

    // Данные должны содержать Event объект
    EXPECT_TRUE(doc.data.is_object());
    auto* event = doc.data.get("Event");
    EXPECT_NE(event, nullptr);
}

/// TST-EVTX-005: Итерация по всем записям
TEST_F(ReaderTestFixture, TST_EVTX_005_Iteration) {
    // SPEC-SLICE-007 FACT-004: итерация по записям
    auto path = get_evtx_fixture_path();
    if (!fs::exists(path)) {
        GTEST_SKIP() << "EVTX fixture not found";
    }

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();

    std::size_t count = 0;
    Document doc;
    while (result.reader->next(doc)) {
        ++count;
        // Каждая запись должна быть EVTX
        EXPECT_EQ(doc.kind, DocumentKind::Evtx);
    }

    // security_sample.evtx должен содержать несколько записей
    EXPECT_GT(count, 0u);
}

/// TST-EVTX-006: Файл не найден → ошибка
TEST_F(ReaderTestFixture, TST_EVTX_006_FileNotFound) {
    auto path = temp_dir_ / "nonexistent.evtx";

    auto result = Reader::open(path, false, false);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error.kind, ReaderErrorKind::FileNotFound);
}

/// TST-EVTX-007: Невалидный EVTX → ошибка
TEST_F(ReaderTestFixture, TST_EVTX_007_InvalidEvtx) {
    // Создаём файл с неправильной магией
    auto path = create_temp_file("invalid.evtx", "This is not a valid EVTX file");

    auto result = Reader::open(path, false, false);
    // Должна быть ошибка парсинга
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.reader == nullptr || result.reader->last_error().has_value());
}

/// TST-EVTX-008: skip_errors=true → пустой Reader
TEST_F(ReaderTestFixture, TST_EVTX_008_SkipErrors) {
    // SPEC-SLICE-007 FACT-009: skip_errors
    auto path = create_temp_file("invalid.evtx", "Not EVTX");

    auto result = Reader::open(path, false, true);  // skip_errors=true
    EXPECT_TRUE(result.ok);
    ASSERT_NE(result.reader, nullptr);

    // Не должно быть записей
    Document doc;
    EXPECT_FALSE(result.reader->next(doc));
}

/// TST-EVTX-009: расширение .evt тоже распознаётся как EVTX
TEST_F(ReaderTestFixture, TST_EVTX_009_EvtExtension) {
    // SPEC-SLICE-007 FACT-007: расширения .evt, .evtx
    auto path = create_temp_file("test.evt", "dummy");

    // Определяем тип по расширению
    auto kind = document_kind_from_path(path);
    EXPECT_EQ(kind, DocumentKind::Evtx);
}

/// TST-EVTX-010: fallback позиция при load_unknown
TEST_F(ReaderTestFixture, TST_EVTX_010_FallbackPosition) {
    // SPEC-SLICE-007 FACT-013: fallback позиция 1
    // EVTX пробуется первым при load_unknown=true
    auto path = get_evtx_fixture_path();
    if (!fs::exists(path)) {
        GTEST_SKIP() << "EVTX fixture not found";
    }

    // Копируем фикстуру с неизвестным расширением
    auto unknown_path = temp_dir_ / "unknown_file";
    fs::copy(path, unknown_path);

    auto result = Reader::open(unknown_path, true, false);  // load_unknown=true
    ASSERT_TRUE(result.ok) << result.error.format();
    EXPECT_EQ(result.reader->kind(), DocumentKind::Evtx);
}

/// TST-EVTX-011: Document source содержит путь к файлу
TEST_F(ReaderTestFixture, TST_EVTX_011_DocumentSource) {
    auto path = get_evtx_fixture_path();
    if (!fs::exists(path)) {
        GTEST_SKIP() << "EVTX fixture not found";
    }

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();

    Document doc;
    ASSERT_TRUE(result.reader->next(doc));

    EXPECT_FALSE(doc.source.empty());
    EXPECT_NE(doc.source.find("evtx"), std::string::npos);
}

/// TST-EVTX-012: Case-insensitive расширение
TEST_F(ReaderTestFixture, TST_EVTX_012_CaseInsensitive) {
    // SPEC-SLICE-007: case-insensitive
    auto kind1 = document_kind_from_extension("EVTX");
    auto kind2 = document_kind_from_extension("Evtx");
    auto kind3 = document_kind_from_extension("evtx");
    auto kind4 = document_kind_from_extension("EVT");

    EXPECT_EQ(kind1, DocumentKind::Evtx);
    EXPECT_EQ(kind2, DocumentKind::Evtx);
    EXPECT_EQ(kind3, DocumentKind::Evtx);
    EXPECT_EQ(kind4, DocumentKind::Evtx);
}

/// TST-EVTX-013: document_kind_extensions возвращает evt, evtx
TEST_F(ReaderTestFixture, TST_EVTX_013_Extensions) {
    // SPEC-SLICE-007 FACT-007
    auto exts = document_kind_extensions(DocumentKind::Evtx);

    EXPECT_EQ(exts.size(), 2u);
    EXPECT_TRUE(std::find(exts.begin(), exts.end(), "evt") != exts.end());
    EXPECT_TRUE(std::find(exts.begin(), exts.end(), "evtx") != exts.end());
}

/// TST-EVTX-014: Пустой EVTX файл (только заголовок)
TEST_F(ReaderTestFixture, TST_EVTX_014_EmptyEvtx) {
    // Создаём минимальный файл только с заголовком
    std::string header(4096, '\0');
    std::memcpy(header.data(), "ElfFile", 8);
    auto path = create_temp_file("empty.evtx", header);

    auto result = Reader::open(path, false, false);
    // Должен открыться, но без записей
    // (поведение зависит от реализации — может быть ошибка или пустой)
    if (result.ok) {
        Document doc;
        EXPECT_FALSE(result.reader->next(doc));
    }
}

/// TST-EVTX-015: has_next() корректно работает
TEST_F(ReaderTestFixture, TST_EVTX_015_HasNext) {
    auto path = get_evtx_fixture_path();
    if (!fs::exists(path)) {
        GTEST_SKIP() << "EVTX fixture not found";
    }

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();

    // В начале has_next должен быть true (если есть записи)
    EXPECT_TRUE(result.reader->has_next());

    // После чтения всех записей has_next должен стать false
    Document doc;
    while (result.reader->next(doc)) {
        // продолжаем
    }

    // После исчерпания записей
    EXPECT_FALSE(result.reader->has_next());
}

/// TST-EVTX-016: Reader::path() возвращает путь к файлу
TEST_F(ReaderTestFixture, TST_EVTX_016_ReaderPath) {
    auto path = get_evtx_fixture_path();
    if (!fs::exists(path)) {
        GTEST_SKIP() << "EVTX fixture not found";
    }

    auto result = Reader::open(path);
    ASSERT_TRUE(result.ok) << result.error.format();

    EXPECT_EQ(result.reader->path(), path);
}
