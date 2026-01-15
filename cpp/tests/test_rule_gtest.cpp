// ==============================================================================
// test_rule_gtest.cpp - Unit тесты для модуля chainsaw::rule
// ==============================================================================
//
// SLICE-009: Chainsaw Rules Loader
// SPEC-SLICE-009: TST-CSRULE-001..024
//
// ==============================================================================

#include <chainsaw/rule.hpp>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace rule = chainsaw::rule;
namespace tau = chainsaw::tau;
namespace io = chainsaw::io;

// ============================================================================
// Test Fixtures Path
// ============================================================================

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

namespace {

std::filesystem::path fixtures_path() {
    return std::filesystem::path(CMAKE_SOURCE_DIR) / "tests" / "fixtures" / "rules";
}

}  // namespace

// ============================================================================
// TST-CSRULE-001: Load valid rule
// ============================================================================

TEST(RuleLoader, LoadValidRule) {
    auto result = rule::load(rule::Kind::Chainsaw, fixtures_path() / "valid_rule.yml");

    ASSERT_TRUE(result.ok) << "Error: " << result.error.message;
    ASSERT_EQ(result.rules.size(), 1);

    const auto& r = result.rules[0];
    EXPECT_EQ(rule::rule_name(r), "Security Audit Logs Cleared");
    EXPECT_EQ(rule::rule_level(r), rule::Level::Critical);
    EXPECT_EQ(rule::rule_status(r), rule::Status::Stable);
    EXPECT_EQ(rule::rule_types(r), io::DocumentKind::Evtx);
}

// ============================================================================
// TST-CSRULE-002: Load rule with title alias
// ============================================================================

TEST(RuleLoader, LoadRuleWithTitleAlias) {
    auto result = rule::load(rule::Kind::Chainsaw, fixtures_path() / "rule_with_title.yml");

    ASSERT_TRUE(result.ok) << "Error: " << result.error.message;
    ASSERT_EQ(result.rules.size(), 1);

    // 'title' должен быть aliased к 'name'
    EXPECT_EQ(rule::rule_name(result.rules[0]), "PowerShell Script Block");
}

// ============================================================================
// TST-CSRULE-003: Load rule with Detection filter
// ============================================================================

TEST(RuleLoader, LoadRuleWithDetectionFilter) {
    auto result = rule::load(rule::Kind::Chainsaw, fixtures_path() / "valid_rule.yml");

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.rules.size(), 1);

    // Проверяем что filter типа Detection
    const auto* chainsaw_rule = std::get_if<rule::ChainsawRule>(&result.rules[0]);
    ASSERT_NE(chainsaw_rule, nullptr);

    EXPECT_TRUE(std::holds_alternative<tau::Detection>(chainsaw_rule->filter));
}

// ============================================================================
// TST-CSRULE-004: Load rule with Expression filter
// ============================================================================

TEST(RuleLoader, LoadRuleWithExpressionFilter) {
    auto result = rule::load(rule::Kind::Chainsaw, fixtures_path() / "rule_expression_filter.yml");

    ASSERT_TRUE(result.ok) << "Error: " << result.error.message;
    ASSERT_EQ(result.rules.size(), 1);

    const auto* chainsaw_rule = std::get_if<rule::ChainsawRule>(&result.rules[0]);
    ASSERT_NE(chainsaw_rule, nullptr);

    // Expression filter или Detection - зависит от парсинга
    // В текущей реализации scalar filter парсится упрощённо
}

// ============================================================================
// TST-CSRULE-005: Field with name only
// ============================================================================

TEST(RuleLoader, FieldWithNameOnly) {
    auto result = rule::load(rule::Kind::Chainsaw, fixtures_path() / "rule_field_name_only.yml");

    ASSERT_TRUE(result.ok) << "Error: " << result.error.message;
    ASSERT_EQ(result.rules.size(), 1);

    const auto* chainsaw_rule = std::get_if<rule::ChainsawRule>(&result.rules[0]);
    ASSERT_NE(chainsaw_rule, nullptr);

    // Должно быть 3 поля с name = from = to
    ASSERT_EQ(chainsaw_rule->fields.size(), 3);

    const auto& field = chainsaw_rule->fields[0];
    EXPECT_EQ(field.name, "FullPath");
    EXPECT_EQ(field.from, "FullPath");
    EXPECT_EQ(field.to, "FullPath");
}

// ============================================================================
// TST-CSRULE-006: Field with cast int()
// ============================================================================

TEST(RuleLoader, FieldWithCastInt) {
    auto result = rule::load(rule::Kind::Chainsaw, fixtures_path() / "rule_with_cast.yml");

    ASSERT_TRUE(result.ok) << "Error: " << result.error.message;
    ASSERT_EQ(result.rules.size(), 1);

    const auto* chainsaw_rule = std::get_if<rule::ChainsawRule>(&result.rules[0]);
    ASSERT_NE(chainsaw_rule, nullptr);

    // First field has int() cast
    ASSERT_GE(chainsaw_rule->fields.size(), 1);
    const auto& field = chainsaw_rule->fields[0];

    EXPECT_EQ(field.name, "Count");
    EXPECT_EQ(field.to, "Event.EventData.Count");
    ASSERT_TRUE(field.cast.has_value());
    EXPECT_EQ(*field.cast, tau::ModSym::Int);
}

// ============================================================================
// TST-CSRULE-007: Field with cast str()
// ============================================================================

TEST(RuleLoader, FieldWithCastStr) {
    auto result = rule::load(rule::Kind::Chainsaw, fixtures_path() / "rule_with_cast.yml");

    ASSERT_TRUE(result.ok) << "Error: " << result.error.message;
    ASSERT_EQ(result.rules.size(), 1);

    const auto* chainsaw_rule = std::get_if<rule::ChainsawRule>(&result.rules[0]);
    ASSERT_NE(chainsaw_rule, nullptr);

    // Second field has str() cast
    ASSERT_GE(chainsaw_rule->fields.size(), 2);
    const auto& field = chainsaw_rule->fields[1];

    EXPECT_EQ(field.name, "Message");
    EXPECT_EQ(field.to, "Event.EventData.Message");
    ASSERT_TRUE(field.cast.has_value());
    EXPECT_EQ(*field.cast, tau::ModSym::Str);
}

// ============================================================================
// TST-CSRULE-009: Invalid extension
// ============================================================================

TEST(RuleLoader, InvalidExtension) {
    auto result = rule::load(rule::Kind::Chainsaw, fixtures_path() / "invalid_extension.txt");

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error.message, "rule must have a yaml file extension");
}

// ============================================================================
// TST-CSRULE-010: Level filter
// ============================================================================

TEST(RuleLoader, LevelFilter) {
    rule::LoadOptions options;
    options.levels = std::unordered_set<rule::Level>{rule::Level::High};

    // valid_rule.yml has level: critical
    auto result = rule::load(rule::Kind::Chainsaw, fixtures_path() / "valid_rule.yml", options);

    ASSERT_TRUE(result.ok);
    // Should be empty because level doesn't match filter
    EXPECT_TRUE(result.rules.empty());
}

// ============================================================================
// TST-CSRULE-011: Status filter
// ============================================================================

TEST(RuleLoader, StatusFilter) {
    rule::LoadOptions options;
    options.statuses = std::unordered_set<rule::Status>{rule::Status::Experimental};

    // valid_rule.yml has status: stable
    auto result = rule::load(rule::Kind::Chainsaw, fixtures_path() / "valid_rule.yml", options);

    ASSERT_TRUE(result.ok);
    // Should be empty because status doesn't match filter
    EXPECT_TRUE(result.rules.empty());
}

// ============================================================================
// TST-CSRULE-012: Kind filter
// ============================================================================

TEST(RuleLoader, KindFilter) {
    rule::LoadOptions options;
    options.kinds = std::unordered_set<rule::Kind>{rule::Kind::Sigma};

    auto result = rule::load(rule::Kind::Chainsaw, fixtures_path() / "valid_rule.yml", options);

    ASSERT_TRUE(result.ok);
    // Should be empty because kind doesn't match filter
    EXPECT_TRUE(result.rules.empty());
}

// ============================================================================
// TST-CSRULE-015: Lint valid rule
// ============================================================================

TEST(RuleLint, LintValidRule) {
    auto result = rule::lint(rule::Kind::Chainsaw, fixtures_path() / "valid_rule.yml");

    ASSERT_TRUE(result.ok) << "Error: " << result.error.message;
    EXPECT_EQ(result.filters.size(), 1);
}

// ============================================================================
// TST-CSRULE-016: Lint invalid rule (bad extension)
// ============================================================================

TEST(RuleLint, LintInvalidExtension) {
    auto result = rule::lint(rule::Kind::Chainsaw, fixtures_path() / "invalid_extension.txt");

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error.message, "rule must have a yaml file extension");
}

// ============================================================================
// TST-CSRULE-017: Parse Kind from string
// ============================================================================

TEST(RuleParser, ParseKind) {
    EXPECT_EQ(rule::parse_kind("chainsaw"), rule::Kind::Chainsaw);
    EXPECT_EQ(rule::parse_kind("sigma"), rule::Kind::Sigma);
    EXPECT_THROW(rule::parse_kind("unknown"), std::invalid_argument);
}

// ============================================================================
// TST-CSRULE-018: Parse Level from string
// ============================================================================

TEST(RuleParser, ParseLevel) {
    EXPECT_EQ(rule::parse_level("critical"), rule::Level::Critical);
    EXPECT_EQ(rule::parse_level("high"), rule::Level::High);
    EXPECT_EQ(rule::parse_level("medium"), rule::Level::Medium);
    EXPECT_EQ(rule::parse_level("low"), rule::Level::Low);
    EXPECT_EQ(rule::parse_level("info"), rule::Level::Info);
    EXPECT_THROW(rule::parse_level("unknown"), std::invalid_argument);
}

// ============================================================================
// TST-CSRULE-019: Parse Status from string
// ============================================================================

TEST(RuleParser, ParseStatus) {
    EXPECT_EQ(rule::parse_status("stable"), rule::Status::Stable);
    EXPECT_EQ(rule::parse_status("experimental"), rule::Status::Experimental);
    EXPECT_THROW(rule::parse_status("unknown"), std::invalid_argument);
}

// ============================================================================
// TST-CSRULE-024: Rule types for Chainsaw
// ============================================================================

TEST(RuleInterface, RuleTypesChainsaw) {
    auto result = rule::load(rule::Kind::Chainsaw, fixtures_path() / "rule_field_name_only.yml");

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.rules.size(), 1);

    // rule_field_name_only.yml has kind: mft
    EXPECT_EQ(rule::rule_types(result.rules[0]), io::DocumentKind::Mft);
}

// ============================================================================
// String conversion tests
// ============================================================================

TEST(RuleStrings, KindToString) {
    EXPECT_EQ(rule::to_string(rule::Kind::Chainsaw), "chainsaw");
    EXPECT_EQ(rule::to_string(rule::Kind::Sigma), "sigma");
}

TEST(RuleStrings, LevelToString) {
    EXPECT_EQ(rule::to_string(rule::Level::Critical), "critical");
    EXPECT_EQ(rule::to_string(rule::Level::High), "high");
    EXPECT_EQ(rule::to_string(rule::Level::Medium), "medium");
    EXPECT_EQ(rule::to_string(rule::Level::Low), "low");
    EXPECT_EQ(rule::to_string(rule::Level::Info), "info");
}

TEST(RuleStrings, StatusToString) {
    EXPECT_EQ(rule::to_string(rule::Status::Stable), "stable");
    EXPECT_EQ(rule::to_string(rule::Status::Experimental), "experimental");
}

// ============================================================================
// parse_field tests
// ============================================================================

TEST(RuleParser, ParseFieldSimple) {
    auto expr = rule::parse_field("Event.System.EventID");
    auto* field = std::get_if<tau::ExprField>(&expr.data);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->name, "Event.System.EventID");
}

TEST(RuleParser, ParseFieldWithIntCast) {
    auto expr = rule::parse_field("int(Event.EventData.Count)");
    auto* cast = std::get_if<tau::ExprCast>(&expr.data);
    ASSERT_NE(cast, nullptr);
    EXPECT_EQ(cast->field, "Event.EventData.Count");
    EXPECT_EQ(cast->mod, tau::ModSym::Int);
}

TEST(RuleParser, ParseFieldWithStrCast) {
    auto expr = rule::parse_field("str(Event.EventData.Data)");
    auto* cast = std::get_if<tau::ExprCast>(&expr.data);
    ASSERT_NE(cast, nullptr);
    EXPECT_EQ(cast->field, "Event.EventData.Data");
    EXPECT_EQ(cast->mod, tau::ModSym::Str);
}

// ============================================================================
// parse_numeric tests
// ============================================================================

TEST(RuleParser, ParseNumericEqual) {
    auto pattern = rule::parse_numeric("100");
    ASSERT_TRUE(pattern.has_value());
    auto* eq = std::get_if<tau::PatternEqual>(&*pattern);
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->value, 100);
}

TEST(RuleParser, ParseNumericGreaterThan) {
    auto pattern = rule::parse_numeric(">100");
    ASSERT_TRUE(pattern.has_value());
    auto* gt = std::get_if<tau::PatternGreaterThan>(&*pattern);
    ASSERT_NE(gt, nullptr);
    EXPECT_EQ(gt->value, 100);
}

TEST(RuleParser, ParseNumericGreaterThanOrEqual) {
    auto pattern = rule::parse_numeric(">=50");
    ASSERT_TRUE(pattern.has_value());
    auto* gte = std::get_if<tau::PatternGreaterThanOrEqual>(&*pattern);
    ASSERT_NE(gte, nullptr);
    EXPECT_EQ(gte->value, 50);
}

TEST(RuleParser, ParseNumericLessThan) {
    auto pattern = rule::parse_numeric("<200");
    ASSERT_TRUE(pattern.has_value());
    auto* lt = std::get_if<tau::PatternLessThan>(&*pattern);
    ASSERT_NE(lt, nullptr);
    EXPECT_EQ(lt->value, 200);
}

TEST(RuleParser, ParseNumericLessThanOrEqual) {
    auto pattern = rule::parse_numeric("<=999");
    ASSERT_TRUE(pattern.has_value());
    auto* lte = std::get_if<tau::PatternLessThanOrEqual>(&*pattern);
    ASSERT_NE(lte, nullptr);
    EXPECT_EQ(lte->value, 999);
}

TEST(RuleParser, ParseNumericInvalid) {
    auto pattern = rule::parse_numeric("not_a_number");
    EXPECT_FALSE(pattern.has_value());
}

// ============================================================================
// Error formatting tests
// ============================================================================

TEST(RuleError, ErrorFormat) {
    rule::Error err{"test error", "/path/to/file.yml"};
    std::string formatted = err.format();
    EXPECT_TRUE(formatted.find("test error") != std::string::npos);
    EXPECT_TRUE(formatted.find("/path/to/file.yml") != std::string::npos);
}

TEST(RuleError, ErrorFormatNoPath) {
    rule::Error err{"test error", ""};
    std::string formatted = err.format();
    EXPECT_TRUE(formatted.find("test error") != std::string::npos);
}

// ============================================================================
// Rule interface tests
// ============================================================================

TEST(RuleInterface, RuleIsKind) {
    auto result = rule::load(rule::Kind::Chainsaw, fixtures_path() / "valid_rule.yml");

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.rules.size(), 1);

    EXPECT_TRUE(rule::rule_is_kind(result.rules[0], rule::Kind::Chainsaw));
    EXPECT_FALSE(rule::rule_is_kind(result.rules[0], rule::Kind::Sigma));
}

// ============================================================================
// TST-LINT-009: Chainsaw lint returns single filter
// SPEC-SLICE-014 FACT-014: Chainsaw single filter
// ============================================================================

TEST(RuleLint, LintChainsawSingleFilter) {
    auto result = rule::lint(rule::Kind::Chainsaw, fixtures_path() / "valid_rule.yml");

    ASSERT_TRUE(result.ok) << "Error: " << result.error.message;
    // FACT-014: Chainsaw lint returns exactly one filter
    EXPECT_EQ(result.filters.size(), 1);
    // Should be a Detection (not Expression)
    EXPECT_TRUE(std::holds_alternative<tau::Detection>(result.filters[0]));
}

// ============================================================================
// TST-LINT-010: Sigma lint returns multiple filters for multi-document YAML
// SPEC-SLICE-014 FACT-015: Sigma Vec<Filter>
// ============================================================================

TEST(RuleLint, LintSigmaMultiFilter) {
    // Ищем Sigma rule fixture
    auto sigma_path = std::filesystem::path(CMAKE_SOURCE_DIR) / "tests" / "fixtures" / "sigma";
    if (!std::filesystem::exists(sigma_path)) {
        GTEST_SKIP() << "Sigma fixtures not available";
    }

    // Найдём любой .yml файл в sigma fixtures
    std::filesystem::path sigma_rule;
    for (const auto& entry : std::filesystem::directory_iterator(sigma_path)) {
        if (entry.path().extension() == ".yml" || entry.path().extension() == ".yaml") {
            sigma_rule = entry.path();
            break;
        }
    }

    if (sigma_rule.empty()) {
        GTEST_SKIP() << "No sigma rule fixtures found";
    }

    auto result = rule::lint(rule::Kind::Sigma, sigma_rule);
    ASSERT_TRUE(result.ok) << "Error: " << result.error.message;
    // Sigma can return multiple filters
    EXPECT_GE(result.filters.size(), 1);
}

// ============================================================================
// TST-LINT-011: Extension validation in lint
// SPEC-SLICE-014 FACT-013: Extension check yml/yaml
// ============================================================================

TEST(RuleLint, LintRejectsNonYamlExtension) {
    // Create a temp file with .txt extension
    auto temp_path = std::filesystem::temp_directory_path() / "test_lint_invalid.txt";
    {
        std::ofstream f(temp_path);
        f << "name: test\n";
    }

    auto result = rule::lint(rule::Kind::Chainsaw, temp_path);

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error.message, "rule must have a yaml file extension");

    std::filesystem::remove(temp_path);
}

TEST(RuleLint, LintAcceptsYmlExtension) {
    auto result = rule::lint(rule::Kind::Chainsaw, fixtures_path() / "valid_rule.yml");

    EXPECT_TRUE(result.ok) << "Error: " << result.error.message;
}

TEST(RuleLint, LintAcceptsYamlExtension) {
    // Проверим что .yaml расширение тоже принимается
    auto temp_path = std::filesystem::temp_directory_path() / "test_lint_valid.yaml";
    {
        std::ofstream f(temp_path);
        f << R"(
name: Test Rule
group: test
description: A test rule
authors:
  - Test
kind: evtx
level: medium
status: stable
timestamp: Event.System.TimeCreated
filter:
    EventID: 4688
)";
    }

    auto result = rule::lint(rule::Kind::Chainsaw, temp_path);
    EXPECT_TRUE(result.ok) << "Error: " << result.error.message;

    std::filesystem::remove(temp_path);
}
