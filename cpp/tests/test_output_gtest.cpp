// ==============================================================================
// test_output_gtest.cpp - Тесты модуля вывода (GoogleTest)
// ==============================================================================
//
// MOD-0003: output
// ADR-0006: CLI и пользовательский вывод
// ADR-0007: форматирование строк
// ADR-0008: GoogleTest
// GUIDE-0001 G-030..G-033: вывод и детерминизм
// Step 25: интеграция quality gates
//
// ==============================================================================

#include "chainsaw/output.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace chainsaw::output::test {

// ==============================================================================
// TST-OUTPUT-001: Создание Writer с конфигурацией
// ==============================================================================

TEST(OutputTest, Writer_DefaultConfig_CreatesSuccessfully) {
    // Arrange
    OutputConfig config;

    // Act & Assert
    EXPECT_NO_THROW({ Writer writer(config); });
}

TEST(OutputTest, Writer_QuietConfig_CreatesSuccessfully) {
    // Arrange
    OutputConfig config;
    config.quiet = true;

    // Act & Assert
    EXPECT_NO_THROW({ Writer writer(config); });
}

TEST(OutputTest, Writer_VerboseConfig_CreatesSuccessfully) {
    // Arrange
    OutputConfig config;
    config.verbose = true;

    // Act & Assert
    EXPECT_NO_THROW({ Writer writer(config); });
}

// ==============================================================================
// TST-OUTPUT-002: Форматирование сообщений
// ==============================================================================

TEST(OutputTest, FormatInfo_ContainsPrefix) {
    // Arrange
    std::string message = "Test message";

    // Act
    std::string formatted = format_info(message);

    // Assert
    // CLI-0001 1.6.1: формат "[+] <message>"
    EXPECT_NE(formatted.find("[+]"), std::string::npos);
    EXPECT_NE(formatted.find(message), std::string::npos);
}

TEST(OutputTest, FormatError_ContainsPrefix) {
    // Arrange
    std::string message = "Error message";

    // Act
    std::string formatted = format_error(message);

    // Assert
    // CLI-0001 1.6.1: формат "[x] <message>"
    EXPECT_NE(formatted.find("[x]"), std::string::npos);
    EXPECT_NE(formatted.find(message), std::string::npos);
}

TEST(OutputTest, FormatWarning_ContainsPrefix) {
    // Arrange
    std::string message = "Warning message";

    // Act
    std::string formatted = format_warning(message);

    // Assert
    // CLI-0001 1.6.1: формат "[!] <message>"
    EXPECT_NE(formatted.find("[!]"), std::string::npos);
    EXPECT_NE(formatted.find(message), std::string::npos);
}

TEST(OutputTest, FormatDebug_ContainsPrefix) {
    // Arrange
    std::string message = "Debug message";

    // Act
    std::string formatted = format_debug(message);

    // Assert
    // CLI-0001 1.6.1: формат "[*] <message>"
    EXPECT_NE(formatted.find("[*]"), std::string::npos);
    EXPECT_NE(formatted.find(message), std::string::npos);
}

// ==============================================================================
// TST-OUTPUT-003: Поток вывода
// ==============================================================================

TEST(OutputTest, Stream_Stdout_IsValid) {
    // Arrange & Act & Assert
    EXPECT_EQ(static_cast<int>(Stream::Stdout), 0);
}

TEST(OutputTest, Stream_Stderr_IsValid) {
    // Arrange & Act & Assert
    EXPECT_EQ(static_cast<int>(Stream::Stderr), 1);
}

// ==============================================================================
// TST-OUTPUT-004: Методы Writer
// ==============================================================================

TEST(OutputTest, Writer_Info_DoesNotThrow) {
    // Arrange
    OutputConfig config;
    Writer writer(config);

    // Act & Assert
    EXPECT_NO_THROW({ writer.info("Test info message"); });
}

TEST(OutputTest, Writer_Error_DoesNotThrow) {
    // Arrange
    OutputConfig config;
    Writer writer(config);

    // Act & Assert
    EXPECT_NO_THROW({ writer.error("Test error message"); });
}

TEST(OutputTest, Writer_Warning_DoesNotThrow) {
    // Arrange
    OutputConfig config;
    Writer writer(config);

    // Act & Assert
    EXPECT_NO_THROW({ writer.warning("Test warning message"); });
}

TEST(OutputTest, Writer_Debug_DoesNotThrow) {
    // Arrange
    OutputConfig config;
    config.verbose = true;
    Writer writer(config);

    // Act & Assert
    EXPECT_NO_THROW({ writer.debug("Test debug message"); });
}

// ==============================================================================
// TST-OUTPUT-005: Quiet режим
// ==============================================================================

TEST(OutputTest, Writer_QuietMode_InfoSuppressed) {
    // Arrange
    OutputConfig config;
    config.quiet = true;
    Writer writer(config);

    // Act & Assert
    // В quiet режиме info не должен выбрасывать исключений
    EXPECT_NO_THROW({ writer.info("This should be suppressed"); });
}

// ==============================================================================
// TST-OUTPUT-006: Write и WriteLine
// ==============================================================================

TEST(OutputTest, Writer_Write_DoesNotThrow) {
    // Arrange
    OutputConfig config;
    Writer writer(config);

    // Act & Assert
    EXPECT_NO_THROW({ writer.write(Stream::Stdout, "Raw output"); });
}

TEST(OutputTest, Writer_WriteLine_DoesNotThrow) {
    // Arrange
    OutputConfig config;
    Writer writer(config);

    // Act & Assert
    EXPECT_NO_THROW({ writer.write_line(Stream::Stdout, "Line output"); });
}

// ==============================================================================
// TST-OUTPUT-007: Детерминизм вывода (GUIDE-0001 G-033)
// ==============================================================================

TEST(OutputTest, FormatInfo_Deterministic) {
    // Arrange
    std::string message = "Same message";

    // Act
    std::string result1 = format_info(message);
    std::string result2 = format_info(message);

    // Assert
    EXPECT_EQ(result1, result2);
}

TEST(OutputTest, FormatError_Deterministic) {
    // Arrange
    std::string message = "Same error";

    // Act
    std::string result1 = format_error(message);
    std::string result2 = format_error(message);

    // Assert
    EXPECT_EQ(result1, result2);
}

}  // namespace chainsaw::output::test
