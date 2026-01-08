# ==============================================================================
# tests/CMakeLists.txt - Тесты для порта Chainsaw
# ==============================================================================
#
# ADR-0008: GoogleTest как фреймворк тестирования
# GUIDE-0001 G-070: test-to-test перенос
# Step 25: интеграция quality gates + GoogleTest
#
# Использование:
#   ctest --test-dir build --output-on-failure
#   ctest --test-dir build -C Release --output-on-failure  # для multi-config
#
# ==============================================================================

# ==============================================================================
# GoogleTest интеграция (ADR-0008)
# ==============================================================================
if(CHAINSAW_USE_GTEST)
    # Проверяем наличие вендорированного GoogleTest
    set(GTEST_VENDORED_DIR "${CMAKE_SOURCE_DIR}/../third_party/googletest")

    if(EXISTS "${GTEST_VENDORED_DIR}/CMakeLists.txt")
        # Используем вендорированную версию (ADR-0002: офлайн-сборка)
        message(STATUS "GoogleTest: используется вендорированная версия из ${GTEST_VENDORED_DIR}")

        # Отключаем предупреждения для GoogleTest (GCC 14 выдаёт ложные срабатывания)
        # Сохраняем текущие флаги и временно отключаем -Werror
        set(GTEST_SAVED_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "14.0")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-null-dereference")
        endif()

        add_subdirectory("${GTEST_VENDORED_DIR}" "${CMAKE_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)

        # Восстанавливаем флаги
        set(CMAKE_CXX_FLAGS "${GTEST_SAVED_CXX_FLAGS}")

        # Отключаем предупреждения как ошибки для GoogleTest таргетов
        if(TARGET gtest)
            target_compile_options(gtest PRIVATE
                $<$<CXX_COMPILER_ID:GNU>:-Wno-null-dereference>
            )
        endif()
        if(TARGET gtest_main)
            target_compile_options(gtest_main PRIVATE
                $<$<CXX_COMPILER_ID:GNU>:-Wno-null-dereference>
            )
        endif()

        set(GTEST_AVAILABLE TRUE)
    else()
        # FetchContent fallback (требует сеть при первой сборке)
        include(FetchContent)

        message(STATUS "GoogleTest: загрузка через FetchContent")
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        v1.15.2
            GIT_SHALLOW    TRUE
        )

        # Для Windows: предотвращаем переопределение опций /MT и /MD
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

        # Не устанавливать GoogleTest
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(googletest)
        set(GTEST_AVAILABLE TRUE)
    endif()
else()
    set(GTEST_AVAILABLE FALSE)
    message(STATUS "GoogleTest: отключён (CHAINSAW_USE_GTEST=OFF)")
endif()

# ==============================================================================
# Вспомогательная функция для регистрации тестов
# ==============================================================================
function(chainsaw_add_test TEST_NAME)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LIBS")

    if(GTEST_AVAILABLE)
        add_executable(${TEST_NAME} ${ARG_SOURCES})
        target_link_libraries(${TEST_NAME} PRIVATE
            GTest::gtest
            GTest::gtest_main
            ${ARG_LIBS}
        )
        include(GoogleTest)
        gtest_discover_tests(${TEST_NAME})
    else()
        # Fallback: базовый тест без GoogleTest
        add_executable(${TEST_NAME} ${ARG_SOURCES})
        target_link_libraries(${TEST_NAME} PRIVATE ${ARG_LIBS})
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endif()
endfunction()

# ==============================================================================
# Базовые тесты (Step 24)
# ==============================================================================

# Базовый тест платформы (без GoogleTest для обратной совместимости)
add_executable(test_platform_basic
    test_platform_basic.cpp
)
target_link_libraries(test_platform_basic PRIVATE chainsaw_platform)
add_test(NAME test_platform_basic COMMAND test_platform_basic)

# Базовый тест CLI (без GoogleTest для обратной совместимости)
add_executable(test_cli_basic
    test_cli_basic.cpp
)
target_link_libraries(test_cli_basic PRIVATE chainsaw_cli chainsaw_output chainsaw_platform)
add_test(NAME test_cli_basic COMMAND test_cli_basic)

# ==============================================================================
# GoogleTest тесты (Step 25+)
# ==============================================================================
if(GTEST_AVAILABLE)
    # TST-PLATFORM: тесты платформенного модуля с GoogleTest
    chainsaw_add_test(test_platform_gtest
        SOURCES test_platform_gtest.cpp
        LIBS chainsaw_platform
    )

    # TST-CLI: тесты CLI с GoogleTest
    chainsaw_add_test(test_cli_gtest
        SOURCES test_cli_gtest.cpp
        LIBS chainsaw_cli chainsaw_output chainsaw_platform
    )

    # TST-OUTPUT: тесты модуля вывода с GoogleTest
    chainsaw_add_test(test_output_gtest
        SOURCES test_output_gtest.cpp
        LIBS chainsaw_output chainsaw_platform
    )
endif()
