// ==============================================================================
// test_hve_gtest.cpp - Unit tests for HVE Parser
// ==============================================================================
//
// SLICE-015: HVE Parser (Windows Registry Hive)
// SPEC-SLICE-015: micro-spec Tests Section 5.2
//
// Tests:
// TST-HVE-001: Load_ValidHive
// TST-HVE-002: Load_WithTransactionLogs (known limitation - logs not supported)
// TST-HVE-003: Load_InvalidFile
// TST-HVE-004: GetKey_ValidPath
// TST-HVE-005: GetKey_InvalidPath
// TST-HVE-006: ReadSubKeys
// TST-HVE-007: ValueIter
// TST-HVE-008: CellValue_Binary
// TST-HVE-009: CellValue_U32
// TST-HVE-010: CellValue_U64
// TST-HVE-011: CellValue_String
// TST-HVE-012: CellValue_MultiString
// TST-HVE-013: KeyLastModified
// TST-HVE-014: Reader_Integration
// TST-HVE-015: Fallback_Position
//
// ==============================================================================

#include <chainsaw/hve.hpp>
#include <chainsaw/platform.hpp>
#include <chainsaw/reader.hpp>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace chainsaw::io::hve {

// ============================================================================
// Test Fixture
// ============================================================================

class HveTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Get fixture paths
        fixtures_dir_ = std::filesystem::path(__FILE__).parent_path() / "fixtures" / "hve";
        valid_hive_ = fixtures_dir_ / "test_minimal.hve";
        invalid_file_ = fixtures_dir_ / "invalid.hve";
    }

    std::filesystem::path fixtures_dir_;
    std::filesystem::path valid_hive_;
    std::filesystem::path invalid_file_;
};

// ============================================================================
// TST-HVE-001: Load_ValidHive
// ============================================================================
// FACT-001, FACT-002: Parser::load() loads valid hive file

TEST_F(HveTest, TST_HVE_001_Load_ValidHive) {
    // Skip if fixture doesn't exist
    if (!std::filesystem::exists(valid_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << valid_hive_;
    }

    HveParser parser;
    bool result = parser.load(valid_hive_);

    EXPECT_TRUE(result) << "Failed to load valid hive: "
                        << (parser.last_error() ? parser.last_error()->message : "unknown error");
    EXPECT_TRUE(parser.loaded());
    EXPECT_FALSE(parser.last_error().has_value());
    EXPECT_EQ(parser.path(), valid_hive_);
}

// ============================================================================
// TST-HVE-002: Load_WithTransactionLogs
// ============================================================================
// FACT-001: Transaction logs are searched and applied

TEST_F(HveTest, TST_HVE_002_Load_WithTransactionLogs) {
    // Test that transaction logs are searched for .LOG, .LOG1, .LOG2 files
    // and the transaction_logs_applied() method reports the status

    if (!std::filesystem::exists(valid_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << valid_hive_;
    }

    // Test 1: Load without any .LOG files
    {
        HveParser parser;
        bool result = parser.load(valid_hive_);
        EXPECT_TRUE(result);
        EXPECT_TRUE(parser.loaded());
        // No .LOG files present, so transaction_logs_applied should be false
        EXPECT_FALSE(parser.transaction_logs_applied());
    }

    // Test 2: Load with invalid .LOG file (should be ignored)
    {
        auto log_file = fixtures_dir_ / "test_minimal.LOG";
        std::ofstream(log_file) << "invalid log data - not a valid HVLE format";

        HveParser parser;
        bool result = parser.load(valid_hive_);

        // Clean up
        std::filesystem::remove(log_file);

        // Parser should succeed even with invalid log files
        EXPECT_TRUE(result);
        EXPECT_TRUE(parser.loaded());
        // Invalid log file format, so transaction_logs_applied should be false
        EXPECT_FALSE(parser.transaction_logs_applied());
    }
}

// ============================================================================
// TST-HVE-003: Load_InvalidFile
// ============================================================================
// FACT-023: Error on invalid file

TEST_F(HveTest, TST_HVE_003_Load_InvalidFile) {
    if (!std::filesystem::exists(invalid_file_)) {
        GTEST_SKIP() << "Test fixture not found: " << invalid_file_;
    }

    HveParser parser;
    bool result = parser.load(invalid_file_);

    EXPECT_FALSE(result);
    EXPECT_FALSE(parser.loaded());
    EXPECT_TRUE(parser.last_error().has_value());
    EXPECT_EQ(parser.last_error()->kind, HveErrorKind::InvalidSignature);
}

// ============================================================================
// TST-HVE-004: GetKey_ValidPath
// ============================================================================
// FACT-007, FACT-008: get_key(path) returns key for valid path

TEST_F(HveTest, TST_HVE_004_GetKey_ValidPath) {
    if (!std::filesystem::exists(valid_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << valid_hive_;
    }

    HveParser parser;
    ASSERT_TRUE(parser.load(valid_hive_));

    // Get root key first
    auto root = parser.get_root_key();
    ASSERT_TRUE(root.has_value()) << "Root key should exist";
    EXPECT_EQ(root->name(), "TestRoot");

    // Get subkey by path
    auto subkey = parser.get_key("Software");
    ASSERT_TRUE(subkey.has_value()) << "Software subkey should exist";
    EXPECT_EQ(subkey->name(), "Software");
}

// ============================================================================
// TST-HVE-005: GetKey_InvalidPath
// ============================================================================
// FACT-007: get_key() returns nullopt for non-existent path

TEST_F(HveTest, TST_HVE_005_GetKey_InvalidPath) {
    if (!std::filesystem::exists(valid_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << valid_hive_;
    }

    HveParser parser;
    ASSERT_TRUE(parser.load(valid_hive_));

    auto key = parser.get_key("NonExistent\\Path\\Here");

    EXPECT_FALSE(key.has_value());
}

// ============================================================================
// TST-HVE-006: ReadSubKeys
// ============================================================================
// FACT-009: subkey_names() returns list of subkeys

TEST_F(HveTest, TST_HVE_006_ReadSubKeys) {
    if (!std::filesystem::exists(valid_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << valid_hive_;
    }

    HveParser parser;
    ASSERT_TRUE(parser.load(valid_hive_));

    auto root = parser.get_root_key();
    ASSERT_TRUE(root.has_value());

    // Check subkeys
    const auto& subkeys = root->subkey_names();
    EXPECT_EQ(subkeys.size(), 1);
    if (!subkeys.empty()) {
        EXPECT_EQ(subkeys[0], "Software");
    }
}

// ============================================================================
// TST-HVE-007: ValueIter
// ============================================================================
// FACT-011-019: values() returns all values

TEST_F(HveTest, TST_HVE_007_ValueIter) {
    if (!std::filesystem::exists(valid_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << valid_hive_;
    }

    HveParser parser;
    ASSERT_TRUE(parser.load(valid_hive_));

    auto root = parser.get_root_key();
    ASSERT_TRUE(root.has_value());

    // Check values
    const auto& values = root->values();
    EXPECT_EQ(values.size(), 1);
    if (!values.empty()) {
        EXPECT_EQ(values[0].name, "TestValue");
        EXPECT_EQ(values[0].type, RegValueType::Dword);
    }
}

// ============================================================================
// TST-HVE-008: CellValue_Binary
// ============================================================================
// FACT-011: REG_BINARY support
// Note: Minimal test hive doesn't have binary value, test API only

TEST_F(HveTest, TST_HVE_008_CellValue_Binary) {
    RegValue value;
    value.type = RegValueType::Binary;
    value.data = std::vector<std::uint8_t>{0x01, 0x02, 0x03};

    auto* binary = value.as_binary();
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary->size(), 3);
    EXPECT_EQ((*binary)[0], 0x01);
    EXPECT_EQ((*binary)[1], 0x02);
    EXPECT_EQ((*binary)[2], 0x03);
}

// ============================================================================
// TST-HVE-009: CellValue_U32
// ============================================================================
// FACT-012: REG_DWORD support

TEST_F(HveTest, TST_HVE_009_CellValue_U32) {
    if (!std::filesystem::exists(valid_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << valid_hive_;
    }

    HveParser parser;
    ASSERT_TRUE(parser.load(valid_hive_));

    auto root = parser.get_root_key();
    ASSERT_TRUE(root.has_value());

    auto value = root->get_value("TestValue");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->type, RegValueType::Dword);

    auto u32_val = value->as_u32();
    ASSERT_TRUE(u32_val.has_value());
    EXPECT_EQ(*u32_val, 42u);
}

// ============================================================================
// TST-HVE-010: CellValue_U64
// ============================================================================
// FACT-013: REG_QWORD support
// Note: Test API only - minimal hive doesn't have QWORD value

TEST_F(HveTest, TST_HVE_010_CellValue_U64) {
    RegValue value;
    value.type = RegValueType::Qword;
    value.data = std::uint64_t{0x123456789ABCDEF0ULL};

    auto u64_val = value.as_u64();
    ASSERT_TRUE(u64_val.has_value());
    EXPECT_EQ(*u64_val, 0x123456789ABCDEF0ULL);
}

// ============================================================================
// TST-HVE-011: CellValue_String
// ============================================================================
// FACT-016: REG_SZ support
// Note: Test API only - minimal hive doesn't have string value

TEST_F(HveTest, TST_HVE_011_CellValue_String) {
    RegValue value;
    value.type = RegValueType::String;
    value.data = std::string{"Hello, Registry!"};

    auto* str_val = value.as_string();
    ASSERT_NE(str_val, nullptr);
    EXPECT_EQ(*str_val, "Hello, Registry!");
}

// ============================================================================
// TST-HVE-012: CellValue_MultiString
// ============================================================================
// FACT-017: REG_MULTI_SZ support
// Note: Test API only

TEST_F(HveTest, TST_HVE_012_CellValue_MultiString) {
    RegValue value;
    value.type = RegValueType::MultiString;
    value.data = std::vector<std::string>{"First", "Second", "Third"};

    auto* multi_val = value.as_multi_string();
    ASSERT_NE(multi_val, nullptr);
    EXPECT_EQ(multi_val->size(), 3);
    EXPECT_EQ((*multi_val)[0], "First");
    EXPECT_EQ((*multi_val)[1], "Second");
    EXPECT_EQ((*multi_val)[2], "Third");
}

// ============================================================================
// TST-HVE-013: KeyLastModified
// ============================================================================
// FACT-010: last_modified() returns timestamp

TEST_F(HveTest, TST_HVE_013_KeyLastModified) {
    if (!std::filesystem::exists(valid_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << valid_hive_;
    }

    HveParser parser;
    ASSERT_TRUE(parser.load(valid_hive_));

    auto root = parser.get_root_key();
    ASSERT_TRUE(root.has_value());

    // Timestamp should be non-zero
    auto timestamp = root->last_modified();
    auto epoch = std::chrono::system_clock::time_point{};
    EXPECT_GT(timestamp, epoch);
}

// ============================================================================
// TST-HVE-014: Reader_Integration
// ============================================================================
// FACT-003, FACT-004: Integration with Reader::open()

TEST_F(HveTest, TST_HVE_014_Reader_Integration) {
    if (!std::filesystem::exists(valid_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << valid_hive_;
    }

    auto result = chainsaw::io::Reader::open(valid_hive_, false, false);

    EXPECT_TRUE(result.ok) << "Reader::open should succeed for .hve file";
    ASSERT_NE(result.reader, nullptr);
    EXPECT_EQ(result.reader->kind(), chainsaw::io::DocumentKind::Hve);

    // Should be able to iterate documents
    chainsaw::io::Document doc;
    bool has_doc = result.reader->next(doc);
    EXPECT_TRUE(has_doc);
    EXPECT_EQ(doc.kind, chainsaw::io::DocumentKind::Hve);
}

// ============================================================================
// TST-HVE-015: Fallback_Position
// ============================================================================
// FACT-005: HVE is position 5 in fallback chain

TEST_F(HveTest, TST_HVE_015_Fallback_Position) {
    if (!std::filesystem::exists(valid_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << valid_hive_;
    }

    // Rename to unknown extension
    auto temp_file = fixtures_dir_ / "test_unknown_ext";
    std::filesystem::copy_file(valid_hive_, temp_file,
                               std::filesystem::copy_options::overwrite_existing);

    // With load_unknown=true, should try HVE parser
    auto result = chainsaw::io::Reader::open(temp_file, true, false);

    // Clean up
    std::filesystem::remove(temp_file);

    // Should succeed via fallback
    EXPECT_TRUE(result.ok) << "Fallback should find HVE parser";
    if (result.reader) {
        EXPECT_EQ(result.reader->kind(), chainsaw::io::DocumentKind::Hve);
    }
}

// ============================================================================
// Additional Tests
// ============================================================================

TEST_F(HveTest, Load_NonExistentFile) {
    HveParser parser;
    bool result = parser.load("/nonexistent/path/file.hve");

    EXPECT_FALSE(result);
    EXPECT_FALSE(parser.loaded());
    EXPECT_TRUE(parser.last_error().has_value());
    EXPECT_EQ(parser.last_error()->kind, HveErrorKind::FileNotFound);
}

TEST_F(HveTest, Iterator_TraverseAllKeys) {
    if (!std::filesystem::exists(valid_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << valid_hive_;
    }

    HveParser parser;
    ASSERT_TRUE(parser.load(valid_hive_));

    auto iter = parser.iter();
    int count = 0;
    RegKey key;
    while (iter.next(key)) {
        count++;
    }

    // Should have root + Software subkey
    EXPECT_EQ(count, 2);
}

TEST_F(HveTest, RegValueType_ToString) {
    EXPECT_STREQ(reg_value_type_to_string(RegValueType::Binary), "REG_BINARY");
    EXPECT_STREQ(reg_value_type_to_string(RegValueType::Dword), "REG_DWORD");
    EXPECT_STREQ(reg_value_type_to_string(RegValueType::Qword), "REG_QWORD");
    EXPECT_STREQ(reg_value_type_to_string(RegValueType::String), "REG_SZ");
    EXPECT_STREQ(reg_value_type_to_string(RegValueType::MultiString), "REG_MULTI_SZ");
    EXPECT_STREQ(reg_value_type_to_string(RegValueType::None), "REG_NONE");
    EXPECT_STREQ(reg_value_type_to_string(RegValueType::Error), "REG_ERROR");
}

TEST_F(HveTest, GetKey_CaseInsensitive) {
    if (!std::filesystem::exists(valid_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << valid_hive_;
    }

    HveParser parser;
    ASSERT_TRUE(parser.load(valid_hive_));

    // Registry keys are case-insensitive
    auto key1 = parser.get_key("SOFTWARE");
    auto key2 = parser.get_key("software");
    auto key3 = parser.get_key("SoFtWaRe");

    EXPECT_TRUE(key1.has_value());
    EXPECT_TRUE(key2.has_value());
    EXPECT_TRUE(key3.has_value());
}

}  // namespace chainsaw::io::hve
