// ==============================================================================
// discovery.cpp - MOD-0005: File Discovery
// ==============================================================================
//
// MOD-0005 io::discovery
// SLICE-004: File Discovery (get_files -> discover_files)
// SPEC-SLICE-004: реализация по micro-spec
//
// Rust source: upstream/chainsaw/src/file/mod.rs:435-498
//
// ==============================================================================

#include "chainsaw/discovery.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <system_error>

namespace chainsaw::io {

namespace {

// ----------------------------------------------------------------------------
// Вспомогательные функции
// ----------------------------------------------------------------------------

/// Проверяет, соответствует ли файл набору расширений
/// INV-001: расширения сравниваются БЕЗ точки, case-sensitive
/// INV-002: специальный случай $MFT
bool matches_extensions(const std::filesystem::path& file_path,
                        const std::optional<std::unordered_set<std::string>>& extensions) {
    // INV-006: если extensions = nullopt, принимаем все файлы
    if (!extensions.has_value()) {
        return true;
    }

    const auto& ext_set = extensions.value();

    // Получаем имя файла для проверки $MFT
    std::string filename = file_path.filename().string();

    // INV-002: специальный случай $MFT
    // Файл без расширения с именем "$MFT" добавляется, если extensions содержит "$MFT"
    if (filename == "$MFT" && ext_set.count("$MFT") > 0) {
        return true;
    }

    // INV-001: получаем расширение без точки
    if (!file_path.has_extension()) {
        return false;
    }

    // extension() возвращает расширение с точкой (например ".evtx")
    std::string ext = file_path.extension().string();
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);  // Убираем точку
    }

    // INV-001: case-sensitive сравнение
    return ext_set.count(ext) > 0;
}

/// Рекурсивно обходит директорию и собирает файлы
/// INV-004: depth-first, post-order сбор
/// INV-005: skip_errors -> stderr предупреждения
void collect_files_recursive(const std::filesystem::path& path,
                             const std::optional<std::unordered_set<std::string>>& extensions,
                             bool skip_errors, std::vector<std::filesystem::path>& result) {
    // SPEC-SLICE-004 алгоритм шаг 1: проверка существования
    std::error_code ec;
    bool exists = std::filesystem::exists(path, ec);

    if (ec) {
        if (skip_errors) {
            std::cerr << "[!] failed to check path existence - " << ec.message() << "\n";
            return;
        }
        throw std::runtime_error("failed to check path existence - " + ec.message());
    }

    if (!exists) {
        if (skip_errors) {
            std::cerr << "[!] Specified path does not exist - " << path.string() << "\n";
            return;
        }
        throw std::runtime_error("Specified event log path is invalid - " + path.string());
    }

    // SPEC-SLICE-004 алгоритм шаг 2: получение метаданных
    std::filesystem::file_status status = std::filesystem::status(path, ec);

    if (ec) {
        if (skip_errors) {
            std::cerr << "[!] failed to get metadata for file - " << ec.message() << "\n";
            return;
        }
        throw std::runtime_error("failed to get metadata for file - " + ec.message());
    }

    // SPEC-SLICE-004 алгоритм шаг 3: обработка директории
    if (std::filesystem::is_directory(status)) {
        std::filesystem::directory_iterator dir_iter(path, ec);

        if (ec) {
            if (skip_errors) {
                std::cerr << "[!] failed to read directory - " << ec.message() << "\n";
                return;
            }
            throw std::runtime_error("failed to read directory - " + ec.message());
        }

        // INV-004: рекурсивный обход
        for (auto it = dir_iter; it != std::filesystem::directory_iterator(); ++it) {
            const auto& entry = *it;
            std::error_code entry_ec;
            std::filesystem::path entry_path = entry.path();

            // Проверка на ошибку итератора
            if (entry_ec) {
                if (skip_errors) {
                    std::cerr << "[!] failed to enter directory - " << entry_ec.message() << "\n";
                    // INV-005: возвращаем накопленные файлы
                    return;
                }
                throw std::runtime_error("failed to enter directory - " + entry_ec.message());
            }

            // INV-004: рекурсивный вызов (depth-first)
            collect_files_recursive(entry_path, extensions, skip_errors, result);
        }
    }
    // SPEC-SLICE-004 алгоритм шаг 4: обработка файла
    else if (std::filesystem::is_regular_file(status)) {
        // INV-001, INV-002: проверка расширения
        if (matches_extensions(path, extensions)) {
            result.push_back(path);
        }
    }
    // Symlinks, special files etc. игнорируются (как в Rust)
}

}  // namespace

// ----------------------------------------------------------------------------
// Публичный API
// ----------------------------------------------------------------------------

std::vector<std::filesystem::path> discover_files(const std::vector<std::filesystem::path>& inputs,
                                                  const DiscoveryOptions& opt) {
    std::vector<std::filesystem::path> result;

    // Обрабатываем каждый входной путь
    for (const auto& input : inputs) {
        collect_files_recursive(input, opt.extensions, opt.skip_errors, result);
    }

    // INV-003 + ADR-0011: сортировка для детерминизма
    // Rust не сортирует (порядок зависит от ОС), но C++ порт делает это
    // для обеспечения кроссплатформенной воспроизводимости
    std::sort(result.begin(), result.end());

    // INV-006: пустой результат - не ошибка
    return result;
}

}  // namespace chainsaw::io
