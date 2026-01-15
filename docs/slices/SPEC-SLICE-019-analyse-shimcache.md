# SPEC-SLICE-019 — Analyse Shimcache Command

## Метаданные

| Параметр | Значение |
|----------|----------|
| **ID** | SLICE-019 |
| **Название** | Analyse Shimcache Command |
| **Статус** | UnitReady |
| **Дата создания** | 2026-01-13 |
| **MOD-*** | MOD-0015 |
| **FEAT-*** | FEAT-0010 |
| **Зависимости** | SLICE-003 (CLI Parser) — Done, SLICE-015 (HVE Parser) — Verified |

---

## 1. Обзор

Команда `analyse shimcache` создаёт execution timeline из shimcache артефакта (SYSTEM registry hive) с опциональным обогащением из Amcache. Анализ позволяет восстановить хронологию выполнения программ на Windows системе.

### 1.1. CLI интерфейс

**Источник:** `upstream/chainsaw/src/main.rs:282-305`

```
chainsaw analyse shimcache <shimcache> [OPTIONS]

ARGUMENTS:
 <shimcache> Путь к shimcache артефакту (SYSTEM registry file)

OPTIONS:
 -e, --regex <pattern> Regex паттерн для detecting shimcache entries (можно указать несколько раз)
 -r, --regexfile <path> Путь к файлу с regex паттернами (по одному на строку)
 -o, --output <path> Путь для вывода CSV файла
 -a, --amcache <path> Путь к Amcache.hve для обогащения timeline
 -p, --tspair Включить near timestamp pair detection (требует --amcache)
```

---

## 2. FACTS (поведенческая истина Rust)

### 2.1. Структуры данных

#### FACT-001: TimelineEntity
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:31-37`
```rust
pub struct TimelineEntity {
 pub amcache_file: Option<Rc<FileEntry>>,
 pub amcache_program: Option<Rc<ProgramEntry>>,
 pub shimcache_entry: Option<ShimcacheEntry>,
 pub timestamp: Option<TimelineTimestamp>,
}
```

#### FACT-002: TimelineTimestamp enum
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:20-29`
```rust
pub enum TimelineTimestamp {
 Exact(DateTime<Utc>, TimestampType),
 Range { from: DateTime<Utc>, to: DateTime<Utc> },
 RangeEnd(DateTime<Utc>),
 RangeStart(DateTime<Utc>),
}
```

#### FACT-003: TimestampType enum
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:13-18`
```rust
pub enum TimestampType {
 AmcacheRangeMatch,
 NearTSMatch,
 PatternMatch,
 ShimcacheLastUpdate,
}
```

#### FACT-004: ShimcacheEntry структура
**Источник:** `upstream/chainsaw/src/file/hve/shimcache.rs:26-37`
```rust
pub struct ShimcacheEntry {
 pub cache_entry_position: u32,
 pub controlset: u32,
 pub data_size: Option<usize>,
 pub data: Option<Vec<u8>>,
 pub entry_type: EntryType,
 pub executed: Option<bool>,
 pub last_modified_ts: Option<DateTime<Utc>>,
 pub path_size: usize,
 pub signature: Option<String>,
}
```

#### FACT-005: EntryType enum
**Источник:** `upstream/chainsaw/src/file/hve/shimcache.rs:60-75`
```rust
pub enum EntryType {
 File { path: String },
 Program {
 raw_entry: String,
 unknown_u32: String,
 architecture: CPUArchitecture,
 program_name: String,
 program_version: String,
 sdk_version: String,
 publisher_id: String,
 neutral: bool,
 },
}
```

#### FACT-006: CPUArchitecture enum
**Источник:** `upstream/chainsaw/src/file/hve/shimcache.rs:40-46`
```rust
pub enum CPUArchitecture {
 Amd64, // 34404 (0x8664)
 Arm, // 452 (0x1C4)
 I386, // 332 (0x14C)
 Ia64, // 512 (0x200)
 Unknown(u16),
}
```

#### FACT-007: ShimcacheVersion enum
**Источник:** `upstream/chainsaw/src/file/hve/shimcache.rs:77-88`
```rust
pub enum ShimcacheVersion {
 Unknown,
 Windows10,
 Windows10Creators,
 Windows7x64Windows2008R2,
 Windows7x86,
 Windows80Windows2012,
 Windows81Windows2012R2,
 WindowsVistaWin2k3Win2k8, // НЕ ПОДДЕРЖИВАЕТСЯ
 WindowsXP, // НЕ ПОДДЕРЖИВАЕТСЯ
}
```

#### FACT-008: ShimcacheArtefact структура
**Источник:** `upstream/chainsaw/src/file/hve/shimcache.rs:111-116`
```rust
pub struct ShimcacheArtefact {
 pub entries: Vec<ShimcacheEntry>,
 pub last_update_ts: DateTime<Utc>,
 pub version: ShimcacheVersion,
}
```

### 2.2. ShimcacheAnalyser

#### FACT-009: Конструктор ShimcacheAnalyser
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:55-61`
```rust
pub fn new(shimcache_path: PathBuf, amcache_path: Option<PathBuf>) -> Self
```

#### FACT-010: Основной метод анализа
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:63-67`
```rust
pub fn amcache_shimcache_timeline(
 &self,
 regex_patterns: &[String],
 ts_near_pair_matching: bool,
) -> crate::Result<Vec<TimelineEntity>>
```

#### FACT-011: Загрузка shimcache
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:76-83`
- Использует `HveParser::load(&self.shimcache_path)`
- Вызывает `shimcache_parser.parse_shimcache`
- Выводит версию shimcache и путь

#### FACT-012: Загрузка amcache (опционально)
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:86-95`
- Если `amcache_path` указан, загружает через `HveParser::load(amcache_path)`
- Вызывает `amcache_parser.parse_amcache`

#### FACT-013: Shimcache Last Update prepend
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:163-175`
- Первый элемент timeline — shimcache.last_update_ts
- TimestampType::ShimcacheLastUpdate
- shimcache_entry = None

#### FACT-014: Pattern matching
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:179-199`
- Для каждого shimcache entry проверяется regex match на path.to_lowercase
- Только EntryType::File участвует в pattern matching
- При совпадении timestamp = Exact(last_modified_ts, PatternMatch)

#### FACT-015: Timestamp range setting
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:116-155`
- Entries между known timestamps получают Range { from, to }
- Entries до первого known timestamp получают RangeStart
- Entries после последнего known timestamp получают RangeEnd

#### FACT-016: Amcache file entry matching
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:217-232`
- Match по `file_entry.path.to_lowercase == shimcache_path.to_lowercase`
- При совпадении устанавливается entity.amcache_file

#### FACT-017: Amcache program entry matching
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:235-254`
- Match по `program_entry.program_name == shimcache.program_name && program_entry.version == shimcache.program_version`

#### FACT-018: Near timestamp pair matching
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:256-293`
- MAX_TIME_DIFFERENCE = 60 * 1000 ms (1 минута)
- Если difference между shimcache_ts и amcache_entry.key_last_modified_ts < 1 min
- Устанавливается timestamp = Exact(amcache_ts, NearTSMatch)
- Не перезаписывает PatternMatch

#### FACT-019: Amcache range match
**Источник:** `upstream/chainsaw/src/analyse/shimcache.rs:296-320`
- Если amcache_ts попадает в Range { from, to }
- Устанавливается timestamp = Exact(amcache_ts, AmcacheRangeMatch)

### 2.3. Shimcache Parser

#### FACT-020: ControlSet detection
**Источник:** `upstream/chainsaw/src/file/hve/shimcache.rs:138-152`
- Читает Select\Current для определения текущего ControlSet
- Формирует путь `ControlSet00X\Control\Session Manager\AppCompatCache`

#### FACT-021: Version detection по сигнатуре
**Источник:** `upstream/chainsaw/src/file/hve/shimcache.rs:180-243`
| Сигнатура | Версия |
|-----------|--------|
| 0xdeadbeef | WindowsXP |
| 0xbadc0ffe | WindowsVistaWin2k3Win2k8 |
| 0xbadc0fee | Windows7 (проверка PROCESSOR_ARCHITECTURE для x86/x64) |
| "00ts" @ 128 | Windows80Windows2012 |
| "10ts" @ 128 | Windows81Windows2012R2 |
| "10ts" @ offset | Windows10/Windows10Creators (offset 0x34 = Creators) |

#### FACT-022: Windows 10 entry parsing
**Источник:** `upstream/chainsaw/src/file/hve/shimcache.rs:295-458`
- Signature: "10ts"
- Path в UTF-16LE
- Program entry detection через regex для UWP apps

#### FACT-023: Windows 7 x64 entry parsing
**Источник:** `upstream/chainsaw/src/file/hve/shimcache.rs:461-583`
- Entry count в header
- Path offset/size separate from entry
- InsertFlag.Executed bit detection

#### FACT-024: Windows 8 entry parsing
**Источник:** `upstream/chainsaw/src/file/hve/shimcache.rs:707-823`
- Signature: "00ts" или "10ts"
- SYSVOL\ replacement с C:\

#### FACT-025: Неподдерживаемые версии
**Источник:** `upstream/chainsaw/src/file/hve/shimcache.rs:825-839`
- WindowsVistaWin2k3Win2k8: `bail!("Windows Vista shimcache parsing not supported!")`
- WindowsXP: `bail!("Windows XP shimcache parsing not supported!")`

### 2.4. CLI Output

#### FACT-026: CSV/Table output
**Источник:** `upstream/chainsaw/src/cli.rs:629-774`
- Колонки: Timestamp, File Path, Program Name, SHA-1 Hash, Timeline Entry Number, Entry Type, Timestamp Description, Raw Entry
- При наличии -o: полный CSV
- При выводе в терминал: первые 4 колонки + warning

#### FACT-027: Amcache row insertion
**Источник:** `upstream/chainsaw/src/cli.rs:727-757`
- При AmcacheRangeMatch или NearTSMatch добавляется отдельная строка для AmcacheFileEntry

#### FACT-028: Timestamp format
**Источник:** `upstream/chainsaw/src/cli.rs:654-656`
- RFC3339 с AutoSi seconds format, UTC (trailing Z)

### 2.5. Amcache Parser

#### FACT-029: Amcache new format detection
**Источник:** `upstream/chainsaw/src/file/hve/amcache.rs:59-62`
- Проверка наличия `Root\InventoryApplicationFile`

#### FACT-030: Amcache FileEntry структура
**Источник:** `upstream/chainsaw/src/file/hve/amcache.rs:7-16`
```rust
pub struct FileEntry {
 pub file_id: Option<String>,
 pub key_last_modified_ts: DateTime<Utc>,
 pub file_last_modified_ts: Option<DateTime<Utc>>,
 pub link_date: Option<DateTime<Utc>>,
 pub path: String,
 pub program_id: Option<String>,
 pub sha1_hash: Option<String>,
}
```

#### FACT-031: Amcache ProgramEntry структура
**Источник:** `upstream/chainsaw/src/file/hve/amcache.rs:18-28`
```rust
pub struct ProgramEntry {
 pub install_date: Option<DateTime<Utc>>,
 pub uninstall_date: Option<DateTime<Utc>>,
 pub last_modified_ts: DateTime<Utc>,
 pub program_id: String,
 pub program_name: String,
 pub version: String,
 pub root_directory_path: Option<String>,
 pub uninstall_string: Option<String>,
}
```

#### FACT-032: SHA-1 hash extraction
**Источник:** `upstream/chainsaw/src/file/hve/amcache.rs:132-139`
- FileId имеет формат "0000" + SHA-1 (44 символа)
- SHA-1 = FileId[4..]

---

## 3. Инварианты (INV)

### INV-001: Shimcache path обязателен
CLI требует путь к SYSTEM hive как позиционный аргумент.

### INV-002: tspair требует amcache
Флаг `-p/--tspair` требует указания `-a/--amcache`.

### INV-003: Pattern matching case-insensitive
Все path comparisons выполняются через `to_lowercase`.

### INV-004: Timeline ordering preserves shimcache order
Порядок entries в timeline соответствует порядку в shimcache (cache_entry_position).

### INV-005: Last update first
Первый элемент timeline — shimcache.last_update_ts с типом ShimcacheLastUpdate.

### INV-006: Pattern match priority
PatternMatch timestamp не перезаписывается NearTSMatch.

---

## 4. Тесты (TST)

### 4.1. Upstream тесты
**Статус:** Отсутствуют

В upstream chainsaw нет unit-тестов для shimcache команды. Нет тестовых SYSTEM hive файлов в `tests/`.

### 4.2. Golden runs
**Статус:** Требуется создать (RISK-0022 CLOSED)

Нет RUN-07xx сценариев в golden runs манифесте. После реализации необходимо создать golden runs с использованием `cpp/tests/fixtures/shimcache/SYSTEM.hive`.

### 4.3. Планируемые C++ unit-тесты

#### Shimcache Parser тесты (TST-SHIM-001..010)

| ID | Название | Описание |
|----|----------|----------|
| TST-SHIM-001 | ParseShimcache_Windows10 | Парсинг Windows 10 shimcache |
| TST-SHIM-002 | ParseShimcache_Windows10Creators | Парсинг Windows 10 Creators shimcache |
| TST-SHIM-003 | ParseShimcache_Windows7x64 | Парсинг Windows 7 x64 shimcache |
| TST-SHIM-004 | ParseShimcache_Windows7x86 | Парсинг Windows 7 x86 shimcache |
| TST-SHIM-005 | ParseShimcache_Windows8 | Парсинг Windows 8 shimcache |
| TST-SHIM-006 | ParseShimcache_Windows81 | Парсинг Windows 8.1 shimcache |
| TST-SHIM-007 | ParseShimcache_UnsupportedVista | Error на Vista shimcache |
| TST-SHIM-008 | ParseShimcache_UnsupportedXP | Error на XP shimcache |
| TST-SHIM-009 | EntryType_File | Парсинг File entry type |
| TST-SHIM-010 | EntryType_Program | Парсинг Program entry type (UWP) |

#### Shimcache Analyser тесты (TST-SHIM-011..020)

| ID | Название | Описание |
|----|----------|----------|
| TST-SHIM-011 | Timeline_LastUpdateFirst | Первый элемент = last_update_ts |
| TST-SHIM-012 | Timeline_PatternMatch | Regex matching устанавливает Exact timestamp |
| TST-SHIM-013 | Timeline_RangeCalculation | Корректные Range timestamps |
| TST-SHIM-014 | Timeline_AmcacheEnrichment | Amcache file entry matching |
| TST-SHIM-015 | Timeline_NearTSPair | Near timestamp pair detection |
| TST-SHIM-016 | Timeline_AmcacheRangeMatch | Amcache range match detection |
| TST-SHIM-017 | Timeline_PatternMatchPriority | PatternMatch не перезаписывается |
| TST-SHIM-018 | Output_CSV | CSV output format |
| TST-SHIM-019 | Output_Table | Table output с truncation |
| TST-SHIM-020 | Output_AmcacheRow | Дополнительная строка для Amcache |

### 4.4. Блокеры для тестов

1. ~~**Отсутствие тестовых данных**~~ — **CLOSED (2026-01-13)**
 - ✅ SYSTEM hive получен: `cpp/tests/fixtures/shimcache/SYSTEM.hive` (12 MB, Windows 11)
 - ⚠️ Нет Amcache.hve для enrichment тестов (RISK-SHIM-002)

2. **Отсутствие golden runs**
 - Нет RUN-07xx для сравнения — требуется создать после реализации

---

## 5. Зависимости

### 5.1. Реализованные зависимости

| SLICE | Название | Статус | Что используется |
|-------|----------|--------|------------------|
| SLICE-003 | CLI Parser | Done | CLI структура, аргументы |
| SLICE-015 | HVE Parser | Verified | HveParser, Registry parsing |

### 5.2. Новый код

1. **ShimcacheAnalyser class** — основной анализатор
2. **Shimcache parser** — расширение HVE Parser
3. **Amcache parser** — расширение HVE Parser (если не реализован в SLICE-015)
4. **Timeline structures** — TimelineEntity, TimelineTimestamp, TimestampType
5. **CSV/Table output** — специфичный для shimcache

---

## 6. Оценка S1–S4

| Ось | Оценка | Обоснование |
|-----|--------|-------------|
| **S1** (ширина контуров) | Low | Один модуль analyse/shimcache |
| **S2** (сложность проверки) | Medium ← High | Есть тестовые данные, нужны golden runs |
| **S3** (unknowns/blockers) | Medium ← High | RISK-0022 CLOSED, остаётся RISK-SHIM-002 |
| **S4** (платформенность) | Low | Кроссплатформенный код |

**Итог:** Все оси ≤ Medium — слайс разблокирован (2026-01-13).

---

## 7. Риски

### RISK-0022: Отсутствие тестовых данных для shimcache

| Параметр | Значение |
|----------|----------|
| **ID** | RISK-0022 |
| **Статус** | **CLOSED** (2026-01-13) |
| **Влияние** | ~~High~~ → Closed |
| **Вероятность** | ~~Certain~~ → Resolved |
| **Описание** | ~~Upstream chainsaw не содержит тестовых SYSTEM hive файлов~~ |
| **Решение** | SYSTEM hive (12 MB) получен с Windows 11 VM через `reg save HKLM\SYSTEM`. Файл содержит AppCompatCache (shimcache) данные. Путь: `cpp/tests/fixtures/shimcache/SYSTEM.hive` |

### RISK-SHIM-001: Неподдерживаемые версии Windows

| Параметр | Значение |
|----------|----------|
| **ID** | RISK-SHIM-001 |
| **Статус** | Accepted |
| **Влияние** | Low — Windows Vista/XP редко встречаются |
| **Описание** | Windows Vista и Windows XP shimcache не поддерживаются в upstream |
| **Митигация** | Документировано, соответствует upstream поведению |

### RISK-SHIM-002: Amcache integration complexity

| Параметр | Значение |
|----------|----------|
| **ID** | RISK-SHIM-002 |
| **Статус** | Open |
| **Влияние** | Medium — требует тестовых Amcache.hve |
| **Описание** | Amcache enrichment требует отдельных тестовых данных |
| **План закрытия** | Тестировать amcache integration после получения данных |

---

## 8. UnitReady Assessment

### Критерии UnitReady (из PLAYBOOK-0001)

| # | Критерий | Статус | Доказательство |
|---|----------|--------|----------------|
| 1 | Micro-spec создан | ✅ PASS | Этот документ |
| 2 | Поведение описано на основе FACTS | ✅ PASS | 32 FACTS из upstream кода |
| 3 | Определён полный набор проверок | ✅ PASS | 20 TST-SHIM-* определены, SYSTEM.hive получен (2026-01-13) |
| 4 | Зависимости оценены | ✅ PASS | SLICE-003 Done, SLICE-015 Verified |
| 5 | Оценка S1–S4 корректна | ✅ PASS | Все оси ≤ Medium (после закрытия RISK-0022) |

### UnitReady Decision

**UnitReady: PASS** (обновлено 2026-01-13)

Слайс готов к реализации:
1. ✅ RISK-0022 CLOSED — SYSTEM hive получен
2. ✅ Реализация возможна на основе FACTS + тестовых данных
3. ⚠️ Golden runs требуется создать после реализации
4. ⚠️ Amcache.hve для enrichment тестов — pending (RISK-SHIM-002)

---

## 9. Ссылки

- Upstream shimcache analyser: `upstream/chainsaw/src/analyse/shimcache.rs`
- Upstream shimcache parser: `upstream/chainsaw/src/file/hve/shimcache.rs`
- Upstream amcache parser: `upstream/chainsaw/src/file/hve/amcache.rs`
- Upstream CLI: `upstream/chainsaw/src/main.rs:282-305`
- Upstream output: `upstream/chainsaw/src/cli.rs:629-774`
- Shimcache patterns example: `upstream/chainsaw/analysis/shimcache_patterns.txt`
- SLICE-015 HVE Parser: `docs/slices/SPEC-SLICE-015-hve-parser.md`
- Risk register:
