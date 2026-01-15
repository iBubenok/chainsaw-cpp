# SPEC-SLICE-004 — File Discovery (get_files)

## Статус
- Версия: 1
- Дата: 2026-01-09
- Слайс: SLICE-004
- Статус: UnitReady
- Step: 31

## Назначение
Micro-spec поведения `get_files` из upstream Chainsaw для портирования в C++ (MOD-0005 `io::discovery`).

## Источники (FACTS)

### Первичный источник
- Файл: `upstream/chainsaw/src/file/mod.rs:435-498`
- Функция: `pub fn get_files(path: &PathBuf, extensions: &Option<HashSet<String>>, skip_errors: bool) -> crate::Result<Vec<PathBuf>>`

### Вторичные источники (использование)
| Команда | Файл | Строки | Расширения |
|---------|------|--------|------------|
| dump | main.rs | 446 | определяются `--extension` |
| lint (chainsaw) | main.rs | 614 | None (все файлы) |
| lint (sigma) | main.rs | 630 | None (все файлы) |
| hunt | main.rs | 731 | определяются rules/mappings |
| analyse srum | main.rs | 819 | None, skip_errors=false |
| search | main.rs | 919 | определяются `--extension` |

---

## Контракт функции

### Сигнатура (Rust)
```rust
pub fn get_files(
 path: &PathBuf,
 extensions: &Option<HashSet<String>>,
 skip_errors: bool,
) -> crate::Result<Vec<PathBuf>>
```

### Параметры

| Параметр | Тип | Описание |
|----------|-----|----------|
| `path` | `&PathBuf` | Путь к файлу или директории |
| `extensions` | `&Option<HashSet<String>>` | Набор расширений БЕЗ точки (например, `"evtx"`, `"json"`) или None для всех файлов |
| `skip_errors` | `bool` | Если true — предупреждения в stderr вместо ошибок |

### Возвращаемое значение
- `Ok(Vec<PathBuf>)` — список найденных файлов
- `Err(...)` — ошибка (только если `skip_errors=false`)

---

## Алгоритм поведения

### 1. Проверка существования пути (mod.rs:441)
```
IF!path.exists THEN
 IF skip_errors THEN
 PRINT stderr: "[!] Specified path does not exist - {path}"
 RETURN Ok(empty)
 ELSE
 RETURN Err("Specified event log path is invalid - {path}")
```

### 2. Получение метаданных (mod.rs:442-452)
```
metadata = fs::metadata(path)
IF metadata.is_err THEN
 IF skip_errors THEN
 PRINT stderr: "[!] failed to get metadata for file - {error}"
 RETURN Ok(empty)
 ELSE
 RETURN Err(error)
```

### 3. Обработка директории (mod.rs:453-478)
```
IF metadata.is_dir THEN
 directory = path.read_dir
 IF read_dir.is_err THEN
 IF skip_errors THEN
 PRINT stderr: "[!] failed to read directory - {error}"
 RETURN Ok(empty)
 ELSE
 RETURN Err(error)

 FOR each entry IN directory DO
 IF entry.is_err THEN
 IF skip_errors THEN
 PRINT stderr: "[!] failed to enter directory - {error}"
 RETURN Ok(current_files) // NB: возвращает накопленные!
 ELSE
 RETURN Err(error)

 files.extend(get_files(&entry.path, extensions, skip_errors)?)
```

### 4. Обработка файла (mod.rs:479-491)
```
ELSE (это файл)
 IF extensions IS Some(e) THEN
 ext = path.extension.to_string_lossy
 IF e.contains(ext) THEN
 files.push(path)

 // Специальный случай $MFT
 IF e.contains("$MFT") AND path.file_name == "$MFT" THEN
 files.push(path)
 ELSE
 files.push(path) // extensions=None → все файлы
```

### 5. Возврат результата (mod.rs:497)
```
RETURN Ok(files)
```

---

## Критичные инварианты

### INV-001: Фильтрация по расширению
- Расширения сравниваются **БЕЗ точки** (например, `"evtx"`, не `".evtx"`)
- Сравнение case-sensitive (как в Rust `HashSet::contains`)
- Используется `to_string_lossy` для не-UTF8 путей

### INV-002: Специальный случай $MFT
- Файл без расширения с именем `$MFT` добавляется, если `extensions` содержит строку `"$MFT"`
- Источник: `Kind::Mft => vec!["mft", "bin", "$MFT"]` (mod.rs:58-62)

### INV-003: Порядок файлов
- Порядок определяется `std::fs::read_dir` (зависит от ОС)
- Rust НЕ сортирует результат
- Для детерминизма C++ порта — **требуется стабильная сортировка** (см. ADR-0011)

### INV-004: Обход директорий
- Рекурсивный depth-first обход
- `files.extend(...)` после рекурсивного вызова → post-order сбор
- Symlinks: `fs::metadata` следует symlinks по умолчанию, `read_dir` — нет

### INV-005: Обработка ошибок
- `skip_errors=true` → предупреждения в stderr (формат `[!]...`)
- `skip_errors=false` → немедленный возврат ошибки
- При ошибке в цикле итерации с `skip_errors=true` — возвращаются **уже накопленные** файлы

### INV-006: Пустой результат — не ошибка
- Функция может вернуть `Ok(vec!)` (пустой вектор)
- Проверка "No compatible files" выполняется на уровне вызывающего кода (main.rs:452-455, 737-740)

---

## Расширения (Kind::extensions)

| Kind | Расширения |
|------|------------|
| Evtx | `evt`, `evtx` |
| Hve | `hve` |
| Json | `json` |
| Jsonl | `jsonl` |
| Mft | `mft`, `bin`, `$MFT` |
| Xml | `xml` |
| Esedb | `dat`, `edb` |
| Unknown | None |

Источник: `mod.rs:51-67`

---

## Зависимости слайса

### Входные зависимости
| ID | Название | Статус |
|----|----------|--------|
| SLICE-001 | Platform Layer | Done |

### Исходящие зависимости (использует SLICE-004)
| ID | Название | Статус |
|----|----------|--------|
| SLICE-011 | Search Command | Backlog |
| SLICE-012 | Hunt Command | Backlog |
| SLICE-013 | Dump Command | Backlog |

---

## Риски

### RISK-0007: Пути с пробелами
- Описание: пути с пробелами и специальными символами
- Влияние: Medium
- План закрытия: unit-тесты с пробелами в именах файлов/директорий

### RISK-NEW: Порядок файлов на разных ОС
- Описание: `read_dir` возвращает файлы в разном порядке на разных ОС
- Влияние: High для 1:1 сравнения
- План закрытия: сортировка по пути (ADR-0011)

---

## Требуемые тесты (test-to-test)

### Unit-тесты (новые для C++)

| ID | Описание | Приоритет |
|----|----------|-----------|
| TST-DISC-001 | Единичный файл с совпадающим расширением | High |
| TST-DISC-002 | Единичный файл без совпадающего расширения | High |
| TST-DISC-003 | Пустая директория | High |
| TST-DISC-004 | Директория с файлами разных расширений | High |
| TST-DISC-005 | Рекурсивный обход поддиректорий | High |
| TST-DISC-006 | extensions=None (все файлы) | High |
| TST-DISC-007 | Несуществующий путь, skip_errors=true | Medium |
| TST-DISC-008 | Несуществующий путь, skip_errors=false | Medium |
| TST-DISC-009 | Специальный случай $MFT | Medium |
| TST-DISC-010 | Путь с пробелами | Medium |
| TST-DISC-011 | Детерминированный порядок (сортировка) | High |
| TST-DISC-012 | Регистр расширений (case-sensitive) | Low |

### Upstream тесты
- **Нет прямых тестов** для `get_files` в upstream
- Косвенное покрытие через TST-0001..TST-0004 (search/hunt интеграция)

---

## C++ API (To-Be контракт)

### Из TOBE-0001/4.5
```cpp
namespace chainsaw::io {
 struct DiscoveryOptions {
 std::optional<std::unordered_set<std::string>> extensions;
 bool skip_errors = false;
 };

 // Возвращает отсортированный по пути список файлов
 std::vector<std::filesystem::path> discover_files(
 const std::vector<std::filesystem::path>& inputs,
 const DiscoveryOptions& opt);
}
```

### Отличия от Rust API
1. Принимает **вектор** путей (агрегация вызовов)
2. **Сортирует** результат для детерминизма (ADR-0011)
3. Использует `std::filesystem::path` (ADR-0010)

---

## S1-S4 Оценка

| Ось | Оценка | Обоснование |
|-----|--------|-------------|
| S1 (ширина контуров) | Low | один модуль io::discovery |
| S2 (сложность проверки) | Low | unit-тесты, без внешних зависимостей |
| S3 (unknowns) | Low | алгоритм полностью задокументирован |
| S4 (платформенность) | Medium | различия путей Windows/Unix |

**Итог:** допустимо к работе (max = Medium, только одна ось)

---

## Критерии UnitReady ✓

- [x] Micro-spec ссылается на первичные источники (код/тесты/запуски)
- [x] Определён полный набор проверок для доказательства 1:1
- [x] Dependencies/unknowns оценены
- [x] Правило гранулярности соблюдено (max 1 High)

---

## Ссылки
- Rust код: `upstream/chainsaw/src/file/mod.rs:435-498`
- To-Be архитектура: `docs/architecture/TOBE-0001-cpp-to-be-architecture.md` (раздел 4.5)
- ADR детерминизм: `docs/adr/ADR-0011-concurrency-and-determinism.md`
- ADR пути: `docs/adr/ADR-0010-filesystem-and-path-encoding.md`
- Backlog: `docs/backlog/BACKLOG-0001-porting-backlog.md` (SLICE-004)
