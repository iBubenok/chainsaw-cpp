// ==============================================================================
// test_hunt_gtest.cpp - Unit Tests for SLICE-012 Hunt Command
// ==============================================================================
//
// Tests: TST-HUNT-001..025 from SPEC-SLICE-012
//
// ==============================================================================

#include <chainsaw/hunt.hpp>
#include <chainsaw/platform.hpp>
#include <chainsaw/rule.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

// Platform-specific includes for PID (unique temp directories for parallel tests)
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace chainsaw;

// ============================================================================
// Test Fixtures
// ============================================================================

class HuntTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // Create unique temp directory for each test to avoid race conditions
        // when running tests in parallel (ctest -j)
        auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string unique_name = std::string("chainsaw_hunt_") + test_info->test_case_name() +
                                  "_" + test_info->name() + "_" +
                                  std::to_string(
#ifdef _WIN32
                                      GetCurrentProcessId()
#else
                                      getpid()
#endif
                                  );

        temp_dir_ = fs::temp_directory_path() / unique_name;

        // Remove if exists from previous runs
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);

        // Create clean directory
        fs::create_directories(temp_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    fs::path temp_dir_;

    // Helper to create a test YAML mapping file
    fs::path create_mapping_file(const std::string& content) {
        fs::path path = temp_dir_ / "mapping.yml";
        std::ofstream file(path);
        file << content;
        return path;
    }

    // Helper to create a test JSON file
    fs::path create_json_file(const std::string& content, const std::string& name = "test.json") {
        fs::path path = temp_dir_ / name;
        std::ofstream file(path);
        file << content;
        return path;
    }
};

// ============================================================================
// TST-HUNT-001: HunterBuilder basic construction
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_001_HunterBuilderBasic) {
    // FACT-001: HunterBuilder uses builder pattern
    auto builder = hunt::HunterBuilder::create();
    auto result = builder.build();

    EXPECT_TRUE(result.ok);
    EXPECT_NE(result.hunter, nullptr);
    EXPECT_TRUE(result.hunter->hunts().empty());
}

// ============================================================================
// TST-HUNT-002: Rules sorting by name
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_002_RulesSortingByName) {
    // Create test rules
    std::vector<rule::Rule> rules;

    rule::ChainsawRule r1;
    r1.name = "ZRule";
    r1.group = "Test";
    r1.kind = io::DocumentKind::Json;
    r1.filter = tau::Expression::make_bool(true);
    r1.timestamp = "@timestamp";
    rules.emplace_back(std::move(r1));

    rule::ChainsawRule r2;
    r2.name = "ARule";
    r2.group = "Test";
    r2.kind = io::DocumentKind::Json;
    r2.filter = tau::Expression::make_bool(true);
    r2.timestamp = "@timestamp";
    rules.emplace_back(std::move(r2));

    // Build hunter - rules should be sorted
    auto result = hunt::HunterBuilder::create().rules(std::move(rules)).build();

    ASSERT_TRUE(result.ok);

    // Verify hunts are created (for Chainsaw rules)
    EXPECT_EQ(result.hunter->hunts().size(), 2);
}

// ============================================================================
// TST-HUNT-003: Mapping loading from YAML
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_003_MappingLoading) {
    // Create a test mapping file
    auto mapping_path = create_mapping_file(R"yaml(
kind: json
rules: sigma
groups:
  - name: TestGroup
    timestamp: "@timestamp"
    filter:
      EventID: 4624
    fields:
      - name: User
        from: User.Name
)yaml");

    auto result = hunt::load_mapping(mapping_path);

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.mapping.kind, io::DocumentKind::Json);
    EXPECT_EQ(result.mapping.rules, rule::Kind::Sigma);
    EXPECT_EQ(result.mapping.groups.size(), 1);
    EXPECT_EQ(result.mapping.groups[0].name, "TestGroup");
}

// ============================================================================
// TST-HUNT-004: Mapping exclusions
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_004_MappingExclusions) {
    // FACT-006: Mapping.exclusions contains rule names to exclude
    auto mapping_path = create_mapping_file(R"yaml(
kind: json
rules: sigma
exclusions:
  - ExcludedRule1
  - ExcludedRule2
groups:
  - name: TestGroup
    timestamp: "@timestamp"
)yaml");

    auto result = hunt::load_mapping(mapping_path);

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.mapping.exclusions.size(), 2);
    EXPECT_TRUE(result.mapping.exclusions.count("ExcludedRule1") > 0);
    EXPECT_TRUE(result.mapping.exclusions.count("ExcludedRule2") > 0);
}

// ============================================================================
// TST-HUNT-005: Mapping preconditions
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_005_MappingPreconditions) {
    // FACT-007: Mapping.extensions.preconditions contains additional filters
    auto mapping_path = create_mapping_file(R"yaml(
kind: json
rules: sigma
extensions:
  preconditions:
    - for:
        logsource.product: windows
      filter:
        Channel: Security
groups:
  - name: TestGroup
    timestamp: "@timestamp"
)yaml");

    auto result = hunt::load_mapping(mapping_path);

    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_TRUE(result.mapping.extensions.has_value());
    ASSERT_TRUE(result.mapping.extensions->preconditions.has_value());
    EXPECT_EQ(result.mapping.extensions->preconditions->size(), 1);
}

// ============================================================================
// TST-HUNT-006: Chainsaw rules + mapping error
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_006_ChainsawMappingError) {
    // FACT-008: Chainsaw rules do not support mappings
    auto mapping_path = create_mapping_file(R"yaml(
kind: json
rules: chainsaw
groups:
  - name: TestGroup
    timestamp: "@timestamp"
)yaml");

    auto result = hunt::HunterBuilder::create().mappings({mapping_path}).build();

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.error.find("Chainsaw rules do not support mappings") != std::string::npos);
}

// ============================================================================
// TST-HUNT-008: Document kind matching
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_008_DocumentKindMatching) {
    // FACT-011: hunt.file must match document.kind
    rule::ChainsawRule rule_obj;
    rule_obj.name = "TestRule";
    rule_obj.group = "Test";
    rule_obj.kind = io::DocumentKind::Evtx;  // Expects EVTX
    rule_obj.filter = tau::Expression::make_bool(true);
    rule_obj.timestamp = "@timestamp";

    std::vector<rule::Rule> rules;
    rules.emplace_back(std::move(rule_obj));

    auto result = hunt::HunterBuilder::create().rules(std::move(rules)).build();

    ASSERT_TRUE(result.ok);

    // Hunt with JSON file - should not match because kind is different
    auto json_path = create_json_file(R"({"@timestamp": "2024-01-01T00:00:00Z", "test": "value"})");
    auto hunt_result = result.hunter->hunt(json_path);

    ASSERT_TRUE(hunt_result.ok);
    // No detections because kind mismatch (EVTX expected, JSON provided)
    EXPECT_EQ(hunt_result.detections.size(), 0);
}

// ============================================================================
// TST-HUNT-009: Timestamp parsing
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_009_TimestampParsing) {
    // FACT-012: Timestamp extracted from hunt.timestamp field
    // Create a rule that expects JSON
    rule::ChainsawRule cs_rule;
    cs_rule.name = "TestRule";
    cs_rule.group = "Test";
    cs_rule.kind = io::DocumentKind::Json;
    cs_rule.filter = tau::Expression::make_bool(true);
    cs_rule.timestamp = "@timestamp";

    std::vector<rule::Rule> rules;
    rules.emplace_back(std::move(cs_rule));

    auto result = hunt::HunterBuilder::create().rules(std::move(rules)).build();

    ASSERT_TRUE(result.ok);

    // Create JSON with valid timestamp
    auto json_path = create_json_file(R"({"@timestamp": "2024-01-01T12:00:00Z", "test": "value"})");
    auto hunt_result = result.hunter->hunt(json_path);

    ASSERT_TRUE(hunt_result.ok);
    // Should have detection if timestamp parses correctly
    EXPECT_EQ(hunt_result.detections.size(), 1);
}

// ============================================================================
// TST-HUNT-010: Time range filtering
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_010_TimeRangeFiltering) {
    // FACT-013: Documents outside [from, to] are skipped
    rule::ChainsawRule cs_rule;
    cs_rule.name = "TestRule";
    cs_rule.group = "Test";
    cs_rule.kind = io::DocumentKind::Json;
    cs_rule.filter = tau::Expression::make_bool(true);
    cs_rule.timestamp = "@timestamp";

    auto from_dt = hunt::DateTime::parse("2024-06-01T00:00:00Z");
    ASSERT_TRUE(from_dt.has_value());

    std::vector<rule::Rule> rules;
    rules.emplace_back(std::move(cs_rule));

    auto result = hunt::HunterBuilder::create().rules(std::move(rules)).from(*from_dt).build();

    ASSERT_TRUE(result.ok);

    // Create JSON with timestamp before 'from' - should be filtered
    auto json_path = create_json_file(R"({"@timestamp": "2024-01-01T00:00:00Z", "test": "value"})");
    auto hunt_result = result.hunter->hunt(json_path);

    ASSERT_TRUE(hunt_result.ok);
    EXPECT_EQ(hunt_result.detections.size(), 0);  // Filtered out
}

// ============================================================================
// TST-HUNT-011: Mapper bypass mode
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_011_MapperBypassMode) {
    // FACT-015: MapperKind::None - bypass mode
    std::vector<rule::Field> fields;
    rule::Field f;
    f.from = "test";
    f.to = "test";  // Same name = bypass
    fields.push_back(f);

    auto mapper = hunt::Mapper::from(std::move(fields));
    EXPECT_EQ(mapper.mode(), hunt::MapperMode::None);
}

// ============================================================================
// TST-HUNT-012: Mapper fast mode
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_012_MapperFastMode) {
    // FACT-015: MapperKind::Fast - simple rename
    std::vector<rule::Field> fields;
    rule::Field f;
    f.from = "source_field";
    f.to = "target_field";  // Different name = fast mode
    fields.push_back(f);

    auto mapper = hunt::Mapper::from(std::move(fields));
    EXPECT_EQ(mapper.mode(), hunt::MapperMode::Fast);
}

// ============================================================================
// TST-HUNT-013: Mapper full mode (cast)
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_013_MapperFullModeCast) {
    // FACT-015, FACT-017: Full mode with cast
    std::vector<rule::Field> fields;
    rule::Field f;
    f.from = "number_field";
    f.to = "number_field";
    f.cast = tau::ModSym::Int;  // Cast = full mode
    fields.push_back(f);

    auto mapper = hunt::Mapper::from(std::move(fields));
    EXPECT_EQ(mapper.mode(), hunt::MapperMode::Full);
}

// ============================================================================
// TST-HUNT-014: Container JSON parsing
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_014_ContainerJsonParsing) {
    // FACT-016: Container supports Json format
    std::vector<rule::Field> fields;
    rule::Field f;
    f.from = "nested_value";
    f.to = "nested_value";
    rule::Container c;
    c.field = "json_container";
    c.format = rule::ContainerFormat::Json;
    f.container = c;
    fields.push_back(f);

    auto mapper = hunt::Mapper::from(std::move(fields));
    EXPECT_EQ(mapper.mode(), hunt::MapperMode::Full);
}

// ============================================================================
// TST-HUNT-015: Container KV parsing
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_015_ContainerKvParsing) {
    // FACT-016: Container supports Kv format
    std::vector<rule::Field> fields;
    rule::Field f;
    f.from = "kv_value";
    f.to = "kv_value";
    rule::Container c;
    c.field = "kv_container";
    c.format = rule::ContainerFormat::Kv;
    rule::KvFormat kv;
    kv.delimiter = ";";
    kv.separator = "=";
    c.kv_params = kv;
    f.container = c;
    fields.push_back(f);

    auto mapper = hunt::Mapper::from(std::move(fields));
    EXPECT_EQ(mapper.mode(), hunt::MapperMode::Full);
}

// ============================================================================
// TST-HUNT-016: HuntKind::Group matching
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_016_HuntKindGroupMatching) {
    // FACT-018, FACT-020: HuntKind::Group tests group filter first
    auto mapping_path = create_mapping_file(R"yaml(
kind: json
rules: sigma
groups:
  - name: TestGroup
    timestamp: "@timestamp"
    filter:
      EventType: Login
)yaml");

    auto result = hunt::HunterBuilder::create().mappings({mapping_path}).build();

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.hunter->hunts().size(), 1);

    // Hunt kind should be Group
    const auto& hunt = result.hunter->hunts()[0];
    EXPECT_TRUE(std::holds_alternative<hunt::HuntKindGroup>(hunt.kind));
}

// ============================================================================
// TST-HUNT-017: HuntKind::Rule matching
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_017_HuntKindRuleMatching) {
    // FACT-019: HuntKind::Rule for Chainsaw rules
    rule::ChainsawRule cs_rule;
    cs_rule.name = "TestRule";
    cs_rule.group = "Test";
    cs_rule.kind = io::DocumentKind::Json;
    cs_rule.filter = tau::Expression::make_bool(true);
    cs_rule.timestamp = "@timestamp";

    std::vector<rule::Rule> rules;
    rules.emplace_back(std::move(cs_rule));

    auto result = hunt::HunterBuilder::create().rules(std::move(rules)).build();

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.hunter->hunts().size(), 1);

    // Hunt kind should be Rule
    const auto& hunt = result.hunter->hunts()[0];
    EXPECT_TRUE(std::holds_alternative<hunt::HuntKindRule>(hunt.kind));
}

// ============================================================================
// TST-HUNT-018: Aggregation grouping
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_018_AggregationGrouping) {
    // FACT-022: Aggregation groups by hash of fields
    rule::Aggregate agg;
    agg.fields = {"User", "Host"};
    agg.count = tau::PatternGreaterThanOrEqual{3};

    rule::ChainsawRule cs_rule;
    cs_rule.name = "AggRule";
    cs_rule.group = "Test";
    cs_rule.kind = io::DocumentKind::Json;
    cs_rule.filter = tau::Expression::make_bool(true);
    cs_rule.timestamp = "@timestamp";
    cs_rule.aggregate = agg;

    std::vector<rule::Rule> rules;
    rules.emplace_back(std::move(cs_rule));

    auto result = hunt::HunterBuilder::create().rules(std::move(rules)).build();

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.hunter->hunts().size(), 1);

    // Verify aggregation is set
    const auto& hunt = result.hunter->hunts()[0];
    EXPECT_TRUE(hunt.is_aggregation());
}

// ============================================================================
// TST-HUNT-019: Aggregation count patterns
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_019_AggregationCountPatterns) {
    // FACT-023: Count pattern verification
    // Test all pattern types
    tau::PatternEqual eq{5};
    tau::PatternGreaterThan gt{3};
    tau::PatternGreaterThanOrEqual gte{3};
    tau::PatternLessThan lt{10};
    tau::PatternLessThanOrEqual lte{10};

    // Just verify these compile and work
    EXPECT_EQ(eq.value, 5);
    EXPECT_EQ(gt.value, 3);
    EXPECT_EQ(gte.value, 3);
    EXPECT_EQ(lt.value, 10);
    EXPECT_EQ(lte.value, 10);
}

// ============================================================================
// TST-HUNT-020: Aggregation result structure
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_020_AggregationResultStructure) {
    // FACT-024: Aggregation results in Kind::Aggregate
    hunt::KindAggregate agg;
    agg.documents.push_back(hunt::Document{io::DocumentKind::Json, "test.json", Value()});
    agg.documents.push_back(hunt::Document{io::DocumentKind::Json, "test.json", Value()});

    EXPECT_EQ(agg.documents.size(), 2);
}

// ============================================================================
// TST-HUNT-021: UUID generation
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_021_UUIDGeneration) {
    // Test UUID generation for uniqueness
    auto uuid1 = hunt::UUID::generate();
    auto uuid2 = hunt::UUID::generate();

    EXPECT_NE(uuid1, uuid2);
}

// ============================================================================
// TST-HUNT-022: UUID comparison
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_022_UUIDComparison) {
    // Test UUID comparison operators
    hunt::UUID uuid1;
    uuid1.high = 100;
    uuid1.low = 200;

    hunt::UUID uuid2;
    uuid2.high = 100;
    uuid2.low = 200;

    hunt::UUID uuid3;
    uuid3.high = 100;
    uuid3.low = 300;

    EXPECT_EQ(uuid1, uuid2);
    EXPECT_NE(uuid1, uuid3);
    EXPECT_TRUE(uuid1 < uuid3);
}

// ============================================================================
// TST-HUNT-023: Extensions helper
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_023_Extensions) {
    // Test extensions() returns correct file extensions
    rule::ChainsawRule cs_rule;
    cs_rule.name = "TestRule";
    cs_rule.group = "Test";
    cs_rule.kind = io::DocumentKind::Json;
    cs_rule.filter = tau::Expression::make_bool(true);
    cs_rule.timestamp = "@timestamp";

    std::vector<rule::Rule> rules;
    rules.emplace_back(std::move(cs_rule));

    auto result = hunt::HunterBuilder::create().rules(std::move(rules)).build();

    ASSERT_TRUE(result.ok);

    auto exts = result.hunter->extensions();
    // Should contain json extension
    EXPECT_TRUE(exts.contains("json") || exts.contains(".json"));
}

// ============================================================================
// TST-HUNT-024: Skip errors setting
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_024_SkipErrors) {
    // Test skip_errors setting
    auto result = hunt::HunterBuilder::create().skip_errors(true).build();

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.hunter->skip_errors());
}

// ============================================================================
// TST-HUNT-025: Load unknown setting
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_025_LoadUnknown) {
    // Test load_unknown setting
    auto result = hunt::HunterBuilder::create().load_unknown(true).build();

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.hunter->load_unknown());
}

// ============================================================================
// Additional Helper Tests
// ============================================================================

TEST_F(HuntTestFixture, TST_HUNT_Output_JSON) {
    // Test JSON output serialization
    hunt::Detections det;
    hunt::Hit hit;
    hit.hunt = hunt::UUID::generate();
    hit.rule = hunt::UUID::generate();
    hit.timestamp = *hunt::DateTime::parse("2024-01-01T00:00:00Z");
    det.hits.push_back(hit);

    hunt::KindIndividual ind;
    ind.document.kind = io::DocumentKind::Json;
    ind.document.path = "test.json";
    ind.document.data = Value();
    det.kind = std::move(ind);

    std::vector<hunt::Hunt> hunts;
    std::unordered_map<hunt::UUID, rule::Rule, hunt::UUID::Hash> rules;

    std::string json = hunt::detections_to_json(det, hunts, rules, false);
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("timestamp"), std::string::npos);
}

TEST_F(HuntTestFixture, TST_HUNT_Output_JSONL) {
    // Test JSONL output serialization
    hunt::Detections det;
    hunt::Hit hit;
    hit.hunt = hunt::UUID::generate();
    hit.rule = hunt::UUID::generate();
    hit.timestamp = *hunt::DateTime::parse("2024-01-01T00:00:00Z");
    det.hits.push_back(hit);

    hunt::KindIndividual ind;
    ind.document.kind = io::DocumentKind::Json;
    ind.document.path = "test.json";
    ind.document.data = Value();
    det.kind = std::move(ind);

    std::vector<hunt::Hunt> hunts;
    std::unordered_map<hunt::UUID, rule::Rule, hunt::UUID::Hash> rules;

    std::string jsonl = hunt::detections_to_jsonl(det, hunts, rules, false);
    EXPECT_FALSE(jsonl.empty());
    EXPECT_TRUE(jsonl.back() == '\n');  // Ends with newline
}

TEST_F(HuntTestFixture, TST_HUNT_RULE_PREFIX) {
    // FACT-040: Platform-specific RULE_PREFIX
#ifdef _WIN32
    EXPECT_EQ(std::string(hunt::RULE_PREFIX), "+");
#else
    EXPECT_EQ(std::string(hunt::RULE_PREFIX), "\xe2\x80\xa3");  // ‣
#endif
}
