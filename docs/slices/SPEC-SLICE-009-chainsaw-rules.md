# SPEC-SLICE-009 — Chainsaw Rules Loader

## Meta

| Field | Value |
|-------|-------|
| Slice ID | SLICE-009 |
| MOD-* | MOD-0008 (Rules/Chainsaw) |
| Status | UnitReady |
| Priority | 9 |
| Dependencies | SLICE-008 (Tau Engine - Done) |
| Created | 2026-01-11 |

## 1) Overview

Chainsaw Rules Loader — модуль загрузки, парсинга и валидации правил в формате Chainsaw YAML. Правила используются командами `hunt` и `lint` для сопоставления событий с детекционными паттернами.

**Ключевая функциональность:**
1. **Загрузка YAML файла** — парсинг Chainsaw правила из YAML
2. **Структура Rule** — название, группа, описание, авторы, поля, фильтр
3. **Фильтрация** — по kind/level/status при загрузке
4. **Оптимизация** — применение tau optimiser passes к filter
5. **Lint** — валидация правила и вывод tau expression

**Использование в chainsaw:**
- `hunt.rs`: загрузка правил через `rule::load`, применение к документам через `rule.solve`
- `lint.rs`: валидация правил через `rule::lint`, вывод tau detection

## 2) Primary Sources (Rust)

### 2.1. Rule Module: `src/rule/mod.rs`

**Source:** `upstream/chainsaw/src/rule/mod.rs:1-308`

**Public API:**

```rust
// Rule enum
pub enum Rule {
 Chainsaw(Chainsaw),
 Sigma(Sigma),
}

impl Rule {
 pub fn aggregate(&self) -> &Option<Aggregate>;
 pub fn is_kind(&self, kind: &Kind) -> bool;
 pub fn level(&self) -> &Level;
 pub fn types(&self) -> &FileKind; // возвращает kind файлов (evtx/mft/json/...)
 pub fn name(&self) -> &String;
 pub fn solve(&self, document: &dyn Document) -> bool;
 pub fn status(&self) -> &Status;
}

// Shared types
pub struct Aggregate {
 pub count: Pattern,
 pub fields: Vec<String>,
}

pub enum Filter {
 Detection(Detection),
 Expression(Expression),
}

pub enum Kind { Chainsaw, Sigma }
pub enum Level { Critical, High, Medium, Low, Info }
pub enum Status { Stable, Experimental }

// Functions
pub fn load(kind: Kind, path: &Path, kinds, levels, statuses) -> Result<Vec<Rule>>;
pub fn lint(kind: &Kind, path: &Path) -> Result<Vec<Filter>>;
```

### 2.2. Chainsaw Rule: `src/rule/chainsaw.rs`

**Source:** `upstream/chainsaw/src/rule/chainsaw.rs:1-198`

**Структура Chainsaw Rule:**

```rust
pub struct Rule {
 #[serde(alias = "title")]
 pub name: String,
 pub group: String,
 pub description: String,
 pub authors: Vec<String>,

 pub kind: FileKind, // evtx, mft, json, xml,...
 pub level: Level, // critical, high, medium, low, info
 pub status: Status, // stable, experimental
 pub timestamp: String, // поле с временной меткой (e.g., "Event.System.TimeCreated")

 pub fields: Vec<Field>, // поля для вывода
 pub filter: Filter, // Detection или Expression
 pub aggregate: Option<Aggregate>,
}
```

**Field структура:**

```rust
pub struct Field {
 pub name: String, // имя для вывода
 pub from: String, // исходное поле в документе
 pub to: String, // целевое поле (с возможным cast)

 pub cast: Option<ModSym>, // int/str/flt
 pub container: Option<Container>,
 pub visible: bool, // default: true
}
```

**Container структура:**

```rust
pub struct Container {
 pub field: String,
 pub format: Format,
}

pub enum Format {
 Json,
 Kv {
 delimiter: String,
 separator: String,
 trim: bool,
 },
}
```

### 2.3. Load Function: `chainsaw::load`

**Source:** `upstream/chainsaw/src/rule/chainsaw.rs:174-197`

**Алгоритм:**
1. Открыть YAML файл
2. Десериализовать в `Rule` struct через serde_yaml
3. Применить оптимизации к filter:
 - Если `Filter::Detection`:
 - `coalesce(expression, identifiers)` — подстановка идентификаторов
 - `identifiers.clear` — очистка после coalesce
 - `shake(expression)` — dead code elimination
 - `rewrite(expression)` — нормализация
 - `matrix(expression)` — multi-field optimization
 - Если `Filter::Expression`:
 - `shake(expression)`
 - `rewrite(expression)`
 - `matrix(expression)`
4. Вернуть оптимизированный Rule

### 2.4. Rule Load Function: `rule::load`

**Source:** `upstream/chainsaw/src/rule/mod.rs:206-268`

**Алгоритм:**
1. Проверить расширение файла (должно быть `.yml` или `.yaml`)
2. В зависимости от `kind`:
 - `Kind::Chainsaw`:
 - Проверить kinds filter (если задан и не содержит Chainsaw — вернуть пустой vec)
 - Вызвать `chainsaw::load(path)`
 - Обернуть в `Rule::Chainsaw`
 - `Kind::Sigma`: (отдельный слайс SLICE-010)
3. Применить фильтр по levels (если задан)
4. Применить фильтр по statuses (если задан)
5. Вернуть отфильтрованный vec

### 2.5. Lint Function: `rule::lint`

**Source:** `upstream/chainsaw/src/rule/mod.rs:270-307`

**Алгоритм:**
1. Проверить расширение файла (`.yml` / `.yaml`)
2. Для `Kind::Chainsaw`:
 - Вызвать `chainsaw::load(path)`
 - Вернуть `vec![rule.filter]`
 - При ошибке — вернуть ошибку с сообщением
3. Вернуть vec<Filter> для вывода tau expression

## 3) Data Model (FACTS)

### FACT-001: YAML Schema
Chainsaw правило — YAML документ с обязательными полями:
- `title` или `name` (alias): string
- `group`: string
- `description`: string
- `authors`: array of strings
- `kind`: enum (evtx, mft, json, xml, hve, esedb, jsonl)
- `level`: enum (critical, high, medium, low, info)
- `status`: enum (stable, experimental)
- `timestamp`: string (dot-notation path)
- `fields`: array of Field objects
- `filter`: Detection или Expression

**Source:** `chainsaw.rs:153-172`

### FACT-002: Field Deserialization
Field десериализуется с custom deserializer:
- Если указан только `name` — `from = to = name`
- Если указан только `to` — `from = to`, `name = to`
- Если указан `from` и `to` — используются как есть
- `to` может содержать cast: `int(field)` → `Cast(field, Int)`, `str(field)` → `Cast(field, Str)`
- `cast` и `container` взаимоисключающие (ошибка если оба заданы)
- `visible` по умолчанию `true`

**Source:** `chainsaw.rs:48-151`

### FACT-003: Filter Types
Filter — untagged enum:
- `Detection`: объект с `condition` и detection blocks (через tau_engine)
- `Expression`: tau expression string (через `deserialize_expression`)

**Source:** `mod.rs:97-103`

### FACT-004: Kind Enum
Kind определяет тип правила:
- `Chainsaw` — формат Chainsaw
- `Sigma` — формат Sigma (отдельный слайс)

Default: `Chainsaw`
Display: lowercase (`"chainsaw"`, `"sigma"`)
FromStr: case-sensitive parse

**Source:** `mod.rs:105-138`

### FACT-005: Level Enum
Level — уровень критичности правила:
- `Critical`, `High`, `Medium`, `Low`, `Info`

Display: lowercase
FromStr: case-sensitive parse

**Source:** `mod.rs:140-176`

### FACT-006: Status Enum
Status — статус зрелости правила:
- `Stable`, `Experimental`

Display: lowercase
FromStr: case-sensitive parse

**Source:** `mod.rs:178-205`

### FACT-007: File Extension Validation
load/lint функции проверяют расширение файла:
- Допустимые: `.yml`, `.yaml`
- Иначе: `anyhow::bail!("rule must have a yaml file extension")`

**Source:** `mod.rs:213-218`, `mod.rs:271-276`

### FACT-008: Kind Filter
При загрузке правил можно фильтровать по kind:
- `kinds: Option<HashSet<Kind>>`
- Если задан и не содержит нужный kind — возвращается пустой vec

**Source:** `mod.rs:220-225`

### FACT-009: Level Filter
При загрузке правил можно фильтровать по level:
- `levels: Option<HashSet<Level>>`
- После загрузки применяется `rules.retain(|r| levels.contains(r.level))`

**Source:** `mod.rs:261-263`

### FACT-010: Status Filter
При загрузке правил можно фильтровать по status:
- `statuses: Option<HashSet<Status>>`
- После загрузки применяется `rules.retain(|r| statuses.contains(r.status))`

**Source:** `mod.rs:264-266`

### FACT-011: Optimisation Pipeline
После десериализации filter оптимизируется:
1. `coalesce` — подстановка identifiers в expression
2. `identifiers.clear` — очистка после coalesce
3. `shake` — удаление dead code, упрощение констант
4. `rewrite` — нормализация порядка
5. `matrix` — объединение Search в Matrix

**Source:** `chainsaw.rs:180-194`

### FACT-012: Rule.solve
Метод solve применяет правило к документу:
- Для `Rule::Chainsaw`:
 - Если `Filter::Detection` → `tau_engine::solve(detection, document)`
 - Если `Filter::Expression` → `tau_engine::core::solve(expression, document)`

**Source:** `mod.rs:71-79`

### FACT-013: Rule.types
Метод types возвращает FileKind для правила:
- Для `Rule::Chainsaw` → `rule.kind` (поле kind из YAML)
- Для `Rule::Sigma` → `FileKind::Unknown`

**Source:** `mod.rs:54-60`

### FACT-014: Aggregate
Опциональное поле aggregate для группировки:
- `count`: Pattern (числовое сравнение)
- `fields`: Vec<String> (поля для группировки)

`count` десериализуется через `deserialize_numeric` (из ext/tau.rs)

**Source:** `mod.rs:90-95`

### FACT-015: Container Format
Container определяет как извлечь вложенные данные:
- `Json`: JSON парсинг поля
- `Kv`: Key-Value парсинг с delimiter/separator/trim

**Source:** `chainsaw.rs:20-28`

### FACT-016: Lint Error Format
lint возвращает ошибки с контекстом:
- Для Chainsaw: `anyhow::bail!("{}", e)`
- Для Sigma: `anyhow::bail!("{} - {}", e, source)` если есть source

**Source:** `mod.rs:281-285`, `mod.rs:297-302`

## 4) C++ Port Specification

### 4.1. Module Boundary: MOD-0008

**Namespace:** `chainsaw::rule`

**Public Interface:**

```cpp
namespace chainsaw::rule {

// === Forward declarations ===
struct ChainsawRule;
struct SigmaRule; // SLICE-010

// === Rule variant ===
using Rule = std::variant<ChainsawRule, SigmaRule>;

// === Shared enums ===
enum class Kind { Chainsaw, Sigma };
enum class Level { Critical, High, Medium, Low, Info };
enum class Status { Stable, Experimental };

// === Aggregate ===
struct Aggregate {
 tau::Pattern count;
 std::vector<std::string> fields;
};

// === Filter ===
using Filter = std::variant<tau::Detection, tau::Expression>;

// === Container ===
enum class ContainerFormat { Json, Kv };

struct KvFormat {
 std::string delimiter;
 std::string separator;
 bool trim = false;
};

struct Container {
 std::string field;
 ContainerFormat format;
 std::optional<KvFormat> kv_params; // only if format == Kv
};

// === Field ===
struct Field {
 std::string name;
 std::string from;
 std::string to;

 std::optional<tau::ModSym> cast;
 std::optional<Container> container;
 bool visible = true;
};

// === Chainsaw Rule ===
struct ChainsawRule {
 std::string name;
 std::string group;
 std::string description;
 std::vector<std::string> authors;

 DocumentKind kind;
 Level level;
 Status status;
 std::string timestamp;

 std::vector<Field> fields;
 Filter filter;
 std::optional<Aggregate> aggregate;
};

// === Rule interface functions ===
const std::optional<Aggregate>& rule_aggregate(const Rule& r);
bool rule_is_kind(const Rule& r, Kind kind);
Level rule_level(const Rule& r);
DocumentKind rule_types(const Rule& r);
const std::string& rule_name(const Rule& r);
bool rule_solve(const Rule& r, const tau::Document& doc);
Status rule_status(const Rule& r);

// === Load/Lint functions ===
struct LoadOptions {
 std::optional<std::unordered_set<Kind>> kinds;
 std::optional<std::unordered_set<Level>> levels;
 std::optional<std::unordered_set<Status>> statuses;
};

std::expected<std::vector<Rule>, Error> load(
 Kind kind,
 const std::filesystem::path& path,
 const LoadOptions& options = {});

std::expected<std::vector<Filter>, Error> lint(
 Kind kind,
 const std::filesystem::path& path);

// === Parse helpers ===
Kind parse_kind(std::string_view s);
Level parse_level(std::string_view s);
Status parse_status(std::string_view s);

std::string to_string(Kind k);
std::string to_string(Level l);
std::string to_string(Status s);

} // namespace chainsaw::rule
```

### 4.2. Internal Dependencies

- `MOD-0009` (SLICE-008): `tau::Detection`, `tau::Expression`, `tau::Pattern`, `tau::Document`, `tau::ModSym`, optimiser functions
- `MOD-0005` (SLICE-005): `DocumentKind`
- yaml-cpp: YAML парсинг

### 4.3. Implementation Constraints

1. **YAML Library:** yaml-cpp для парсинга (ADR-0003)
2. **Field Deserialization:** Custom logic для field с cast detection
3. **Optimisation:** Использовать tau::coalesce, shake, rewrite, matrix из SLICE-008
4. **Error Messages:** Формат ошибок должен совпадать с Rust

### 4.4. Behavioral 1:1 Requirements

| Behavior | Requirement | Source |
|----------|-------------|--------|
| File extension check | `.yml` или `.yaml`, иначе ошибка | FACT-007 |
| Field alias | `title` → `name` | FACT-001 |
| Cast parsing | `int(field)` → Cast(Int), `str(field)` → Cast(Str) | FACT-002 |
| Default values | `visible=true`, `trim=false` | FACT-002, FACT-015 |
| Optimisation order | coalesce → clear → shake → rewrite → matrix | FACT-011 |
| Filter dispatch | Detection → tau::solve, Expression → tau::core::solve | FACT-012 |

## 5) Test Matrix

### 5.1. Upstream TST-* Mapping (TESTMAT-0001)

| TST-ID | Test | Coverage |
|--------|------|----------|
| TST-0004 | hunt test | Косвенно: load chainsaw rules, solve |

**Примечание:** Upstream тесты для chainsaw rules минимальны. Основное покрытие через hunt integration test.

### 5.2. New Slice Unit Tests (TST-CSRULE-*)

| Test ID | Description | Expected | Priority |
|---------|-------------|----------|----------|
| TST-CSRULE-001 | Load valid rule | Rule loaded successfully | HIGH |
| TST-CSRULE-002 | Load rule with title alias | name = title | HIGH |
| TST-CSRULE-003 | Load rule with Detection filter | Filter::Detection parsed | HIGH |
| TST-CSRULE-004 | Load rule with Expression filter | Filter::Expression parsed | HIGH |
| TST-CSRULE-005 | Field with name only | from = to = name | HIGH |
| TST-CSRULE-006 | Field with cast int | cast = Int | HIGH |
| TST-CSRULE-007 | Field with cast str | cast = Str | HIGH |
| TST-CSRULE-008 | Field cast + container error | Error returned | MEDIUM |
| TST-CSRULE-009 | Invalid extension | Error "yaml file extension" | HIGH |
| TST-CSRULE-010 | Level filter | Only matching levels returned | MEDIUM |
| TST-CSRULE-011 | Status filter | Only matching statuses returned | MEDIUM |
| TST-CSRULE-012 | Kind filter | Empty vec if kind not in filter | MEDIUM |
| TST-CSRULE-013 | Rule.solve with matching doc | Returns true | HIGH |
| TST-CSRULE-014 | Rule.solve with non-matching doc | Returns false | HIGH |
| TST-CSRULE-015 | Lint valid rule | Filter returned | HIGH |
| TST-CSRULE-016 | Lint invalid rule | Error with message | MEDIUM |
| TST-CSRULE-017 | Parse Kind from string | Chainsaw/Sigma parsed | LOW |
| TST-CSRULE-018 | Parse Level from string | All levels parsed | LOW |
| TST-CSRULE-019 | Parse Status from string | Stable/Experimental parsed | LOW |
| TST-CSRULE-020 | Container Json format | Container.format = Json | MEDIUM |
| TST-CSRULE-021 | Container Kv format | delimiter/separator/trim parsed | MEDIUM |
| TST-CSRULE-022 | Aggregate parsing | count Pattern + fields | MEDIUM |
| TST-CSRULE-023 | Optimisation applied | Expression optimised after load | MEDIUM |
| TST-CSRULE-024 | Rule.types for Chainsaw | Returns rule.kind | LOW |

### 5.3. Golden Tests via RUN-*

| RUN-ID | Description | Coverage |
|--------|-------------|----------|
| | hunt with chainsaw rule | Rule loading, solve |
| | hunt --jsonl with rule | Rule loading, output |
| | hunt --cache-to-disk | Rule loading, caching |
| | lint --kind chainsaw | Lint function |

## 6) S1–S4 Assessment

| Dimension | Rating | Notes |
|-----------|--------|-------|
| S1 (Complexity) | Low | Single struct + load/lint functions |
| S2 (Risk) | Low | Well-defined YAML schema |
| S3 (Dependencies) | Low | SLICE-008 Done |
| S4 (Effort) | Low | Straightforward YAML parsing |

**Итого:** Все оси Low — слайс готов к реализации без дробления.

## 7) Unknowns & Risks

| Risk | Description | Mitigation |
|------|-------------|------------|
| RISK-CSRULE-001 | yaml-cpp API для custom deserialization | Реализовать Field parsing вручную |
| RISK-CSRULE-002 | Совместимость yaml-cpp с upstream YAML | Протестировать на реальных правилах |

## 8) References

- **chainsaw mod.rs:** `upstream/chainsaw/src/rule/mod.rs`
- **chainsaw.rs:** `upstream/chainsaw/src/rule/chainsaw.rs`
- **ext/tau.rs:** `upstream/chainsaw/src/ext/tau.rs`
- **TOBE-0001:** MOD-0008 specification
- **SLICE-008:** Tau Engine (dependency)
- **SLICE-005:** Reader/DocumentKind (dependency)
- **Example rules:** `upstream/chainsaw/rules/evtx/**/*.yml`

---

## Appendix A: Example Chainsaw Rule

```yaml
---
title: Security Audit Logs Cleared
group: Log Tampering
description: The security audit logs were cleared.
authors:
 - FranticTyping

kind: evtx
level: critical
status: stable
timestamp: Event.System.TimeCreated

fields:
 - name: Event ID
 to: Event.System.EventID
 - name: Record ID
 to: Event.System.EventRecordID
 - name: Computer
 to: Event.System.Computer
 - name: User
 to: Event.UserData.LogFileCleared.SubjectUserName

filter:
 condition: security_log_cleared and not empty

 security_log_cleared:
 Event.System.EventID: 1102
 empty:
 Event.UserData.LogFileCleared.SubjectUserName:
```

## Appendix B: Field Examples

```yaml
# Only name specified
fields:
 - name: EventID
# Result: from = to = name = "EventID"

# With to (and optional cast)
fields:
 - name: Event ID
 to: Event.System.EventID
# Result: from = to = "Event.System.EventID", name = "Event ID"

# With cast
fields:
 - name: Count
 to: int(Event.EventData.Count)
# Result: to = "Event.EventData.Count", cast = Int

# With container
fields:
 - name: Parsed
 to: Event.EventData.Data
 container:
 field: Data
 format: json
```
