// ==============================================================================
// test_tau_gtest.cpp - Unit-тесты для Tau Engine (SLICE-008)
// ==============================================================================
//
// SPEC-SLICE-008: Tau Engine micro-spec
// TST-TAU-001..022: тесты Expression IR, Solver, Parser, Optimiser
//
// ==============================================================================

#include <chainsaw/tau.hpp>
#include <chainsaw/value.hpp>
#include <gtest/gtest.h>
#include <string>

namespace tau = chainsaw::tau;
using chainsaw::Value;

// ============================================================================
// Helper: создание тестового документа
// ============================================================================

class TestDocument : public tau::Document {
public:
    TestDocument() : value_(Value::make_object()) {}

    explicit TestDocument(Value v) : value_(std::move(v)) {}

    void set(const std::string& key, Value val) { value_.set(key, std::move(val)); }

    std::optional<Value> find(std::string_view key) const override {
        // Поддержка dot-notation
        std::string_view remaining = key;
        const Value* current = &value_;

        while (!remaining.empty()) {
            auto dot_pos = remaining.find('.');
            std::string_view part;
            if (dot_pos == std::string_view::npos) {
                part = remaining;
                remaining = {};
            } else {
                part = remaining.substr(0, dot_pos);
                remaining = remaining.substr(dot_pos + 1);
            }

            if (!current->is_object()) {
                return std::nullopt;
            }

            std::string key_str(part);
            const Value* found = current->get(key_str);
            if (!found) {
                return std::nullopt;
            }

            if (remaining.empty()) {
                return *found;
            }

            current = found;
        }

        return std::nullopt;
    }

private:
    Value value_;
};

// ============================================================================
// TST-TAU-001: Expression::Field solve
// ============================================================================

TEST(TauSolver, TST_TAU_001_FieldExists) {
    TestDocument doc;
    doc.set("EventID", Value(std::int64_t(4688)));
    doc.set("CommandLine", Value(std::string("powershell.exe")));

    // Field exists -> true
    tau::Expression expr_exists(tau::ExprField{"EventID"});
    EXPECT_TRUE(tau::solve(expr_exists, doc));

    // Field missing -> false
    tau::Expression expr_missing(tau::ExprField{"NonExistent"});
    EXPECT_FALSE(tau::solve(expr_missing, doc));

    // Field is null -> false
    doc.set("NullField", Value::make_null());
    tau::Expression expr_null(tau::ExprField{"NullField"});
    EXPECT_FALSE(tau::solve(expr_null, doc));
}

// ============================================================================
// TST-TAU-002: Expression::BooleanGroup(And)
// ============================================================================

TEST(TauSolver, TST_TAU_002_BooleanGroupAnd) {
    TestDocument doc;
    doc.set("EventID", Value(std::int64_t(4688)));
    doc.set("CommandLine", Value(std::string("powershell.exe")));

    // All true -> true
    tau::ExprBooleanGroup group_and;
    group_and.op = tau::BoolSym::And;
    group_and.expressions.push_back(tau::Expression(tau::ExprField{"EventID"}));
    group_and.expressions.push_back(tau::Expression(tau::ExprField{"CommandLine"}));

    EXPECT_TRUE(tau::solve(tau::Expression(std::move(group_and)), doc));

    // One false -> false
    tau::ExprBooleanGroup group_and_fail;
    group_and_fail.op = tau::BoolSym::And;
    group_and_fail.expressions.push_back(tau::Expression(tau::ExprField{"EventID"}));
    group_and_fail.expressions.push_back(tau::Expression(tau::ExprField{"NonExistent"}));

    EXPECT_FALSE(tau::solve(tau::Expression(std::move(group_and_fail)), doc));
}

// ============================================================================
// TST-TAU-003: Expression::BooleanGroup(Or)
// ============================================================================

TEST(TauSolver, TST_TAU_003_BooleanGroupOr) {
    TestDocument doc;
    doc.set("EventID", Value(std::int64_t(4688)));

    // At least one true -> true
    tau::ExprBooleanGroup group_or;
    group_or.op = tau::BoolSym::Or;
    group_or.expressions.push_back(tau::Expression(tau::ExprField{"NonExistent"}));
    group_or.expressions.push_back(tau::Expression(tau::ExprField{"EventID"}));

    EXPECT_TRUE(tau::solve(tau::Expression(std::move(group_or)), doc));

    // All false -> false
    tau::ExprBooleanGroup group_or_fail;
    group_or_fail.op = tau::BoolSym::Or;
    group_or_fail.expressions.push_back(tau::Expression(tau::ExprField{"NonExistent1"}));
    group_or_fail.expressions.push_back(tau::Expression(tau::ExprField{"NonExistent2"}));

    EXPECT_FALSE(tau::solve(tau::Expression(std::move(group_or_fail)), doc));
}

// ============================================================================
// TST-TAU-004: Expression::Negate
// ============================================================================

TEST(TauSolver, TST_TAU_004_Negate) {
    TestDocument doc;
    doc.set("EventID", Value(std::int64_t(4688)));

    // NOT exists -> false
    tau::ExprNegate neg_exists;
    neg_exists.inner = std::make_unique<tau::Expression>(tau::ExprField{"EventID"});
    EXPECT_FALSE(tau::solve(tau::Expression(std::move(neg_exists)), doc));

    // NOT missing -> true
    tau::ExprNegate neg_missing;
    neg_missing.inner = std::make_unique<tau::Expression>(tau::ExprField{"NonExistent"});
    EXPECT_TRUE(tau::solve(tau::Expression(std::move(neg_missing)), doc));
}

// ============================================================================
// TST-TAU-005: Search::Exact case-sensitive
// ============================================================================

TEST(TauSolver, TST_TAU_005_SearchExact) {
    TestDocument doc;
    doc.set("CommandLine", Value(std::string("PowerShell.exe")));

    // Exact match
    tau::ExprSearch search_exact;
    search_exact.search = tau::SearchExact{"PowerShell.exe"};
    search_exact.field = "CommandLine";
    search_exact.cast_to_str = false;
    EXPECT_TRUE(tau::solve(tau::Expression(std::move(search_exact)), doc));

    // Case mismatch
    tau::ExprSearch search_case;
    search_case.search = tau::SearchExact{"powershell.exe"};
    search_case.field = "CommandLine";
    search_case.cast_to_str = false;
    EXPECT_FALSE(tau::solve(tau::Expression(std::move(search_case)), doc));
}

// ============================================================================
// TST-TAU-006: Search::AhoCorasick case-insensitive
// ============================================================================

TEST(TauSolver, TST_TAU_006_SearchAhoCorasickIgnoreCase) {
    TestDocument doc;
    doc.set("CommandLine", Value(std::string("PowerShell.exe")));

    // Case-insensitive exact match
    tau::ExprSearch search_ic;
    tau::SearchAhoCorasick ac;
    ac.match_types.push_back({tau::MatchType::Exact, "powershell.exe"});
    ac.ignore_case = true;
    search_ic.search = std::move(ac);
    search_ic.field = "CommandLine";
    search_ic.cast_to_str = false;

    EXPECT_TRUE(tau::solve(tau::Expression(std::move(search_ic)), doc));
}

// ============================================================================
// TST-TAU-007: Search::Contains
// ============================================================================

TEST(TauSolver, TST_TAU_007_SearchContains) {
    TestDocument doc;
    doc.set("CommandLine", Value(std::string("C:\\Windows\\PowerShell.exe -NoProfile")));

    // Contains match
    tau::ExprSearch search_contains;
    search_contains.search = tau::SearchContains{"PowerShell"};
    search_contains.field = "CommandLine";
    search_contains.cast_to_str = false;
    EXPECT_TRUE(tau::solve(tau::Expression(std::move(search_contains)), doc));

    // Contains no match
    tau::ExprSearch search_no;
    search_no.search = tau::SearchContains{"cmd.exe"};
    search_no.field = "CommandLine";
    search_no.cast_to_str = false;
    EXPECT_FALSE(tau::solve(tau::Expression(std::move(search_no)), doc));
}

// ============================================================================
// TST-TAU-008: Search::StartsWith
// ============================================================================

TEST(TauSolver, TST_TAU_008_SearchStartsWith) {
    TestDocument doc;
    doc.set("CommandLine", Value(std::string("C:\\Windows\\System32\\cmd.exe")));

    // StartsWith match
    tau::ExprSearch search_sw;
    search_sw.search = tau::SearchStartsWith{"C:\\Windows"};
    search_sw.field = "CommandLine";
    search_sw.cast_to_str = false;
    EXPECT_TRUE(tau::solve(tau::Expression(std::move(search_sw)), doc));

    // StartsWith no match
    tau::ExprSearch search_no;
    search_no.search = tau::SearchStartsWith{"D:\\"};
    search_no.field = "CommandLine";
    search_no.cast_to_str = false;
    EXPECT_FALSE(tau::solve(tau::Expression(std::move(search_no)), doc));
}

// ============================================================================
// TST-TAU-009: Search::EndsWith
// ============================================================================

TEST(TauSolver, TST_TAU_009_SearchEndsWith) {
    TestDocument doc;
    doc.set("CommandLine", Value(std::string("C:\\Windows\\System32\\cmd.exe")));

    // EndsWith match
    tau::ExprSearch search_ew;
    search_ew.search = tau::SearchEndsWith{"cmd.exe"};
    search_ew.field = "CommandLine";
    search_ew.cast_to_str = false;
    EXPECT_TRUE(tau::solve(tau::Expression(std::move(search_ew)), doc));

    // EndsWith no match
    tau::ExprSearch search_no;
    search_no.search = tau::SearchEndsWith{"powershell.exe"};
    search_no.field = "CommandLine";
    search_no.cast_to_str = false;
    EXPECT_FALSE(tau::solve(tau::Expression(std::move(search_no)), doc));
}

// ============================================================================
// TST-TAU-010: Search::Regex
// ============================================================================

TEST(TauSolver, TST_TAU_010_SearchRegex) {
    TestDocument doc;
    doc.set("CommandLine", Value(std::string("powershell.exe -encodedcommand ABC123")));

    // Regex match
    tau::ExprSearch search_rx;
    tau::SearchRegex srx;
    srx.pattern = "-encodedcommand\\s+\\w+";
    srx.regex = std::regex(srx.pattern, std::regex::ECMAScript);
    srx.ignore_case = false;
    search_rx.search = std::move(srx);
    search_rx.field = "CommandLine";
    search_rx.cast_to_str = false;
    EXPECT_TRUE(tau::solve(tau::Expression(std::move(search_rx)), doc));

    // Regex no match
    tau::ExprSearch search_no;
    tau::SearchRegex srx_no;
    srx_no.pattern = "\\d{10}";
    srx_no.regex = std::regex(srx_no.pattern, std::regex::ECMAScript);
    srx_no.ignore_case = false;
    search_no.search = std::move(srx_no);
    search_no.field = "CommandLine";
    search_no.cast_to_str = false;
    EXPECT_FALSE(tau::solve(tau::Expression(std::move(search_no)), doc));
}

// ============================================================================
// TST-TAU-011: BooleanExpression numeric
// ============================================================================

TEST(TauSolver, TST_TAU_011_BooleanExpressionNumeric) {
    TestDocument doc;
    doc.set("EventID", Value(std::int64_t(4688)));

    // Equal
    tau::ExprBooleanExpression be_eq;
    be_eq.left = std::make_unique<tau::Expression>(tau::ExprField{"EventID"});
    be_eq.op = tau::BoolSym::Equal;
    be_eq.right = std::make_unique<tau::Expression>(tau::ExprInteger{4688});
    EXPECT_TRUE(tau::solve(tau::Expression(std::move(be_eq)), doc));

    // GreaterThan
    tau::ExprBooleanExpression be_gt;
    be_gt.left = std::make_unique<tau::Expression>(tau::ExprField{"EventID"});
    be_gt.op = tau::BoolSym::GreaterThan;
    be_gt.right = std::make_unique<tau::Expression>(tau::ExprInteger{4000});
    EXPECT_TRUE(tau::solve(tau::Expression(std::move(be_gt)), doc));

    // LessThan
    tau::ExprBooleanExpression be_lt;
    be_lt.left = std::make_unique<tau::Expression>(tau::ExprField{"EventID"});
    be_lt.op = tau::BoolSym::LessThan;
    be_lt.right = std::make_unique<tau::Expression>(tau::ExprInteger{5000});
    EXPECT_TRUE(tau::solve(tau::Expression(std::move(be_lt)), doc));
}

// ============================================================================
// TST-TAU-012: Cast(Int)
// ============================================================================

TEST(TauSolver, TST_TAU_012_CastInt) {
    TestDocument doc;
    doc.set("StringNumber", Value(std::string("12345")));

    // Cast string to int and compare
    tau::ExprBooleanExpression be;
    be.left = std::make_unique<tau::Expression>(tau::ExprCast{"StringNumber", tau::ModSym::Int});
    be.op = tau::BoolSym::Equal;
    be.right = std::make_unique<tau::Expression>(tau::ExprInteger{12345});
    EXPECT_TRUE(tau::solve(tau::Expression(std::move(be)), doc));
}

// ============================================================================
// TST-TAU-013: Cast(Str)
// ============================================================================

TEST(TauSolver, TST_TAU_013_CastStr) {
    TestDocument doc;
    doc.set("IntValue", Value(std::int64_t(4688)));

    // Cast int to string and search
    tau::ExprSearch search;
    search.search = tau::SearchContains{"4688"};
    search.field = "IntValue";
    search.cast_to_str = true;
    EXPECT_TRUE(tau::solve(tau::Expression(std::move(search)), doc));
}

// ============================================================================
// TST-TAU-014: Nested object access
// ============================================================================

TEST(TauSolver, TST_TAU_014_NestedObjectAccess) {
    // Создаём вложенную структуру
    Value inner = Value::make_object();
    inner.set("SubField", Value(std::string("nested_value")));

    Value root = Value::make_object();
    root.set("Outer", std::move(inner));

    TestDocument doc(std::move(root));

    // Dot-notation access
    tau::ExprSearch search;
    search.search = tau::SearchExact{"nested_value"};
    search.field = "Outer.SubField";
    search.cast_to_str = false;
    EXPECT_TRUE(tau::solve(tau::Expression(std::move(search)), doc));
}

// ============================================================================
// TST-TAU-015: Array iteration
// ============================================================================

TEST(TauSolver, TST_TAU_015_ArrayIteration) {
    // Создаём документ с массивом
    Value arr = Value::make_array();
    arr.push_back(Value(std::string("value1")));
    arr.push_back(Value(std::string("target_value")));
    arr.push_back(Value(std::string("value3")));

    Value root = Value::make_object();
    root.set("ArrayField", std::move(arr));

    TestDocument doc(std::move(root));

    // Search should match any element
    tau::ExprSearch search;
    search.search = tau::SearchExact{"target_value"};
    search.field = "ArrayField";
    search.cast_to_str = false;
    EXPECT_TRUE(tau::solve(tau::Expression(std::move(search)), doc));

    // Search should not match non-existent element
    tau::ExprSearch search_no;
    search_no.search = tau::SearchExact{"non_existent"};
    search_no.field = "ArrayField";
    search_no.cast_to_str = false;
    EXPECT_FALSE(tau::solve(tau::Expression(std::move(search_no)), doc));
}

// ============================================================================
// TST-TAU-016: optimiser::coalesce
// ============================================================================

TEST(TauOptimiser, TST_TAU_016_Coalesce) {
    // Создаём identifiers map
    std::unordered_map<std::string, tau::Expression> identifiers;
    identifiers["selection"] = tau::Expression(tau::ExprField{"EventID"});

    // Expression с identifier
    tau::Expression expr(tau::ExprIdentifier{"selection"});

    // Coalesce должен заменить identifier на Expression
    tau::Expression resolved = tau::coalesce(tau::clone(expr), identifiers);

    // Проверяем что identifier был заменён
    EXPECT_TRUE(resolved.is_field());
    auto* field = resolved.get_field();
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->name, "EventID");
}

// ============================================================================
// TST-TAU-017: optimiser::shake
// ============================================================================

TEST(TauOptimiser, TST_TAU_017_Shake) {
    // NOT NOT x -> x
    tau::ExprNegate double_neg;
    tau::ExprNegate inner_neg;
    inner_neg.inner = std::make_unique<tau::Expression>(tau::ExprField{"EventID"});
    double_neg.inner = std::make_unique<tau::Expression>(std::move(inner_neg));

    tau::Expression shaken = tau::shake(tau::Expression(std::move(double_neg)));

    EXPECT_TRUE(shaken.is_field());
    auto* field = shaken.get_field();
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->name, "EventID");

    // true AND x -> x
    tau::ExprBooleanGroup group;
    group.op = tau::BoolSym::And;
    group.expressions.push_back(tau::Expression(tau::ExprBoolean{true}));
    group.expressions.push_back(tau::Expression(tau::ExprField{"Field"}));

    tau::Expression shaken2 = tau::shake(tau::Expression(std::move(group)));
    EXPECT_TRUE(shaken2.is_field());
}

// ============================================================================
// TST-TAU-018: optimiser::matrix (stub test)
// ============================================================================

TEST(TauOptimiser, TST_TAU_018_Matrix) {
    // Matrix optimization - для MVP просто проверяем что функция не падает
    tau::Expression expr(tau::ExprField{"EventID"});
    tau::Expression result = tau::matrix(tau::clone(expr));
    EXPECT_TRUE(result.is_field());
}

// ============================================================================
// TST-TAU-019: parse_kv with int()
// ============================================================================

TEST(TauParser, TST_TAU_019_ParseKvInt) {
    auto expr = tau::parse_kv("int(EventID): 4688");
    ASSERT_TRUE(expr.has_value());

    // Должно быть BooleanExpression
    auto* be = std::get_if<tau::ExprBooleanExpression>(&expr->data);
    ASSERT_NE(be, nullptr);
    EXPECT_EQ(be->op, tau::BoolSym::Equal);

    // Left should be Cast
    auto* cast = std::get_if<tau::ExprCast>(&be->left->data);
    ASSERT_NE(cast, nullptr);
    EXPECT_EQ(cast->field, "EventID");
    EXPECT_EQ(cast->mod, tau::ModSym::Int);

    // Right should be Integer
    auto* right_int = std::get_if<tau::ExprInteger>(&be->right->data);
    ASSERT_NE(right_int, nullptr);
    EXPECT_EQ(right_int->value, 4688);
}

// ============================================================================
// TST-TAU-020: parse_kv with not()
// ============================================================================

TEST(TauParser, TST_TAU_020_ParseKvNot) {
    auto expr = tau::parse_kv("not(User): SYSTEM");
    ASSERT_TRUE(expr.has_value());

    // Должно быть Negate
    auto* neg = std::get_if<tau::ExprNegate>(&expr->data);
    ASSERT_NE(neg, nullptr);

    // Inner should be Search
    auto* search = std::get_if<tau::ExprSearch>(&neg->inner->data);
    ASSERT_NE(search, nullptr);
    EXPECT_EQ(search->field, "User");
}

// ============================================================================
// TST-TAU-021: extract_fields
// ============================================================================

TEST(TauUtils, TST_TAU_021_ExtractFields) {
    // Создаём сложное выражение
    tau::ExprBooleanGroup group;
    group.op = tau::BoolSym::And;
    group.expressions.push_back(tau::Expression(tau::ExprField{"EventID"}));

    tau::ExprSearch search;
    search.search = tau::SearchContains{"powershell"};
    search.field = "CommandLine";
    search.cast_to_str = false;
    group.expressions.push_back(tau::Expression(std::move(search)));

    tau::Expression expr(std::move(group));

    auto fields = tau::extract_fields(expr);

    EXPECT_EQ(fields.size(), 2u);
    EXPECT_TRUE(fields.count("EventID") > 0);
    EXPECT_TRUE(fields.count("CommandLine") > 0);
}

// ============================================================================
// TST-TAU-022: update_fields
// ============================================================================

TEST(TauUtils, TST_TAU_022_UpdateFields) {
    // Создаём выражение
    tau::Expression expr(tau::ExprField{"OldFieldName"});

    // Создаём lookup
    std::unordered_map<std::string, std::string> lookup;
    lookup["OldFieldName"] = "NewFieldName";

    // Update
    tau::Expression updated = tau::update_fields(tau::clone(expr), lookup);

    EXPECT_TRUE(updated.is_field());
    auto* field = updated.get_field();
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->name, "NewFieldName");
}

// ============================================================================
// Дополнительные тесты
// ============================================================================

TEST(TauParser, ParseIdentifierString_Wildcard) {
    // *value* -> Contains
    auto result1 = tau::parse_identifier_string("*powershell*");
    ASSERT_TRUE(result1.has_value());
    EXPECT_FALSE(result1->ignore_case);
    auto* contains = std::get_if<tau::PatternContains>(&result1->pattern);
    ASSERT_NE(contains, nullptr);
    EXPECT_EQ(contains->value, "powershell");

    // i*value* -> Contains with ignore_case
    auto result2 = tau::parse_identifier_string("i*powershell*");
    ASSERT_TRUE(result2.has_value());
    EXPECT_TRUE(result2->ignore_case);

    // value* -> StartsWith
    auto result3 = tau::parse_identifier_string("cmd*");
    ASSERT_TRUE(result3.has_value());
    auto* starts = std::get_if<tau::PatternStartsWith>(&result3->pattern);
    ASSERT_NE(starts, nullptr);
    EXPECT_EQ(starts->value, "cmd");

    // *value -> EndsWith
    auto result4 = tau::parse_identifier_string("*.exe");
    ASSERT_TRUE(result4.has_value());
    auto* ends = std::get_if<tau::PatternEndsWith>(&result4->pattern);
    ASSERT_NE(ends, nullptr);
    EXPECT_EQ(ends->value, ".exe");
}

TEST(TauParser, ParseIdentifierString_Regex) {
    // ?regex
    auto result = tau::parse_identifier_string("?\\d+");
    ASSERT_TRUE(result.has_value());
    auto* regex = std::get_if<tau::PatternRegex>(&result->pattern);
    ASSERT_NE(regex, nullptr);
    EXPECT_EQ(regex->pattern, "\\d+");
}

TEST(TauParser, ParseNumeric) {
    // Simple integer
    auto result1 = tau::parse_numeric("4688");
    ASSERT_TRUE(result1.has_value());
    auto* eq = std::get_if<tau::PatternEqual>(&*result1);
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->value, 4688);

    // Greater than
    auto result2 = tau::parse_numeric(">100");
    ASSERT_TRUE(result2.has_value());
    auto* gt = std::get_if<tau::PatternGreaterThan>(&*result2);
    ASSERT_NE(gt, nullptr);
    EXPECT_EQ(gt->value, 100);

    // Less than or equal
    auto result3 = tau::parse_numeric("<=50");
    ASSERT_TRUE(result3.has_value());
    auto* lte = std::get_if<tau::PatternLessThanOrEqual>(&*result3);
    ASSERT_NE(lte, nullptr);
    EXPECT_EQ(lte->value, 50);
}

TEST(TauParser, ParseField) {
    // Simple field
    auto expr1 = tau::parse_field("EventID");
    EXPECT_TRUE(expr1.is_field());
    auto* f1 = expr1.get_field();
    ASSERT_NE(f1, nullptr);
    EXPECT_EQ(f1->name, "EventID");

    // Cast int
    auto expr2 = tau::parse_field("int(EventID)");
    auto* c2 = std::get_if<tau::ExprCast>(&expr2.data);
    ASSERT_NE(c2, nullptr);
    EXPECT_EQ(c2->field, "EventID");
    EXPECT_EQ(c2->mod, tau::ModSym::Int);

    // Cast str
    auto expr3 = tau::parse_field("str(Value)");
    auto* c3 = std::get_if<tau::ExprCast>(&expr3.data);
    ASSERT_NE(c3, nullptr);
    EXPECT_EQ(c3->field, "Value");
    EXPECT_EQ(c3->mod, tau::ModSym::Str);
}

TEST(TauSolver, Detection) {
    TestDocument doc;
    doc.set("EventID", Value(std::int64_t(4688)));

    // Создаём Detection с identifier
    tau::Detection detection;
    detection.expression = tau::Expression(tau::ExprIdentifier{"selection"});
    detection.identifiers["selection"] = tau::Expression(tau::ExprField{"EventID"});

    EXPECT_TRUE(tau::solve(detection, doc));
}

TEST(TauUtils, AsciiLowercase) {
    EXPECT_EQ(tau::ascii_lowercase("PowerShell"), "powershell");
    EXPECT_EQ(tau::ascii_lowercase("ABC123"), "abc123");
    EXPECT_EQ(tau::ascii_lowercase(""), "");
}

TEST(TauUtils, IEquals) {
    EXPECT_TRUE(tau::iequals("PowerShell", "powershell"));
    EXPECT_TRUE(tau::iequals("ABC", "abc"));
    EXPECT_FALSE(tau::iequals("abc", "abcd"));
}

TEST(TauUtils, IContains) {
    EXPECT_TRUE(tau::icontains("PowerShell.exe", "shell"));
    EXPECT_TRUE(tau::icontains("PowerShell.exe", "SHELL"));
    EXPECT_FALSE(tau::icontains("PowerShell.exe", "cmd"));
}

TEST(TauUtils, IStartsWith) {
    EXPECT_TRUE(tau::istarts_with("PowerShell.exe", "power"));
    EXPECT_TRUE(tau::istarts_with("PowerShell.exe", "POWER"));
    EXPECT_FALSE(tau::istarts_with("PowerShell.exe", "shell"));
}

TEST(TauUtils, IEndsWith) {
    EXPECT_TRUE(tau::iends_with("PowerShell.exe", ".EXE"));
    EXPECT_TRUE(tau::iends_with("PowerShell.exe", ".exe"));
    EXPECT_FALSE(tau::iends_with("PowerShell.exe", ".dll"));
}
