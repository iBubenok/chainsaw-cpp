# SPEC-SLICE-001 — Micro-spec для Platform Layer

## Метаданные
- **SLICE-ID:** SLICE-001
- **Название:** Platform Abstractions
- **MOD-\*:** MOD-0004
- **TST-\*:** — (нет upstream тестов; покрытие через unit-тесты C++)
- **RUN-\*:** — (нет golden runs; инфраструктурный слайс)
- **REQ-\*:** REQ-NFR-0012 (кроссплатформенность), REQ-NFR-0015 (воспроизводимость), REQ-NFR-0016 (1:1 вывод), REQ-SEC-0017 (безопасность входов)
- **RISK-\*:** RISK-0007 (пути с пробелами)

## Наблюдаемое поведение (FACTS)

### F1. Работа с путями (path handling)

**Источник:** `upstream/chainsaw/src/file/mod.rs`, `upstream/chainsaw/src/main.rs`

1. **[FACT-001]** Chainsaw использует `std::path::{Path, PathBuf, OsStr}` для работы с путями (file/mod.rs:3, main.rs:7).
2. **[FACT-002]** `PathBuf` используется для CLI аргументов (main.rs:63, 79, 93, 99, 102, 106, 109).
3. **[FACT-003]** Преобразование OsStr → String выполняется через `to_string_lossy` с fallback при невалидном UTF-8 (file/mod.rs:481, file/mft.rs:167-168).
4. **[FACT-004]** Преобразование &[u8] → String выполняется через `from_utf8_lossy` для обработки non-ASCII символов (file/esedb/srum.rs:56-58).
5. **[FACT-005]** Получение текущей директории: `std::env::current_dir` используется как fallback для Search команды (main.rs:911).
6. **[FACT-006]** Проверка разделителя пути: `path::is_separator(c)` (file/mft.rs:162).

### F2. TTY Detection

**Источник:** `upstream/chainsaw/src/main.rs`, `upstream/chainsaw/src/cli.rs`, `upstream/chainsaw/src/write.rs`

7. **[FACT-007]** Размер терминала определяется через `terminal_size` крейт (main.rs:340-341).
8. **[FACT-008]** Ширина колонок таблицы зависит от ширины терминала (main.rs:342-350).
9. **[FACT-009]** Progress bar скрывается при `quiet=true` или `verbose=true` (cli.rs:44-52).
10. **[FACT-010]** Progress bar использует `ProgressDrawTarget::stderr` для TTY и `hidden` для non-TTY (cli.rs:48-51).

### F3. Временные файлы (Temp Files)

**Источник:** `upstream/chainsaw/src/main.rs`

11. **[FACT-011]** Временные файлы создаются через `tempfile::tempfile` для флага `--cache-to-disk` (main.rs:757-765).
12. **[FACT-012]** При ошибке создания temp файла: `anyhow::bail!("Failed to create cache on disk - {}", e)`.
13. **[FACT-013]** Кеш используется в команде Hunt для промежуточного хранения результатов.

### F4. Платформенные константы (Platform-specific)

**Источник:** `upstream/chainsaw/src/cli.rs`

14. **[FACT-014]** Префикс правил: `RULE_PREFIX = "‣"` (non-Windows) / `"+"` (Windows) (cli.rs:25-28).
15. **[FACT-015]** Символы progress bar: Unicode braille dots на non-Windows, ASCII `-\|/-` на Windows (cli.rs:30-35).
16. **[FACT-016]** Скорость обновления progress bar: 80ms (non-Windows) / 200ms (Windows) (cli.rs:31, 35).

### F5. Кодировки (Encoding)

**Источник:** `upstream/chainsaw/src/file/esedb/srum.rs`, `upstream/chainsaw/src/hunt.rs`

17. **[FACT-017]** `from_utf8_lossy` используется для обработки Chinese characters и других non-ASCII (file/esedb/srum.rs:56-58, комментарий).
18. **[FACT-018]** Нулевые байты удаляются: `.replace('\u{0000}', "")` (file/esedb/srum.rs:58).
19. **[FACT-019]** `from_utf8_unchecked` используется только после гарантии валидности UTF-8 (hunt.rs:347).

### F6. Зависимости Rust (для понимания C++ аналогов)

**Источник:** `upstream/chainsaw/Cargo.toml`

| Rust крейт | Версия | Назначение | C++ аналог (ADR) |
|------------|--------|------------|------------------|
| `std::path` | std | Path handling | `std::filesystem::path` (ADR-0010) |
| `std::fs` | std | File system | `std::filesystem` (ADR-0010) |
| `std::env` | std | Environment | `<cstdlib>` |
| `tempfile` | 3.2 | Temp files | Pending (TOBE-0001/4.4) |
| `terminal_size` | 0.4 | Terminal size | Pending (нет в скелете) |
| `crossterm` | 0.29 | TTY control | Не требуется для 1:1 (output слой) |

## Ожидаемые входы/выходы

### API MOD-0004 (согласно TOBE-0001/4.4 + ADR-0010)

```cpp
namespace chainsaw::platform {
 // Преобразования путей (ADR-0010)
 std::filesystem::path path_from_utf8(std::string_view u8str);
 std::string path_to_utf8(const std::filesystem::path& p); // lossy на Unix

 // TTY detection
 bool is_tty_stdout;
 bool is_tty_stderr;

 // Временные файлы
 std::filesystem::path make_temp_file(std::string_view prefix); // Pending

 // Платформенные константы
 const char* rule_prefix; // "‣" / "+"
 std::string os_name; // "Windows" / "Linux" / "macOS"
}
```

### Текущее состояние C++

В `cpp/include/chainsaw/platform.hpp` и `cpp/src/platform/platform.cpp` уже реализовано:
- `path_from_utf8` ✓
- `path_to_utf8` ✓
- `is_tty_stdout` ✓
- `is_tty_stderr` ✓
- `rule_prefix` ✓
- `os_name` ✓

**Не реализовано:**
- `make_temp_file` — требуется для SLICE-012 (Hunt, --cache-to-disk)

## Критерий закрытия слайса (UnitDone)

| # | Критерий | Проверка |
|---|----------|----------|
| 1 | API соответствует TOBE-0001/4.4 | Функции из micro-spec присутствуют в `platform.hpp` |
| 2 | Компилируется на 3 ОС | Сборка PASS на Windows/Linux/macOS (CI или SSH) |
| 3 | Unit-тесты path conversion | Тесты `path_from_utf8` / `path_to_utf8` включают edge cases |
| 4 | Unit-тесты TTY detection | Тесты `is_tty_stdout` / `is_tty_stderr` проходят |
| 5 | Unit-тесты temp files | Тест `make_temp_file` проверяет создание и удаление |
| 6 | Unit-тесты platform constants | Тесты `rule_prefix` и `os_name` |
| 7 | Нет регрессий | Существующие тесты (-25) проходят |

## Зависимости

- **Входные SLICE-\*:** — (это первый слайс, нет зависимостей)
- **Блокирует:** SLICE-002 (Output), SLICE-003 (CLI), SLICE-004 (File Discovery), SLICE-005 (Reader)

## Оценка S1-S4

| Ось | Оценка | Обоснование |
|-----|--------|-------------|
| S1 (ширина) | Low | Один модуль MOD-0004 |
| S2 (сложность проверки) | Low | Unit-тесты, нет e2e |
| S3 (unknowns) | Low | ADR-0010 принят, контракт чёткий |
| S4 (platform-specific) | Medium | Windows vs Unix пути, но логика изолирована |

**Вывод:** Max(S1-S4) = Medium. Правило гранулярности выполнено (не более одной High оси).

## Расширения относительно текущего скелета

### 1. Добавить `make_temp_file`

```cpp
std::filesystem::path make_temp_file(std::string_view prefix) {
 // Создаёт временный файл с указанным префиксом
 // Возвращает путь к созданному файлу
 // Файл должен существовать после вызова (но может быть пустым)
 // При ошибке выбрасывает std::runtime_error
}
```

Реализация:
- Windows: `GetTempPath` + `CreateFile` с `FILE_ATTRIBUTE_TEMPORARY`
- Unix: `mkstemp` или `std::tmpfile` с переименованием

### 2. Расширить unit-тесты

Добавить в `cpp/tests/test_platform_gtest.cpp`:
- Тесты path conversion с Unicode символами
- Тесты path conversion с пробелами
- Тесты TTY (mock не требуется, проверяем возврат bool)
- Тесты make_temp_file (создание, проверка существования, удаление)

## Ссылки

- **Rust исходники:**
 - `upstream/chainsaw/src/file/mod.rs` — path handling
 - `upstream/chainsaw/src/main.rs:757-765` — tempfile usage
 - `upstream/chainsaw/src/cli.rs:25-35` — platform constants
 - `upstream/chainsaw/src/write.rs` — output management
- **C++ реализация:**
 - `cpp/include/chainsaw/platform.hpp` — текущий API
 - `cpp/src/platform/platform.cpp` — текущая реализация
- **Архитектура:**
 - `docs/architecture/TOBE-0001-cpp-to-be-architecture.md` — MOD-0004 spec
 - `docs/adr/ADR-0010-filesystem-and-path-encoding.md` — решение по путям
- **Тесты:**
 - `cpp/tests/test_platform_gtest.cpp` — существующие тесты
