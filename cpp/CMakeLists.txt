# ==============================================================================
# CMakeLists.txt - Корневой файл сборки порта Chainsaw (Rust -> C++)
# ==============================================================================
#
# MOD-: корневой build, собирает все модули
# REQ-OPS-0023: воспроизводимый цикл configure -> build -> test
# ADR-0001: CMake + C++17
# ADR-0002: офлайн-сборка (без сети по умолчанию)
# Step 25: интеграция quality gates
#
# Использование (single-config, Ninja/Unix Makefiles):
#   cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ctest --test-dir build --output-on-failure
#
# Использование (multi-config, Visual Studio/Xcode):
#   cmake -S cpp -B build
#   cmake --build build --config Release
#   ctest --test-dir build -C Release --output-on-failure
#
# Quality gates (Step 25):
#   # Санитайзеры (Linux/macOS):
#   cmake -S cpp -B build -DCHAINSAW_SANITIZER=address
#   cmake -S cpp -B build -DCHAINSAW_SANITIZER=undefined
#
#   # Статический анализ (при наличии clang-tidy):
#   cmake -S cpp -B build -DCMAKE_CXX_CLANG_TIDY="clang-tidy"
#
#   # Форматирование (вручную):
#   clang-format -i cpp/src/**/*.cpp cpp/include/**/*.hpp
#
# ==============================================================================

cmake_minimum_required(VERSION 3.16)

project(chainsaw
    VERSION 0.1.0
    DESCRIPTION "Порт Chainsaw на C++ для быстрой работы с криминалистическими артефактами"
    LANGUAGES CXX
)

# ==============================================================================
# Пути к модулям CMake
# ==============================================================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ==============================================================================
# Стандарт языка (ADR-0001)
# ==============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Экспорт compile_commands.json для clang-tidy и IDE
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# Опции сборки
# ==============================================================================
option(CHAINSAW_BUILD_TESTS "Собирать тесты" ON)
option(CHAINSAW_WARNINGS_AS_ERRORS "Трактовать предупреждения как ошибки" OFF)
option(CHAINSAW_USE_GTEST "Использовать GoogleTest для тестов (ADR-0008)" ON)
option(CHAINSAW_ENABLE_CLANG_TIDY "Включить clang-tidy анализ при сборке" OFF)

# ==============================================================================
# Санитайзеры (REQ-SEC-0022, Step 25)
# ==============================================================================
include(Sanitizers)

# ==============================================================================
# Настройки предупреждений компилятора (GUIDE-0001)
# ==============================================================================
if(MSVC)
    # MSVC: строгие предупреждения
    add_compile_options(
        /W4           # Высокий уровень предупреждений
        /permissive-  # Строгий режим соответствия стандарту
    )
    if(CHAINSAW_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
    # Отключаем предупреждения для безопасных версий функций CRT
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    # GCC/Clang: строгие предупреждения
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wformat=2
        -Wundef
        -Wcast-qual
        -Wcast-align
        -Wwrite-strings
        -Wdouble-promotion
        -Wnull-dereference
    )
    # Дополнительные предупреждения для GCC
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(
            -Wlogical-op
            -Wduplicated-cond
            -Wduplicated-branches
        )
    endif()
    # Дополнительные предупреждения для Clang
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(
            -Wno-unknown-warning-option
        )
    endif()
    if(CHAINSAW_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

# ==============================================================================
# clang-tidy интеграция (опционально)
# ==============================================================================
if(CHAINSAW_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-17 clang-tidy-16 clang-tidy-15)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy найден: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "clang-tidy не найден, статический анализ отключён")
    endif()
endif()

# ==============================================================================
# Пути к заголовкам
# ==============================================================================
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ==============================================================================
# Модули (MOD-*)
# ==============================================================================

# MOD-0004: platform - платформенные абстракции
add_library(chainsaw_platform STATIC
    src/platform/platform.cpp
)
target_include_directories(chainsaw_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# MOD-0003: output - пользовательский вывод
add_library(chainsaw_output STATIC
    src/output/output.cpp
)
target_link_libraries(chainsaw_output PRIVATE chainsaw_platform)
target_include_directories(chainsaw_output PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# MOD-0002: cli - парсинг аргументов командной строки
add_library(chainsaw_cli STATIC
    src/cli/cli.cpp
    src/cli/commands.cpp
)
target_link_libraries(chainsaw_cli PRIVATE chainsaw_output chainsaw_platform)
target_include_directories(chainsaw_cli PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# MOD-0001: app - точка входа
add_executable(chainsaw
    src/app/main.cpp
)
target_link_libraries(chainsaw PRIVATE chainsaw_cli chainsaw_output chainsaw_platform)

# ==============================================================================
# Тесты (ADR-0008)
# ==============================================================================
if(CHAINSAW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ==============================================================================
# Установка
# ==============================================================================
install(TARGETS chainsaw
    RUNTIME DESTINATION bin
)

# ==============================================================================
# Вспомогательные таргеты для разработчика
# ==============================================================================

# Поиск clang-format
find_program(CLANG_FORMAT_EXE NAMES clang-format clang-format-17 clang-format-16 clang-format-15)

if(CLANG_FORMAT_EXE)
    # Сбор всех исходных файлов
    file(GLOB_RECURSE ALL_SOURCE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
    )

    # Таргет format: форматирование кода
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${ALL_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Форматирование кода с помощью clang-format"
        VERBATIM
    )

    # Таргет format-check: проверка форматирования (без изменения файлов)
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${ALL_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Проверка форматирования кода"
        VERBATIM
    )

    message(STATUS "clang-format найден: ${CLANG_FORMAT_EXE}")
    message(STATUS "  Доступны таргеты: format, format-check")
else()
    message(STATUS "clang-format не найден, таргеты форматирования недоступны")
endif()

# ==============================================================================
# Информация о сборке
# ==============================================================================
message(STATUS "")
message(STATUS "=== Chainsaw C++ Port ===")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build Tests: ${CHAINSAW_BUILD_TESTS}")
message(STATUS "  Use GoogleTest: ${CHAINSAW_USE_GTEST}")
message(STATUS "  Sanitizer: ${CHAINSAW_SANITIZER}")
message(STATUS "  Warnings as Errors: ${CHAINSAW_WARNINGS_AS_ERRORS}")
message(STATUS "")
