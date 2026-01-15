// ==============================================================================
// test_search_gtest.cpp - Unit-тесты для Search Command (SLICE-011)
// ==============================================================================
//
// SPEC-SLICE-011: Search Command micro-spec
// TST-SEARCH-001..016: тесты Searcher, DateTime, pattern matching
//
// ==============================================================================

#include <chainsaw/reader.hpp>
#include <chainsaw/search.hpp>
#include <chainsaw/value.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace search = chainsaw::search;
using chainsaw::Value;
using chainsaw::io::Document;
using chainsaw::io::DocumentKind;

// ============================================================================
// Helper: создание тестового документа
// ============================================================================

Document make_test_doc(Value val) {
    Document doc;
    doc.kind = DocumentKind::Json;
    doc.data = std::move(val);
    doc.source = "test.json";
    return doc;
}

Value make_obj(std::initializer_list<std::pair<std::string, Value>> pairs) {
    Value::Object obj;
    for (const auto& p : pairs) {
        obj[p.first] = p.second;
    }
    return Value(std::move(obj));
}

// ============================================================================
// TST-SEARCH-001: DateTime parsing
// ============================================================================

TEST(SearchDateTime, TST_SEARCH_001_ParseISO8601) {
    // Basic ISO 8601 format
    auto dt1 = search::DateTime::parse("2024-01-15T10:30:45Z");
    ASSERT_TRUE(dt1.has_value());
    EXPECT_EQ(dt1->year, 2024);
    EXPECT_EQ(dt1->month, 1);
    EXPECT_EQ(dt1->day, 15);
    EXPECT_EQ(dt1->hour, 10);
    EXPECT_EQ(dt1->minute, 30);
    EXPECT_EQ(dt1->second, 45);
    EXPECT_EQ(dt1->microsecond, 0);

    // With microseconds
    auto dt2 = search::DateTime::parse("2024-06-30T23:59:59.123456Z");
    ASSERT_TRUE(dt2.has_value());
    EXPECT_EQ(dt2->year, 2024);
    EXPECT_EQ(dt2->month, 6);
    EXPECT_EQ(dt2->day, 30);
    EXPECT_EQ(dt2->hour, 23);
    EXPECT_EQ(dt2->minute, 59);
    EXPECT_EQ(dt2->second, 59);
    EXPECT_EQ(dt2->microsecond, 123456);

    // Without Z suffix
    auto dt3 = search::DateTime::parse("2024-12-25T00:00:00");
    ASSERT_TRUE(dt3.has_value());
    EXPECT_EQ(dt3->year, 2024);
    EXPECT_EQ(dt3->month, 12);
    EXPECT_EQ(dt3->day, 25);
}

TEST(SearchDateTime, TST_SEARCH_002_ParseInvalid) {
    // Too short
    EXPECT_FALSE(search::DateTime::parse("2024-01-15").has_value());

    // Invalid month
    EXPECT_FALSE(search::DateTime::parse("2024-13-15T10:30:45Z").has_value());

    // Invalid day
    EXPECT_FALSE(search::DateTime::parse("2024-01-32T10:30:45Z").has_value());

    // Invalid hour
    EXPECT_FALSE(search::DateTime::parse("2024-01-15T25:30:45Z").has_value());

    // Invalid separator
    EXPECT_FALSE(search::DateTime::parse("2024/01/15T10:30:45Z").has_value());
}

TEST(SearchDateTime, TST_SEARCH_003_Comparison) {
    auto dt1 = search::DateTime::parse("2024-01-01T00:00:00Z");
    auto dt2 = search::DateTime::parse("2024-01-02T00:00:00Z");
    auto dt3 = search::DateTime::parse("2024-01-01T00:00:00Z");

    ASSERT_TRUE(dt1.has_value() && dt2.has_value() && dt3.has_value());

    EXPECT_TRUE(*dt1 < *dt2);
    EXPECT_FALSE(*dt2 < *dt1);
    EXPECT_TRUE(*dt1 <= *dt2);
    EXPECT_TRUE(*dt1 <= *dt3);
    EXPECT_TRUE(*dt1 == *dt3);
    EXPECT_FALSE(*dt1 == *dt2);
    EXPECT_TRUE(*dt2 > *dt1);
    EXPECT_TRUE(*dt2 >= *dt1);
}

TEST(SearchDateTime, TST_SEARCH_004_ToString) {
    auto dt = search::DateTime::parse("2024-03-20T14:45:30.123000Z");
    ASSERT_TRUE(dt.has_value());

    std::string s = dt->to_string();
    EXPECT_EQ(s, "2024-03-20T14:45:30.123000Z");

    auto dt2 = search::DateTime::parse("2024-01-01T00:00:00Z");
    ASSERT_TRUE(dt2.has_value());
    EXPECT_EQ(dt2->to_string(), "2024-01-01T00:00:00Z");
}

// ============================================================================
// TST-SEARCH-005: SearcherBuilder basic
// ============================================================================

TEST(SearcherBuilder, TST_SEARCH_005_BasicBuild) {
    auto result = search::SearcherBuilder::create().patterns({"test"}).build();

    EXPECT_TRUE(result.ok);
    EXPECT_NE(result.searcher, nullptr);
    EXPECT_TRUE(result.searcher->has_patterns());
    EXPECT_FALSE(result.searcher->has_tau());
    EXPECT_FALSE(result.searcher->has_time_filter());
}

TEST(SearcherBuilder, TST_SEARCH_006_EmptyPatterns) {
    auto result = search::SearcherBuilder::create().build();

    EXPECT_TRUE(result.ok);
    EXPECT_NE(result.searcher, nullptr);
    EXPECT_FALSE(result.searcher->has_patterns());
}

TEST(SearcherBuilder, TST_SEARCH_007_InvalidRegex) {
    auto result = search::SearcherBuilder::create()
                      .patterns({"[invalid"})  // unclosed bracket
                      .build();

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.error.find("invalid regex") != std::string::npos);
}

// ============================================================================
// TST-SEARCH-008: Pattern matching
// ============================================================================

TEST(SearchPatternMatching, TST_SEARCH_008_SimpleMatch) {
    auto result = search::SearcherBuilder::create().patterns({"powershell"}).build();
    ASSERT_TRUE(result.ok);

    auto doc1 = make_test_doc(
        make_obj({{"CommandLine", Value(std::string("powershell.exe -Command ..."))}}));

    auto doc2 = make_test_doc(make_obj({{"CommandLine", Value(std::string("cmd.exe"))}}));

    EXPECT_TRUE(result.searcher->matches(doc1));
    EXPECT_FALSE(result.searcher->matches(doc2));
}

TEST(SearchPatternMatching, TST_SEARCH_009_CaseInsensitive) {
    auto result =
        search::SearcherBuilder::create().patterns({"POWERSHELL"}).ignore_case(true).build();
    ASSERT_TRUE(result.ok);

    auto doc = make_test_doc(make_obj({{"CommandLine", Value(std::string("PowerShell.exe"))}}));

    EXPECT_TRUE(result.searcher->matches(doc));
}

TEST(SearchPatternMatching, TST_SEARCH_010_CaseSensitive) {
    auto result =
        search::SearcherBuilder::create().patterns({"POWERSHELL"}).ignore_case(false).build();
    ASSERT_TRUE(result.ok);

    auto doc = make_test_doc(make_obj({{"CommandLine", Value(std::string("PowerShell.exe"))}}));

    EXPECT_FALSE(result.searcher->matches(doc));
}

TEST(SearchPatternMatching, TST_SEARCH_011_MultipleAnd) {
    auto result = search::SearcherBuilder::create()
                      .patterns({"powershell", "encoded"})
                      .match_any(false)  // AND semantics
                      .ignore_case(true)
                      .build();
    ASSERT_TRUE(result.ok);

    auto doc1 = make_test_doc(
        make_obj({{"CommandLine", Value(std::string("powershell -EncodedCommand ..."))}}));

    auto doc2 = make_test_doc(make_obj({{"CommandLine", Value(std::string("powershell.exe"))}}));

    EXPECT_TRUE(result.searcher->matches(doc1));   // Contains both
    EXPECT_FALSE(result.searcher->matches(doc2));  // Missing "encoded"
}

TEST(SearchPatternMatching, TST_SEARCH_012_MultipleOr) {
    auto result = search::SearcherBuilder::create()
                      .patterns({"powershell", "cmd"})
                      .match_any(true)  // OR semantics
                      .ignore_case(true)
                      .build();
    ASSERT_TRUE(result.ok);

    auto doc1 = make_test_doc(make_obj({{"CommandLine", Value(std::string("powershell.exe"))}}));

    auto doc2 = make_test_doc(make_obj({{"CommandLine", Value(std::string("cmd.exe"))}}));

    auto doc3 = make_test_doc(make_obj({{"CommandLine", Value(std::string("notepad.exe"))}}));

    EXPECT_TRUE(result.searcher->matches(doc1));
    EXPECT_TRUE(result.searcher->matches(doc2));
    EXPECT_FALSE(result.searcher->matches(doc3));
}

// ============================================================================
// TST-SEARCH-013: Time filtering
// ============================================================================

TEST(SearchTimeFilter, TST_SEARCH_013_FromFilter) {
    auto from_dt = search::DateTime::parse("2024-01-15T00:00:00Z");
    ASSERT_TRUE(from_dt.has_value());

    auto result = search::SearcherBuilder::create().timestamp("timestamp").from(*from_dt).build();
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.searcher->has_time_filter());

    // Document before --from: excluded
    auto doc_before =
        make_test_doc(make_obj({{"timestamp", Value(std::string("2024-01-14T23:59:59Z"))}}));
    EXPECT_FALSE(result.searcher->matches(doc_before));

    // Document after --from: included
    auto doc_after =
        make_test_doc(make_obj({{"timestamp", Value(std::string("2024-01-15T00:00:01Z"))}}));
    EXPECT_TRUE(result.searcher->matches(doc_after));
}

TEST(SearchTimeFilter, TST_SEARCH_014_ToFilter) {
    auto to_dt = search::DateTime::parse("2024-01-15T23:59:59Z");
    ASSERT_TRUE(to_dt.has_value());

    auto result = search::SearcherBuilder::create().timestamp("timestamp").to(*to_dt).build();
    ASSERT_TRUE(result.ok);

    // Document before --to: included
    auto doc_before =
        make_test_doc(make_obj({{"timestamp", Value(std::string("2024-01-15T12:00:00Z"))}}));
    EXPECT_TRUE(result.searcher->matches(doc_before));

    // Document after --to: excluded
    auto doc_after =
        make_test_doc(make_obj({{"timestamp", Value(std::string("2024-01-16T00:00:00Z"))}}));
    EXPECT_FALSE(result.searcher->matches(doc_after));
}

TEST(SearchTimeFilter, TST_SEARCH_015_RangeFilter) {
    auto from_dt = search::DateTime::parse("2024-01-15T00:00:00Z");
    auto to_dt = search::DateTime::parse("2024-01-15T23:59:59Z");
    ASSERT_TRUE(from_dt.has_value() && to_dt.has_value());

    auto result =
        search::SearcherBuilder::create().timestamp("timestamp").from(*from_dt).to(*to_dt).build();
    ASSERT_TRUE(result.ok);

    // Document within range: included
    auto doc_inside =
        make_test_doc(make_obj({{"timestamp", Value(std::string("2024-01-15T12:00:00Z"))}}));
    EXPECT_TRUE(result.searcher->matches(doc_inside));

    // Document outside range: excluded
    auto doc_before =
        make_test_doc(make_obj({{"timestamp", Value(std::string("2024-01-14T12:00:00Z"))}}));
    auto doc_after =
        make_test_doc(make_obj({{"timestamp", Value(std::string("2024-01-16T12:00:00Z"))}}));
    EXPECT_FALSE(result.searcher->matches(doc_before));
    EXPECT_FALSE(result.searcher->matches(doc_after));
}

TEST(SearchTimeFilter, TST_SEARCH_016_NestedTimestamp) {
    auto from_dt = search::DateTime::parse("2024-01-01T00:00:00Z");
    ASSERT_TRUE(from_dt.has_value());

    auto result = search::SearcherBuilder::create()
                      .timestamp("Event.System.TimeCreated")
                      .from(*from_dt)
                      .build();
    ASSERT_TRUE(result.ok);

    // Nested timestamp field
    auto doc = make_test_doc(make_obj(
        {{"Event",
          make_obj({{"System",
                     make_obj({{"TimeCreated", Value(std::string("2024-06-15T10:00:00Z"))}})}})}}));
    EXPECT_TRUE(result.searcher->matches(doc));
}

// ============================================================================
// TST-SEARCH-017: extract_timestamp
// ============================================================================

TEST(SearchHelpers, TST_SEARCH_017_ExtractTimestamp) {
    auto value = make_obj({{"timestamp", Value(std::string("2024-03-20T14:30:00Z"))}});

    auto ts = search::extract_timestamp(value, "timestamp");
    ASSERT_TRUE(ts.has_value());
    EXPECT_EQ(ts->year, 2024);
    EXPECT_EQ(ts->month, 3);
    EXPECT_EQ(ts->day, 20);
}

TEST(SearchHelpers, TST_SEARCH_018_ExtractTimestampNested) {
    auto value = make_obj(
        {{"Event",
          make_obj({{"System",
                     make_obj({{"TimeCreated", Value(std::string("2024-12-25T00:00:00Z"))}})}})}});

    auto ts = search::extract_timestamp(value, "Event.System.TimeCreated");
    ASSERT_TRUE(ts.has_value());
    EXPECT_EQ(ts->year, 2024);
    EXPECT_EQ(ts->month, 12);
    EXPECT_EQ(ts->day, 25);
}

TEST(SearchHelpers, TST_SEARCH_019_ExtractTimestampMissing) {
    auto value = make_obj({{"other_field", Value(std::string("not a timestamp"))}});

    auto ts = search::extract_timestamp(value, "timestamp");
    EXPECT_FALSE(ts.has_value());
}

// ============================================================================
// TST-SEARCH-020: normalize_json_for_search
// ============================================================================

TEST(SearchHelpers, TST_SEARCH_020_NormalizeJson) {
    auto value = make_obj({{"path", Value(std::string("C:\\Windows\\System32"))}});

    std::string normalized = search::normalize_json_for_search(value);
    // Should contain the escaped backslash
    EXPECT_TRUE(normalized.find("C:\\\\Windows\\\\System32") != std::string::npos);
}

// ============================================================================
// TST-SEARCH-021: Combined pattern + time filter
// ============================================================================

TEST(SearchCombined, TST_SEARCH_021_PatternAndTimeFilter) {
    auto from_dt = search::DateTime::parse("2024-01-01T00:00:00Z");
    ASSERT_TRUE(from_dt.has_value());

    auto result = search::SearcherBuilder::create()
                      .patterns({"powershell"})
                      .ignore_case(true)
                      .timestamp("timestamp")
                      .from(*from_dt)
                      .build();
    ASSERT_TRUE(result.ok);

    // Matches pattern and time
    auto doc1 =
        make_test_doc(make_obj({{"CommandLine", Value(std::string("powershell.exe"))},
                                {"timestamp", Value(std::string("2024-06-15T10:00:00Z"))}}));
    EXPECT_TRUE(result.searcher->matches(doc1));

    // Matches pattern but not time
    auto doc2 =
        make_test_doc(make_obj({{"CommandLine", Value(std::string("powershell.exe"))},
                                {"timestamp", Value(std::string("2023-12-31T23:59:59Z"))}}));
    EXPECT_FALSE(result.searcher->matches(doc2));

    // Matches time but not pattern
    auto doc3 =
        make_test_doc(make_obj({{"CommandLine", Value(std::string("cmd.exe"))},
                                {"timestamp", Value(std::string("2024-06-15T10:00:00Z"))}}));
    EXPECT_FALSE(result.searcher->matches(doc3));
}
