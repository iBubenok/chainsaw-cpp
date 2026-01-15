# SPEC-SLICE-014 — Lint Command

## Meta

| Field | Value |
|-------|-------|
| Slice ID | SLICE-014 |
| MOD-* | MOD-0013 (Lint) |
| FEAT-* | FEAT-0006 (lint rules) |
| TST-* | TST-CSRULE-015..016 (unit), TST-CLI-006 (CLI integration) |
| RUN-* | |
| Status | UnitReady |
| Priority | 14 |
| Dependencies | SLICE-003 (CLI - Done), SLICE-009 (Chainsaw Rules - Verified), SLICE-010 (Sigma Rules - Verified) |
| Created | 2026-01-12 |

## 1) Overview

Lint Command — команда для валидации detection rules. Проверяет, что правила в формате Chainsaw или Sigma корректно загружаются и парсятся.

**Ключевая функциональность:**
1. **Rule Validation** — проверка загрузки правил через `rule::lint`
2. **Tau Output** — опциональный вывод tau expression (`--tau`)
3. **Error Reporting** — вывод ошибок парсинга с именем файла
4. **Summary Statistics** — подсчёт успешных/неудачных правил

**CLI signature:**
```
chainsaw lint <path> --kind <chainsaw|sigma> [-t|--tau]
```

**Upstream тесты:** Нет unit-тестов для lint command, только для `rule::lint` функции.

**Golden runs:** (`chainsaw lint <sigma_rules_dir> --kind sigma`)

## 2) Primary Sources (Rust)

### 2.1. CLI Definition: `src/main.rs:191-201`

**Source:** `upstream/chainsaw/src/main.rs:191-201`

```rust
/// Lint provided rules to ensure that they load correctly
Lint {
 /// The path to a collection of rules.
 path: PathBuf,
 /// The kind of rule to lint: chainsaw, sigma or stalker
 #[arg(long = "kind")]
 kind: RuleKind,
 /// Output tau logic.
 #[arg(short = 't', long = "tau")]
 tau: bool,
},
```

**FACT-001:** `path` — обязательный позиционный аргумент (путь к директории или файлу с правилами).

**FACT-002:** `--kind` — обязательный флаг, определяющий тип правил (chainsaw, sigma).

**FACT-003:** `--tau` — опциональный флаг для вывода tau expression.

**FACT-004:** В Rust поддерживается также `stalker` kind, но в C++ порте он не реализован (нет upstream тестов/документации).

### 2.2. Implementation: `src/main.rs:811-872`

**Source:** `upstream/chainsaw/src/main.rs:811-872`

```rust
Command::Lint { path, kind, tau } => {
 init_writer(None, false, false, false, args.verbose)?;
 if!args.no_banner {
 print_title;
 }
 cs_eprintln!("[+] Validating as {} for supplied detection rules...", kind);
 let mut count = 0;
 let mut failed = 0;
 for file in get_files(&path, &None, false)? {
 match lint_rule(&kind, &file) {
 Ok(filters) => {
 if tau {
 for filter in filters {
 match filter {
 Filter::Detection(detection) => {
 let mut detection = detection;
 // Apply optimizer passes
 let expression = coalesce(
 detection.expression,
 &mut detection.identifiers,
 );
 detection.identifiers.clear;
 let expression = shake(expression);
 let expression = rewrite(expression);
 let expression = matrix(expression);
 cs_println_yaml!(&expression)?;
 }
 Filter::Expression(_) => {
 cs_eyellowln!(
 "[!] Tau does not support visual representation of expressions"
 );
 }
 }
 }
 }
 count += 1;
 }
 Err(e) => {
 failed += 1;
 cs_eredln!(
 "[!] {}: {}",
 file.file_name.unwrap.to_string_lossy,
 e
 );
 }
 }
 }
 cs_eprintln!(
 "[+] Validated {} detection rules out of {}",
 count,
 count + failed
 );
}
```

**FACT-005:** Информационное сообщение `[+] Validating as {kind} for supplied detection rules...` выводится в stderr.

**FACT-006:** Для каждого файла вызывается `lint_rule(&kind, &file)`.

**FACT-007:** При успехе `count += 1`, при ошибке `failed += 1`.

**FACT-008:** При ошибке выводится `[!] {filename}: {error}` в stderr (красным).

**FACT-009:** Если `--tau`, для каждого Filter::Detection применяются optimizer passes и выводится YAML.

**FACT-010:** Для Filter::Expression выводится предупреждение (жёлтым).

**FACT-011:** В конце выводится `[+] Validated {count} detection rules out of {count + failed}`.

**FACT-012:** Файлы собираются через `get_files(&path, &None, false)` — без фильтрации по расширениям.

### 2.3. Lint Function: `src/rule/mod.rs:270-307`

**Source:** `upstream/chainsaw/src/rule/mod.rs:270-307`

```rust
pub fn lint(kind: &Kind, path: &Path) -> crate::Result<Vec<Filter>> {
 let extension = path
.extension
.and_then(|s| s.to_str)
.unwrap_or_default
.to_lowercase;
 if extension!= "yml" && extension!= "yaml" {
 return Err(anyhow::anyhow!(
 "rule must have a yaml file extension"
 ));
 }
 let mut filters = vec!;
 match kind {
 Kind::Chainsaw => {
 let rule = chainsaw::load(path)?;
 filters.push(rule.filter);
 }
 Kind::Sigma => {
 for yaml in sigma::load(path)? {
 let rule: sigma::Rule = serde_yaml::from_value(yaml)?;
 let filter = Filter::Detection(rule.tau.into);
 filters.push(filter);
 }
 }
 Kind::Stalker => {
 // Not implemented in C++ port
 }
 }
 Ok(filters)
}
```

**FACT-013:** Lint проверяет расширение файла: только `.yml` или `.yaml` (case-insensitive).

**FACT-014:** Для Chainsaw: вызывает `chainsaw::load(path)` и возвращает один filter.

**FACT-015:** Для Sigma: вызывает `sigma::load(path)` и парсит каждый YAML документ как sigma::Rule, возвращая Vec<Filter>.

**FACT-016:** Ошибка загрузки правила пробрасывается наверх (не продолжает итерацию).

### 2.4. Optimizer Passes for Tau Output

**Source:** `upstream/chainsaw/src/ext/tau.rs` и `tau-engine` crate

При `--tau` применяются следующие проходы оптимизации:
1. `coalesce(expression, identifiers)` — инлайнит identifiers в expression
2. `identifiers.clear` — очищает identifiers
3. `shake(expression)` — dead code elimination
4. `rewrite(expression)` — нормализация
5. `matrix(expression)` — multi-field optimization

**FACT-017:** Порядок optimizer passes фиксирован.

**FACT-018:** После оптимизации expression сериализуется в YAML.

## 3) C++ Implementation Status

### 3.1. CLI Parser (Done)

`LintCommand` уже определён в `cpp/include/chainsaw/cli.hpp:87-92`:

```cpp
struct LintCommand {
 std::filesystem::path path;
 std::optional<std::string> kind; // --kind (chainsaw|sigma)
 bool tau = false; // -t, --tau
};
```

**Отличие от Rust:** `kind` в C++ опциональный, в Rust обязательный. Нужно добавить валидацию.

### 3.2. Lint Function (Done)

`rule::lint` уже реализована в `cpp/src/rule/rule.cpp:945-993`:

```cpp
LintResult lint(Kind kind, const std::filesystem::path& path) {
 LintResult result;
 // Check extension
 // Load rule based on kind (Chainsaw/Sigma)
 // Return filters
}
```

### 3.3. Command Handler (Stub)

`run_lint` в `cpp/src/app/main.cpp:381-387` — заглушка:

```cpp
int run_lint(const chainsaw::cli::LintCommand& cmd,...) {
 writer.info("lint command invoked (not yet implemented)");
 return 0;
}
```

**Требуется:** Реализовать полную логику согласно FACT-005..012.

## 4) Data Model (FACTS Summary)

| FACT-ID | Description | Source |
|---------|-------------|--------|
| FACT-001 | path обязательный | main.rs:192 |
| FACT-002 | --kind обязательный | main.rs:194-195 |
| FACT-003 | --tau опциональный | main.rs:197-198 |
| FACT-005 | Validating message in stderr | main.rs:815 |
| FACT-006 | lint_rule for each file | main.rs:818 |
| FACT-007 | count/failed counters | main.rs:817-818, 838, 857 |
| FACT-008 | Error format [!] filename: error | main.rs:858-862 |
| FACT-009 | tau: optimizer + YAML | main.rs:820-852 |
| FACT-010 | Expression warning | main.rs:847-850 |
| FACT-011 | Summary message | main.rs:864-868 |
| FACT-012 | get_files with no extension filter | main.rs:818 |
| FACT-013 | Extension check yml/yaml | mod.rs:271-278 |
| FACT-014 | Chainsaw: single filter | mod.rs:284-286 |
| FACT-015 | Sigma: Vec<Filter> | mod.rs:288-293 |
| FACT-017 | Optimizer order fixed | main.rs:824-837 |

## 5) Test Plan (TST-LINT-*)

### 5.1. Existing Unit Tests

Уже существуют в `cpp/tests/test_rule_gtest.cpp`:

| Test ID | Description | Status |
|---------|-------------|--------|
| TST-CSRULE-015 | Lint valid rule | Done |
| TST-CSRULE-016 | Lint invalid extension | Done |

### 5.2. Existing CLI Tests

В `cpp/tests/test_cli_gtest.cpp`:

| Test ID | Description | Status |
|---------|-------------|--------|
| TST-CLI-006 | Parse lint command | Done |
| TST-CLI-STALKER | Reject stalker kind | Done |

### 5.3. New Unit Tests (TST-LINT-*)

| Test ID | Description | Verifies |
|---------|-------------|----------|
| TST-LINT-001 | Kind required validation | FACT-002 |
| TST-LINT-002 | Validating message format | FACT-005 |
| TST-LINT-003 | File iteration with lint_rule | FACT-006 |
| TST-LINT-004 | Count/failed counters | FACT-007 |
| TST-LINT-005 | Error message format | FACT-008 |
| TST-LINT-006 | Tau output for Detection | FACT-009 |
| TST-LINT-007 | Tau warning for Expression | FACT-010 |
| TST-LINT-008 | Summary message format | FACT-011 |
| TST-LINT-009 | Chainsaw single filter | FACT-014 |
| TST-LINT-010 | Sigma multi-filter | FACT-015 |
| TST-LINT-011 | Optimizer passes order | FACT-017 |

### 5.4. Integration Tests

| Test ID | Golden Run | Description |
|---------|------------|-------------|
| TST-LINT-INT-001 | | Sigma lint golden run comparison |
| TST-LINT-INT-002 | — | Chainsaw lint (синтетический) |
| TST-LINT-INT-003 | — | Mixed valid/invalid rules |
| TST-LINT-INT-004 | — | Tau output comparison |

## 6) S1–S4 Assessment

| Axis | Rating | Rationale |
|------|--------|-----------|
| **S1: Scope** | LOW | Один модуль, простая логика (iteration + lint + output) |
| **S2: Verification** | LOW | Есть unit-тесты для lint, один golden run |
| **S3: Unknowns** | LOW | Все зависимости Verified (SLICE-009, SLICE-010) |
| **S4: Platform** | LOW | Кроссплатформенный вывод текста |

**Rule:** Не более одной оси High → **READY for implementation (UnitReady)**.

## 7) Risks and Mitigations

| Risk ID | Description | Mitigation | Status |
|---------|-------------|------------|--------|
| RISK-LINT-001 | --kind optional в C++, required в Rust | Добавить валидацию в run_lint | **Closed** (exit code 2) |
| RISK-LINT-002 | Tau output YAML formatting | Использовать detection_to_yaml | **Closed** |
| RISK-LINT-003 | No stalker support | Отклонять с ошибкой | Mitigated (CLI test exists) |

## 8) Invariants (INV-*)

| INV-ID | Statement |
|--------|-----------|
| INV-001 | Если --kind не указан — ошибка CLI |
| INV-002 | Validating message всегда в stderr |
| INV-003 | Ошибки lint выводятся с форматом `[!] filename: error` |
| INV-004 | Summary message содержит count и total (count + failed) |
| INV-005 | При --tau Filter::Detection сериализуется после optimizer |
| INV-006 | При --tau Filter::Expression выводит предупреждение |
| INV-007 | Файлы без.yml/.yaml расширения отклоняются |

## 9) Implementation Approach

### 9.1. Recommended Order

1. **CLI validation** — добавить проверку обязательности `--kind`
2. **File discovery** — использовать `io::discover_files` без фильтра расширений
3. **Lint loop** — итерация с вызовом `rule::lint`
4. **Error handling** — формат ошибок `[!] filename: error`
5. **Counter management** — подсчёт count/failed
6. **Tau output** — optimizer passes + YAML serialization
7. **Summary output** — формат `[+] Validated N out of M`

### 9.2. Key Code Changes

```cpp
// main.cpp: run_lint
int run_lint(const cli::LintCommand& cmd, const cli::GlobalOptions& global,
 output::Writer& writer) {
 // 1. Validate --kind is provided
 if (!cmd.kind.has_value) {
 writer.error("error: the following required arguments were not provided:\n"
 " --kind <KIND>");
 return 2;
 }

 rule::Kind kind = rule::parse_kind(*cmd.kind);

 // 2. Print validating message
 writer.info(std::format("[+] Validating as {} for supplied detection rules...",
 rule::to_string(kind)));

 // 3. Discover files
 io::DiscoveryOptions disc_opt;
 disc_opt.skip_errors = false;
 auto files = io::discover_files({cmd.path}, disc_opt);

 // 4. Iterate and lint
 std::size_t count = 0;
 std::size_t failed = 0;

 for (const auto& file: files) {
 auto result = rule::lint(kind, file);
 if (result.ok) {
 if (cmd.tau) {
 // Output tau expression
 for (const auto& filter: result.filters) {
 // Apply optimizer and serialize to YAML
 }
 }
 ++count;
 } else {
 ++failed;
 writer.error(std::format("[!] {}: {}",
 file.filename.string,
 result.error.message));
 }
 }

 // 5. Print summary
 writer.info(std::format("[+] Validated {} detection rules out of {}",
 count, count + failed));

 return 0;
}
```

## 10) Links

- Upstream source: `upstream/chainsaw/src/main.rs:191-201, 811-872`
- Lint function: `upstream/chainsaw/src/rule/mod.rs:270-307`
- CLI contract: `docs/as_is/CLI-0001-chainsaw-cli-contract.md#26-lint`
- Golden runs: `docs/reports/REP-0001-rust-golden-runs.md`
- Feature inventory: `docs/as_is/FEAT-0001-feature-inventory.md` (FEAT-0006)
- Backlog: `docs/backlog/BACKLOG-0001-porting-backlog.md` (SLICE-014)
- C++ rule module: `cpp/include/chainsaw/rule.hpp`, `cpp/src/rule/rule.cpp`

---

## UnitReady Checklist

- [x] Micro-spec links to primary sources (code/tests)
- [x] All FACTS numbered and sourced
- [x] C++ implementation status documented
- [x] Test plan defined (existing + new)
- [x] S1-S4 assessed (all Low)
- [x] Risks identified (3 risks, 1 mitigated)
- [x] Dependencies verified (SLICE-003 Done, SLICE-009 Verified, SLICE-010 Verified)
- [x] No S1-S4 axis is High

**UnitReady: PASS**
