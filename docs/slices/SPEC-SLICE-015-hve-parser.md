# SPEC-SLICE-015 — HVE Parser (Windows Registry Hive)

## Meta

| Field | Value |
|-------|-------|
| Slice ID | SLICE-015 |
| MOD-* | MOD-0008 (file formats) |
| Status | UnitReady |
| Priority | 15 |
| Dependencies | SLICE-005 (Reader Framework - Done) |
| Created | 2026-01-12 |

## 1) Overview

HVE Parser — парсер Windows Registry Hive файлов (формат REGF). Является критической зависимостью для:
- **analyse shimcache** (SLICE-019) — требует SYSTEM hive
- **analyse srum** (SLICE-016/017) — требует SOFTWARE hive
- **analyse amcache** — требует Amcache.hve

**Upstream использует crate `notatin`** — fork библиотеки от WithSecureLabs для парсинга registry hives с поддержкой транзакционных логов (.LOG,.LOG1,.LOG2).

**Ключевая функциональность:**
1. **Загрузка hive файла** с автоматическим поиском и подключением transaction logs
2. **Навигация по registry дереву** — `get_key`, `read_sub_keys`, `value_iter`
3. **Чтение значений** — Binary, U32, U64, I32, I64, String, MultiString, None, Error
4. **Восстановление удалённых записей** из транзакционных логов

## 2) Primary Sources (Rust)

### 2.1. External Crate: notatin (git fork)

**Source:** `upstream/chainsaw/Cargo.toml:25`
```toml
notatin = { git = "https://github.com/forensicmatt/notatin.git", features = ["recovery"] }
```

**Repository:** https://github.com/forensicmatt/notatin (MIT License)
**Fork of:** https://github.com/WithSecureLabs/notatin

**Public API используемый chainsaw:**

```rust
// notatin::parser
pub struct Parser {... }
impl Parser {
 pub fn get_key(&mut self, path: &str, with_logs: bool) -> Result<Option<CellKeyNode>>;
}

// notatin::parser_builder
pub struct ParserBuilder {... }
impl ParserBuilder {
 pub fn new -> Self;
 pub fn with_path(self, path: &Path) -> Result<Self>;
 pub fn with_recovered_deleted(self, enable: bool) -> Self;
 pub fn with_logs(self, logs: &[PathBuf]) -> Self;
 pub fn build(self) -> Result<Parser>;
}

// notatin::cell_key_node
pub struct CellKeyNode {... }
impl CellKeyNode {
 pub fn key_name: String;
 pub fn get_pretty_path(&self) -> String;
 pub fn get_value(&self, name: &str) -> Option<&CellValue>;
 pub fn value_iter(&self) -> impl Iterator<Item=&CellValue>;
 pub fn read_sub_keys(&mut self, parser: &mut Parser) -> Vec<CellKeyNode>;
 pub fn last_key_written_date_and_time(&self) -> DateTime<Utc>;
}

// notatin::cell_value
pub enum CellValue {
 Binary(Vec<u8>),
 U32(u32),
 U64(u64),
 I32(i32),
 I64(i64),
 String(String),
 MultiString(Vec<String>),
 None,
 Error,
}
```

### 2.2. Chainsaw HVE Module: `src/file/hve/mod.rs`

**Source:** `upstream/chainsaw/src/file/hve/mod.rs:1-94`

```rust
pub mod amcache;
pub mod shimcache;
pub mod srum;

use notatin::parser::Parser as HveParser;
use notatin::parser_builder::ParserBuilder;

pub type Hve = Json; // Type alias: HVE представлен как JSON

pub struct Parser {
 inner: HveParser,
}

impl Parser {
 /// Загрузить hive файл с автоматическим поиском transaction logs
 /// FACT-001: Ищет.LOG,.LOG1,.LOG2 в той же директории
 /// FACT-002: Использует recovered_deleted=true для восстановления удалённых записей
 pub fn load(path: &Path) -> crate::Result<Self> {
 // Построить список возможных log файлов
 let logs: Vec<PathBuf> = ["LOG", "LOG1", "LOG2"]
.iter
.filter_map(|ext| {
 let log_path = path.with_extension(ext);
 if log_path.exists { Some(log_path) } else { None }
 })
.collect;

 let inner = ParserBuilder::new
.with_path(path)?
.with_recovered_deleted(true)
.with_logs(&logs)
.build?;

 Ok(Self { inner })
 }
}
```

### 2.3. Integration with Reader Framework

**Source:** `upstream/chainsaw/src/file/mod.rs:38-68`

```rust
pub enum Kind {
 Evtx,
 Hve, // FACT-003: Kind::Hve для registry hives
 Json,
 Jsonl,
 Mft,
 Xml,
 Esedb,
 Unknown,
}

impl Kind {
 pub fn extensions(&self) -> &'static [&'static str] {
 match self {
 Kind::Hve => &["hve"], // FACT-004: Расширение "hve"
 //...
 }
 }
}
```

**Fallback position:** `upstream/chainsaw/src/file/mod.rs:241-250`
```rust
// FACT-005: Fallback позиция 5 (после EVTX, MFT, JSON, XML)
// При load_unknown=true пробуется в указанном порядке
```

## 3) FACTS — Поведенческая истина

### 3.1. Parser Loading

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-001 | hve/mod.rs:24-35 | Parser::load автоматически ищет transaction logs (.LOG,.LOG1,.LOG2) в директории hive файла |
| FACT-002 | hve/mod.rs:40 | Parser использует `with_recovered_deleted(true)` для восстановления удалённых записей |
| FACT-003 | mod.rs:41 | Kind::Hve — отдельный вариант enum для registry hives |
| FACT-004 | mod.rs:52 | Расширение ".hve" (без точки в коде: "hve") |
| FACT-005 | mod.rs:241-250 | Fallback позиция 5 при load_unknown=true |
| FACT-006 | hve/mod.rs:11 | Type alias: `pub type Hve = Json` — HVE представляется как JSON |

### 3.2. Key Navigation

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-007 | notatin | `get_key(path, with_logs)` возвращает `Option<CellKeyNode>` для пути registry |
| FACT-008 | notatin | Путь registry использует обратный слэш: `r"Microsoft\Windows NT\CurrentVersion"` |
| FACT-009 | notatin | `read_sub_keys` требует mutable reference на Parser |
| FACT-010 | notatin | `last_key_written_date_and_time` возвращает timestamp последней модификации ключа |

### 3.3. Value Types

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-011 | notatin | CellValue::Binary(Vec<u8>) — REG_BINARY |
| FACT-012 | notatin | CellValue::U32(u32) — REG_DWORD |
| FACT-013 | notatin | CellValue::U64(u64) — REG_QWORD |
| FACT-014 | notatin | CellValue::I32(i32) — signed DWORD |
| FACT-015 | notatin | CellValue::I64(i64) — signed QWORD |
| FACT-016 | notatin | CellValue::String(String) — REG_SZ |
| FACT-017 | notatin | CellValue::MultiString(Vec<String>) — REG_MULTI_SZ |
| FACT-018 | notatin | CellValue::None — пустое значение |
| FACT-019 | notatin | CellValue::Error — ошибка чтения значения |

### 3.4. Submodule Contracts (API для анализаторов)

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-020 | srum.rs:33-195 | `Parser::parse_srum_entries` парсит SRUM параметры из SOFTWARE hive |
| FACT-021 | shimcache.rs | `Parser::parse_shimcache` парсит Application Compatibility Cache из SYSTEM hive |
| FACT-022 | amcache.rs:37-280 | `Parser::parse_amcache` парсит Amcache.hve с поддержкой старого и нового формата |

### 3.5. Error Handling

| FACT-ID | Source | Fact |
|---------|--------|------|
| FACT-023 | hve/mod.rs:42-45 | Ошибка при невозможности построить парсер через `anyhow!` |
| FACT-024 | srum.rs:41 | Ошибка при отсутствии обязательного registry key |

## 4) Инварианты

| INV-ID | Description |
|--------|-------------|
| INV-001 | Parser::load должен находить и подключать существующие transaction logs автоматически |
| INV-002 | CellValue преобразование в JSON должно сохранять типы (числа как числа, строки как строки) |
| INV-003 | Registry пути должны использовать backslash (`\`) разделитель |
| INV-004 | Значения с CellValue::None или CellValue::Error должны маппиться в JSON null |

## 5) Tests

### 5.1. Upstream Tests (Indirect)

HVE Parser **не имеет прямых unit-тестов** в chainsaw. Тестирование происходит через integration tests SRUM:

| TST-ID | Source | Description | Uses HVE |
|--------|--------|-------------|----------|
| TST-0005 | tests/clo.rs:96-120 | `analyse_srum_database_table_details` | SOFTWARE hive |
| TST-0006 | tests/clo.rs:122-145 | `analyse_srum_database_json` | SOFTWARE hive |

### 5.2. Unit Tests for C++ Port

| TST-ID | Test Name | Description | FACT-* |
|--------|-----------|-------------|--------|
| TST-HVE-001 | Load_ValidHive | Загрузка валидного SOFTWARE hive | FACT-001, FACT-002 |
| TST-HVE-002 | Load_WithTransactionLogs | Загрузка hive с.LOG/.LOG1/.LOG2 | FACT-001 |
| TST-HVE-003 | Load_InvalidFile | Ошибка при невалидном файле | FACT-023 |
| TST-HVE-004 | GetKey_ValidPath | Получение ключа по пути | FACT-007, FACT-008 |
| TST-HVE-005 | GetKey_InvalidPath | None для несуществующего ключа | FACT-007 |
| TST-HVE-006 | ReadSubKeys | Итерация по подключам | FACT-009 |
| TST-HVE-007 | ValueIter | Итерация по значениям ключа | FACT-011-019 |
| TST-HVE-008 | CellValue_Binary | Чтение REG_BINARY | FACT-011 |
| TST-HVE-009 | CellValue_U32 | Чтение REG_DWORD | FACT-012 |
| TST-HVE-010 | CellValue_U64 | Чтение REG_QWORD | FACT-013 |
| TST-HVE-011 | CellValue_String | Чтение REG_SZ | FACT-016 |
| TST-HVE-012 | CellValue_MultiString | Чтение REG_MULTI_SZ | FACT-017 |
| TST-HVE-013 | KeyLastModified | Получение timestamp ключа | FACT-010 |
| TST-HVE-014 | Reader_Integration | Интеграция с Reader::open | FACT-003, FACT-004 |
| TST-HVE-015 | Fallback_Position | Позиция в fallback цепочке | FACT-005 |

### 5.3. Golden Runs (Integration)

| RUN-ID | Command | Description | Fixture |
|--------|---------|-------------|---------|
|x | `analyse srum --software SOFTWARE SRUDB.dat -q` | SRUM analysis (requires SLICE-016/017) | tests/srum/* |

**Note:** Golden runs для HVE Parser зависят от SLICE-016/017 (SRUM Analyser). Отдельные golden runs для самого парсера отсутствуют.

## 6) Implementation Strategy (ADR-0009)

### 6.1. C++ Library Options

| Option | Library | License | Status | Pros | Cons |
|--------|---------|---------|--------|------|------|
| A | libregf | LGPLv3+ | Alpha | libyal ecosystem, documented format | Alpha status, C API, **NO transaction log API** |
| B | windows-hive-parser | Unknown | Active | Modern C++, simple | Less tested, **NO transaction log support** |
| C | Custom parser | N/A | N/A | Full control, can port notatin logic | Significant effort |

### 6.2. Critical Finding: Transaction Log Support

**RISK-HVE-002 Research Results:**

Исследование показало, что **ни libregf, ни windows-hive-parser НЕ поддерживают transaction logs** (.LOG,.LOG1,.LOG2) на уровне публичного API:

- **libregf**: Документация описывает формат transaction log файлов (dirty vector, variants 1/2/6), но API (`libregf.h`) не содержит функций для их обработки
- **windows-hive-parser**: Простой парсер без forensic features

**Upstream поведение (notatin):**
```rust
// notatin автоматически ищет и подключает transaction logs
let logs: Vec<PathBuf> = ["LOG", "LOG1", "LOG2"]
.iter
.filter_map(|ext| {
 let log_path = path.with_extension(ext);
 if log_path.exists { Some(log_path) } else { None }
 })
.collect;

ParserBuilder::new
.with_path(path)?
.with_recovered_deleted(true) // восстановление удалённых записей
.with_logs(&logs) // подключение transaction logs
.build?
```

### 6.3. Recommended Approach

**Option C: Custom/Hybrid Parser** (updated recommendation)

Учитывая отсутствие transaction log support в существующих библиотеках:

1. **Базовый парсинг**: использовать libregf для чтения структуры hive
2. **Transaction logs**: портировать логику из notatin для:
 - Поиск.LOG,.LOG1,.LOG2 файлов
 - Применение dirty vector для восстановления
 - Восстановление удалённых записей

**Альтернатива (упрощённая):**
- Использовать libregf без transaction logs
- Принять ограничение: некоторые удалённые записи не будут восстановлены
- Задокументировать как known limitation

**Решение должно быть зафиксировано в ADR-0009.**

### 6.3. API Design (C++)

```cpp
namespace chainsaw::io {

/// Registry value types
enum class RegValueType {
 Binary,
 Dword, // U32
 Qword, // U64
 String, // REG_SZ
 MultiString,// REG_MULTI_SZ
 None,
 Error
};

/// Registry value
struct RegValue {
 std::string name;
 RegValueType type;
 std::variant<
 std::vector<uint8_t>, // Binary
 uint32_t, // Dword
 uint64_t, // Qword
 std::string, // String
 std::vector<std::string>, // MultiString
 std::monostate // None/Error
 > data;
};

/// Registry key
class RegKey {
public:
 std::string name const;
 std::string path const;
 std::chrono::system_clock::time_point last_modified const;

 std::optional<RegValue> get_value(std::string_view name) const;
 std::vector<RegValue> values const;
 std::vector<RegKey> subkeys;
};

/// HVE Parser
class HveParser {
public:
 static std::expected<HveParser, std::string> load(
 const std::filesystem::path& path);

 std::optional<RegKey> get_key(std::string_view path);

private:
 // Implementation-specific (libregf or custom)
};

/// HVE Reader for Reader framework integration
std::unique_ptr<Reader> create_hve_reader(
 const std::filesystem::path& path,
 bool skip_errors);

} // namespace chainsaw::io
```

## 7) Risks

| Risk ID | Description | Status | Mitigation |
|---------|-------------|--------|------------|
| RISK-HVE-001 | libregf API differences from notatin | Open | Create abstraction layer matching notatin semantics |
| RISK-HVE-002 | Transaction log support | **Mitigated** | Исследование: ни libregf, ни windows-hive-parser НЕ поддерживают transaction logs. Решение: (1) принять ограничение, или (2) портировать логику из notatin |
| RISK-HVE-003 | No direct unit tests upstream | **Mitigated** | 15 unit-тестов TST-HVE-001..015 определены в SPEC; реализация в |
| RISK-HVE-004 | No golden runs for standalone HVE | **Mitigated** | Стратегия: unit-тесты + integration через TST-0005/0006 (SRUM) |

## 8) S1–S4 Assessment

| Axis | Score | Justification |
|------|-------|---------------|
| **S1 Complexity** | HIGH | External C library integration (libregf), registry format parsing, transaction log support |
| **S2 Risk** | HIGH | ADR-0009 pending decision, library maturity unknown, no direct upstream tests |
| **S3 Dependencies** | MEDIUM | SLICE-005 (Done), libregf external dependency |
| **S4 Effort** | HIGH | Library evaluation, wrapper implementation, test fixtures creation |

**Overall:** HIGH complexity slice requiring ADR-0009 decision before implementation.

## 9) UnitReady Checklist

| # | Criterion | Status |
|---|-----------|--------|
| 1 | Micro-spec created | PASS |
| 2 | Behavior described with FACTS | PASS (24 facts) |
| 3 | Full test set defined | PASS (15 unit tests, integration via TST-0005/0006) |
| 4 | Dependencies evaluated | PASS (SLICE-005 Done, libregf pending evaluation) |
| 5 | S1–S4 assessment correct | PASS (High/High/Medium/High) |

**Status: UnitReady PASS** (with prerequisite: ADR-0009 decision required before )

## 10) Prerequisites for

Before starting implementation, the following must be resolved:

1. **ADR-0009 Update:** Finalize decision on HVE parser library (libregf vs custom)
2. **libregf Spike:** If libregf selected, verify:
 - Build on all 3 platforms (Windows/Linux/macOS)
 - API compatibility with required features
 - Transaction log support availability
3. **Test Fixtures:** Obtain/create test registry hive files:
 - Valid SOFTWARE hive (for SRUM tests)
 - ✅ Valid SYSTEM hive (for shimcache tests) — RISK-0022 CLOSED (2026-01-13)
 - Small synthetic hive for unit tests

## 11) Related Documents

- ADR-0009: Forensic Parsers Strategy
- BACKLOG-0001: Porting Backlog (SLICE-015 entry)
- ~~RISK-0022: No SYSTEM hive for shimcache coverage~~ — CLOSED (2026-01-13)
- RISK-0030: C++ stack for forensic formats (HVE pending)
- TESTMAT-0001: TST-0005, TST-0006 (SRUM tests using HVE)
