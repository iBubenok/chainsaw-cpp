# SPEC-SLICE-017 — Analyse SRUM Command

## Meta

| Field | Value |
|-------|-------|
| Slice ID | SLICE-017 |
| MOD-* | MOD-0014 |
| FEAT-* | FEAT-0011 |
| Status | UnitReady (2026-01-12) |
| Priority | 17 |
| Dependencies | SLICE-003 (CLI Parser - Done), SLICE-015 (HVE Parser - Verified), SLICE-016 (ESEDB Parser - **Implemented**) |
| Created | 2026-01-12 |
| Updated | 2026-01-13 |

## 1) Overview

Analyse SRUM Command — команда CLI для анализа System Resource Usage Monitor (SRUM) базы данных Windows.

**SRUM (System Resource Usage Monitor):**
- Механизм Windows (с Windows 8), отслеживающий использование ресурсов программами, службами и приложениями
- Данные хранятся в ESE database: `%SystemRoot%\System32\sru\SRUDB.dat`
- Конфигурация в реестре: `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\SRUM\`
- Extensions/providers определяют типы собираемых данных

**Ключевая функциональность:**
1. **Загрузка SRUDB.dat** через ESEDB Parser (SLICE-016)
2. **Загрузка SOFTWARE hive** через HVE Parser (SLICE-015)
3. **Парсинг SRUM registry entries** — global parameters, extensions, user info
4. **Парсинг SruDbIdMapTable** — маппинг AppId/UserId → имена
5. **Аналитика** — расширение записей AppName, UserName, UserSID, TableName
6. **Вывод** — таблица деталей (--stats-only) или полный JSON

## 2) Primary Sources (Rust)

### 2.1. SrumAnalyser: `src/analyse/srum.rs`

**Source:** `upstream/chainsaw/src/analyse/srum.rs:1-415`

```rust
pub struct SrumAnalyser {
 srum_path: PathBuf,
 software_hive_path: PathBuf,
}

pub struct SrumDbInfo {
 pub table_details: Table, // prettytable::Table
 pub db_content: Json, // serde_json::Value (array of records)
}

impl SrumAnalyser {
 pub fn new(srum_path: PathBuf, software_hive_path: PathBuf) -> Self;
 pub fn parse_srum_database(&self) -> Result<SrumDbInfo, Error>;
}

/// Convert hex bytes to Windows SID string (S-1-5-...)
pub fn bytes_to_sid_string(hex: &[u8]) -> Option<String>;

/// Format duration in days to "N days, M hours, K minutes"
fn format_duration(days: f64) -> String;
```

### 2.2. SruDbIdMapTable Parser: `src/file/esedb/srum.rs`

**Source:** `upstream/chainsaw/src/file/esedb/srum.rs:1-68`

```rust
#[derive(Debug)]
pub struct SruDbIdMapTableEntry {
 pub id_type: i8,
 pub id_index: i32,
 pub id_blob: Option<Vec<u8>>,
 pub id_blob_as_string: Option<String>,
}

impl super::Parser {
 pub fn parse_sru_db_id_map_table(&self)
 -> crate::Result<HashMap<String, SruDbIdMapTableEntry>>;
}
```

### 2.3. SRUM Registry Parser: `src/file/hve/srum.rs`

**Source:** `upstream/chainsaw/src/file/hve/srum.rs:1-196`

```rust
#[derive(Debug)]
pub struct SrumRegInfo {
 pub global_parameters: Json, // Tier1Period, Tier2Period, Tier2MaxEntries, etc.
 pub extensions: Json, // GUID → {(default), DllName,...}
 pub user_info: Json, // SID → {GUID, SID, Username}
}

impl super::Parser {
 pub fn parse_srum_entries(&mut self) -> crate::Result<SrumRegInfo>;
}
```

### 2.4. CLI Command: `src/main.rs`

**Source:** `upstream/chainsaw/src/main.rs:306-316, 1090-1145`

```rust
/// Analyse the SRUM database
Srum {
 /// The path to the SRUM database
 srum_path: PathBuf,
 /// The path to the SOFTWARE hive
 #[arg(short = 's', long = "software")]
 software_hive_path: PathBuf,
 /// Only output details about the SRUM database
 #[arg(long = "stats-only")]
 stats_only: bool,
 /// Suppress informational output.
 #[arg(short = 'q', long = "quiet")]
 quiet: bool,
}
```

## 3) FACTS — Поведенческая истина

### 3.1. CLI Interface

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-001 | main.rs:306-316 | Команда `analyse srum` принимает позиционный аргумент `srum_path` (путь к SRUDB.dat) |
| FACT-002 | main.rs:310-312 | Обязательный аргумент `--software/-s` — путь к SOFTWARE hive |
| FACT-003 | main.rs:313-315 | Флаг `--stats-only` — вывод только таблицы деталей (без JSON) |
| FACT-004 | main.rs:316-318 | Флаг `-q/--quiet` — подавление информационных сообщений |
| FACT-005 | main.rs:1101 | При `!quiet` выводится `[+] Details about the tables related to the SRUM extensions:` |

### 3.2. SrumAnalyser

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-006 | srum.rs:90-95 | `SrumAnalyser::new(srum_path, software_hive_path)` — конструктор |
| FACT-007 | srum.rs:97-100 | `parse_srum_database` загружает ESE database через `EsedbParser::load` |
| FACT-008 | srum.rs:105-110 | Загружает SOFTWARE hive через `HveParser::load`, выводит `[+] SOFTWARE hive loaded from {:?}` |
| FACT-009 | srum.rs:112 | Выводит `[+] Parsing the SOFTWARE registry hive...` |
| FACT-010 | srum.rs:114-116 | Парсит SRUM registry entries через `registry_parser.parse_srum_entries` |
| FACT-011 | srum.rs:223 | Выводит `[+] Analysing the SRUM database...` |

### 3.3. bytes_to_sid_string

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-012 | srum.rs:40-64 | `bytes_to_sid_string(hex)` конвертирует bytes в Windows SID строку формата `S-1-5-...` |
| FACT-013 | srum.rs:43-45 | Возвращает `None` если `hex.is_empty` или `hex.len <= 8` |
| FACT-014 | srum.rs:47 | SID version — `hex[0]` |
| FACT-015 | srum.rs:48 | Authority ID — `i32::from_le_bytes([hex[7], hex[6], hex[5], hex[4]])` |
| FACT-016 | srum.rs:50-62 | Sub-authorities — каждые 4 байта начиная с offset 8, формат hex string → i64 |

### 3.4. format_duration

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-017 | srum.rs:66-87 | `format_duration(days)` форматирует дни в строку "N days, M hours, K minutes" |
| FACT-018 | srum.rs:74-76 | Дни добавляются только если `whole_days > 0` |
| FACT-019 | srum.rs:78-80 | Часы добавляются только если `whole_hours > 0` |
| FACT-020 | srum.rs:82-84 | Минуты добавляются только если `minutes > 0` |

### 3.5. Retention Time Calculation

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-021 | srum.rs:186-207 | Retention time = `Tier2Period * Tier2MaxEntries / 3600 / 24` (в днях) |
| FACT-022 | srum.rs:140-184 | Long Term retention = `Tier2LongTermPeriod * Tier2LongTermMaxEntries / 3600 / 24` |
| FACT-023 | srum.rs:173 | Long Term таблицы имеют суффикс "LT" в GUID и "(Long Term)" в имени |
| FACT-024 | srum.rs:131-137 | Extension parameters переопределяют global defaults из `srum_parameters_reg` |

### 3.6. Record Enrichment

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-025 | srum.rs:230-242 | `AppId` → `AppName` через SruDbIdMapTable lookup (id_blob_as_string) |
| FACT-026 | srum.rs:243-264 | `UserId` → `UserSID` + `UserName` через SruDbIdMapTable + ProfileList |
| FACT-027 | srum.rs:251 | `UserSID` генерируется через `bytes_to_sid_string` |
| FACT-028 | srum.rs:254-257 | `UserName` берётся из `srum_reg_info.user_info[sid]["Username"]` |
| FACT-029 | srum.rs:265-297 | `Table` GUID → `TableName` readable через extensions registry |
| FACT-030 | srum.rs:272-285 | Если GUID заканчивается на "LT", имя получает суффикс "(Long Term)" |

### 3.7. Timestamp Processing

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-031 | srum.rs:309-354 | Timeframe (from/to) вычисляется по min/max `TimeStamp` для каждой таблицы |
| FACT-032 | srum.rs:326 | Timestamp парсится через `DateTime::parse_from_rfc3339` |
| FACT-033 | srum.rs:357-375 | Windows timestamps (`EndTime`, `ConnectStartTime`, `StartTime`) конвертируются через `win32_ts_to_datetime` |
| FACT-034 | srum.rs:366-367 | Конвертированные timestamps форматируются как RFC3339 с секундной точностью |

### 3.8. Table Output

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-035 | srum.rs:382-389 | Таблица деталей имеет 5 колонок: Table GUID, Table Name, DLL Path, Timeframe of the data, Expected Retention Time |
| FACT-036 | srum.rs:392-396 | Timeframe форматируется как "from\nto" или "No records" |
| FACT-037 | srum.rs:398 | Retention time форматируется через `format_duration` |
| FACT-038 | srum.rs:391 | Таблицы выводятся отсортированными по GUID (BTreeMap) |

### 3.9. SruDbIdMapTable

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-039 | esedb/srum.rs:18-24 | Фильтрация записей по `Table == "SruDbIdMapTable"` |
| FACT-040 | esedb/srum.rs:42-49 | `SruDbIdMapTableEntry`: id_type (i8), id_index (i32), id_blob (Option<Vec<u8>>), id_blob_as_string (Option<String>) |
| FACT-041 | esedb/srum.rs:52-58 | Если `id_type!= 3` (не Windows SID), id_blob конвертируется в строку через `String::from_utf8_lossy` с удалением null bytes |
| FACT-042 | esedb/srum.rs:61-63 | Результат индексируется по `id_index.to_string` |

### 3.10. SRUM Registry Entries

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-043 | hve/srum.rs:35-41 | SRUM Parameters path: `Microsoft\Windows NT\CurrentVersion\SRUM\Parameters` |
| FACT-044 | hve/srum.rs:46-52 | Default global parameters: Tier1Period=60, Tier2Period=3600, Tier2MaxEntries=1440, Tier2LongTermPeriod=604800, Tier2LongTermMaxEntries=260 |
| FACT-045 | hve/srum.rs:88-94 | SRUM Extensions path: `Microsoft\Windows NT\CurrentVersion\SRUM\Extensions` |
| FACT-046 | hve/srum.rs:101 | Extension GUID нормализуется к UPPERCASE |
| FACT-047 | hve/srum.rs:148-151 | ProfileList path: `Microsoft\Windows NT\CurrentVersion\ProfileList` |
| FACT-048 | hve/srum.rs:170-173 | ProfileImagePath с заменой `\` → `//` для извлечения username через `file_name` |

## 4) Инварианты

| INV-ID | Description |
|--------|-------------|
| INV-001 | Каждая запись обогащается полями AppName, UserName, UserSID, TableName |
| INV-002 | Таблицы сортируются по GUID (BTreeMap ordering) |
| INV-003 | Long Term таблицы обозначаются суффиксом "LT" в GUID и "(Long Term)" в имени |
| INV-004 | Windows timestamps конвертируются в RFC3339 формат |
| INV-005 | SID формат: `S-{version}-{authority}-{sub1}-{sub2}-...` |
| INV-006 | Duration формат: `N days, M hours, K minutes` (пропуск нулевых компонентов) |

## 5) Tests

### 5.1. Upstream Tests (CLI Integration)

| TST-ID | Source | Description | Command | Fixture | Expected |
|--------|--------|-------------|---------|---------|----------|
| TST-0005 | tests/clo.rs:96-120 | `analyse_srum_database_table_details` | `analyse srum --software SOFTWARE SRUDB.dat --stats-only -q` | SRUDB.dat + SOFTWARE | analysis_srum_database_table_details.txt |
| TST-0006 | tests/clo.rs:122-145 | `analyse_srum_database_json` | `analyse srum --software SOFTWARE SRUDB.dat -q` | SRUDB.dat + SOFTWARE | analysis_srum_database_json.txt |

### 5.2. Unit Tests for C++ Port (TST-SRUM-*)

| TST-ID | Test Name | Description | FACT-* |
|--------|-----------|-------------|--------|
| TST-SRUM-001 | New_ValidPaths | Конструктор с валидными путями | FACT-006 |
| TST-SRUM-002 | Parse_LoadEsedb | Загрузка ESEDB базы | FACT-007 |
| TST-SRUM-003 | Parse_LoadHve | Загрузка SOFTWARE hive | FACT-008 |
| TST-SRUM-004 | Parse_SrumEntries | Парсинг SRUM registry entries | FACT-010 |
| TST-SRUM-005 | Parse_SruDbIdMapTable | Парсинг SruDbIdMapTable | FACT-039, FACT-040 |
| TST-SRUM-006 | BytesToSid_Valid | Конверсия bytes → SID строка | FACT-012..016 |
| TST-SRUM-007 | BytesToSid_TooShort | None для коротких bytes | FACT-013 |
| TST-SRUM-008 | BytesToSid_Empty | None для пустых bytes | FACT-013 |
| TST-SRUM-009 | FormatDuration_Full | Все компоненты (days, hours, minutes) | FACT-017..020 |
| TST-SRUM-010 | FormatDuration_DaysOnly | Только дни | FACT-018 |
| TST-SRUM-011 | FormatDuration_Zero | Пустая строка для 0 | FACT-018..020 |
| TST-SRUM-012 | RetentionTime_Default | Расчёт retention по defaults | FACT-021 |
| TST-SRUM-013 | RetentionTime_LongTerm | Расчёт long term retention | FACT-022, FACT-023 |
| TST-SRUM-014 | Enrich_AppName | Добавление AppName | FACT-025 |
| TST-SRUM-015 | Enrich_UserName | Добавление UserName и UserSID | FACT-026..028 |
| TST-SRUM-016 | Enrich_TableName | Добавление TableName | FACT-029, FACT-030 |
| TST-SRUM-017 | Timestamp_Timeframe | Вычисление from/to по TimeStamp | FACT-031, FACT-032 |
| TST-SRUM-018 | Timestamp_Windows | Конверсия Windows timestamps | FACT-033, FACT-034 |
| TST-SRUM-019 | Table_Columns | 5 колонок в таблице деталей | FACT-035 |
| TST-SRUM-020 | Table_Timeframe_NoRecords | "No records" для пустых таблиц | FACT-036 |
| TST-SRUM-021 | Table_Sorted | Сортировка по GUID | FACT-038 |
| TST-SRUM-022 | IdMapTable_SkipSid | Пропуск SID (id_type=3) при конверсии в строку | FACT-041 |
| TST-SRUM-023 | IdMapTable_Index | Индексация по id_index | FACT-042 |
| TST-SRUM-024 | Registry_DefaultParams | Default SRUM parameters | FACT-044 |

### 5.3. Golden Runs

| RUN-ID | Command | Description | Expected |
|--------|---------|-------------|----------|
| | `chainsaw analyse srum --software SOFTWARE SRUDB.dat --stats-only -q` | Table details only | analysis_srum_database_table_details.txt |
| | `chainsaw analyse srum --software SOFTWARE SRUDB.dat -q` | Full JSON output | analysis_srum_database_json.txt |

**Fixtures:**
- `tests/srum/SRUDB.dat` — 1.8MB ESE database
- `tests/srum/SOFTWARE` — 73MB registry hive
- `tests/srum/analysis_srum_database_table_details.txt` — 5KB expected table
- `tests/srum/analysis_srum_database_json.txt` — 3.5MB expected JSON

## 6) Implementation Strategy

### 6.1. API Design (C++)

```cpp
namespace chainsaw::analyse {

/// SRUM database analysis result
struct SrumDbInfo {
 std::string table_details; // Formatted table string
 rapidjson::Document db_content; // JSON array of enriched records
};

/// SRUM analyser
class SrumAnalyser {
public:
 SrumAnalyser(std::filesystem::path srum_path,
 std::filesystem::path software_hive_path);

 /// Analyse SRUM database and SOFTWARE hive
 std::expected<SrumDbInfo, std::string> parse_srum_database;

private:
 std::filesystem::path srum_path_;
 std::filesystem::path software_hive_path_;
};

/// Convert bytes to Windows SID string
std::optional<std::string> bytes_to_sid_string(std::span<const uint8_t> bytes);

/// Format duration in days to human-readable string
std::string format_duration(double days);

} // namespace chainsaw::analyse
```

### 6.2. Dependencies

| Dependency | Status | Notes |
|------------|--------|-------|
| SLICE-003 (CLI Parser) | Done | CLI options parsing |
| SLICE-015 (HVE Parser) | Verified | SOFTWARE hive parsing, parse_srum_entries |
| SLICE-016 (ESEDB Parser) | **Implemented** | Нативный парсер ESE (без libesedb), parse_sru_db_id_map_table |

**Обновление 2026-01-13:** SLICE-016 реализован как нативный парсер ESE формата:
- Не требует внешних зависимостей (libesedb)
- Работает на всех платформах (Windows/Linux/macOS)
- 20 unit-тестов, 24 SRUM unit-тестов проходят
- SRUDB.dat (1.8MB): 14 таблиц, 3590 записей, 714 SruDbIdMapTable entries

### 6.3. Implementation Notes

1. **prettytable** — C++ реализация Table formatting с Unicode box-drawing (уже есть в SLICE-002)
2. **BTreeMap** → `std::map` — автоматическая сортировка по ключу (GUID)
3. **win32_ts_to_datetime** — уже реализовано в SLICE-016 (filetime_to_iso8601)
4. **JSON output** — RapidJSON serialization (SLICE-005)

## 7) Risks

| Risk ID | Description | Status | Mitigation |
|---------|-------------|--------|------------|
| RISK-SRUM-001 | Large SOFTWARE hive (73MB) performance | Open | Lazy parsing, streaming if needed |
| RISK-SRUM-002 | ProfileList parsing edge cases | Open | Unit tests with various hive structures |
| RISK-SRUM-003 | Windows SID format variations | Open | bytes_to_sid_string unit tests |

**Related Risks (Closed):**
- RISK-ESEDB-001..004: **Closed** (SLICE-016 Implemented — нативный парсер)
- RISK-HVE-001..004: **Closed** (SLICE-015 Verified)

## 8) S1–S4 Assessment

| Axis | Score | Justification |
|------|-------|---------------|
| **S1 Complexity** | LOW | Single command, clear data flow, dependencies verified |
| **S2 Risk** | LOW | 2 upstream tests, dependencies fully implemented |
| **S3 Dependencies** | LOW | SLICE-015 (Verified), SLICE-016 (Verified) — all ready |
| **S4 Effort** | MEDIUM | Record enrichment logic, Table formatting, SID conversion |

**Overall:** LOW-MEDIUM complexity slice. All axes at Low except Effort at Medium → acceptable for single iteration.

## 9) UnitReady Checklist

| # | Criterion | Status |
|---|-----------|--------|
| 1 | Micro-spec created | PASS |
| 2 | Behavior described with FACTS | PASS (48 facts) |
| 3 | Full test set defined | PASS (24 unit tests + 2 upstream integration + 2 golden runs) |
| 4 | Dependencies evaluated | PASS (SLICE-003 Done, SLICE-015 Verified, SLICE-016 Verified) |
| 5 | S1–S4 assessment correct | PASS (Low/Low/Low/Medium) |

**Status: UnitReady PASS**

## 10) Prerequisites for

Before starting implementation, verify:

1. **SLICE-016 ESEDB Parser:**
 - `EsedbParser::load` works
 - `parse_sru_db_id_map_table` available (need to implement)

2. **SLICE-015 HVE Parser:**
 - `HveParser::load` works
 - `parse_srum_entries` available (need to implement)

3. **Test fixtures:**
 - `tests/srum/SRUDB.dat` — already available
 - `tests/srum/SOFTWARE` — 73MB, download on demand (Category B per POL-0003)
 - Expected outputs available

## 11) Related Documents

- BACKLOG-0001: Porting Backlog (SLICE-017 entry)
- SPEC-SLICE-015: HVE Parser (dependency)
- SPEC-SLICE-016: ESEDB Parser (dependency)
- TESTMAT-0001: TST-0005, TST-0006 (upstream tests)
- GOV-0002: Equivalence Criteria
