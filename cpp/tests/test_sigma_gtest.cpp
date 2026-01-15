// ==============================================================================
// test_sigma_gtest.cpp - Unit-тесты для Sigma Rules Loader (SLICE-010)
// ==============================================================================
//
// SPEC-SLICE-010: micro-spec поведения
// TST-SIGMA-001..028: unit-тесты для модуля Sigma
//
// Покрытие:
// - Match functions (as_contains, as_endswith, as_startswith, as_match, as_regex)
// - Base64 encoding (base64_encode, base64_offset_encode)
// - Modifier support (is_modifier_supported, get_unsupported_modifiers)
// - Condition checking (is_condition_unsupported)
// - load (single rules and Rule Collections)
//
// ==============================================================================

#include <chainsaw/rule.hpp>
#include <chainsaw/sigma.hpp>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

namespace sigma = chainsaw::rule::sigma;
namespace rule = chainsaw::rule;

// ============================================================================
// Match Functions Tests (TST-0010..TST-0014, TST-SIGMA-009..012)
// ============================================================================

// TST-0010: test_match_contains
TEST(SigmaMatch, AsContains) {
    // SPEC-SLICE-010 FACT-005: contains -> "i*value*"
    EXPECT_EQ(sigma::as_contains("foobar"), "i*foobar*");
    EXPECT_EQ(sigma::as_contains(""), "i**");
    EXPECT_EQ(sigma::as_contains("test value"), "i*test value*");
}

// TST-0011: test_match_endswith
TEST(SigmaMatch, AsEndswith) {
    // SPEC-SLICE-010 FACT-005: endswith -> "i*value"
    EXPECT_EQ(sigma::as_endswith("foobar"), "i*foobar");
    EXPECT_EQ(sigma::as_endswith(".exe"), "i*.exe");
    EXPECT_EQ(sigma::as_endswith("\\powershell.exe"), "i*\\powershell.exe");
}

// TST-0014: test_match_startswith
TEST(SigmaMatch, AsStartswith) {
    // SPEC-SLICE-010 FACT-005: startswith -> "ivalue*"
    EXPECT_EQ(sigma::as_startswith("foobar"), "ifoobar*");
    EXPECT_EQ(sigma::as_startswith("C:\\"), "iC:\\*");
}

// TST-0012: test_match
TEST(SigmaMatch, AsMatch) {
    // Basic cases
    auto result = sigma::as_match("foobar");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "ifoobar");

    result = sigma::as_match("*foobar");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "i*foobar");

    result = sigma::as_match("foobar*");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "ifoobar*");

    result = sigma::as_match("*foobar*");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "i*foobar*");

    // SPEC-SLICE-010 FACT-006: nested wildcards -> nullopt
    result = sigma::as_match("foo*bar");
    EXPECT_FALSE(result.has_value());

    result = sigma::as_match("foo?bar");
    EXPECT_FALSE(result.has_value());
}

// TST-0013: test_match_regex
TEST(SigmaMatch, AsRegex) {
    // Without conversion
    auto result = sigma::as_regex("foobar", false);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "?foobar");

    // With conversion (wildcard to regex)
    result = sigma::as_regex("foo*bar", true);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "?foo.*bar");

    // Invalid regex
    result = sigma::as_regex("[invalid(", false);
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// Base64 Tests (TST-SIGMA-007, TST-SIGMA-008)
// ============================================================================

// TST-SIGMA-007: base64 modifier
TEST(SigmaBase64, Encode) {
    // Standard base64 encoding
    EXPECT_EQ(sigma::base64_encode("test"), "dGVzdA==");
    EXPECT_EQ(sigma::base64_encode("hello"), "aGVsbG8=");
    EXPECT_EQ(sigma::base64_encode(""), "");
    EXPECT_EQ(sigma::base64_encode("a"), "YQ==");
    EXPECT_EQ(sigma::base64_encode("ab"), "YWI=");
    EXPECT_EQ(sigma::base64_encode("abc"), "YWJj");
}

// TST-SIGMA-008: base64offset modifier
TEST(SigmaBase64, OffsetEncode) {
    // base64offset generates 3 variants
    auto result = sigma::base64_offset_encode("test");
    ASSERT_EQ(result.size(), 3u);

    // First is standard encoding
    EXPECT_EQ(result[0], "dGVzdA==");

    // Others are offset variants (trimmed)
    EXPECT_FALSE(result[1].empty());
    EXPECT_FALSE(result[2].empty());
}

// ============================================================================
// Modifier Support Tests (TST-SIGMA-013, TST-SIGMA-014)
// ============================================================================

TEST(SigmaModifier, IsSupported) {
    // All supported modifiers
    EXPECT_TRUE(sigma::is_modifier_supported("all"));
    EXPECT_TRUE(sigma::is_modifier_supported("base64"));
    EXPECT_TRUE(sigma::is_modifier_supported("base64offset"));
    EXPECT_TRUE(sigma::is_modifier_supported("contains"));
    EXPECT_TRUE(sigma::is_modifier_supported("endswith"));
    EXPECT_TRUE(sigma::is_modifier_supported("startswith"));
    EXPECT_TRUE(sigma::is_modifier_supported("re"));

    // Unsupported
    EXPECT_FALSE(sigma::is_modifier_supported("utf16"));
    EXPECT_FALSE(sigma::is_modifier_supported("utf16le"));
    EXPECT_FALSE(sigma::is_modifier_supported("wide"));
}

TEST(SigmaModifier, GetUnsupported) {
    std::unordered_set<std::string> modifiers = {"contains", "utf16", "wide"};
    auto unsupported = sigma::get_unsupported_modifiers(modifiers);

    EXPECT_EQ(unsupported.size(), 2u);
    EXPECT_TRUE(std::find(unsupported.begin(), unsupported.end(), "utf16") != unsupported.end());
    EXPECT_TRUE(std::find(unsupported.begin(), unsupported.end(), "wide") != unsupported.end());

    // All supported
    modifiers = {"contains", "endswith"};
    unsupported = sigma::get_unsupported_modifiers(modifiers);
    EXPECT_TRUE(unsupported.empty());
}

// ============================================================================
// Condition Checking Tests (TST-0009, TST-SIGMA-020)
// ============================================================================

// TST-0009: test_unsupported_conditions
TEST(SigmaCondition, IsUnsupported) {
    // SPEC-SLICE-010 FACT-014: Unsupported conditions

    // Pipe (aggregation)
    EXPECT_TRUE(sigma::is_condition_unsupported("search_expression | aggregation_expression"));

    // Wildcards in condition
    EXPECT_TRUE(sigma::is_condition_unsupported("selection*"));

    // "of" keyword (except special cases handled separately)
    EXPECT_TRUE(sigma::is_condition_unsupported("1 of them"));

    // Aggregation keywords
    EXPECT_TRUE(sigma::is_condition_unsupported("search avg field"));
    EXPECT_TRUE(sigma::is_condition_unsupported("search max field"));
    EXPECT_TRUE(sigma::is_condition_unsupported("search min field"));
    EXPECT_TRUE(sigma::is_condition_unsupported("search sum field"));
    EXPECT_TRUE(sigma::is_condition_unsupported("search near other"));

    // Valid conditions
    EXPECT_FALSE(sigma::is_condition_unsupported("selection and filter"));
    EXPECT_FALSE(sigma::is_condition_unsupported("A or B"));
    EXPECT_FALSE(sigma::is_condition_unsupported("not selection"));
}

// ============================================================================
// Load Tests (TST-0007, TST-0008, TST-SIGMA-001..006)
// ============================================================================

class SigmaLoadTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir_;

    void SetUp() override {
        // Use unique directory per test to avoid race conditions
        auto test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string unique_name =
            std::string("chainsaw_sigma_") + test_info->test_suite_name() + "_" +
            test_info->name() + "_" +
            std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        temp_dir_ = std::filesystem::temp_directory_path() / unique_name;
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);  // Clean up any leftovers
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    void WriteFile(const std::string& name, const std::string& content) {
        std::ofstream file(temp_dir_ / name);
        file << content;
    }
};

// TST-0007: convert_sigma!("simple")
TEST_F(SigmaLoadTest, SimpleRule) {
    const char* rule_str = R"(---
title: simple
id: simple
status: experimental
description: A simple rule for testing
author: Alex Kornitzer
date: 1970/01/01
references: []
detection:
  search:
    CommandLine|contains:
      - ' -Nop '
  condition: search
)";

    WriteFile("simple.yml", rule_str);

    auto result = sigma::load(temp_dir_ / "simple.yml");
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.rules.size(), 1u);

    auto& loaded = result.rules[0];
    EXPECT_EQ(loaded.title, "simple");
    EXPECT_EQ(loaded.status, "experimental");
    EXPECT_EQ(loaded.level, "info");
}

// TST-0008: convert_sigma!("collection")
TEST_F(SigmaLoadTest, RuleCollection) {
    const char* rule_str = R"(---
title: collection
id: collection
status: experimental
description: A collection rule for testing
author: Alex Kornitzer
date: 1970/01/01
references: []
action: global
detection:
  base:
    Image|contains:
      - \powershell.exe
  condition: search
---
detection:
  search:
    CommandLine|contains:
      - ' -Nop '
  condition: search and base
---
detection:
  search:
    CommandLine|contains:
      - ' -encodedcommand '
  condition: search and base
)";

    WriteFile("collection.yml", rule_str);

    auto result = sigma::load(temp_dir_ / "collection.yml");
    ASSERT_TRUE(result.ok);
    // Rule Collection should produce 2 rules
    EXPECT_EQ(result.rules.size(), 2u);

    for (const auto& r : result.rules) {
        EXPECT_EQ(r.title, "collection");
    }
}

// TST-SIGMA-001: Load valid sigma rule
TEST_F(SigmaLoadTest, ValidRule) {
    const char* rule_str = R"(---
title: Test Rule
description: Test description
author: Test Author
status: stable
level: high
detection:
  selection:
    EventID: 1
  condition: selection
)";

    WriteFile("valid.yml", rule_str);

    auto result = sigma::load(temp_dir_ / "valid.yml");
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.rules.size(), 1u);
    EXPECT_EQ(result.rules[0].title, "Test Rule");
    EXPECT_EQ(result.rules[0].level, "high");
    EXPECT_EQ(result.rules[0].status, "stable");
}

// TST-SIGMA-003: Author split by comma
TEST_F(SigmaLoadTest, AuthorSplit) {
    const char* rule_str = R"(---
title: Test
description: Test
author: Alice, Bob, Charlie
detection:
  sel:
    field: value
  condition: sel
)";

    WriteFile("authors.yml", rule_str);

    auto result = sigma::load(temp_dir_ / "authors.yml");
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.rules.size(), 1u);

    auto& authors = result.rules[0].authors;
    ASSERT_EQ(authors.size(), 3u);
    EXPECT_EQ(authors[0], "Alice");
    EXPECT_EQ(authors[1], "Bob");
    EXPECT_EQ(authors[2], "Charlie");
}

// TST-SIGMA-004: Unknown author
TEST_F(SigmaLoadTest, UnknownAuthor) {
    const char* rule_str = R"(---
title: Test
description: Test
detection:
  sel:
    field: value
  condition: sel
)";

    WriteFile("no_author.yml", rule_str);

    auto result = sigma::load(temp_dir_ / "no_author.yml");
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.rules.size(), 1u);

    auto& authors = result.rules[0].authors;
    ASSERT_EQ(authors.size(), 1u);
    EXPECT_EQ(authors[0], "unknown");
}

// TST-SIGMA-005: Level normalization
TEST_F(SigmaLoadTest, LevelNormalization) {
    const char* rule_str = R"(---
title: Test
description: Test
level: unknown_level
detection:
  sel:
    field: value
  condition: sel
)";

    WriteFile("level.yml", rule_str);

    auto result = sigma::load(temp_dir_ / "level.yml");
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.rules.size(), 1u);
    EXPECT_EQ(result.rules[0].level, "info");
}

// TST-SIGMA-006: Status normalization
TEST_F(SigmaLoadTest, StatusNormalization) {
    const char* rule_str = R"(---
title: Test
description: Test
status: testing
detection:
  sel:
    field: value
  condition: sel
)";

    WriteFile("status.yml", rule_str);

    auto result = sigma::load(temp_dir_ / "status.yml");
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.rules.size(), 1u);
    EXPECT_EQ(result.rules[0].status, "experimental");
}

// TST-SIGMA-022: LogSource fields
TEST_F(SigmaLoadTest, LogSource) {
    const char* rule_str = R"(---
title: Test
description: Test
logsource:
  category: process_creation
  product: windows
  service: sysmon
detection:
  sel:
    field: value
  condition: sel
)";

    WriteFile("logsource.yml", rule_str);

    auto result = sigma::load(temp_dir_ / "logsource.yml");
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.rules.size(), 1u);

    auto& ls = result.rules[0].logsource;
    ASSERT_TRUE(ls.has_value());
    EXPECT_EQ(ls->category, "process_creation");
    EXPECT_EQ(ls->product, "windows");
    EXPECT_EQ(ls->service, "sysmon");
}

// ============================================================================
// rule::load integration tests (TST-SIGMA-026, TST-SIGMA-027, TST-SIGMA-028)
// ============================================================================

TEST_F(SigmaLoadTest, RuleLoadIntegration) {
    const char* rule_str = R"(---
title: Integration Test
description: Test rule::load integration
author: Test
level: medium
status: stable
detection:
  selection:
    CommandLine|contains: test
  condition: selection
)";

    WriteFile("integration.yml", rule_str);

    auto result = rule::load(rule::Kind::Sigma, temp_dir_ / "integration.yml");
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.rules.size(), 1u);

    // Check it's a SigmaRule
    auto* sigma_rule = std::get_if<rule::SigmaRule>(&result.rules[0]);
    ASSERT_NE(sigma_rule, nullptr);

    EXPECT_EQ(sigma_rule->name, "Integration Test");
    EXPECT_EQ(sigma_rule->level, rule::Level::Medium);
    EXPECT_EQ(sigma_rule->status, rule::Status::Stable);
}

// TST-SIGMA-028: rule_types() returns Unknown for Sigma
TEST_F(SigmaLoadTest, RuleTypes) {
    const char* rule_content = R"(---
title: Test
description: Test
detection:
  sel:
    field: value
  condition: sel
)";

    WriteFile("types.yml", rule_content);

    auto result = rule::load(rule::Kind::Sigma, temp_dir_ / "types.yml");
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.rules.size(), 1u);

    auto kind = rule::rule_types(result.rules[0]);
    EXPECT_EQ(kind, chainsaw::io::DocumentKind::Unknown);
}

// ============================================================================
// SigmaRule::find tests (TST-SIGMA-023)
// ============================================================================

TEST(SigmaRuleFind, BasicFields) {
    rule::SigmaRule sigma_rule;
    sigma_rule.name = "Test Rule";
    sigma_rule.level = rule::Level::High;
    sigma_rule.status = rule::Status::Stable;
    sigma_rule.id = "12345";

    EXPECT_EQ(sigma_rule.find("title"), "Test Rule");
    EXPECT_EQ(sigma_rule.find("level"), "high");
    EXPECT_EQ(sigma_rule.find("status"), "stable");
    EXPECT_EQ(sigma_rule.find("id"), "12345");
    EXPECT_FALSE(sigma_rule.find("nonexistent").has_value());
}

TEST(SigmaRuleFind, LogSourceFields) {
    rule::SigmaRule sigma_rule;
    sigma_rule.logsource = rule::LogSource{.category = "process_creation",
                                           .definition = std::nullopt,
                                           .product = "windows",
                                           .service = "sysmon"};

    EXPECT_EQ(sigma_rule.find("logsource.category"), "process_creation");
    EXPECT_FALSE(sigma_rule.find("logsource.definition").has_value());
    EXPECT_EQ(sigma_rule.find("logsource.product"), "windows");
    EXPECT_EQ(sigma_rule.find("logsource.service"), "sysmon");
}
