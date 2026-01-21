# ==============================================================================
# tests/CMakeLists.txt - Тесты для порта Chainsaw
# ==============================================================================
#
# ADR-0008: GoogleTest как фреймворк тестирования
# GUIDE-0001 G-070: test-to-test перенос
# : интеграция quality gates + GoogleTest
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
# Базовые тесты ()
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
# GoogleTest тесты (+)
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

    # TST-DISC: тесты модуля File Discovery с GoogleTest
    # SLICE-004, SPEC-SLICE-004
    chainsaw_add_test(test_discovery_gtest
        SOURCES test_discovery_gtest.cpp
        LIBS chainsaw_discovery chainsaw_platform
    )

    # TST-RDR, TST-JSON, TST-JSONL, TST-VALUE: тесты Reader Framework
    # SLICE-005, SPEC-SLICE-005
    # TST-EVTX: тесты EVTX парсера (SLICE-007, SPEC-SLICE-007)
    chainsaw_add_test(test_reader_gtest
        SOURCES test_reader_gtest.cpp
        LIBS chainsaw_reader chainsaw_platform
    )
    # Определяем CMAKE_SOURCE_DIR для разрешения путей к fixture файлам
    target_compile_definitions(test_reader_gtest PRIVATE
        CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    # TST-TAU-001..022: тесты Tau Engine
    # SLICE-008, SPEC-SLICE-008
    chainsaw_add_test(test_tau_gtest
        SOURCES test_tau_gtest.cpp
        LIBS chainsaw_tau chainsaw_reader chainsaw_platform
    )

    # TST-CSRULE-001..024: тесты Chainsaw Rules Loader
    # SLICE-009, SPEC-SLICE-009
    chainsaw_add_test(test_rule_gtest
        SOURCES test_rule_gtest.cpp
        LIBS chainsaw_rule chainsaw_tau chainsaw_reader chainsaw_platform
    )
    # Определяем CMAKE_SOURCE_DIR для разрешения путей к fixture файлам
    target_compile_definitions(test_rule_gtest PRIVATE
        CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    # TST-SIGMA-001..028: тесты Sigma Rules Loader
    # SLICE-010, SPEC-SLICE-010
    chainsaw_add_test(test_sigma_gtest
        SOURCES test_sigma_gtest.cpp
        LIBS chainsaw_rule chainsaw_tau chainsaw_reader chainsaw_platform
    )
    # Определяем CMAKE_SOURCE_DIR для разрешения путей к fixture файлам
    target_compile_definitions(test_sigma_gtest PRIVATE
        CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    # TST-SEARCH-001..021: тесты Search Command
    # SLICE-011, SPEC-SLICE-011
    chainsaw_add_test(test_search_gtest
        SOURCES test_search_gtest.cpp
        LIBS chainsaw_search chainsaw_reader chainsaw_tau chainsaw_platform
    )

    # TST-HUNT-001..025: тесты Hunt Command
    # SLICE-012, SPEC-SLICE-012
    chainsaw_add_test(test_hunt_gtest
        SOURCES test_hunt_gtest.cpp
        LIBS chainsaw_hunt chainsaw_rule chainsaw_tau chainsaw_search chainsaw_reader chainsaw_platform
    )

    # TST-DUMP-001..016, TST-DUMP-INT-001..008: тесты Dump Command
    # SLICE-013, SPEC-SLICE-013
    chainsaw_add_test(test_dump_gtest
        SOURCES test_dump_gtest.cpp
        LIBS chainsaw_reader chainsaw_discovery chainsaw_cli chainsaw_platform
    )

    # TST-HVE-001..015: тесты HVE Parser
    # SLICE-015, SPEC-SLICE-015
    chainsaw_add_test(test_hve_gtest
        SOURCES test_hve_gtest.cpp
        LIBS chainsaw_reader chainsaw_platform
    )

    # TST-ESEDB-001..017: тесты ESEDB Parser
    # SLICE-016, SPEC-SLICE-016
    chainsaw_add_test(test_esedb_gtest
        SOURCES test_esedb_gtest.cpp
        LIBS chainsaw_reader chainsaw_platform
    )

    # TST-SRUM-001..024: тесты SRUM Analyser
    # SLICE-017, SPEC-SLICE-017
    chainsaw_add_test(test_srum_gtest
        SOURCES test_srum_gtest.cpp
        LIBS chainsaw_srum chainsaw_reader chainsaw_platform
    )

    # TST-MFT-001..017: тесты MFT Parser
    # SLICE-018, SPEC-SLICE-018
    chainsaw_add_test(test_mft_gtest
        SOURCES test_mft_gtest.cpp
        LIBS chainsaw_reader chainsaw_platform
    )

    # TST-SHIM-001..020: тесты Shimcache Parser & Analyser
    # SLICE-019, SPEC-SLICE-019
    chainsaw_add_test(test_shimcache_gtest
        SOURCES test_shimcache_gtest.cpp
        LIBS chainsaw_shimcache chainsaw_reader chainsaw_platform
    )

    # TST-DET/ROB/SEC/CLI: Расширенные тесты (TEST-EXPAND-0001, )
    # P1 + P2 тесты: Determinism, Robustness, Security, CLI
    chainsaw_add_test(test_extended_gtest
        SOURCES test_extended_gtest.cpp
        LIBS chainsaw_reader chainsaw_discovery chainsaw_cli chainsaw_platform
    )
endif()

# ==============================================================================
# Sanitizer builds (TEST-EXPAND-0001 TST-SAN-*)
# ==============================================================================
# Использование:
#   cmake -DCHAINSAW_ENABLE_ASAN=ON ..
#   cmake -DCHAINSAW_ENABLE_UBSAN=ON ..
#   cmake -DCHAINSAW_ENABLE_ASAN=ON -DCHAINSAW_ENABLE_UBSAN=ON ..
#
# Поддерживаемые платформы:
#   - Linux: GCC/Clang
#   - macOS: Clang
#   - Windows: MSVC (ограниченная поддержка)

option(CHAINSAW_ENABLE_ASAN "Enable AddressSanitizer (ASan)" OFF)
option(CHAINSAW_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (UBSan)" OFF)

if(CHAINSAW_ENABLE_ASAN OR CHAINSAW_ENABLE_UBSAN)
    message(STATUS "=== Sanitizer builds enabled ===")

    # Общие флаги для sanitizers
    set(SANITIZER_FLAGS "")

    if(CHAINSAW_ENABLE_ASAN)
        message(STATUS "  AddressSanitizer: ON")
        if(MSVC)
            set(SANITIZER_FLAGS "${SANITIZER_FLAGS} /fsanitize=address")
        else()
            set(SANITIZER_FLAGS "${SANITIZER_FLAGS} -fsanitize=address")
        endif()
    endif()

    if(CHAINSAW_ENABLE_UBSAN)
        message(STATUS "  UndefinedBehaviorSanitizer: ON")
        if(MSVC)
            message(WARNING "UBSan not fully supported on MSVC")
        else()
            set(SANITIZER_FLAGS "${SANITIZER_FLAGS} -fsanitize=undefined")
        endif()
    endif()

    if(NOT MSVC)
        # Дополнительные флаги для GCC/Clang
        set(SANITIZER_FLAGS "${SANITIZER_FLAGS} -fno-omit-frame-pointer -g")
    endif()

    # Применяем флаги ко всем тестовым таргетам
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SANITIZER_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SANITIZER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${SANITIZER_FLAGS}")

    message(STATUS "  Sanitizer flags: ${SANITIZER_FLAGS}")
endif()
