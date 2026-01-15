// ==============================================================================
// chainsaw/discovery.hpp - MOD-0005: File Discovery
// ==============================================================================
//
// MOD-0005 io::discovery
// SLICE-004: File Discovery (get_files -> discover_files)
// SPEC-SLICE-004: micro-spec поведения
// ADR-0010: std::filesystem::path
// ADR-0011: детерминизм (сортировка результатов)
//
// Назначение:
// - Рекурсивный обход директорий для поиска файлов
// - Фильтрация по расширениям (INV-001)
// - Специальный случай $MFT (INV-002)
// - Детерминированный порядок результатов (INV-003 + ADR-0011)
// - Режим skip_errors для обработки ошибок (INV-005)
//
// ==============================================================================

#ifndef CHAINSAW_DISCOVERY_HPP
#define CHAINSAW_DISCOVERY_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace chainsaw::io {

// ----------------------------------------------------------------------------
// DiscoveryOptions - параметры поиска файлов
// ----------------------------------------------------------------------------

/// Параметры для функции discover_files()
/// SPEC-SLICE-004: параметры из Rust get_files()
struct DiscoveryOptions {
    /// Набор допустимых расширений (БЕЗ точки: "evtx", не ".evtx")
    /// INV-001: расширения сравниваются без точки
    /// INV-002: "$MFT" - специальный случай для файла без расширения
    /// None (nullopt) означает все файлы
    std::optional<std::unordered_set<std::string>> extensions;

    /// Режим обработки ошибок
    /// INV-005: true = предупреждения в stderr вместо ошибок
    bool skip_errors = false;
};

// ----------------------------------------------------------------------------
// discover_files - основная функция поиска
// ----------------------------------------------------------------------------

/// Найти файлы по путям с фильтрацией по расширениям
///
/// @param inputs Вектор путей к файлам или директориям
/// @param opt Параметры поиска (расширения, skip_errors)
/// @return Отсортированный по пути список найденных файлов (ADR-0011)
///
/// Поведение (из SPEC-SLICE-004):
/// - Если путь - файл: проверяет расширение и добавляет при совпадении
/// - Если путь - директория: рекурсивно обходит все поддиректории (INV-004)
/// - При ошибках с skip_errors=true: выводит предупреждение в stderr (INV-005)
/// - Результат сортируется для детерминизма (ADR-0011)
/// - Пустой результат - не ошибка (INV-006)
///
/// Специальные случаи:
/// - INV-002: файл "$MFT" добавляется если extensions содержит "$MFT"
/// - INV-001: расширения сравниваются case-sensitive
///
/// @throws std::runtime_error при ошибке (если skip_errors=false)
std::vector<std::filesystem::path> discover_files(const std::vector<std::filesystem::path>& inputs,
                                                  const DiscoveryOptions& opt);

}  // namespace chainsaw::io

#endif  // CHAINSAW_DISCOVERY_HPP
