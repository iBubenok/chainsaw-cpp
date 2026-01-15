# ==============================================================================
# CMakeLists.txt - Корневой файл сборки порта Chainsaw (Rust -> C++)
# ==============================================================================
#
# MOD-: корневой build, собирает все модули
# REQ-OPS-0023: воспроизводимый цикл configure -> build -> test
# ADR-0001: CMake + C++17
# ADR-0002: офлайн-сборка (без сети по умолчанию)
# : интеграция quality gates
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
# Quality gates ():
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
set(CMAKE_CXX_STANDARD 20)
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
# Санитайзеры (REQ-SEC-0022, )
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
# RapidJSON (ADR-0003: JSON сериализация)
# ==============================================================================
set(RAPIDJSON_VENDORED_DIR "${CMAKE_SOURCE_DIR}/../third_party/rapidjson")
if(EXISTS "${RAPIDJSON_VENDORED_DIR}/include")
    message(STATUS "RapidJSON: используется вендорированная версия из ${RAPIDJSON_VENDORED_DIR}")
    include_directories(SYSTEM "${RAPIDJSON_VENDORED_DIR}/include")
else()
    message(FATAL_ERROR "RapidJSON не найден в ${RAPIDJSON_VENDORED_DIR}. "
        "Выполните: git clone --depth 1 https://github.com/Tencent/rapidjson.git third_party/rapidjson")
endif()

# ==============================================================================
# pugixml (SLICE-006: XML Parser)
# ==============================================================================
# ADR-0009: pugixml выбран для парсинга XML (MIT лицензия, header+cpp)
set(PUGIXML_VENDORED_DIR "${CMAKE_SOURCE_DIR}/../third_party/pugixml")
if(EXISTS "${PUGIXML_VENDORED_DIR}/src/pugixml.hpp")
    message(STATUS "pugixml: используется вендорированная версия из ${PUGIXML_VENDORED_DIR}")
    include_directories(SYSTEM "${PUGIXML_VENDORED_DIR}/src")
    # pugixml как статическая библиотека
    add_library(pugixml STATIC "${PUGIXML_VENDORED_DIR}/src/pugixml.cpp")
    target_include_directories(pugixml PUBLIC "${PUGIXML_VENDORED_DIR}/src")
    # Отключаем предупреждения для pugixml (сторонняя библиотека)
    if(MSVC)
        target_compile_options(pugixml PRIVATE /W0)
    else()
        target_compile_options(pugixml PRIVATE -w)
    endif()
else()
    message(FATAL_ERROR "pugixml не найден в ${PUGIXML_VENDORED_DIR}. "
        "Выполните: curl -sL https://github.com/zeux/pugixml/releases/download/v1.14/pugixml-1.14.tar.gz | tar -xz -C third_party && mv third_party/pugixml-1.14 third_party/pugixml")
endif()

# ==============================================================================
# SLICE-016: ESEDB Parser (Собственная реализация)
# ==============================================================================
# Собственный кроссплатформенный парсер ESE Database.
# Не требует внешних зависимостей (заменён libesedb).
# Поддерживаемые платформы: Windows, Linux, macOS.
#
message(STATUS "ESEDB Parser: собственная реализация (без внешних зависимостей)")

# ==============================================================================
# yaml-cpp (SLICE-009: Chainsaw Rules Loader)
# ==============================================================================
# ADR-0011: yaml-cpp выбран для парсинга YAML правил (MIT лицензия)
set(YAML_CPP_VENDORED_DIR "${CMAKE_SOURCE_DIR}/../third_party/yaml-cpp")
if(EXISTS "${YAML_CPP_VENDORED_DIR}/include/yaml-cpp/yaml.h")
    message(STATUS "yaml-cpp: используется вендорированная версия из ${YAML_CPP_VENDORED_DIR}")

    # Настройки yaml-cpp
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)

    # Добавляем yaml-cpp как подпроект
    add_subdirectory("${YAML_CPP_VENDORED_DIR}" "${CMAKE_BINARY_DIR}/yaml-cpp" EXCLUDE_FROM_ALL)

    # Отключаем предупреждения для yaml-cpp (сторонняя библиотека)
    if(TARGET yaml-cpp)
        if(MSVC)
            target_compile_options(yaml-cpp PRIVATE /W0)
        else()
            target_compile_options(yaml-cpp PRIVATE -w)
        endif()
        # Помечаем include directories yaml-cpp как SYSTEM чтобы избежать
        # предупреждений от заголовочных файлов yaml-cpp при -Werror
        get_target_property(YAML_CPP_INCLUDE_DIRS yaml-cpp INTERFACE_INCLUDE_DIRECTORIES)
        if(YAML_CPP_INCLUDE_DIRS)
            set_target_properties(yaml-cpp PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${YAML_CPP_INCLUDE_DIRS}")
        endif()
    endif()
else()
    message(FATAL_ERROR "yaml-cpp не найден в ${YAML_CPP_VENDORED_DIR}. "
        "Выполните: curl -sL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz | tar -xz -C third_party && mv third_party/yaml-cpp-0.8.0 third_party/yaml-cpp")
endif()

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

# MOD-0005: io::discovery - поиск файлов
add_library(chainsaw_discovery STATIC
    src/io/discovery.cpp
)
target_link_libraries(chainsaw_discovery PRIVATE chainsaw_platform)
target_include_directories(chainsaw_discovery PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# MOD-0006: io::reader + MOD-0007: formats (Value, Reader, JSON/JSONL/XML/EVTX/HVE/ESEDB)
# SLICE-005: Reader Framework + JSON Parser
# SLICE-006: XML Parser (pugixml)
# SLICE-007: EVTX Parser
# SLICE-015: HVE Parser (Windows Registry Hive)
# SLICE-016: ESEDB Parser (ESE Database)
# SLICE-018: MFT Parser (NTFS Master File Table)
add_library(chainsaw_reader STATIC
    src/io/value.cpp
    src/io/reader.cpp
    src/io/evtx.cpp
    src/io/hve.cpp
    src/io/esedb.cpp
    src/io/mft.cpp
)
target_link_libraries(chainsaw_reader PRIVATE chainsaw_platform pugixml)
target_include_directories(chainsaw_reader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# SLICE-016: ESEDB Parser - собственная реализация (без внешних зависимостей)

# MOD-0009: tau - Tau Engine (Expression IR + Solver)
# SLICE-008: Tau Engine Implementation
add_library(chainsaw_tau STATIC
    src/tau/tau.cpp
)
target_link_libraries(chainsaw_tau PRIVATE chainsaw_reader chainsaw_platform)
target_include_directories(chainsaw_tau PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# MOD-0008: rule - Chainsaw Rules Loader + Sigma Rules
# SLICE-009: Chainsaw Rules Loader Implementation
# SLICE-010: Sigma Rules Loader Implementation
add_library(chainsaw_rule STATIC
    src/rule/rule.cpp
    src/rule/sigma.cpp
)
target_link_libraries(chainsaw_rule PRIVATE chainsaw_tau chainsaw_reader chainsaw_platform yaml-cpp)
target_include_directories(chainsaw_rule PUBLIC
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

# SLICE-011: search - Search Command Implementation
add_library(chainsaw_search STATIC
    src/search/search.cpp
)
target_link_libraries(chainsaw_search PRIVATE chainsaw_reader chainsaw_tau chainsaw_platform)
target_include_directories(chainsaw_search PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# SLICE-012: hunt - Hunt Command Implementation
add_library(chainsaw_hunt STATIC
    src/hunt/hunt.cpp
)
target_link_libraries(chainsaw_hunt PRIVATE
    chainsaw_reader
    chainsaw_tau
    chainsaw_rule
    chainsaw_platform
    yaml-cpp
)
target_include_directories(chainsaw_hunt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# SLICE-017: srum - SRUM Analyser Implementation
add_library(chainsaw_srum STATIC
    src/analyse/srum.cpp
)
target_link_libraries(chainsaw_srum PRIVATE
    chainsaw_reader
    chainsaw_platform
)
target_include_directories(chainsaw_srum PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# SLICE-016: ESEDB Parser - интегрирован в SRUM (собственная реализация)

# SLICE-019: shimcache - Shimcache Analyser Implementation
add_library(chainsaw_shimcache STATIC
    src/analyse/shimcache.cpp
)
target_link_libraries(chainsaw_shimcache PRIVATE
    chainsaw_reader
    chainsaw_platform
)
target_include_directories(chainsaw_shimcache PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# MOD-0001: app - точка входа
add_executable(chainsaw
    src/app/main.cpp
)
target_link_libraries(chainsaw PRIVATE
    chainsaw_cli
    chainsaw_output
    chainsaw_platform
    chainsaw_discovery
    chainsaw_search
    chainsaw_hunt
    chainsaw_srum
    chainsaw_shimcache
    chainsaw_reader
    chainsaw_rule
)

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

    # Таргет chainsaw-format: форматирование кода
    # Используем префикс chainsaw- чтобы избежать конфликта с yaml-cpp target
    add_custom_target(chainsaw-format
        COMMAND ${CLANG_FORMAT_EXE} -i ${ALL_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Форматирование кода с помощью clang-format"
        VERBATIM
    )

    # Таргет chainsaw-format-check: проверка форматирования (без изменения файлов)
    add_custom_target(chainsaw-format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${ALL_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Проверка форматирования кода"
        VERBATIM
    )

    message(STATUS "clang-format найден: ${CLANG_FORMAT_EXE}")
    message(STATUS "  Доступны таргеты: chainsaw-format, chainsaw-format-check")
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
