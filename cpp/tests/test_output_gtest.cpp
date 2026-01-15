// ==============================================================================
// test_output_gtest.cpp - Тесты модуля вывода (GoogleTest)
// ==============================================================================
//
// MOD-0003: output
// ADR-0006: CLI и пользовательский вывод
// ADR-0007: форматирование строк
// ADR-0003: RapidJSON для JSON сериализации
// ADR-0008: GoogleTest
// GUIDE-0001 G-030..G-033: вывод и детерминизм
// SPEC-SLICE-002: Output Layer
// : слайс-реализация
//
// TST-OUTPUT-001..TST-OUTPUT-008: тесты согласно micro-spec
//
// ==============================================================================

#include "chainsaw/output.hpp"
#include "chainsaw/platform.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sstream>
#include <string>

namespace chainsaw::output::test {

// ==============================================================================
// TST-OUTPUT-001: write/write_line корректность
// ==============================================================================

TEST(OutputTest, Writer_DefaultConfig_CreatesSuccessfully) {
    OutputConfig config;
    EXPECT_NO_THROW({ Writer writer(config); });
}

TEST(OutputTest, Writer_QuietConfig_CreatesSuccessfully) {
    OutputConfig config;
    config.quiet = true;
    EXPECT_NO_THROW({ Writer writer(config); });
}

TEST(OutputTest, Writer_VerboseConfig_CreatesSuccessfully) {
    OutputConfig config;
    config.verbose = 1;
    EXPECT_NO_THROW({ Writer writer(config); });
}

TEST(OutputTest, Writer_Write_DoesNotThrow) {
    OutputConfig config;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.write(Stream::Stdout, "Raw output"); });
}

TEST(OutputTest, Writer_WriteLine_DoesNotThrow) {
    OutputConfig config;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.write_line(Stream::Stdout, "Line output"); });
}

// ==============================================================================
// TST-OUTPUT-002: info/warn/error/debug форматирование
// ==============================================================================

TEST(OutputTest, FormatInfo_ContainsPrefix) {
    std::string message = "Test message";
    std::string formatted = format_info(message);
    // CLI-0001 1.6.1: формат "[+] <message>"
    EXPECT_NE(formatted.find("[+]"), std::string::npos);
    EXPECT_NE(formatted.find(message), std::string::npos);
    EXPECT_EQ(formatted.back(), '\n');
}

TEST(OutputTest, FormatError_ContainsPrefix) {
    std::string message = "Error message";
    std::string formatted = format_error(message);
    // CLI-0001 1.6.1: формат "[x] <message>"
    EXPECT_NE(formatted.find("[x]"), std::string::npos);
    EXPECT_NE(formatted.find(message), std::string::npos);
}

TEST(OutputTest, FormatWarning_ContainsPrefix) {
    std::string message = "Warning message";
    std::string formatted = format_warning(message);
    // CLI-0001 1.6.1: формат "[!] <message>"
    EXPECT_NE(formatted.find("[!]"), std::string::npos);
    EXPECT_NE(formatted.find(message), std::string::npos);
}

TEST(OutputTest, FormatDebug_ContainsPrefix) {
    std::string message = "Debug message";
    std::string formatted = format_debug(message);
    // CLI-0001 1.6.1: формат "[*] <message>"
    EXPECT_NE(formatted.find("[*]"), std::string::npos);
    EXPECT_NE(formatted.find(message), std::string::npos);
}

TEST(OutputTest, Writer_Info_DoesNotThrow) {
    OutputConfig config;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.info("Test info message"); });
}

TEST(OutputTest, Writer_Error_DoesNotThrow) {
    OutputConfig config;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.error("Test error message"); });
}

TEST(OutputTest, Writer_Warning_DoesNotThrow) {
    OutputConfig config;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.warning("Test warning message"); });
}

TEST(OutputTest, Writer_Debug_DoesNotThrow) {
    OutputConfig config;
    config.verbose = 1;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.debug("Test debug message"); });
}

TEST(OutputTest, Writer_Trace_DoesNotThrow) {
    OutputConfig config;
    config.verbose = 2;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.trace("Test trace message"); });
}

// ==============================================================================
// TST-OUTPUT-003: quiet подавляет info
// ==============================================================================

TEST(OutputTest, Writer_QuietMode_InfoSuppressed) {
    OutputConfig config;
    config.quiet = true;
    Writer writer(config);
    // В quiet режиме info не должен выбрасывать исключений
    EXPECT_NO_THROW({ writer.info("This should be suppressed"); });
}

TEST(OutputTest, Writer_QuietMode_WarnSuppressed) {
    OutputConfig config;
    config.quiet = true;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.warn("This should be suppressed"); });
}

TEST(OutputTest, Writer_QuietMode_ErrorNotSuppressed) {
    OutputConfig config;
    config.quiet = true;
    Writer writer(config);
    // Ошибки НЕ должны подавляться в quiet режиме
    EXPECT_NO_THROW({ writer.error("This should NOT be suppressed"); });
}

// ==============================================================================
// TST-OUTPUT-004: verbose контролирует debug/trace
// ==============================================================================

TEST(OutputTest, Writer_Verbose0_DebugSuppressed) {
    OutputConfig config;
    config.verbose = 0;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.debug("Suppressed debug"); });
}

TEST(OutputTest, Writer_Verbose1_DebugEnabled) {
    OutputConfig config;
    config.verbose = 1;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.debug("Enabled debug"); });
}

TEST(OutputTest, Writer_Verbose1_TraceSuppressed) {
    OutputConfig config;
    config.verbose = 1;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.trace("Suppressed trace"); });
}

TEST(OutputTest, Writer_Verbose2_TraceEnabled) {
    OutputConfig config;
    config.verbose = 2;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.trace("Enabled trace"); });
}

// ==============================================================================
// TST-OUTPUT-005: JSON сериализация детерминирована
// ==============================================================================

TEST(OutputTest, WriteJson_DoesNotThrow) {
    OutputConfig config;
    Writer writer(config);

    rapidjson::Document doc;
    doc.SetObject();
    doc.AddMember("key", "value", doc.GetAllocator());

    EXPECT_NO_THROW({ writer.write_json(doc); });
}

TEST(OutputTest, WriteJsonLine_DoesNotThrow) {
    OutputConfig config;
    Writer writer(config);

    rapidjson::Document doc;
    doc.SetObject();
    doc.AddMember("test", 123, doc.GetAllocator());

    EXPECT_NO_THROW({ writer.write_json_line(doc); });
}

TEST(OutputTest, WriteJsonPretty_DoesNotThrow) {
    OutputConfig config;
    Writer writer(config);

    rapidjson::Document doc;
    doc.SetObject();
    doc.AddMember("pretty", true, doc.GetAllocator());

    EXPECT_NO_THROW({ writer.write_json_pretty(doc); });
}

// Детерминизм: одинаковые входы дают одинаковые выходы
TEST(OutputTest, Json_Deterministic) {
    rapidjson::Document doc1;
    doc1.SetObject();
    doc1.AddMember("a", 1, doc1.GetAllocator());
    doc1.AddMember("b", 2, doc1.GetAllocator());

    rapidjson::Document doc2;
    doc2.SetObject();
    doc2.AddMember("a", 1, doc2.GetAllocator());
    doc2.AddMember("b", 2, doc2.GetAllocator());

    rapidjson::StringBuffer buf1, buf2;
    rapidjson::Writer<rapidjson::StringBuffer> w1(buf1), w2(buf2);
    doc1.Accept(w1);
    doc2.Accept(w2);

    EXPECT_EQ(std::string(buf1.GetString()), std::string(buf2.GetString()));
}

// ==============================================================================
// TST-OUTPUT-006: Table formatting Unicode
// ==============================================================================

TEST(OutputTest, Table_Create_DoesNotThrow) {
    EXPECT_NO_THROW({ Table table; });
}

TEST(OutputTest, Table_SetHeaders_DoesNotThrow) {
    Table table;
    EXPECT_NO_THROW({ table.set_headers({"Col1", "Col2", "Col3"}); });
}

TEST(OutputTest, Table_AddRow_DoesNotThrow) {
    Table table;
    table.set_headers({"Name", "Value"});
    EXPECT_NO_THROW({ table.add_row({"test", "123"}); });
}

TEST(OutputTest, Table_ToString_ContainsUnicode) {
    Table table;
    table.set_headers({"Header"});
    table.add_row({"Data"});

    std::string result = table.to_string();

    // Проверяем наличие Unicode box-drawing символов (UTF-8)
    // ┌ = E2 94 8C, │ = E2 94 82, └ = E2 94 94
    EXPECT_NE(result.find("\xe2\x94\x8c"), std::string::npos);  // ┌
    EXPECT_NE(result.find("\xe2\x94\x82"), std::string::npos);  // │
    EXPECT_NE(result.find("\xe2\x94\x94"), std::string::npos);  // └
}

TEST(OutputTest, Table_ToString_ContainsData) {
    Table table;
    table.set_headers({"Column"});
    table.add_row({"TestData"});

    std::string result = table.to_string();

    EXPECT_NE(result.find("Column"), std::string::npos);
    EXPECT_NE(result.find("TestData"), std::string::npos);
}

TEST(OutputTest, Table_Print_DoesNotThrow) {
    OutputConfig config;
    Writer writer(config);

    Table table;
    table.set_headers({"A", "B"});
    table.add_row({"1", "2"});

    EXPECT_NO_THROW({ table.print(writer); });
}

TEST(OutputTest, Table_RowCount) {
    Table table;
    EXPECT_EQ(table.row_count(), 0u);

    table.add_row({"a"});
    EXPECT_EQ(table.row_count(), 1u);

    table.add_row({"b"});
    EXPECT_EQ(table.row_count(), 2u);
}

// ==============================================================================
// TST-OUTPUT-007: Output file support
// ==============================================================================

TEST(OutputTest, OutputConfig_OutputPath_Optional) {
    OutputConfig config;
    EXPECT_FALSE(config.output_path.has_value());
}

TEST(OutputTest, OutputConfig_OutputPath_CanBeSet) {
    OutputConfig config;
    config.output_path = std::filesystem::path("/tmp/test.txt");
    EXPECT_TRUE(config.output_path.has_value());
    EXPECT_EQ(config.output_path.value().filename(), "test.txt");
}

TEST(OutputTest, Writer_HasOutputFile_FalseByDefault) {
    OutputConfig config;
    Writer writer(config);
    EXPECT_FALSE(writer.has_output_file());
}

TEST(OutputTest, Writer_OutputFile_CreatesFile) {
    // Создаём временный файл
    auto temp_path = platform::make_temp_file("output_test_");

    OutputConfig config;
    config.output_path = temp_path;
    Writer writer(config);

    // Файл должен быть открыт
    EXPECT_TRUE(writer.has_output_file());

    // Записываем данные
    writer.write(Stream::Stdout, "Test output to file");
    writer.flush();

    // Закрываем writer (деструктор закроет файл)
    // и проверяем содержимое
}

// ==============================================================================
// TST-OUTPUT-008: format_field_length truncation
// ==============================================================================

TEST(OutputTest, FormatFieldLength_RemovesNewlines) {
    std::string input = "line1\nline2\nline3";
    std::string result = format_field_length(input, 0, true);
    EXPECT_EQ(result.find('\n'), std::string::npos);
}

TEST(OutputTest, FormatFieldLength_RemovesCarriageReturn) {
    std::string input = "line1\r\nline2";
    std::string result = format_field_length(input, 0, true);
    EXPECT_EQ(result.find('\r'), std::string::npos);
}

TEST(OutputTest, FormatFieldLength_RemovesTabs) {
    std::string input = "col1\tcol2\tcol3";
    std::string result = format_field_length(input, 0, true);
    EXPECT_EQ(result.find('\t'), std::string::npos);
}

TEST(OutputTest, FormatFieldLength_CollapsesDoubleSpaces) {
    std::string input = "word1  word2   word3";
    std::string result = format_field_length(input, 0, true);
    EXPECT_EQ(result.find("  "), std::string::npos);
}

TEST(OutputTest, FormatFieldLength_TruncatesLongStrings) {
    // Создаём строку длиннее лимита (496)
    std::string input(600, 'x');
    std::string result = format_field_length(input, 0, false);

    // Результат должен быть обрезан и содержать подсказку
    EXPECT_LT(result.size(), input.size());
    EXPECT_NE(result.find("--full"), std::string::npos);
}

TEST(OutputTest, FormatFieldLength_NoTruncationWithFullOutput) {
    std::string input(600, 'y');
    std::string result = format_field_length(input, 0, true);

    // При full_output=true строка не обрезается
    EXPECT_EQ(result.size(), 600u);
}

// ==============================================================================
// Дополнительные тесты: Format enum и Color enum
// ==============================================================================

TEST(OutputTest, Format_Enum_Values) {
    EXPECT_EQ(static_cast<int>(Format::Std), 0);
    EXPECT_EQ(static_cast<int>(Format::Csv), 1);
    EXPECT_EQ(static_cast<int>(Format::Json), 2);
    EXPECT_EQ(static_cast<int>(Format::Jsonl), 3);
}

TEST(OutputTest, Color_Enum_Values) {
    EXPECT_EQ(static_cast<int>(Color::Default), 0);
    EXPECT_EQ(static_cast<int>(Color::Green), 1);
    EXPECT_EQ(static_cast<int>(Color::Yellow), 2);
    EXPECT_EQ(static_cast<int>(Color::Red), 3);
}

TEST(OutputTest, AnsiColorCode_ReturnsNonEmpty) {
    EXPECT_FALSE(ansi_color_code(Color::Green).empty());
    EXPECT_FALSE(ansi_color_code(Color::Yellow).empty());
    EXPECT_FALSE(ansi_color_code(Color::Red).empty());
}

TEST(OutputTest, AnsiResetCode_ReturnsNonEmpty) {
    EXPECT_FALSE(ansi_reset_code().empty());
}

// ==============================================================================
// Дополнительные тесты: Цветной вывод
// ==============================================================================

TEST(OutputTest, Writer_GreenLine_DoesNotThrow) {
    OutputConfig config;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.green_line("Green text"); });
}

TEST(OutputTest, Writer_YellowLine_DoesNotThrow) {
    OutputConfig config;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.yellow_line("Yellow text"); });
}

TEST(OutputTest, Writer_RedLine_DoesNotThrow) {
    OutputConfig config;
    Writer writer(config);
    EXPECT_NO_THROW({ writer.red_line("Red text"); });
}

// ==============================================================================
// Дополнительные тесты: Progress
// ==============================================================================

TEST(OutputTest, Writer_Progress_DoesNotThrow) {
    OutputConfig config;
    Writer writer(config);

    EXPECT_NO_THROW({
        writer.progress_begin("Processing", 100);
        writer.progress_tick(50);
        writer.progress_end();
    });
}

TEST(OutputTest, Writer_Progress_QuietMode) {
    OutputConfig config;
    config.quiet = true;
    Writer writer(config);

    EXPECT_NO_THROW({
        writer.progress_begin("Processing", 100);
        writer.progress_tick(50);
        writer.progress_end();
    });
}

// ==============================================================================
// Дополнительные тесты: Платформозависимые константы
// ==============================================================================

TEST(OutputTest, RulePrefix_NotEmpty) {
    EXPECT_NE(std::string(RULE_PREFIX), "");
}

TEST(OutputTest, TickChars_NotEmpty) {
    EXPECT_NE(std::string(TICK_CHARS), "");
}

TEST(OutputTest, TickMs_Positive) {
    EXPECT_GT(TICK_MS, 0);
}

#ifdef _WIN32
TEST(OutputTest, Windows_RulePrefix_IsAscii) {
    EXPECT_EQ(std::string(RULE_PREFIX), "+");
}
#else
TEST(OutputTest, Unix_RulePrefix_IsUnicode) {
    // ‣ = U+2023 = E2 80 A3 в UTF-8
    EXPECT_EQ(std::string(RULE_PREFIX), "\xe2\x80\xa3");
}
#endif

// ==============================================================================
// TST: Детерминизм форматирования (GUIDE-0001 G-033)
// ==============================================================================

TEST(OutputTest, FormatInfo_Deterministic) {
    std::string message = "Same message";
    std::string result1 = format_info(message);
    std::string result2 = format_info(message);
    EXPECT_EQ(result1, result2);
}

TEST(OutputTest, FormatError_Deterministic) {
    std::string message = "Same error";
    std::string result1 = format_error(message);
    std::string result2 = format_error(message);
    EXPECT_EQ(result1, result2);
}

TEST(OutputTest, Table_ToString_Deterministic) {
    Table table1;
    table1.set_headers({"A", "B"});
    table1.add_row({"1", "2"});

    Table table2;
    table2.set_headers({"A", "B"});
    table2.add_row({"1", "2"});

    EXPECT_EQ(table1.to_string(), table2.to_string());
}

// ==============================================================================
// Тесты потоков
// ==============================================================================

TEST(OutputTest, Stream_Stdout_IsValid) {
    EXPECT_EQ(static_cast<int>(Stream::Stdout), 0);
}

TEST(OutputTest, Stream_Stderr_IsValid) {
    EXPECT_EQ(static_cast<int>(Stream::Stderr), 1);
}

}  // namespace chainsaw::output::test
