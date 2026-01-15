# SPEC-SLICE-013 — Dump Command Pipeline

## Meta

| Field | Value |
|-------|-------|
| Slice ID | SLICE-013 |
| MOD-* | MOD-0010 (Dump) |
| FEAT-* | FEAT-0002 (dump) |
| TST-* | — (нет upstream тестов) |
| RUN-* |, |
| Status | UnitReady |
| Priority | 13 |
| Dependencies | SLICE-003 (CLI - Verified), SLICE-004 (Discovery - Verified), SLICE-005 (Reader - Verified), SLICE-006 (XML - Verified), SLICE-007 (EVTX - Verified) |
| Created | 2026-01-12 |

## 1) Overview

Dump Command — команда для выгрузки содержимого forensic artefacts в различных форматах. Простейшая по логике команда в Chainsaw: читает файлы через Reader и выводит их содержимое.

**Ключевая функциональность:**
1. **File Loading** — загрузка файлов через get_files + Reader::load
2. **Document Iteration** — итерация по documents для каждого файла
3. **Output Formats** — YAML (default), JSON (--json), JSONL (--jsonl)
4. **MFT Support** — специфичные опции для MFT data streams

**CLI signature:**
```
chainsaw dump <path>... [options]
```

**Upstream тесты:** Нет (только golden runs)

**Golden runs:** (YAML), (JSON)

## 2) Primary Sources (Rust)

### 2.1. CLI Definition: `src/main.rs:59-94`

**Source:** `upstream/chainsaw/src/main.rs:59-94`

```rust
Command::Dump {
 /// The paths containing files to dump.
 path: Vec<PathBuf>,

 /// Dump in json format.
 #[arg(group = "format", short = 'j', long = "json")]
 json: bool,
 /// Print the output in jsonl format.
 #[arg(group = "format", long = "jsonl")]
 jsonl: bool,
 /// Allow chainsaw to try and load files it cannot identify.
 #[arg(long = "load-unknown")]
 load_unknown: bool,
 /// Only dump files with the provided extension.
 #[arg(long = "extension")]
 extension: Option<String>,
 /// A path to output results to.
 #[arg(short = 'o', long = "output")]
 output: Option<PathBuf>,
 /// Suppress informational output.
 #[arg(short = 'q')]
 quiet: bool,
 /// Continue to hunt when an error is encountered.
 #[arg(long = "skip-errors")]
 skip_errors: bool,

 // MFT Specific Options
 /// Attempt to decode all extracted data streams from Hex to UTF-8
 #[arg(long = "decode-data-streams", help_heading = "MFT Specific Options")]
 decode_data_streams: bool,
 /// Extracted data streams will be decoded and written to this directory
 #[arg(long = "data-streams-directory", help_heading = "MFT Specific Options")]
 data_streams_directory: Option<PathBuf>,
}
```

**FACT-001:** Dump требует хотя бы один path (обязательный позиционный аргумент).

**FACT-002:** `--json` и `--jsonl` взаимоисключающие (clap group "format").

**FACT-003:** `--extension` фильтрует файлы по одному расширению (String, не Vec).

**FACT-004:** MFT-специфичные опции находятся в отдельной help_heading группе.

### 2.2. Implementation: `src/main.rs:409-517`

**Source:** `upstream/chainsaw/src/main.rs:409-517`

```rust
Command::Dump {
 path,
 json,
 jsonl,
 load_unknown,
 extension,
 output,
 quiet,
 skip_errors,
 decode_data_streams,
 data_streams_directory,
} => {
 init_writer(output, false, json, quiet, args.verbose)?;
 if!args.no_banner {
 print_title;
 }
 cs_eprintln!(
 "[+] Dumping the contents of forensic artefacts from: {} (extensions: {})",
 path.iter
.map(|r| r.display.to_string)
.collect::<Vec<_>>
.join(", "),
 extension.clone.unwrap_or("*".to_string)
 );

 if json {
 cs_print!("[");
 }

 let mut files = vec!;
 let mut size = ByteSize::mb(0);
 let mut extensions: Option<HashSet<String>> = None;
 if let Some(extension) = extension {
 extensions = Some(HashSet::from([extension]));
 }
 for path in &path {
 let res = get_files(path, &extensions, skip_errors)?;
 for i in &res {
 size += i.metadata?.len;
 }
 files.extend(res);
 }
 if files.is_empty {
 return Err(anyhow::anyhow!(
 "No compatible files were found in the provided paths",
 ));
 } else {
 cs_eprintln!("[+] Loaded {} forensic artefacts ({})", files.len, size);
 }

 let mut first = true;
 for path in &files {
 let mut reader = Reader::load(
 path,
 load_unknown,
 skip_errors,
 decode_data_streams,
 data_streams_directory.clone,
 )?;

 for result in reader.documents {
 let document = match result {
 Ok(document) => document,
 Err(e) => {
 if skip_errors {
 cs_eyellowln!(
 "[!] failed to parse document '{}' - {}\n",
 path.display,
 e
 );
 continue;
 }
 return Err(e);
 }
 };
 let value = match document {
 Document::Evtx(evtx) => evtx.data,
 Document::Hve(json)
 | Document::Json(json)
 | Document::Xml(json)
 | Document::Mft(json)
 | Document::Esedb(json) => json,
 };

 if json {
 if first {
 first = false;
 } else {
 cs_println!(",");
 }
 cs_print_json_pretty!(&value)?;
 } else if jsonl {
 cs_print_json!(&value)?;
 cs_println!;
 } else {
 cs_println!("---");
 cs_print_yaml!(&value)?;
 }
 }
 }
 if json {
 cs_println!("]");
 }
 cs_eprintln!("[+] Done");
}
```

**FACT-005:** Информационные сообщения выводятся в stderr через `cs_eprintln!`.

**FACT-006:** JSON формат выводится как массив `[...]` с pretty-printing.

**FACT-007:** JSONL формат выводит по одному JSON объекту на строку (compact).

**FACT-008:** YAML формат (default) выводит каждый документ с разделителем `---`.

**FACT-009:** Для JSON формата запятая между элементами выводится через `cs_println!(",")` (с newline).

**FACT-010:** Файлы обрабатываются последовательно (не parallel).

**FACT-011:** Документы внутри файла также обрабатываются последовательно.

**FACT-012:** При `--skip-errors` ошибки парсинга документов логируются в stderr и пропускаются.

**FACT-013:** Если `files.is_empty` — возвращается ошибка "No compatible files were found".

### 2.3. Document Types: `src/file/mod.rs:24-32`

**Source:** `upstream/chainsaw/src/file/mod.rs:24-32`

```rust
#[derive(Clone)]
pub enum Document {
 Evtx(Evtx),
 Hve(Hve),
 Json(Json),
 Mft(Mft),
 Xml(Xml),
 Esedb(Esedb),
}
```

**FACT-014:** Dump поддерживает 6 типов документов: Evtx, Hve, Json, Mft, Xml, Esedb.

**FACT-015:** Document::Evtx содержит `evtx.data` (serde_json::Value).

**FACT-016:** Остальные Document типы (Hve, Json, Xml, Mft, Esedb) содержат serde_json::Value напрямую.

### 2.4. Reader Extension Detection: `src/file/mod.rs:103-383`

**Source:** `upstream/chainsaw/src/file/mod.rs:113-115, 137, 159, 181, 207, 229, 251`

```rust
match file.extension.and_then(|e| e.to_str) {
 Some(extension) => match extension {
 "evt" | "evtx" => Parser::Evtx,
 "json" => Parser::Json,
 "jsonl" => Parser::Jsonl,
 "bin" | "mft" => Parser::Mft,
 "xml" => Parser::Xml,
 "hve" => Parser::Hve,
 "dat" | "edb" => Parser::Esedb,
 _ => try load_unknown or Unknown
 }
 None => {
 // Edge case: filename "$MFT" => Parser::Mft
 // Otherwise try load_unknown
 }
}
```

**FACT-017:** Reader определяет тип файла по расширению (case-sensitive).

**FACT-018:** Файл без расширения с именем `$MFT` распознаётся как MFT.

**FACT-019:** `--load-unknown` пытается определить тип методом проб в порядке: Evtx, Mft, Json, Xml, Hve, Esedb.

**FACT-020:** Порядок проб в load_unknown фиксирован и влияет на результат при неоднозначных файлах.

### 2.5. File Extensions: `src/file/mod.rs:52-67`

**Source:** `upstream/chainsaw/src/file/mod.rs:52-67`

```rust
impl Kind {
 pub fn extensions(&self) -> Option<Vec<String>> {
 match self {
 Kind::Evtx => Some(vec!["evt".to_string, "evtx".to_string]),
 Kind::Hve => Some(vec!["hve".to_string]),
 Kind::Json => Some(vec!["json".to_string]),
 Kind::Jsonl => Some(vec!["jsonl".to_string]),
 Kind::Mft => Some(vec!["mft".to_string, "bin".to_string, "$MFT".to_string]),
 Kind::Xml => Some(vec!["xml".to_string]),
 Kind::Esedb => Some(vec!["dat".to_string, "edb".to_string]),
 Kind::Unknown => None,
 }
 }
}
```

**FACT-021:** Для EVTX поддерживаются расширения `.evt` и `.evtx`.

**FACT-022:** Для MFT поддерживаются расширения `.mft`, `.bin`, и имя `$MFT`.

**FACT-023:** Для ESEDB поддерживаются расширения `.dat` и `.edb`.

### 2.6. Output: `src/write.rs` (macros)

**Source:** Из использования в main.rs

```rust
cs_print_json_pretty!(&value)?; // JSON pretty-printed (no newline)
cs_print_json!(&value)?; // JSON compact (no newline)
cs_print_yaml!(&value)?; // YAML formatted
cs_println!; // newline
cs_println!("---"); // YAML document separator
```

**FACT-024:** `cs_print_json_pretty!` использует serde_json pretty formatting (2 spaces indent).

**FACT-025:** `cs_print_json!` использует compact JSON без лишних пробелов.

**FACT-026:** `cs_print_yaml!` использует serde_yaml для YAML formatting.

**FACT-027:** Все макросы пишут в установленный Writer (stdout или file).

## 3) Golden Runs Mapping

| RUN-ID | Command | Verifies |
|--------|---------|----------|
| | `chainsaw dump <security_sample.evtx> -q` | YAML output |
| | `chainsaw dump <security_sample.evtx> -q --json` | JSON output |

**Примечание:** Golden runs используют только EVTX файлы. Для полного покрытия требуются дополнительные фикстуры (JSON, XML, MFT).

## 4) C++ Test Plan (TST-DUMP-*)

### 4.1. Unit Tests

| Test ID | Description | Verifies |
|---------|-------------|----------|
| TST-DUMP-001 | Path required validation | FACT-001 |
| TST-DUMP-002 | JSON/JSONL mutual exclusion | FACT-002 |
| TST-DUMP-003 | Extension filter (single) | FACT-003 |
| TST-DUMP-004 | Stderr info messages | FACT-005 |
| TST-DUMP-005 | JSON array output format | FACT-006 |
| TST-DUMP-006 | JSONL output format | FACT-007 |
| TST-DUMP-007 | YAML separator "---" | FACT-008 |
| TST-DUMP-008 | JSON comma placement | FACT-009 |
| TST-DUMP-009 | Sequential file processing | FACT-010, FACT-011 |
| TST-DUMP-010 | Skip errors on document parse | FACT-012 |
| TST-DUMP-011 | Empty files error | FACT-013 |
| TST-DUMP-012 | Document type Evtx.data extraction | FACT-015 |
| TST-DUMP-013 | Document type JSON value extraction | FACT-016 |
| TST-DUMP-014 | Extension detection case-sensitive | FACT-017 |
| TST-DUMP-015 | $MFT filename detection | FACT-018 |
| TST-DUMP-016 | Load unknown probe order | FACT-019, FACT-020 |

### 4.2. Integration Tests (Golden Run Comparison)

| Test ID | Golden Run | Description |
|---------|------------|-------------|
| TST-DUMP-INT-001 | | YAML output byte-to-byte |
| TST-DUMP-INT-002 | | JSON output byte-to-byte |

### 4.3. Additional Tests (нет golden runs)

| Test ID | Description | Notes |
|---------|-------------|-------|
| TST-DUMP-INT-003 | JSONL output | Нет golden run, синтетический |
| TST-DUMP-INT-004 | JSON file dump | Требуется фикстура |
| TST-DUMP-INT-005 | XML file dump | Требуется фикстура |
| TST-DUMP-INT-006 | MFT file dump | Требуется фикстура |
| TST-DUMP-INT-007 | Multiple files dump | Порядок вывода |
| TST-DUMP-INT-008 | Output to file (-o) | Файловый вывод |

## 5) Dependencies

| Slice | Status | Required For |
|-------|--------|--------------|
| SLICE-003 | Verified | CLI parsing (Command::Dump) |
| SLICE-004 | Verified | File discovery (get_files) |
| SLICE-005 | Verified | Reader framework, JSON parser |
| SLICE-006 | Verified | XML parser |
| SLICE-007 | Verified | EVTX parser |

**All dependencies are Verified** — SLICE-013 is ready for implementation.

**Примечание:** HVE, MFT, ESEDB парсеры ещё не портированы (SLICE-015..018). Dump command будет поддерживать только EVTX, JSON, XML форматы до завершения этих слайсов.

## 6) S1–S4 Assessment

| Axis | Rating | Rationale |
|------|--------|-----------|
| **S1: Scope** | LOW | Один модуль, простая логика |
| **S2: Verification** | MEDIUM | Нет upstream тестов, только 2 golden runs |
| **S3: Unknowns** | LOW | Все зависимости Verified, логика прозрачная |
| **S4: Platform** | LOW | Кроссплатформенный вывод текста |

**Rule:** Не более одной оси High → **READY for implementation**.

## 7) Risks and Mitigations

| Risk ID | Description | Mitigation |
|---------|-------------|------------|
| RISK-DUMP-001 | Нет upstream тестов | Создать синтетические unit-тесты по FACTS |
| RISK-DUMP-002 | Только EVTX golden runs | Создать фикстуры для JSON/XML |
| RISK-DUMP-003 | HVE/MFT/ESEDB не портированы | Пометить как unsupported до SLICE-015..018 |
| RISK-DUMP-004 | JSON pretty-print formatting differences | Использовать RapidJSON с 2-space indent |

## 8) Invariants (INV-*)

| INV-ID | Statement |
|--------|-----------|
| INV-001 | Для JSON формата вывод начинается с `[` и заканчивается `]` |
| INV-002 | Для JSON формата запятая выводится МЕЖДУ элементами (не после последнего) |
| INV-003 | Для YAML формата каждый документ начинается с `---` |
| INV-004 | Файлы обрабатываются последовательно (детерминированный порядок) |
| INV-005 | Документы внутри файла обрабатываются последовательно |
| INV-006 | Информационные сообщения идут в stderr, данные в stdout |
| INV-007 | При -q информационные сообщения подавляются |
| INV-008 | При --skip-errors ошибки парсинга логируются и пропускаются |

## 9) Implementation Approach

### 9.1. Recommended Order

1. **CLI integration** — расширить Command::Dump в существующем CLI парсере
2. **Core dump loop** — обход файлов и документов
3. **YAML output** — default формат (используем yaml-cpp)
4. **JSON output** — pretty-printed массив
5. **JSONL output** — compact по строкам
6. **Error handling** — skip_errors logic
7. **Output to file** — Writer integration

### 9.2. Partial Support Strategy

До завершения SLICE-015..018 dump будет поддерживать:
- ✅ EVTX (SLICE-007)
- ✅ JSON (SLICE-005)
- ✅ XML (SLICE-006)
- ❌ HVE (ждёт SLICE-015)
- ❌ MFT (ждёт SLICE-018)
- ❌ ESEDB (ждёт SLICE-016)

При попытке dump неподдерживаемых форматов — вернуть ошибку или использовать Unknown parser.

### 9.3. JSON/YAML Library Alignment

- JSON: RapidJSON (ADR-0003) с 2-space indent для pretty-print
- YAML: yaml-cpp для сериализации

## 10) Links

- Upstream source: `upstream/chainsaw/src/main.rs:59-94, 409-517`
- Reader source: `upstream/chainsaw/src/file/mod.rs`
- CLI contract: `docs/as_is/CLI-0001-chainsaw-cli-contract.md#23-dump`
- Golden runs: `docs/reports/REP-0001-rust-golden-runs.md`,
- Feature inventory: `docs/as_is/FEAT-0001-feature-inventory.md` (FEAT-0002)
- Backlog: `docs/backlog/BACKLOG-0001-porting-backlog.md` (SLICE-013)
