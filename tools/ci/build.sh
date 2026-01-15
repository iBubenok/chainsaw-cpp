#!/usr/bin/env bash
# ==============================================================================
# build.sh - Скрипт сборки для Unix (Linux/macOS)
# ==============================================================================
#
# : CI кроссплатформенный
#
# Использование:
#   ./tools/ci/build.sh [OPTIONS]
#
# Опции:
#   --build-type TYPE    Тип сборки: Debug|Release (default: Release)
#   --sanitizer SAN      Санитайзер: address|undefined|thread|memory|none (default: none)
#   --warnings-as-errors Трактовать предупреждения как ошибки
#   --no-tests           Не собирать тесты
#   --clean              Очистить каталог сборки перед конфигурацией
#   --test               Запустить тесты после сборки
#   --format-check       Проверить форматирование кода
#   --jobs N             Количество параллельных задач (default: auto)
#   --help               Показать справку
#
# Примеры:
#   ./tools/ci/build.sh --build-type Debug --sanitizer address --test
#   ./tools/ci/build.sh --clean --warnings-as-errors --test
#
# ==============================================================================

set -euo pipefail

# Определяем корень проекта относительно скрипта
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CPP_DIR="$PROJECT_ROOT/cpp"
BUILD_DIR="$PROJECT_ROOT/build"

# Параметры по умолчанию
BUILD_TYPE="Release"
SANITIZER="none"
WARNINGS_AS_ERRORS="OFF"
BUILD_TESTS="ON"
CLEAN_BUILD=false
RUN_TESTS=false
FORMAT_CHECK=false
JOBS=""

# Определяем количество CPU
if command -v nproc &> /dev/null; then
    DEFAULT_JOBS=$(nproc)
elif command -v sysctl &> /dev/null; then
    DEFAULT_JOBS=$(sysctl -n hw.ncpu)
else
    DEFAULT_JOBS=4
fi

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $*"
}

log_success() {
    echo -e "${GREEN}[OK]${NC} $*"
}

log_warning() {
    echo -e "${YELLOW}[WARN]${NC} $*"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*" >&2
}

show_help() {
    head -40 "$0" | tail -30
    exit 0
}

# Парсинг аргументов
while [[ $# -gt 0 ]]; do
    case $1 in
        --build-type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        --sanitizer)
            SANITIZER="$2"
            shift 2
            ;;
        --warnings-as-errors)
            WARNINGS_AS_ERRORS="ON"
            shift
            ;;
        --no-tests)
            BUILD_TESTS="OFF"
            shift
            ;;
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --test)
            RUN_TESTS=true
            shift
            ;;
        --format-check)
            FORMAT_CHECK=true
            shift
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --help)
            show_help
            ;;
        *)
            log_error "Неизвестная опция: $1"
            exit 1
            ;;
    esac
done

# Установка количества задач
JOBS="${JOBS:-$DEFAULT_JOBS}"

# Вывод конфигурации
log_info "=== Chainsaw C++ Build ==="
log_info "Project root: $PROJECT_ROOT"
log_info "Build type: $BUILD_TYPE"
log_info "Sanitizer: $SANITIZER"
log_info "Warnings as errors: $WARNINGS_AS_ERRORS"
log_info "Build tests: $BUILD_TESTS"
log_info "Parallel jobs: $JOBS"
echo ""

# Проверка наличия CMake
if ! command -v cmake &> /dev/null; then
    log_error "CMake не найден. Установите CMake версии 3.16 или выше."
    exit 1
fi

# Очистка каталога сборки
if $CLEAN_BUILD && [[ -d "$BUILD_DIR" ]]; then
    log_info "Очистка каталога сборки..."
    rm -rf "$BUILD_DIR"
fi

# Формирование аргументов CMake
CMAKE_ARGS=(
    -S "$CPP_DIR"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCHAINSAW_BUILD_TESTS="$BUILD_TESTS"
    -DCHAINSAW_USE_GTEST=ON
    -DCHAINSAW_WARNINGS_AS_ERRORS="$WARNINGS_AS_ERRORS"
)

# Добавление санитайзера
if [[ "$SANITIZER" != "none" ]]; then
    CMAKE_ARGS+=(-DCHAINSAW_SANITIZER="$SANITIZER")
fi

# Выбор генератора
if command -v ninja &> /dev/null; then
    CMAKE_ARGS+=(-G Ninja)
    log_info "Используется генератор: Ninja"
else
    log_info "Используется генератор: Unix Makefiles"
fi

# Конфигурация
log_info "Конфигурация CMake..."
cmake "${CMAKE_ARGS[@]}"

# Сборка
log_info "Сборка проекта..."
cmake --build "$BUILD_DIR" -j "$JOBS"

log_success "Сборка завершена успешно"

# Проверка форматирования
if $FORMAT_CHECK; then
    log_info "Проверка форматирования кода..."
    if cmake --build "$BUILD_DIR" --target format-check; then
        log_success "Форматирование соответствует стандарту"
    else
        log_error "Форматирование не соответствует стандарту"
        log_info "Запустите 'cmake --build build --target format' для исправления"
        exit 1
    fi
fi

# Запуск тестов
if $RUN_TESTS && [[ "$BUILD_TESTS" == "ON" ]]; then
    log_info "Запуск тестов..."
    if ctest --test-dir "$BUILD_DIR" --output-on-failure; then
        log_success "Все тесты пройдены"
    else
        log_error "Некоторые тесты не прошли"
        exit 1
    fi
fi

log_success "=== Сборка завершена ==="
