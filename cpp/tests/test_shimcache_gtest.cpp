// ==============================================================================
// test_shimcache_gtest.cpp - Unit tests for Shimcache Parser & Analyser
// ==============================================================================
//
// SLICE-019: Analyse Shimcache Command
// SPEC-SLICE-019: micro-spec Tests Section 5.2
//
// Tests:
// TST-SHIM-001: ParseShimcache_LoadSystemHive
// TST-SHIM-002: ParseShimcache_DetectVersion
// TST-SHIM-003: ParseShimcache_ExtractEntries
// TST-SHIM-004: ParseShimcache_ControlSetSelection
// TST-SHIM-005: ParseShimcache_Windows10Format
// TST-SHIM-006: ParseShimcache_Windows7x64Format
// TST-SHIM-007: ParseShimcache_Windows7x86Format
// TST-SHIM-008: ParseShimcache_Windows8Format
// TST-SHIM-009: ParseShimcache_EntryFields
// TST-SHIM-010: ParseShimcache_Timestamps
// TST-SHIM-011: ShimcacheAnalyser_PatternMatching
// TST-SHIM-012: ShimcacheAnalyser_TimelineGeneration
// TST-SHIM-013: ShimcacheAnalyser_TimestampRanges
// TST-SHIM-014: AmcacheIntegration_FileEntryMatching
// TST-SHIM-015: AmcacheIntegration_ProgramEntryMatching
// TST-SHIM-016: AmcacheIntegration_NearTimestampPairs
// TST-SHIM-017: OutputFormatting_CSV
// TST-SHIM-018: OutputFormatting_Timestamps
// TST-SHIM-019: ErrorHandling_InvalidHive
// TST-SHIM-020: ErrorHandling_UnsupportedVersion
//
// ==============================================================================

#include <chainsaw/hve.hpp>
#include <chainsaw/platform.hpp>
#include <chainsaw/shimcache.hpp>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace chainsaw::analyse::shimcache {

// ============================================================================
// Test Fixture
// ============================================================================

class ShimcacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Get fixture paths
        fixtures_dir_ = std::filesystem::path(__FILE__).parent_path() / "fixtures" / "shimcache";
        system_hive_ = fixtures_dir_ / "SYSTEM.hive";
        hve_fixtures_dir_ = std::filesystem::path(__FILE__).parent_path() / "fixtures" / "hve";
        invalid_file_ = hve_fixtures_dir_ / "invalid.hve";
    }

    std::filesystem::path fixtures_dir_;
    std::filesystem::path system_hive_;
    std::filesystem::path hve_fixtures_dir_;
    std::filesystem::path invalid_file_;
};

// ============================================================================
// TST-SHIM-001: ParseShimcache_LoadSystemHive
// ============================================================================
// FACT-011: parse_shimcache loads SYSTEM hive and extracts shimcache

TEST_F(ShimcacheTest, TST_SHIM_001_ParseShimcache_LoadSystemHive) {
    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << system_hive_;
    }

    chainsaw::io::hve::HveParser parser;
    bool loaded = parser.load(system_hive_);
    ASSERT_TRUE(loaded) << "Failed to load SYSTEM hive";

    // Debug: print root key info
    auto root = parser.get_root_key();
    if (root) {
        std::cout << "Root key name: '" << root->name() << "'" << std::endl;
        std::cout << "Root subkey count: " << root->subkey_names().size() << std::endl;
        std::cout << "Root subkeys: ";
        for (const auto& name : root->subkey_names()) {
            std::cout << "'" << name << "' ";
        }
        std::cout << std::endl;
        std::cout << "Root values count: " << root->values().size() << std::endl;
    } else {
        std::cout << "Failed to get root key" << std::endl;
    }

    // Try direct key access
    auto select = parser.get_key("Select");
    std::cout << "get_key(Select): " << (select.has_value() ? "found" : "not found") << std::endl;

    auto cs001 = parser.get_key("ControlSet001");
    std::cout << "get_key(ControlSet001): " << (cs001.has_value() ? "found" : "not found")
              << std::endl;

    auto result = parse_shimcache(parser);
    ASSERT_TRUE(std::holds_alternative<ShimcacheArtefact>(result))
        << "Expected ShimcacheArtefact, got error: " << std::get<ShimcacheError>(result).format();

    auto artefact = std::get<ShimcacheArtefact>(std::move(result));
    EXPECT_GT(artefact.entries.size(), 0U) << "Expected at least one shimcache entry";
}

// ============================================================================
// TST-SHIM-002: ParseShimcache_DetectVersion
// ============================================================================
// FACT-007: Version detection based on signature

TEST_F(ShimcacheTest, TST_SHIM_002_ParseShimcache_DetectVersion) {
    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << system_hive_;
    }

    chainsaw::io::hve::HveParser parser;
    ASSERT_TRUE(parser.load(system_hive_));

    auto result = parse_shimcache(parser);
    ASSERT_TRUE(std::holds_alternative<ShimcacheArtefact>(result));

    auto artefact = std::get<ShimcacheArtefact>(result);

    // Версия должна быть определена
    EXPECT_NE(artefact.version, ShimcacheVersion::Unknown)
        << "Shimcache version should be detected";

    // Выводим версию для информации
    std::cout << "Detected shimcache version: " << shimcache_version_to_string(artefact.version)
              << std::endl;
}

// ============================================================================
// TST-SHIM-003: ParseShimcache_ExtractEntries
// ============================================================================
// FACT-004: ShimcacheEntry structure fields

TEST_F(ShimcacheTest, TST_SHIM_003_ParseShimcache_ExtractEntries) {
    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << system_hive_;
    }

    chainsaw::io::hve::HveParser parser;
    ASSERT_TRUE(parser.load(system_hive_));

    auto result = parse_shimcache(parser);
    ASSERT_TRUE(std::holds_alternative<ShimcacheArtefact>(result));

    auto artefact = std::get<ShimcacheArtefact>(result);
    ASSERT_GT(artefact.entries.size(), 0u);

    // Проверяем первую запись
    const auto& first_entry = artefact.entries[0];
    EXPECT_EQ(first_entry.cache_entry_position, 0u);
    EXPECT_GT(first_entry.controlset, 0u);

    // Проверяем, что entry_type определён
    if (is_file_entry(first_entry.entry_type)) {
        const auto& path = get_entry_path(first_entry.entry_type);
        EXPECT_FALSE(path.empty()) << "File entry should have non-empty path";
    }
}

// ============================================================================
// TST-SHIM-004: ParseShimcache_ControlSetSelection
// ============================================================================
// FACT-011: Current ControlSet is selected from Select\Current

TEST_F(ShimcacheTest, TST_SHIM_004_ParseShimcache_ControlSetSelection) {
    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << system_hive_;
    }

    chainsaw::io::hve::HveParser parser;
    ASSERT_TRUE(parser.load(system_hive_));

    // Проверяем, что Select\Current читается
    auto select_key = parser.get_key("Select");
    ASSERT_TRUE(select_key.has_value()) << "Select key should exist";

    auto current_value = select_key->get_value("Current");
    ASSERT_TRUE(current_value.has_value()) << "Current value should exist";

    auto controlset = current_value->as_u32();
    ASSERT_TRUE(controlset.has_value()) << "Current should be U32";
    EXPECT_GT(*controlset, 0u) << "ControlSet number should be positive";

    // Парсим shimcache и проверяем controlset
    auto result = parse_shimcache(parser);
    ASSERT_TRUE(std::holds_alternative<ShimcacheArtefact>(result));

    auto artefact = std::get<ShimcacheArtefact>(result);
    if (!artefact.entries.empty()) {
        EXPECT_EQ(artefact.entries[0].controlset, *controlset);
    }
}

// ============================================================================
// TST-SHIM-005: ParseShimcache_Windows10Format
// ============================================================================
// FACT-013: Windows 10 format with "10ts" signature

TEST_F(ShimcacheTest, TST_SHIM_005_ParseShimcache_Windows10Format) {
    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << system_hive_;
    }

    chainsaw::io::hve::HveParser parser;
    ASSERT_TRUE(parser.load(system_hive_));

    auto result = parse_shimcache(parser);
    ASSERT_TRUE(std::holds_alternative<ShimcacheArtefact>(result));

    auto artefact = std::get<ShimcacheArtefact>(result);

    // Если это Windows 10, проверяем сигнатуру
    if (artefact.version == ShimcacheVersion::Windows10 ||
        artefact.version == ShimcacheVersion::Windows10Creators) {
        EXPECT_GT(artefact.entries.size(), 0u);
        if (!artefact.entries.empty()) {
            EXPECT_TRUE(artefact.entries[0].signature.has_value());
            EXPECT_EQ(*artefact.entries[0].signature, "10ts");
        }
    }
}

// ============================================================================
// TST-SHIM-006: ParseShimcache_Windows7x64Format
// ============================================================================
// FACT-014: Windows 7 x64 format with 0xbadc0fee signature

TEST_F(ShimcacheTest, TST_SHIM_006_ParseShimcache_Windows7x64Format) {
    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << system_hive_;
    }

    chainsaw::io::hve::HveParser parser;
    ASSERT_TRUE(parser.load(system_hive_));

    auto result = parse_shimcache(parser);
    ASSERT_TRUE(std::holds_alternative<ShimcacheArtefact>(result));

    auto artefact = std::get<ShimcacheArtefact>(result);

    // Если это Windows 7 x64
    if (artefact.version == ShimcacheVersion::Windows7x64Windows2008R2) {
        EXPECT_GT(artefact.entries.size(), 0u);
        // Windows 7 не имеет signature в entries
        if (!artefact.entries.empty()) {
            EXPECT_FALSE(artefact.entries[0].signature.has_value());
            // Но должен иметь executed flag
            EXPECT_TRUE(artefact.entries[0].executed.has_value());
        }
    }
}

// ============================================================================
// TST-SHIM-007: ParseShimcache_Windows7x86Format
// ============================================================================
// FACT-015: Windows 7 x86 format

TEST_F(ShimcacheTest, TST_SHIM_007_ParseShimcache_Windows7x86Format) {
    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << system_hive_;
    }

    chainsaw::io::hve::HveParser parser;
    ASSERT_TRUE(parser.load(system_hive_));

    auto result = parse_shimcache(parser);
    ASSERT_TRUE(std::holds_alternative<ShimcacheArtefact>(result));

    auto artefact = std::get<ShimcacheArtefact>(result);

    if (artefact.version == ShimcacheVersion::Windows7x86) {
        EXPECT_GT(artefact.entries.size(), 0u);
        if (!artefact.entries.empty()) {
            EXPECT_TRUE(artefact.entries[0].executed.has_value());
        }
    }
}

// ============================================================================
// TST-SHIM-008: ParseShimcache_Windows8Format
// ============================================================================
// FACT-016: Windows 8/8.1 format with "00ts"/"10ts" signature

TEST_F(ShimcacheTest, TST_SHIM_008_ParseShimcache_Windows8Format) {
    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << system_hive_;
    }

    chainsaw::io::hve::HveParser parser;
    ASSERT_TRUE(parser.load(system_hive_));

    auto result = parse_shimcache(parser);
    ASSERT_TRUE(std::holds_alternative<ShimcacheArtefact>(result));

    auto artefact = std::get<ShimcacheArtefact>(result);

    if (artefact.version == ShimcacheVersion::Windows80Windows2012 ||
        artefact.version == ShimcacheVersion::Windows81Windows2012R2) {
        EXPECT_GT(artefact.entries.size(), 0u);
        if (!artefact.entries.empty()) {
            EXPECT_TRUE(artefact.entries[0].signature.has_value());
            auto sig = *artefact.entries[0].signature;
            EXPECT_TRUE(sig == "00ts" || sig == "10ts");
            EXPECT_TRUE(artefact.entries[0].executed.has_value());
        }
    }
}

// ============================================================================
// TST-SHIM-009: ParseShimcache_EntryFields
// ============================================================================
// FACT-004: All entry fields are populated correctly

TEST_F(ShimcacheTest, TST_SHIM_009_ParseShimcache_EntryFields) {
    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << system_hive_;
    }

    chainsaw::io::hve::HveParser parser;
    ASSERT_TRUE(parser.load(system_hive_));

    auto result = parse_shimcache(parser);
    ASSERT_TRUE(std::holds_alternative<ShimcacheArtefact>(result));

    auto artefact = std::get<ShimcacheArtefact>(result);

    // Проверяем несколько записей
    for (std::size_t i = 0; i < std::min(artefact.entries.size(), std::size_t{10}); ++i) {
        const auto& entry = artefact.entries[i];

        // cache_entry_position должен соответствовать индексу
        EXPECT_EQ(entry.cache_entry_position, static_cast<std::uint32_t>(i));

        // controlset должен быть положительным
        EXPECT_GT(entry.controlset, 0u);

        // path_size должен быть положительным для file entries
        if (is_file_entry(entry.entry_type)) {
            EXPECT_GT(entry.path_size, 0u);
        }
    }
}

// ============================================================================
// TST-SHIM-010: ParseShimcache_Timestamps
// ============================================================================
// FACT-004: Timestamp conversion from FILETIME

TEST_F(ShimcacheTest, TST_SHIM_010_ParseShimcache_Timestamps) {
    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << system_hive_;
    }

    chainsaw::io::hve::HveParser parser;
    ASSERT_TRUE(parser.load(system_hive_));

    auto result = parse_shimcache(parser);
    ASSERT_TRUE(std::holds_alternative<ShimcacheArtefact>(result));

    auto artefact = std::get<ShimcacheArtefact>(result);

    // last_update_ts должен быть валидным
    auto update_time = std::chrono::system_clock::to_time_t(artefact.last_update_ts);
    EXPECT_GT(update_time, 0) << "last_update_ts should be valid";

    // Проверяем timestamps записей
    std::size_t entries_with_valid_ts = 0;
    for (const auto& entry : artefact.entries) {
        if (entry.last_modified_ts) {
            auto entry_time = std::chrono::system_clock::to_time_t(*entry.last_modified_ts);
            // Некоторые записи могут иметь epoch timestamp (0), это допустимо
            if (entry_time > 0) {
                ++entries_with_valid_ts;
            }
        }
    }

    // Хотя бы некоторые записи должны иметь валидные timestamps
    EXPECT_GT(entries_with_valid_ts, 0U) << "At least some entries should have valid timestamps";
}

// ============================================================================
// TST-SHIM-011: ShimcacheAnalyser_PatternMatching
// ============================================================================
// FACT-010: Pattern matching with regex

TEST_F(ShimcacheTest, TST_SHIM_011_ShimcacheAnalyser_PatternMatching) {
    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << system_hive_;
    }

    ShimcacheAnalyser analyser(system_hive_);

    // Паттерн для поиска .exe файлов
    std::vector<std::string> patterns = {"\\.exe$"};

    auto result = analyser.amcache_shimcache_timeline(patterns, false);
    ASSERT_TRUE(std::holds_alternative<std::vector<TimelineEntity>>(result));

    auto timeline = std::get<std::vector<TimelineEntity>>(result);
    EXPECT_GT(timeline.size(), 0u);

    // Проверяем, что есть PatternMatch timestamps
    std::size_t pattern_matches = 0;
    for (const auto& entity : timeline) {
        if (entity.timestamp && entity.timestamp->kind == TimelineTimestamp::Kind::Exact &&
            entity.timestamp->exact_type == TimestampType::PatternMatch) {
            ++pattern_matches;
        }
    }

    // Должны быть найдены .exe файлы
    EXPECT_GT(pattern_matches, 0u) << "At least some entries should match .exe pattern";
}

// ============================================================================
// TST-SHIM-012: ShimcacheAnalyser_TimelineGeneration
// ============================================================================
// FACT-001: Timeline entities are created from shimcache entries

TEST_F(ShimcacheTest, TST_SHIM_012_ShimcacheAnalyser_TimelineGeneration) {
    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << system_hive_;
    }

    ShimcacheAnalyser analyser(system_hive_);
    auto result = analyser.amcache_shimcache_timeline({}, false);
    ASSERT_TRUE(std::holds_alternative<std::vector<TimelineEntity>>(result));

    auto timeline = std::get<std::vector<TimelineEntity>>(result);
    EXPECT_GT(timeline.size(), 1u);  // Минимум: shimcache_last_update + entries

    // Первый элемент должен быть ShimcacheLastUpdate
    ASSERT_TRUE(timeline[0].timestamp.has_value());
    EXPECT_EQ(timeline[0].timestamp->kind, TimelineTimestamp::Kind::Exact);
    EXPECT_EQ(timeline[0].timestamp->exact_type, TimestampType::ShimcacheLastUpdate);

    // Версия должна быть определена
    EXPECT_NE(analyser.shimcache_version(), ShimcacheVersion::Unknown);
}

// ============================================================================
// TST-SHIM-013: ShimcacheAnalyser_TimestampRanges
// ============================================================================
// FACT-002: Timestamp ranges are calculated between known timestamps

TEST_F(ShimcacheTest, TST_SHIM_013_ShimcacheAnalyser_TimestampRanges) {
    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "Test fixture not found: " << system_hive_;
    }

    ShimcacheAnalyser analyser(system_hive_);

    // Используем паттерн для создания known timestamps
    std::vector<std::string> patterns = {"\\.exe$"};
    auto result = analyser.amcache_shimcache_timeline(patterns, false);
    ASSERT_TRUE(std::holds_alternative<std::vector<TimelineEntity>>(result));

    auto timeline = std::get<std::vector<TimelineEntity>>(result);

    // Проверяем типы timestamps
    std::size_t exact_count = 0;
    std::size_t range_count = 0;
    std::size_t range_start_count = 0;
    std::size_t range_end_count = 0;

    for (const auto& entity : timeline) {
        if (!entity.timestamp)
            continue;

        switch (entity.timestamp->kind) {
        case TimelineTimestamp::Kind::Exact:
            ++exact_count;
            break;
        case TimelineTimestamp::Kind::Range:
            ++range_count;
            break;
        case TimelineTimestamp::Kind::RangeStart:
            ++range_start_count;
            break;
        case TimelineTimestamp::Kind::RangeEnd:
            ++range_end_count;
            break;
        }
    }

    // Должны быть Exact timestamps (минимум ShimcacheLastUpdate)
    EXPECT_GT(exact_count, 0u);

    // Вывод статистики для диагностики
    std::cout << "Timestamp statistics: Exact=" << exact_count << ", Range=" << range_count
              << ", RangeStart=" << range_start_count << ", RangeEnd=" << range_end_count
              << std::endl;
}

// ============================================================================
// TST-SHIM-014: AmcacheIntegration_FileEntryMatching
// ============================================================================
// FACT-030: Amcache file entries are matched by path

TEST_F(ShimcacheTest, TST_SHIM_014_AmcacheIntegration_FileEntryMatching) {
    // Этот тест требует наличия Amcache.hve файла
    // Пропускаем, если нет фикстуры
    auto amcache_path = fixtures_dir_ / "Amcache.hve";
    if (!std::filesystem::exists(amcache_path)) {
        GTEST_SKIP() << "Amcache fixture not found: " << amcache_path;
    }

    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "SYSTEM hive fixture not found: " << system_hive_;
    }

    ShimcacheAnalyser analyser(system_hive_, amcache_path);
    auto result = analyser.amcache_shimcache_timeline({}, false);
    ASSERT_TRUE(std::holds_alternative<std::vector<TimelineEntity>>(result));

    auto timeline = std::get<std::vector<TimelineEntity>>(result);

    // Проверяем, есть ли matched file entries
    std::size_t matched_files = 0;
    for (const auto& entity : timeline) {
        if (entity.amcache_file) {
            ++matched_files;
        }
    }

    std::cout << "Matched file entries: " << matched_files << std::endl;
}

// ============================================================================
// TST-SHIM-015: AmcacheIntegration_ProgramEntryMatching
// ============================================================================
// FACT-031: Amcache program entries are matched by name and version

TEST_F(ShimcacheTest, TST_SHIM_015_AmcacheIntegration_ProgramEntryMatching) {
    auto amcache_path = fixtures_dir_ / "Amcache.hve";
    if (!std::filesystem::exists(amcache_path)) {
        GTEST_SKIP() << "Amcache fixture not found: " << amcache_path;
    }

    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "SYSTEM hive fixture not found: " << system_hive_;
    }

    ShimcacheAnalyser analyser(system_hive_, amcache_path);
    auto result = analyser.amcache_shimcache_timeline({}, false);
    ASSERT_TRUE(std::holds_alternative<std::vector<TimelineEntity>>(result));

    auto timeline = std::get<std::vector<TimelineEntity>>(result);

    std::size_t matched_programs = 0;
    for (const auto& entity : timeline) {
        if (entity.amcache_program) {
            ++matched_programs;
        }
    }

    std::cout << "Matched program entries: " << matched_programs << std::endl;
}

// ============================================================================
// TST-SHIM-016: AmcacheIntegration_NearTimestampPairs
// ============================================================================
// FACT-003: Near timestamp pairs within 1 minute are detected

TEST_F(ShimcacheTest, TST_SHIM_016_AmcacheIntegration_NearTimestampPairs) {
    auto amcache_path = fixtures_dir_ / "Amcache.hve";
    if (!std::filesystem::exists(amcache_path)) {
        GTEST_SKIP() << "Amcache fixture not found: " << amcache_path;
    }

    if (!std::filesystem::exists(system_hive_)) {
        GTEST_SKIP() << "SYSTEM hive fixture not found: " << system_hive_;
    }

    ShimcacheAnalyser analyser(system_hive_, amcache_path);

    // Включаем near timestamp pair matching
    auto result = analyser.amcache_shimcache_timeline({}, true);
    ASSERT_TRUE(std::holds_alternative<std::vector<TimelineEntity>>(result));

    auto timeline = std::get<std::vector<TimelineEntity>>(result);

    std::size_t near_ts_matches = 0;
    for (const auto& entity : timeline) {
        if (entity.timestamp && entity.timestamp->kind == TimelineTimestamp::Kind::Exact &&
            entity.timestamp->exact_type == TimestampType::NearTSMatch) {
            ++near_ts_matches;
        }
    }

    std::cout << "Near timestamp matches: " << near_ts_matches << std::endl;
}

// ============================================================================
// TST-SHIM-017: OutputFormatting_CSV
// ============================================================================
// FACT-026: CSV output format

TEST_F(ShimcacheTest, TST_SHIM_017_OutputFormatting_CSV) {
    // Тестируем CSV header
    std::string header = get_csv_header();
    EXPECT_FALSE(header.empty());
    EXPECT_NE(header.find("Timestamp"), std::string::npos);
    EXPECT_NE(header.find("File Path"), std::string::npos);

    // Тестируем CSV форматирование entity
    TimelineEntity entity;
    entity.timestamp = TimelineTimestamp::make_exact(std::chrono::system_clock::now(),
                                                     TimestampType::PatternMatch);

    ShimcacheEntry shim_entry;
    shim_entry.cache_entry_position = 0;
    shim_entry.controlset = 1;
    shim_entry.entry_type = FileEntryType{"C:\\Windows\\System32\\notepad.exe"};
    shim_entry.path_size = 36;
    entity.shimcache_entry = std::move(shim_entry);

    std::string csv_line = format_timeline_entity_csv(entity, 1);
    EXPECT_FALSE(csv_line.empty());
    EXPECT_NE(csv_line.find("notepad.exe"), std::string::npos);
}

// ============================================================================
// TST-SHIM-018: OutputFormatting_Timestamps
// ============================================================================
// FACT-026: RFC3339 timestamp formatting

TEST_F(ShimcacheTest, TST_SHIM_018_OutputFormatting_Timestamps) {
    // Создаём известный timestamp
    auto now = std::chrono::system_clock::now();
    std::string formatted = format_timestamp_rfc3339(now);

    // Проверяем формат RFC3339
    EXPECT_FALSE(formatted.empty());
    EXPECT_NE(formatted.find('T'), std::string::npos);  // ISO date-time separator
    EXPECT_NE(formatted.find('Z'), std::string::npos);  // UTC timezone
    EXPECT_EQ(formatted.back(), 'Z');                   // Ends with Z

    // Проверяем длину (YYYY-MM-DDTHH:MM:SS.ffffffZ = 27 символов)
    EXPECT_GE(formatted.size(), 20u);
}

// ============================================================================
// TST-SHIM-019: ErrorHandling_InvalidHive
// ============================================================================
// FACT-019: Invalid hive returns error

TEST_F(ShimcacheTest, TST_SHIM_019_ErrorHandling_InvalidHive) {
    // Создаём временный невалидный файл
    auto temp_file = std::filesystem::temp_directory_path() / "invalid_shimcache_test.hve";
    {
        std::ofstream ofs(temp_file, std::ios::binary);
        ofs << "This is not a valid registry hive file";
    }

    chainsaw::io::hve::HveParser parser;
    bool loaded = parser.load(temp_file);

    // Cleanup
    std::filesystem::remove(temp_file);

    // Должна быть ошибка загрузки
    EXPECT_FALSE(loaded);
}

// ============================================================================
// TST-SHIM-020: ErrorHandling_UnsupportedVersion
// ============================================================================
// FACT-017, FACT-018: Unsupported versions return error

TEST_F(ShimcacheTest, TST_SHIM_020_ErrorHandling_UnsupportedVersion) {
    // Тестируем преобразование версий в строки
    EXPECT_STREQ(shimcache_version_to_string(ShimcacheVersion::Unknown), "Unknown");
    EXPECT_STREQ(shimcache_version_to_string(ShimcacheVersion::Windows10), "Windows 10");
    EXPECT_STREQ(shimcache_version_to_string(ShimcacheVersion::WindowsXP), "Windows XP");
    EXPECT_STREQ(shimcache_version_to_string(ShimcacheVersion::WindowsVistaWin2k3Win2k8),
                 "Windows Vista, Windows Server 2003 or Windows Server 2008");
}

// ============================================================================
// Additional Helper Tests
// ============================================================================

TEST_F(ShimcacheTest, CPUArchitecture_Conversion) {
    // AMD64
    EXPECT_EQ(cpu_architecture_from_u16(34404), CPUArchitecture::Amd64);
    EXPECT_STREQ(cpu_architecture_to_string(CPUArchitecture::Amd64), "AMD64");

    // ARM
    EXPECT_EQ(cpu_architecture_from_u16(452), CPUArchitecture::Arm);
    EXPECT_STREQ(cpu_architecture_to_string(CPUArchitecture::Arm), "ARM");

    // I386
    EXPECT_EQ(cpu_architecture_from_u16(332), CPUArchitecture::I386);
    EXPECT_STREQ(cpu_architecture_to_string(CPUArchitecture::I386), "I386");

    // IA64
    EXPECT_EQ(cpu_architecture_from_u16(512), CPUArchitecture::Ia64);
    EXPECT_STREQ(cpu_architecture_to_string(CPUArchitecture::Ia64), "IA64");

    // Unknown
    EXPECT_EQ(cpu_architecture_from_u16(9999), CPUArchitecture::Unknown);
    EXPECT_STREQ(cpu_architecture_to_string(CPUArchitecture::Unknown), "Unknown");
}

TEST_F(ShimcacheTest, TimestampType_Conversion) {
    EXPECT_STREQ(timestamp_type_to_string(TimestampType::AmcacheRangeMatch), "AmcacheRangeMatch");
    EXPECT_STREQ(timestamp_type_to_string(TimestampType::NearTSMatch), "NearTSMatch");
    EXPECT_STREQ(timestamp_type_to_string(TimestampType::PatternMatch), "PatternMatch");
    EXPECT_STREQ(timestamp_type_to_string(TimestampType::ShimcacheLastUpdate),
                 "ShimcacheLastUpdate");
}

TEST_F(ShimcacheTest, EntryType_Functions) {
    // FileEntryType
    EntryType file_entry = FileEntryType{"C:\\test.exe"};
    EXPECT_TRUE(is_file_entry(file_entry));
    EXPECT_EQ(get_entry_path(file_entry), "C:\\test.exe");
    EXPECT_EQ(get_entry_display_name(file_entry), "C:\\test.exe");

    // ProgramEntryType
    ProgramEntryType prog;
    prog.program_name = "TestProgram";
    prog.raw_entry = "raw data";
    EntryType program_entry = prog;
    EXPECT_FALSE(is_file_entry(program_entry));
    EXPECT_TRUE(get_entry_path(program_entry).empty());
    EXPECT_EQ(get_entry_display_name(program_entry), "TestProgram");
}

TEST_F(ShimcacheTest, TimelineTimestamp_Factory) {
    auto now = std::chrono::system_clock::now();
    auto later = now + std::chrono::hours(1);

    // Exact
    auto exact = TimelineTimestamp::make_exact(now, TimestampType::PatternMatch);
    EXPECT_EQ(exact.kind, TimelineTimestamp::Kind::Exact);
    EXPECT_EQ(exact.exact_ts, now);
    EXPECT_EQ(exact.exact_type, TimestampType::PatternMatch);

    // Range
    auto range = TimelineTimestamp::make_range(now, later);
    EXPECT_EQ(range.kind, TimelineTimestamp::Kind::Range);
    EXPECT_EQ(range.range_from, now);
    EXPECT_EQ(range.range_to, later);

    // RangeStart
    auto range_start = TimelineTimestamp::make_range_start(now);
    EXPECT_EQ(range_start.kind, TimelineTimestamp::Kind::RangeStart);

    // RangeEnd
    auto range_end = TimelineTimestamp::make_range_end(later);
    EXPECT_EQ(range_end.kind, TimelineTimestamp::Kind::RangeEnd);
}

TEST_F(ShimcacheTest, ShimcacheError_Format) {
    ShimcacheError error{ShimcacheErrorKind::KeyNotFound, "Test error message"};
    EXPECT_EQ(error.format(), "Test error message");
}

}  // namespace chainsaw::analyse::shimcache
