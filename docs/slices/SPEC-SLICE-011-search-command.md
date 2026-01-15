# SPEC-SLICE-011 — Search Command Pipeline

## Meta

| Field | Value |
|-------|-------|
| Slice ID | SLICE-011 |
| MOD-* | MOD-0003 (Search) |
| Status | UnitReady |
| Priority | 11 |
| Dependencies | SLICE-003 (CLI Parser - Verified), SLICE-005 (Reader - Verified), SLICE-007 (EVTX - Verified), SLICE-008 (Tau - Verified) |
| Created | 2026-01-11 |

## 1) Overview

Search Command — команда поиска по forensic artefacts. Поддерживает поиск по:
1. **Текстовым паттернам** (строка или regex)
2. **Tau-выражениям** (структурный поиск по полям документа)
3. **Комбинация** — множественные паттерны с AND/OR семантикой

**Ключевая функциональность:**
1. **Pattern search** — текстовый поиск по JSON-сериализованному документу (regex/contains)
2. **Tau search** — структурный поиск через tau_engine::solve
3. **Time filtering** — `--from`/`--to` с указанием `--timestamp` поля
4. **Output formats** — YAML (default), JSON array (`--json`), JSON Lines (`--jsonl`)
5. **Parallel processing** — `files.par_iter` через Rayon

**CLI signature:**
```
chainsaw search [pattern] <path>... [options]
```

**Upstream тесты:** 3 теста (TST-0001, TST-0002, TST-0003)

**Golden runs:**,,,

## 2) Primary Sources (Rust)

### 2.1. Searcher Builder Pattern: `src/search.rs:11-111`

**Source:** `upstream/chainsaw/src/search.rs:11-111`

```rust
#[derive(Clone, Debug)]
pub struct Searcher {
 match_any: bool,

 regex: Option<RegexSet>,
 tau: Vec<Detection>,

 from: Option<NaiveDateTime>,
 to: Option<NaiveDateTime>,
 timestamp: Option<String>,
 timezone: Option<Tz>,
 local: bool,

 load_unknown: bool,
 skip_errors: bool,
}

impl SearcherBuilder {
 pub fn patterns(mut self, patterns: Vec<String>) -> Self {... }
 pub fn tau(mut self, tau: Vec<String>) -> Self {... }
 pub fn ignore_case(mut self, ignore_case: bool) -> Self {... }
 pub fn match_any(mut self, match_any: bool) -> Self {... }
 pub fn from(mut self, from: NaiveDateTime) -> Self {... }
 pub fn to(mut self, to: NaiveDateTime) -> Self {... }
 pub fn timestamp(mut self, timestamp: String) -> Self {... }
 pub fn timezone(mut self, timezone: Tz) -> Self {... }
 pub fn local(mut self, local: bool) -> Self {... }
 pub fn load_unknown(mut self, load_unknown: bool) -> Self {... }
 pub fn skip_errors(mut self, skip_errors: bool) -> Self {... }
 pub fn build(self) -> Result<Searcher> {... }
}
```

**FACT-001:** Searcher использует builder pattern для конструирования.

**FACT-002:** `patterns` компилируются в `RegexSet` с опциональным `(?i)` prefix для case-insensitivity.

**FACT-003:** `tau` expressions парсятся через `tau_engine::core::parser::parse_identifier_string`.

### 2.2. Search Execution: `src/search.rs:113-188`

**Source:** `upstream/chainsaw/src/search.rs:113-188`

```rust
impl Searcher {
 pub fn search(&self, path: &Path) -> Result<Results> {
 let mut reader = Reader::load(path, self.load_unknown, self.skip_errors, false, None)?;
 Results { reader, searcher: self }
 }
}

impl<'a> Results<'a> {
 fn search(&self, document: &Document) -> bool {
 let json = match document {
 Document::Evtx(evtx) => &evtx.data,
 Document::Hve(json) | Document::Json(json) | Document::Xml(json)
 | Document::Mft(json) | Document::Esedb(json) => json,
 };

 // Pattern matching (regex)
 if let Some(regex) = &self.searcher.regex {
 let str = json.to_string.replace("\\\\", "\\");
 let matched = regex.matches(&str);
 // match_any: OR, else: AND
...
 }

 // Tau matching
 for tau in &self.searcher.tau {
 let matched = tau_engine::core::solve(&tau.expression, json);
...
 }
 }
}
```

**FACT-004:** Regex поиск выполняется по JSON.to_string с заменой `\\\\` → `\\` (нормализация escape-последовательностей).

**FACT-005:** Tau поиск выполняется через `tau_engine::core::solve(&detection.expression, document)`.

**FACT-006:** `match_any=false` (default) — AND семантика для множественных паттернов.
**FACT-007:** `match_any=true` — OR семантика.

### 2.3. Time Filtering: `src/search.rs:145-163`

**Source:** `upstream/chainsaw/src/search.rs:145-163`

```rust
// Extract timestamp from document
if let (Some(timestamp), Some(from), Some(to)) = (&self.timestamp, &self.from, &self.to) {
 if let Some(ts) = json.get(timestamp).and_then(|v| v.as_str) {
 let ts = NaiveDateTime::parse_from_str(ts, "%Y-%m-%dT%H:%M:%S%.fZ")
.or_else(|_| NaiveDateTime::parse_from_str(ts, "%Y-%m-%dT%H:%M:%S"))
.ok;
 if let Some(ts) = ts {
 if ts < *from || ts > *to { return false; }
 }
 }
}
```

**FACT-008:** `--from`/`--to` требуют `--timestamp <field>` для указания поля времени.

**FACT-009:** Поддерживаемые форматы времени: `%Y-%m-%dT%H:%M:%S%.fZ` и `%Y-%m-%dT%H:%M:%S`.

**FACT-010:** Документы вне диапазона [from, to] исключаются из результатов.

### 2.4. CLI Integration: `src/main.rs:203-270, 873-1043`

**Source:** `upstream/chainsaw/src/main.rs:203-270`

```rust
Command::Search {
 pattern: Option<String>, // Positional or -e/--regex
 path: Vec<PathBuf>, // Input paths
 additional_pattern: Option<Vec<String>>, // -e, --regex
 extension: Option<Vec<String>>,
 from: Option<NaiveDateTime>,
 ignore_case: bool, // -i, --ignore-case
 json: bool, // -j, --json
 jsonl: bool, // --jsonl
 load_unknown: bool,
 local: bool,
 match_any: bool, // --match-any
 output: Option<PathBuf>,
 quiet: bool, // -q
 skip_errors: bool,
 tau: Option<Vec<String>>, // -t, --tau
 timestamp: Option<String>,
 timezone: Option<Tz>,
 to: Option<NaiveDateTime>,
}
```

**FACT-011:** `pattern` обязателен, если не указан `-e/--regex` или `-t/--tau`.

**FACT-012:** Если `pattern` указан вместе с `-e/--tau`, он интерпретируется как первый путь.

**FACT-013:** Если `path` пуст, используется текущая директория.

### 2.5. Output Formatting: `src/main.rs:984-1038`

**Source:** `upstream/chainsaw/src/main.rs:984-1038`

```rust
if json {
 if *hit_count!= 0 { cs_print!(","); }
 cs_print_json!(&hit)?;
} else if jsonl {
 cs_print_json!(&hit)?;
 cs_println!;
} else {
 cs_println!("---");
 cs_print_yaml!(&hit)?;
}
```

**FACT-014:** Default format: YAML с `---` разделителем между документами.

**FACT-015:** `--json`: JSON array — `[doc1,doc2,...]` (compact, без разделителей строк).

**FACT-016:** `--jsonl`: JSON Lines — один JSON-объект на строку.

### 2.6. Parallel Processing: `src/main.rs:990-1034`

**Source:** `upstream/chainsaw/src/main.rs:990-1034`

```rust
files.par_iter.try_for_each(|file| {
 match searcher.search(file) {... }
})?;
```

**FACT-017:** Поиск выполняется параллельно через `files.par_iter` (Rayon).

**FACT-018:** Порядок результатов НЕ детерминирован при множестве файлов.

**FACT-019:** Mutex используется для thread-safe подсчёта hits и JSON-вывода.

### 2.7. Informational Output: `src/main.rs:925-955, 983, 1039-1042`

**Source:** `upstream/chainsaw/src/main.rs:925-955`

```rust
cs_eprintln!("[+] Loading forensic artefacts from: {} (extensions: {})",...);
cs_eprintln!("[+] Loaded {} forensic files ({})", files.len, size);
cs_eprintln!("[+] Searching forensic artefacts...");
// After search:
cs_eprintln!("[+] Found {} hits", total_hits);
```

**FACT-020:** Информационные сообщения пишутся в stderr с префиксом `[+]`.

**FACT-021:** `-q` (quiet) подавляет информационные сообщения (stderr).

**FACT-022:** Данные результатов всегда пишутся в stdout (или `--output` файл).

## 3) Upstream Tests (Terse Listing)

| Test ID | Source | Verifies |
|---------|--------|----------|
| TST-0001 | `tests/clo.rs:6-31` | `search -q` YAML output |
| TST-0002 | `tests/clo.rs:33-56` | `search -jq` JSON output |
| TST-0003 | `tests/clo.rs:58-81` | `search -q --jsonl` JSONL output |

**Source:** `upstream/chainsaw/tests/clo.rs`

```rust
#[test]
fn search_qj_simple_string {
 let ev = std::fs::read_to_string("tests/evtx/clo_search_qj_simple_string.txt").unwrap;
 Command::cargo_bin("chainsaw").unwrap
.args(["search", "4624", "tests/evtx/security_sample.evtx", "-q", "-j"])
.assert
.success
.stdout(ev);
}
```

## 4) Golden Runs Mapping

| RUN-ID | Command | Verifies |
|--------|---------|----------|
| | `search 4624 <evtx> -q` | YAML output (repeats=3) |
| | `search 4624 <evtx> -jq` | JSON output (repeats=3) |
| | `search 4624 <evtx> -q --jsonl` | JSONL output (repeats=3) |
| | `search 4624 <evtx with spaces> -q --jsonl` | Path with spaces |

## 5) C++ Test Plan (TST-SEARCH-*)

### 5.1. Unit Tests

| Test ID | Description | Verifies |
|---------|-------------|----------|
| TST-SEARCH-001 | SearcherBuilder basic construction | FACT-001 |
| TST-SEARCH-002 | Pattern compilation with ignore_case | FACT-002 |
| TST-SEARCH-003 | Tau expression parsing | FACT-003 |
| TST-SEARCH-004 | Regex search on JSON string | FACT-004 |
| TST-SEARCH-005 | Tau solve integration | FACT-005 |
| TST-SEARCH-006 | match_any=false (AND) | FACT-006 |
| TST-SEARCH-007 | match_any=true (OR) | FACT-007 |
| TST-SEARCH-008 | Time filtering basic | FACT-008, FACT-009 |
| TST-SEARCH-009 | Time filtering range exclusion | FACT-010 |
| TST-SEARCH-010 | Pattern as path fallback | FACT-012 |
| TST-SEARCH-011 | Default path is cwd | FACT-013 |
| TST-SEARCH-012 | YAML output format | FACT-014 |
| TST-SEARCH-013 | JSON output format | FACT-015 |
| TST-SEARCH-014 | JSONL output format | FACT-016 |
| TST-SEARCH-015 | Hit count tracking | FACT-019 |
| TST-SEARCH-016 | Quiet mode suppresses stderr | FACT-021 |

### 5.2. Integration Tests (Golden Run Comparison)

| Test ID | Golden Run | Description |
|---------|------------|-------------|
| TST-SEARCH-INT-001 | | YAML output byte-to-byte |
| TST-SEARCH-INT-002 | | JSON output byte-to-byte |
| TST-SEARCH-INT-003 | | JSONL output byte-to-byte |
| TST-SEARCH-INT-004 | | Path with spaces |

## 6) Dependencies

| Slice | Status | Required For |
|-------|--------|--------------|
| SLICE-003 | Verified | CLI parsing (Command::Search) |
| SLICE-005 | Verified | Reader::load |
| SLICE-007 | Verified | EVTX parsing |
| SLICE-008 | Verified | Tau engine (solve, parse_identifier_string) |

All dependencies are **Verified** — SLICE-011 is ready for implementation.

## 7) S1–S4 Assessment

| Axis | Rating | Rationale |
|------|--------|-----------|
| **S1: Complexity** | MEDIUM | Builder pattern + regex + tau + time filtering + parallel |
| **S2: Risk** | MEDIUM | Rayon parallelism → порядок результатов недетерминирован |
| **S3: Dependencies** | LOW | Все зависимости Verified |
| **S4: Effort** | MEDIUM | ~300 LOC Rust → ~400 LOC C++ |

**Overall:** Ready for implementation with caution on parallelism/ordering.

## 8) Risks and Mitigations

| Risk ID | Description | Mitigation |
|---------|-------------|------------|
| RISK-SEARCH-001 | Non-deterministic output order (par_iter) | Use `--num-threads 1` for deterministic tests; document behavior |
| RISK-SEARCH-002 | RegexSet compilation performance | Use std::regex or RE2; profile |
| RISK-SEARCH-003 | JSON string escape handling (`\\\\` → `\\`) | Implement identical normalization |

## 9) Invariants (INV-*)

| INV-ID | Statement |
|--------|-----------|
| INV-001 | Результаты содержат только документы, соответствующие всем (match_any=false) или любому (match_any=true) паттернам |
| INV-002 | `--from`/`--to` требуют `--timestamp`; без него фильтрация по времени не применяется |
| INV-003 | JSON output — валидный JSON array; JSONL — одна строка = один валидный JSON object |
| INV-004 | YAML output — каждый документ предваряется `---` |
| INV-005 | Hit count в итоговом сообщении равен количеству выведенных документов |

## 10) Notes

### 10.1. BACKLOG-0001 ID Discrepancy

В NEXT.md указано SLICE-011 как "Hunt Command", но согласно BACKLOG-0001 (Single Source of Truth):
- SLICE-011 = Search Command (Priority 11)
- SLICE-012 = Hunt Command (Priority 12)

Данный документ следует BACKLOG-0001 как SSoT.

### 10.2. Implementation Approach

Рекомендуемый порядок реализации:
1. `SearcherBuilder` + `Searcher` class
2. Pattern compilation (regex)
3. Tau expression parsing (reuse SLICE-008)
4. Search execution logic
5. Time filtering
6. Output formatting (reuse SLICE-002)
7. CLI integration (extend SLICE-003)
8. Parallel processing (std::execution или последовательно для детерминизма)

### 10.3. Parallelism Decision

C++17 `std::execution::par` или последовательная обработка?
- Rust использует Rayon с недетерминированным порядком
- Для 1:1 эквивалентности можно оставить последовательную обработку
- ADR-0012 required для окончательного решения
