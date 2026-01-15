# SPEC-SLICE-012 — Hunt Command Pipeline

## Meta

| Field | Value |
|-------|-------|
| Slice ID | SLICE-012 |
| MOD-* | MOD-0012 (Hunt) |
| FEAT-* | FEAT-0003 (hunt) |
| TST-* | TST-0004 (upstream) |
| RUN-* |,, |
| Status | UnitReady |
| Priority | 12 |
| Dependencies | SLICE-003 (CLI - Verified), SLICE-004 (Discovery - Verified), SLICE-007 (EVTX - Verified), SLICE-008 (Tau - Verified), SLICE-009 (Chainsaw Rules - Verified), SLICE-010 (Sigma Rules - Verified) |
| Created | 2026-01-11 |

## 1) Overview

Hunt Command — основная команда детектирования угроз в forensic artefacts. Применяет правила (Chainsaw и/или Sigma) к документам и выводит детектированные события.

**Ключевая функциональность:**
1. **Rule Loading** — загрузка Chainsaw и Sigma правил с фильтрацией по kind/level/status
2. **Mapping Files** — загрузка mapping YAML для связи Sigma правил с источниками данных
3. **Hunting** — применение правил к документам через tau_engine::solve
4. **Aggregation** — группировка результатов по полям с count patterns
5. **Field Mapping** — преобразование полей документа для вывода
6. **Time Filtering** — `--from`/`--to` фильтрация по timestamp
7. **Cache-to-Disk** — опциональное кеширование результатов для экономии памяти
8. **Output Formats** — Table (default), JSON, JSONL, CSV, Log

**CLI signature:**
```
chainsaw hunt [rules] <path>... [options]
```

**Upstream тесты:** 1 тест (TST-0004 `hunt_r_any_logon`)

**Golden runs:**,,

## 2) Primary Sources (Rust)

### 2.1. HunterBuilder Pattern: `src/hunt.rs:134-547`

**Source:** `upstream/chainsaw/src/hunt.rs:134-547`

```rust
#[derive(Default)]
pub struct HunterBuilder {
 mappings: Option<Vec<PathBuf>>,
 rules: Option<Vec<Rule>>,

 load_unknown: Option<bool>,
 local: Option<bool>,
 preprocess: Option<bool>,
 from: Option<NaiveDateTime>,
 skip_errors: Option<bool>,
 timezone: Option<Tz>,
 to: Option<NaiveDateTime>,
}

impl HunterBuilder {
 pub fn new -> Self {... }
 pub fn build(self) -> crate::Result<Hunter> {... }
 pub fn from(mut self, datetime: NaiveDateTime) -> Self {... }
 pub fn load_unknown(mut self, allow: bool) -> Self {... }
 pub fn local(mut self, local: bool) -> Self {... }
 pub fn preprocess(mut self, preprocess: bool) -> Self {... }
 pub fn mappings(mut self, paths: Vec<PathBuf>) -> Self {... }
 pub fn rules(mut self, rules: Vec<Rule>) -> Self {... }
 pub fn skip_errors(mut self, skip: bool) -> Self {... }
 pub fn timezone(mut self, tz: Tz) -> Self {... }
 pub fn to(mut self, datetime: NaiveDateTime) -> Self {... }
}
```

**FACT-001:** HunterBuilder использует builder pattern для конструирования Hunter.

**FACT-002:** `rules` сортируются по имени при построении: `rules.sort_by(|x, y| x.name.cmp(y.name))`.

**FACT-003:** Для каждого Chainsaw rule создаётся Hunt с `HuntKind::Rule`.

### 2.2. Mapping Files: `src/hunt.rs:59-68, 184-274`

**Source:** `upstream/chainsaw/src/hunt.rs:59-68`

```rust
#[derive(Deserialize)]
pub struct Mapping {
 #[serde(default)]
 pub exclusions: HashSet<String>,
 #[serde(default)]
 pub extensions: Option<Extensions>,
 pub groups: Vec<Group>,
 pub kind: FileKind,
 pub rules: RuleKind,
}

#[derive(Clone, Deserialize)]
pub struct Group {
 #[serde(skip, default = "Uuid::new_v4")]
 pub id: Uuid,
 pub fields: Vec<Field>,
 #[serde(deserialize_with = "crate::ext::tau::deserialize_expression")]
 pub filter: Expression,
 pub name: String,
 pub timestamp: String,
}
```

**FACT-004:** Mapping загружается из YAML файлов через `serde_yaml::from_str`.

**FACT-005:** Mappings сортируются по path, groups сортируются по name.

**FACT-006:** Mapping.exclusions содержит имена правил для исключения из группы.

**FACT-007:** Mapping.extensions.preconditions содержит дополнительные фильтры для правил.

**FACT-008:** Если `RuleKind::Chainsaw` и есть mapping — ошибка: `"Chainsaw rules do not support mappings"`.

### 2.3. Hunt Execution: `src/hunt.rs:773-1103`

**Source:** `upstream/chainsaw/src/hunt.rs:773-1103`

```rust
impl Hunter {
 pub fn hunt<'a>(
 &'a self,
 file: &'a Path,
 cache: &Option<std::fs::File>,
 ) -> crate::Result<Vec<Detections<'a>>> {
 let mut reader = Reader::load(
 file,
 self.inner.load_unknown,
 self.inner.skip_errors,
 true,
 None,
 )?;

 reader
.documents
.par_bridge
.filter_map(|document| {... })
.collect::<crate::Result<Vec<Detections>>>?
 }
}
```

**FACT-009:** `hunt` использует `Reader::load` для загрузки документов.

**FACT-010:** Документы обрабатываются параллельно через `par_bridge` (Rayon).

**FACT-011:** Для каждого документа проверяется соответствие `hunt.file == document.kind`.

**FACT-012:** Timestamp извлекается из документа по полю `hunt.timestamp`, парсится формат `"%Y-%m-%dT%H:%M:%S%.fZ"`.

**FACT-013:** Если timestamp вне диапазона [from, to] — документ пропускается.

### 2.4. Field Mapping: `src/hunt.rs:562-714`

**Source:** `upstream/chainsaw/src/hunt.rs:562-714`

```rust
pub enum MapperKind {
 None,
 Fast(FxHashMap<String, String>),
 Full(FxHashMap<String, (String, Option<Container>, Option<ModSym>)>),
}

impl Mapper {
 pub fn from(fields: Vec<Field>) -> Self {... }
 pub fn mapped<'a, D>(&'a self, document: &'a D) -> Mapped<'a>
 where D: TauDocument {... }
}
```

**FACT-014:** Mapper преобразует поля документа согласно правилам Field.

**FACT-015:** Три режима: `None` (bypass), `Fast` (простое переименование), `Full` (с cast и container).

**FACT-016:** Container поддерживает форматы: `Json` (парсинг JSON из строки) и `Kv` (key-value pairs).

**FACT-017:** Cast поддерживает: `Int` (str→i64), `Str` (value→string).

### 2.5. Rule Matching: `src/hunt.rs:908-1019`

**Source:** `upstream/chainsaw/src/hunt.rs:908-1019`

```rust
match &hunt.kind {
 HuntKind::Group { exclusions, filter, kind, preconditions } => {
 if tau_engine::core::solve(filter, &mapped) {
 let rules = self.inner.rules.iter.collect::<Vec<(_, _)>>;
 let matches = rules.iter.filter_map(|(rid, rule)| {
 if!rule.is_kind(kind) { return None; }
 if exclusions.contains(rid) { return None; }
 if let Some(filter) = preconditions.get(rid)
 &&!tau_engine::core::solve(filter, &mapped) {
 return None;
 }
 if rule.solve(&mapped) { Some((*rid, rule)) }
 else { None }
 })...
 }
 }
 HuntKind::Rule { aggregate, filter } => {
 let hit = match &filter {
 Filter::Detection(detection) => tau_engine::solve(detection, &mapped),
 Filter::Expression(expression) => tau_engine::core::solve(expression, &mapped),
 };
...
 }
}
```

**FACT-018:** HuntKind::Group — для mapping groups с фильтром и списком правил.

**FACT-019:** HuntKind::Rule — для standalone Chainsaw правил.

**FACT-020:** Group сначала проверяет filter, потом проверяет все подходящие правила.

**FACT-021:** Preconditions применяются к правилам на основе метаданных (`for_` matching).

### 2.6. Aggregation: `src/hunt.rs:1062-1101`

**Source:** `upstream/chainsaw/src/hunt.rs:1062-1101`

```rust
for ((hid, rid), (aggregate, docs)) in aggregates {
 for ids in docs.values {
 let hit = match aggregate.count {
 Pattern::Equal(i) => ids.len == (i as usize),
 Pattern::GreaterThan(i) => ids.len > (i as usize),
 Pattern::GreaterThanOrEqual(i) => ids.len >= (i as usize),
 Pattern::LessThan(i) => ids.len < (i as usize),
 Pattern::LessThanOrEqual(i) => ids.len <= (i as usize),
 _ => false,
 };
...
 }
}
```

**FACT-022:** Aggregation группирует документы по hash полей (`aggregate.fields`).

**FACT-023:** Count pattern проверяется против количества сгруппированных документов.

**FACT-024:** Aggregated результат содержит все документы группы в `Kind::Aggregate`.

### 2.7. Preprocessing: `src/hunt.rs:282-443`

**Source:** `upstream/chainsaw/src/hunt.rs:282-443`

```rust
if preprocess {
 let mut keys = HashSet::new;
 for hunt in &hunts {
 keys.insert(hunt.timestamp.clone);
 // Extract all fields from filters, aggregates, preconditions
...
 }
 // Create compact field lookup
 let mut lookup = HashMap::with_capacity(keys.len);
 for (i, f) in keys.into_iter.enumerate {
 let x = (i / 255) as u8;
 let y = (i % 255) as u8;
 //... create compact field names
 }
}
```

**FACT-025:** Preprocessing (BETA) создаёт компактный lookup для полей.

**FACT-026:** Все поля заменяются на короткие идентификаторы для ускорения.

### 2.8. Cache-to-Disk: `src/hunt.rs:756-765, 1023-1043`

**Source:** `upstream/chainsaw/src/main.rs:756-765`

```rust
let cache = if cache {
 match tempfile::tempfile {
 Ok(f) => Some(f),
 Err(e) => { anyhow::bail!("Failed to create cache on disk - {}", e); }
 }
} else { None };
```

**FACT-027:** `--cache-to-disk` создаёт временный файл для кеширования результатов.

**FACT-028:** Cache требует `--jsonl` и конфликтует с `--json`.

**FACT-029:** При cache результаты пишутся в tempfile как JSON строки с offset/size.

### 2.9. CLI Integration: `src/main.rs:96-189, 518-810`

**Source:** `upstream/chainsaw/src/main.rs:96-189`

```rust
Command::Hunt {
 rules: Option<PathBuf>, // Positional: путь к правилам
 path: Vec<PathBuf>, // Positional: пути к артефактам

 mapping: Option<Vec<PathBuf>>, // -m, --mapping
 rule: Option<Vec<PathBuf>>, // -r, --rule (additional)
 sigma: Option<Vec<PathBuf>>, // -s, --sigma (requires mapping)

 cache: bool, // -c, --cache-to-disk
 column_width: Option<u32>, // --column-width
 csv: bool, // --csv (requires output)
 extension: Option<Vec<String>>, // --extension
 from: Option<NaiveDateTime>, // --from
 full: bool, // --full
 json: bool, // -j, --json
 jsonl: bool, // --jsonl
 kind: Vec<RuleKind>, // --kind
 level: Vec<RuleLevel>, // --level
 load_unknown: bool, // --load-unknown
 local: bool, // --local
 metadata: bool, // --metadata
 output: Option<PathBuf>, // -o, --output
 log: bool, // --log
 preprocess: bool, // --preprocess (BETA)
 quiet: bool, // -q
 skip_errors: bool, // --skip-errors
 status: Vec<RuleStatus>, // --status
 timezone: Option<Tz>, // --timezone
 to: Option<NaiveDateTime>, // --to
}
```

**FACT-030:** Позиционный `rules` переиспользуется как path, если указаны `-r` или `-s`.

**FACT-031:** `--sigma` требует `--mapping` для работы.

**FACT-032:** `--csv` требует `--output` (директория).

**FACT-033:** `--cache-to-disk` требует `--jsonl` и конфликтует с `--json`.

### 2.10. Output Formats: `src/main.rs:786-808, src/cli.rs`

**Source:** `upstream/chainsaw/src/main.rs:786-808`

```rust
if csv {
 cli::print_csv(&detections, hunter.hunts, hunter.rules, local, timezone)?;
} else if json {
 cli::print_json(&detections, hunter.hunts, hunter.rules, local, timezone)?;
} else if jsonl {
 // Work already done in loop
} else if log {
 cli::print_log(&detections, hunter.hunts, hunter.rules, local, timezone)?;
} else {
 cli::print_detections(...); // Table output
}
```

**FACT-034:** Default output — табличный формат с prettytable, ANSI colors, unicode box-drawing.

**FACT-035:** Table output содержит: timestamp, detections (с rule prefix), и поля из Fields.

**FACT-036:** JSON output — массив объектов с полями: group, timestamp, name, authors, level, status, document, kind.

**FACT-037:** JSONL output — по одному JSON объекту на строку.

**FACT-038:** CSV output — создаёт файлы в указанной директории.

**FACT-039:** Log output — текстовый формат для логирования.

### 2.11. Platform-Specific Output: `src/cli.rs:25-70`

**Source:** `upstream/chainsaw/src/cli.rs:25-27`

```rust
#[cfg(not(windows))]
pub const RULE_PREFIX: &str = "‣";
#[cfg(windows)]
pub const RULE_PREFIX: &str = "+";
```

**FACT-040:** RULE_PREFIX различается: `‣` на Unix/macOS, `+` на Windows.

**FACT-041:** Табличный вывод использует Unicode box-drawing characters.

**FACT-042:** ANSI escape sequences используются для цветного вывода (зелёный для group).

## 3) Upstream Tests (TST-0004)

| Test ID | Source | Verifies |
|---------|--------|----------|
| TST-0004 | `tests/clo.rs:72-94` | `hunt -r` table output |

**Source:** `upstream/chainsaw/tests/clo.rs:72-94`

```rust
#[test]
fn hunt_r_any_logon {
 let ev = std::fs::read_to_string("tests/evtx/clo_hunt_r_any_logon.txt").unwrap;
 Command::cargo_bin("chainsaw").unwrap
.args(["hunt", "tests/evtx/security_sample.evtx", "-r", "tests/evtx/rule-any-logon.yml"])
.assert
.success
.stdout(ev);
}
```

**FACT-043:** TST-0004 проверяет табличный вывод с Chainsaw правилом.

**FACT-044:** Expected output содержит ANSI codes и Unicode box-drawing.

**FACT-045:** Тест использует `security_sample.evtx` и `rule-any-logon.yml` fixtures.

## 4) Golden Runs Mapping

| RUN-ID | Command | Verifies |
|--------|---------|----------|
| | `hunt <evtx> -r <rule>` | Table output |
| | `hunt <evtx> -r <rule> --jsonl -q` | JSONL output quiet |
| | `hunt <evtx> -r <rule> --jsonl --cache-to-disk -q` | Cache-to-disk |

## 5) C++ Test Plan (TST-HUNT-*)

### 5.1. Unit Tests

| Test ID | Description | Verifies |
|---------|-------------|----------|
| TST-HUNT-001 | HunterBuilder basic construction | FACT-001 |
| TST-HUNT-002 | Rules sorting by name | FACT-002 |
| TST-HUNT-003 | Mapping loading from YAML | FACT-004, FACT-005 |
| TST-HUNT-004 | Mapping exclusions | FACT-006 |
| TST-HUNT-005 | Mapping preconditions | FACT-007 |
| TST-HUNT-006 | Chainsaw rules + mapping error | FACT-008 |
| TST-HUNT-007 | Reader integration | FACT-009 |
| TST-HUNT-008 | Document kind matching | FACT-011 |
| TST-HUNT-009 | Timestamp parsing | FACT-012 |
| TST-HUNT-010 | Time range filtering | FACT-013 |
| TST-HUNT-011 | Mapper bypass mode | FACT-014, FACT-015 |
| TST-HUNT-012 | Mapper fast mode | FACT-015 |
| TST-HUNT-013 | Mapper full mode (cast) | FACT-015, FACT-017 |
| TST-HUNT-014 | Container JSON parsing | FACT-016 |
| TST-HUNT-015 | Container KV parsing | FACT-016 |
| TST-HUNT-016 | HuntKind::Group matching | FACT-018, FACT-020 |
| TST-HUNT-017 | HuntKind::Rule matching | FACT-019 |
| TST-HUNT-018 | Aggregation grouping | FACT-022 |
| TST-HUNT-019 | Aggregation count patterns | FACT-023 |
| TST-HUNT-020 | Aggregation result structure | FACT-024 |
| TST-HUNT-021 | Cache-to-disk creation | FACT-027 |
| TST-HUNT-022 | Cache-to-disk JSON format | FACT-029 |
| TST-HUNT-023 | Rules path as path fallback | FACT-030 |
| TST-HUNT-024 | Sigma requires mapping | FACT-031 |
| TST-HUNT-025 | CSV requires output | FACT-032 |

### 5.2. Integration Tests (Golden Run Comparison)

| Test ID | Golden Run | Description |
|---------|------------|-------------|
| TST-HUNT-INT-001 | | Table output (platform-specific) |
| TST-HUNT-INT-002 | | JSONL output byte-to-byte |
| TST-HUNT-INT-003 | | Cache-to-disk JSONL output |
| TST-HUNT-INT-004 | TST-0004 | Upstream test port |

## 6) Dependencies

| Slice | Status | Required For |
|-------|--------|--------------|
| SLICE-003 | Verified | CLI parsing (Command::Hunt) |
| SLICE-004 | Verified | File discovery (get_files) |
| SLICE-007 | Verified | EVTX parsing |
| SLICE-008 | Verified | Tau engine (solve, Detection) |
| SLICE-009 | Verified | Chainsaw rules loading |
| SLICE-010 | Verified | Sigma rules loading + conversion |

**All dependencies are Verified** — SLICE-012 is ready for implementation.

## 7) S1–S4 Assessment

| Axis | Rating | Rationale |
|------|--------|-----------|
| **S1: Scope** | MEDIUM | Много опций CLI, несколько output форматов |
| **S2: Verification** | LOW | 1 upstream тест + 3 golden runs |
| **S3: Unknowns** | LOW | Все зависимости Verified |
| **S4: Platform** | MEDIUM | ANSI/Unicode на Windows (RULE_PREFIX) |

**Rule:** Не более одной оси High → **READY for implementation**.

## 8) Risks and Mitigations

| Risk ID | Description | Mitigation |
|---------|-------------|------------|
| RISK-HUNT-001 | Non-deterministic output (par_bridge) | Sequential processing for C++; document behavior |
| RISK-HUNT-002 | Table output platform differences (ANSI/Unicode) | Platform-specific expected outputs; `--no-banner` |
| RISK-HUNT-003 | Aggregation memory usage | Implement cache-to-disk properly |
| RISK-HUNT-004 | Preprocessing complexity | Defer to optimization phase (Phase G) |

## 9) Invariants (INV-*)

| INV-ID | Statement |
|--------|-----------|
| INV-001 | Результаты содержат только документы, соответствующие хотя бы одному правилу |
| INV-002 | Для HuntKind::Group сначала проверяется group filter, потом правила |
| INV-003 | Exclusions применяются до matching правил |
| INV-004 | Preconditions применяются на основе метаданных правила |
| INV-005 | Aggregation группирует по hash всех aggregate fields |
| INV-006 | --from/--to фильтрация происходит до rule matching |
| INV-007 | Timestamp field берётся из hunt.timestamp, не из правила |
| INV-008 | JSONL output — один JSON объект на строку, без trailing comma |

## 10) Implementation Approach

### 10.1. Recommended Order

1. **Mapping structures** — Mapping, Group, Extensions, Precondition
2. **Mapper** — None/Fast/Full modes, Container support
3. **HunterBuilder + Hunter** — builder pattern, configuration
4. **Hunt execution** — document iteration, type matching, time filtering
5. **Rule matching** — HuntKind::Group and HuntKind::Rule
6. **Aggregation** — grouping, count patterns
7. **Output formats** — Table, JSON, JSONL (CSV/Log optional)
8. **CLI integration** — extend Command::Hunt
9. **Cache-to-disk** — optional optimization

### 10.2. Parallelism Decision

Rust использует `par_bridge` с недетерминированным порядком.
Для 1:1 эквивалентности рекомендуется **последовательная обработка** (как в SLICE-011).

### 10.3. Table Output

Table output требует:
- prettytable-like formatting
- ANSI escape sequences (зелёный для group name)
- Unicode box-drawing characters
- Platform-specific RULE_PREFIX

Можно использовать терминальную библиотеку или простой formatter.

## 11) Links

- Upstream source: `upstream/chainsaw/src/hunt.rs`
- CLI source: `upstream/chainsaw/src/main.rs:96-189, 518-810`
- CLI contract: `docs/as_is/CLI-0001-chainsaw-cli-contract.md#22-hunt`
- Test matrix: `docs/tests/TESTMAT-0001-rust-test-matrix.md` (TST-0004)
- Golden runs: `docs/reports/REP-0001-rust-golden-runs.md`..0303)
- Feature inventory: `docs/as_is/FEAT-0001-feature-inventory.md` (FEAT-0003)
- Backlog: `docs/backlog/BACKLOG-0001-porting-backlog.md` (SLICE-012)
