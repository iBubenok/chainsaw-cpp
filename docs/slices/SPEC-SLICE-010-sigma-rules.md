# SPEC-SLICE-010 — Sigma Rules Loader + Conversion

## Meta

| Field | Value |
|-------|-------|
| Slice ID | SLICE-010 |
| MOD-* | MOD-0008 (Rules/Sigma) |
| Status | UnitReady |
| Priority | 10 |
| Dependencies | SLICE-008 (Tau Engine - Done), SLICE-009 (Chainsaw Rules - Verified) |
| Created | 2026-01-11 |

## 1) Overview

Sigma Rules Loader — модуль загрузки, парсинга и конвертации правил в формате Sigma YAML в формат Tau для выполнения. Это критически важный компонент, т.к. Sigma является открытым стандартом для описания детекций угроз.

**Ключевая функциональность:**
1. **Загрузка Sigma YAML** — парсинг одиночных правил и Rule Collections (multi-doc)
2. **Конвертация Sigma → Tau** — преобразование detection, modifiers, conditions
3. **Обработка модификаторов** — contains, endswith, startswith, re, base64, base64offset, all
4. **Обработка условий** — "all of them", "1 of selection*", логические операции
5. **Aggregation** — count, group by fields
6. **Document trait** — доступ к метаданным правила (logsource, level, etc.)

**Upstream тесты:** **16 unit-тестов** (TST-0007..TST-0022) — это основная масса тестов Chainsaw!

**Использование в chainsaw:**
- `hunt.rs`: загрузка Sigma правил через `rule::load(Kind::Sigma,...)`, применение через `rule.solve`
- `lint.rs`: валидация Sigma правил через `rule::lint(Kind::Sigma,...)`

## 2) Primary Sources (Rust)

### 2.1. Sigma Rule Structure: `src/rule/sigma.rs:16-73`

**Source:** `upstream/chainsaw/src/rule/sigma.rs:16-73`

```rust
#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "lowercase")]
pub struct Rule {
 #[serde(alias = "title")]
 pub name: String,
 #[serde(flatten)]
 pub tau: Tau, // tau_engine::Rule

 #[serde(default)]
 pub aggregate: Option<super::Aggregate>,

 pub authors: Vec<String>,
 pub description: String,
 pub level: Level,
 pub status: Status,

 #[serde(default)]
 pub falsepositives: Option<Vec<String>>,
 #[serde(default)]
 pub id: Option<String>,
 #[serde(default)]
 pub logsource: Option<LogSource>,
 #[serde(default)]
 pub references: Option<Vec<String>>,
 #[serde(default)]
 pub tags: Option<Vec<String>>,
}
```

### 2.2. LogSource Structure: `src/rule/sigma.rs:111-121`

**Source:** `upstream/chainsaw/src/rule/sigma.rs:111-121`

```rust
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct LogSource {
 #[serde(default, skip_serializing_if = "Option::is_none")]
 pub category: Option<String>,
 #[serde(default, skip_serializing_if = "Option::is_none")]
 pub definition: Option<String>,
 #[serde(default, skip_serializing_if = "Option::is_none")]
 pub product: Option<String>,
 #[serde(default, skip_serializing_if = "Option::is_none")]
 pub service: Option<String>,
}
```

### 2.3. Document Trait Implementation: `src/rule/sigma.rs:44-73`

**Source:** `upstream/chainsaw/src/rule/sigma.rs:44-73`

Позволяет применять Tau-правила к самому Sigma-правилу (для фильтрации по logsource, level, etc.):

```rust
impl Document for Rule {
 fn find(&self, key: &str) -> Option<tau_engine::Value<'_>> {
 match key {
 "title" => Some(Tau::String(Cow::Borrowed(&self.name))),
 "level" => Some(Tau::String(Cow::Owned(self.level.to_string))),
 "status" => Some(Tau::String(Cow::Owned(self.status.to_string))),
 "id" => self.id.as_ref.map(|id| Tau::String(Cow::Borrowed(id))),
 "logsource.category" =>...,
 "logsource.definition" =>...,
 "logsource.product" =>...,
 "logsource.service" =>...,
 _ => None,
 }
 }
}
```

### 2.4. Supported Modifiers: `src/rule/sigma.rs:265-277`

**Source:** `upstream/chainsaw/src/rule/sigma.rs:265-277`

```rust
lazy_static::lazy_static! {
 static ref SUPPORTED_MODIFIERS: HashSet<String> = {
 let mut set = HashSet::new;
 set.insert("all".to_owned);
 set.insert("base64".to_owned);
 set.insert("base64offset".to_owned);
 set.insert("contains".to_owned);
 set.insert("endswith".to_owned);
 set.insert("startswith".to_owned);
 set.insert("re".to_owned);
 set
 };
}
```

### 2.5. Match Trait — Pattern Transformation: `src/rule/sigma.rs:199-262`

**Source:** `upstream/chainsaw/src/rule/sigma.rs:199-262`

```rust
trait Match {
 fn as_contains(&self) -> String; // "foobar" → "i*foobar*"
 fn as_endswith(&self) -> String; // "foobar" → "i*foobar"
 fn as_match(&self) -> Option<String>; // wildcard handling
 fn as_regex(&self, convert: bool) -> Option<String>; // "?pattern"
 fn as_startswith(&self) -> String; // "foobar" → "ifoobar*"
}
```

### 2.6. Condition Trait — Unsupported Detection: `src/rule/sigma.rs:182-197`

**Source:** `upstream/chainsaw/src/rule/sigma.rs:182-197`

```rust
trait Condition {
 fn unsupported(&self) -> bool;
}

impl Condition for String {
 fn unsupported(&self) -> bool {
 self.contains(" | ") // pipe (aggregation pipeline)
 | self.contains('*') // nested wildcards
 | self.contains(" avg ")
 | self.contains(" of ") // except special cases
 | self.contains(" max ")
 | self.contains(" min ")
 | self.contains(" near ")
 | self.contains(" sum ")
 }
}
```

### 2.7. Parse Identifier — Modifier Processing: `src/rule/sigma.rs:279-363`

**Source:** `upstream/chainsaw/src/rule/sigma.rs:279-363`

Рекурсивная обработка YAML-значений с применением модификаторов:

```rust
fn parse_identifier(value: &Yaml, modifiers: &HashSet<String>) -> Result<Yaml> {
 // 1. Check for unsupported modifiers
 // 2. For Mapping/Sequence — recurse
 // 3. For String:
 // - base64 → encode and recurse
 // - base64offset → generate 3 variants with offsets
 // - contains → as_contains
 // - endswith → as_endswith
 // - re → as_regex
 // - startswith → as_startswith
 // - default → as_match or as_regex(convert=true)
}
```

### 2.8. Prepare Condition — Aggregation Handling: `src/rule/sigma.rs:365-418`

**Source:** `upstream/chainsaw/src/rule/sigma.rs:365-418`

```rust
fn prepare_condition(condition: &str) -> Result<(String, Option<Aggregate>)> {
 // Parse: "search_expression | count(field) by group_field >= N"
 // Returns: (condition_without_agg, Some(Aggregate{count, fields}))
}
```

### 2.9. Prepare — Rule Collection Merging: `src/rule/sigma.rs:420-505`

**Source:** `upstream/chainsaw/src/rule/sigma.rs:420-505`

Объединение базового detection с extension detection:

```rust
fn prepare(
 detection: Detection,
 extra: Option<Detection>,
) -> Result<(Detection, Option<Aggregate>)> {
 // Merge conditions and identifiers from extra into base
}
```

### 2.10. Detections to Tau — Main Conversion: `src/rule/sigma.rs:507-771`

**Source:** `upstream/chainsaw/src/rule/sigma.rs:507-771`

Главная функция конвертации Sigma detection → Tau YAML:

```rust
fn detections_to_tau(detection: Detection) -> Result<Mapping> {
 // 1. Parse condition string
 // 2. Handle identifiers (Sequence → split into C_0, C_1,...; Mapping → AND fields)
 // 3. Apply modifiers via parse_identifier
 // 4. Handle special conditions:
 // - "all of them" → "A and B and C"
 // - "1 of them" → "A or B or C"
 // - "all of selection*" → "(selection0 and selection1)"
 // - "1 of selection*" → "(selection0 or selection1)"
 // 5. Check for unsupported conditions
 // 6. Return: {detection: {..., condition:...}, true_positives:, true_negatives: }
}
```

### 2.11. Load Function — File Parsing: `src/rule/sigma.rs:773-868`

**Source:** `upstream/chainsaw/src/rule/sigma.rs:773-868`

```rust
pub fn load(rule: &Path) -> Result<Vec<Yaml>> {
 // 1. Read file content
 // 2. Split by "---\n" (YAML multi-doc)
 // 3. Parse each document as Sigma
 // 4. Handle Rule Collections:
 // - If main.header.action == "global" → iterate over subsequent docs
 // - For each extension: prepare(main.detection, extension)
 // 5. For single rules: prepare(detection, None)
 // 6. Convert each to Tau and return Vec<Yaml>
}
```

### 2.12. Rule Load (mod.rs) — Integration: `src/rule/mod.rs:229-259`

**Source:** `upstream/chainsaw/src/rule/mod.rs:229-259`

```rust
Kind::Sigma => {
 // 1. Check kinds filter
 // 2. Load via sigma::load(path)
 // 3. Deserialize each Yaml to Rule
 // 4. Apply optimisation passes:
 // - coalesce(expression, identifiers)
 // - identifiers.clear
 // - shake(expression)
 // - rewrite(expression)
 // - matrix(expression)
 // 5. Filter by levels/statuses
}
```

## 3) Data Model (FACTS)

### FACT-001: Sigma Rule YAML Schema
Sigma правило — YAML документ со следующими полями:
- `title` (alias: `name`): обязательное
- `description`: обязательное
- `author`: строка, разделяется по `,` → `authors: Vec<String>`
- `status`: "stable" или иное → experimental
- `level`: "critical", "high", "medium", "low" или иное → info
- `id`: опционально
- `logsource`: опционально, содержит category/definition/product/service
- `detection`: обязательное
- `references`: опционально
- `tags`: опционально
- `falsepositives`: опционально

**Source:** `sigma.rs:16-42`, `sigma.rs:89-109` (Header)

### FACT-002: Detection Block Structure
Detection содержит:
- `condition`: строка или одноэлементный массив строк
- `identifiers`: Mapping с detection blocks (filter: YAML → identifiers)

**Source:** `sigma.rs:81-87`

### FACT-003: Identifier Block Types
Identifier может быть:
- **Mapping**: `{field: value, field2: value2}` → AND всех условий
- **Sequence**: `[{field: val1}, {field: val2}]` → OR блоков, каждый блок — AND

**Source:** `sigma.rs:536-651`

### FACT-004: Modifier Syntax
Модификаторы указываются через `|` в имени поля:
- `CommandLine|contains: "-Nop"` → `CommandLine: "i*-Nop*"`
- `Image|endswith: "\\powershell.exe"` → `Image: "i*\\powershell.exe"`
- Поддерживается цепочка: `field|base64|contains`

**Source:** `sigma.rs:560-563`, `sigma.rs:279-363`

### FACT-005: Match Pattern Format (Tau)
- `i` prefix: case-insensitive
- `*` prefix/suffix: wildcard
- `?` prefix: regex

Примеры:
- `as_contains("foo")` → `"i*foo*"`
- `as_endswith("foo")` → `"i*foo"`
- `as_startswith("foo")` → `"ifoo*"`
- `as_match("foo")` → `"ifoo"`
- `as_regex("foo")` → `"?foo"`

**Source:** `sigma.rs:207-262`

### FACT-006: Nested Wildcards
`as_match` возвращает `None` для строк с внутренними wildcards:
- `"foo*bar"` → `None` (требует regex)
- `"foo?bar"` → `None` (требует regex)

В этом случае применяется `as_regex(convert=true)`.

**Source:** `sigma.rs:214-231`, `sigma.rs:347-355`

### FACT-007: base64 Modifier
`base64` modifier кодирует строку в base64 и рекурсивно применяет оставшиеся модификаторы.

**Source:** `sigma.rs:309-313`

### FACT-008: base64offset Modifier
`base64offset` генерирует 3 варианта для покрытия смещений:
- offset 0: encode(value)
- offset 1: encode(" " + value), trim
- offset 2: encode(" " + value), trim

**Source:** `sigma.rs:314-331`

### FACT-009: all Modifier
`all` modifier оборачивает поле в `all(field)`:
- `field|all: [val1, val2]` → все значения должны присутствовать

**Source:** `sigma.rs:561-563`, `sigma.rs:621-624`

### FACT-010: Condition "all of them"
`condition: all of them` преобразуется в AND всех identifiers:
- `all of them` → `"A and B and C"`

**Source:** `sigma.rs:669-678`

### FACT-011: Condition "1 of them"
`condition: 1 of them` преобразуется в OR всех identifiers:
- `1 of them` → `"A or B or C"`

**Source:** `sigma.rs:679-688`

### FACT-012: Condition "all of prefix*"
`condition: all of selection*` преобразуется в AND identifiers с префиксом:
- `all of selection*` → `"(selection0 and selection1)"`

**Source:** `sigma.rs:714-735`

### FACT-013: Condition "1 of prefix*"
`condition: 1 of selection*` преобразуется в OR identifiers с префиксом:
- `1 of selection*` → `"(selection0 or selection1)"`

**Source:** `sigma.rs:714-735`

### FACT-014: Unsupported Conditions
Условия с следующими элементами не поддерживаются:
- Pipe: `" | "` (aggregation pipeline)
- Nested wildcards: `"*"` внутри condition
- Aggregation keywords: `avg`, `max`, `min`, `sum`, `near`
- Generic "of": `" of "` (кроме специальных случаев)

**Source:** `sigma.rs:182-197`, `sigma.rs:760-762`

### FACT-015: Aggregation in Condition
Aggregation в условии парсится из pipe:
- `"search | count(field) by group >= 5"` → `Aggregate{count: ">=5", fields: [field, group]}`

**Source:** `sigma.rs:365-418`

### FACT-016: Rule Collections
Multi-doc YAML с `action: global` обрабатывается как коллекция:
- Первый документ: базовые metadata + detection
- Последующие документы: extension detection + condition
- Результат: несколько правил с merged detection

**Source:** `sigma.rs:803-841`

### FACT-017: Author Parsing
`author` поле разделяется по `,` для получения массива:
- `"Alice, Bob"` → `["Alice", "Bob"]`
- Если отсутствует → `["unknown"]`

**Source:** `sigma.rs:166-177`

### FACT-018: Level Normalization
Уровни нормализуются:
- `"critical"`, `"high"`, `"medium"`, `"low"` → как есть
- Любое другое → `"info"`

**Source:** `sigma.rs:819-827`, `sigma.rs:848-856`

### FACT-019: Status Normalization
Статусы нормализуются:
- `"stable"` → `"stable"`
- Любое другое → `"experimental"`

**Source:** `sigma.rs:139-147`

### FACT-020: Tau Output Structure
Результат конвертации содержит:
- `detection`: условия с идентификаторами
- `condition`: переписанное условие
- `true_positives`: пустой массив
- `true_negatives`: пустой массив

**Source:** `sigma.rs:764-770`

## 4) C++ Port Specification

### 4.1. Module Boundary: MOD-0008 (Sigma extension)

**Namespace:** `chainsaw::rule::sigma`

**Public Interface (дополнение к rule.hpp):**

```cpp
namespace chainsaw::rule::sigma {

// === LogSource ===
struct LogSource {
 std::optional<std::string> category;
 std::optional<std::string> definition;
 std::optional<std::string> product;
 std::optional<std::string> service;
};

// === Aggregate (Sigma-specific) ===
struct SigmaAggregate {
 std::string count; // ">=5", ">10", etc.
 std::vector<std::string> fields;
};

// === SigmaRule (обновление) ===
//... см. chainsaw::rule::SigmaRule

// === Load function ===
// Вызывается из rule::load при Kind::Sigma
std::expected<std::vector<YAML::Node>, Error> load_sigma(
 const std::filesystem::path& path);

// === Internal functions ===

// Проверить поддержку модификаторов
bool is_modifier_supported(const std::string& modifier);

// Применить модификаторы к значению
std::expected<YAML::Node, Error> parse_identifier(
 const YAML::Node& value,
 const std::unordered_set<std::string>& modifiers);

// Подготовить condition (извлечь aggregation)
std::expected<std::pair<std::string, std::optional<SigmaAggregate>>, Error>
prepare_condition(const std::string& condition);

// Объединить detection с extension
std::expected<std::pair<Detection, std::optional<SigmaAggregate>>, Error>
prepare(Detection detection, std::optional<Detection> extra);

// Конвертировать detection в tau format
std::expected<YAML::Node, Error> detections_to_tau(const Detection& detection);

// Проверить на неподдерживаемые условия
bool is_condition_unsupported(const std::string& condition);

} // namespace chainsaw::rule::sigma
```

### 4.2. SigmaRule Structure Update

```cpp
namespace chainsaw::rule {

struct SigmaRule {
 // Metadata
 std::string name;
 std::string description;
 std::vector<std::string> authors;
 Level level = Level::Info;
 Status status = Status::Experimental;

 // Optional fields
 std::optional<std::string> id;
 std::optional<sigma::LogSource> logsource;
 std::optional<std::vector<std::string>> references;
 std::optional<std::vector<std::string>> tags;
 std::optional<std::vector<std::string>> falsepositives;

 // Detection
 tau::Detection detection; // после конвертации и оптимизации
 std::optional<Aggregate> aggregate;

 // Document interface (для фильтрации по logsource)
 std::optional<tau::Value> find(const std::string& key) const;
};

} // namespace chainsaw::rule
```

### 4.3. Match Functions

```cpp
namespace chainsaw::rule::sigma {

// Pattern transformation functions
std::string as_contains(const std::string& value); // → "i*value*"
std::string as_endswith(const std::string& value); // → "i*value"
std::string as_startswith(const std::string& value); // → "ivalue*"
std::optional<std::string> as_match(const std::string& value); // → "ivalue" or nullopt
std::optional<std::string> as_regex(const std::string& value, bool convert);

// Base64 encoding
std::string base64_encode(const std::string& value);
std::vector<std::string> base64_offset_encode(const std::string& value);

} // namespace chainsaw::rule::sigma
```

### 4.4. Internal Dependencies

- `MOD-0009` (SLICE-008): `tau::Detection`, `tau::Expression`, optimiser functions
- `MOD-0008` (SLICE-009): `rule::Kind`, `rule::Level`, `rule::Status`, `rule::Aggregate`
- yaml-cpp: YAML парсинг
- base64 library: для `base64` и `base64offset` модификаторов (можно использовать inline реализацию)

### 4.5. Implementation Constraints

1. **YAML Multi-doc:** yaml-cpp `LoadAllFromFile` для Rule Collections
2. **Regex Validation:** проверка regex через `std::regex`
3. **Base64:** standalone implementation (нет зависимости от OpenSSL)
4. **Error Messages:** формат ошибок должен совпадать с Rust
5. **Optimisation:** после конвертации применять те же tau passes что и для Chainsaw rules

### 4.6. Behavioral 1:1 Requirements

| Behavior | Requirement | Source |
|----------|-------------|--------|
| Modifier support | Только 7 модификаторов: all, base64, base64offset, contains, endswith, startswith, re | FACT-004 |
| Contains pattern | `as_contains("foo")` → `"i*foo*"` | FACT-005 |
| EndsWith pattern | `as_endswith("foo")` → `"i*foo"` | FACT-005 |
| StartsWith pattern | `as_startswith("foo")` → `"ifoo*"` | FACT-005 |
| Regex pattern | `as_regex("foo")` → `"?foo"` | FACT-005 |
| "all of them" | → `"A and B"` | FACT-010 |
| "1 of them" | → `"A or B"` | FACT-011 |
| "all of prefix*" | → `"(prefix0 and prefix1)"` | FACT-012 |
| "1 of prefix*" | → `"(prefix0 or prefix1)"` | FACT-013 |
| Unsupported cond | Reject conditions with `|`, nested `*`, avg/max/min/sum/near | FACT-014 |
| Author parsing | Split by `,`, trim | FACT-017 |
| Level normalization | Unknown → info | FACT-018 |
| Status normalization | Not "stable" → experimental | FACT-019 |
| Rule Collections | action: global → merge base with extensions | FACT-016 |

## 5) Test Matrix

### 5.1. Upstream TST-* Mapping (TESTMAT-0001)

**Integration tests (files):**

| TST-ID | Test | Description | Fixtures |
|--------|------|-------------|----------|
| TST-0007 | `convert_sigma!("simple")` | Single Sigma rule conversion | `sigma_simple.yml` → `sigma_simple_output.yml` |
| TST-0008 | `convert_sigma!("collection")` | Rule Collection conversion | `sigma_collection.yml` → `sigma_collection_output.yml` |

**Unit tests (inline):**

| TST-ID | Test | Description | Expected |
|--------|------|-------------|----------|
| TST-0009 | `test_unsupported_conditions` | Detect unsupported conditions | `" | "`, `*`, `" of "` → unsupported |
| TST-0010 | `test_match_contains` | as_contains | `"foobar"` → `"i*foobar*"` |
| TST-0011 | `test_match_endswith` | as_endswith | `"foobar"` → `"i*foobar"` |
| TST-0012 | `test_match` | as_match with wildcards | Various patterns |
| TST-0013 | `test_match_regex` | as_regex | `"foobar"` → `"?foobar"` |
| TST-0014 | `test_match_startswith` | as_startswith | `"foobar"` → `"ifoobar*"` |
| TST-0015 | `test_parse_identifier` | Modifier application | `i` prefix for strings |
| TST-0016 | `test_prepare` | prepare simple | No change |
| TST-0017 | `test_prepare_group` | prepare with extension | Merge detections |
| TST-0018 | `test_detection_to_tau_0` | Complex conversion | Modifiers, sequences, `all(B)` |
| TST-0019 | `test_detection_to_tau_all_of_them` | "all of them" | → `"A and B"` |
| TST-0020 | `test_detection_to_tau_one_of_them` | "1 of them" | → `"A or B"` |
| TST-0021 | `test_detection_to_tau_all_of_selection` | "all of selection*" | → `"(selection0 and selection1)"` |
| TST-0022 | `test_detection_to_tau_one_of_selection` | "1 of selection*" | → `"(selection0 or selection1)"` |

### 5.2. New Slice Unit Tests (TST-SIGMA-*)

| Test ID | Description | Expected | Priority |
|---------|-------------|----------|----------|
| TST-SIGMA-001 | Load valid sigma rule | Rule loaded | HIGH |
| TST-SIGMA-002 | Load sigma collection | Multiple rules | HIGH |
| TST-SIGMA-003 | Author split by comma | `"A, B"` → `["A", "B"]` | HIGH |
| TST-SIGMA-004 | Unknown author | → `["unknown"]` | MEDIUM |
| TST-SIGMA-005 | Level normalization | Unknown → info | HIGH |
| TST-SIGMA-006 | Status normalization | Unknown → experimental | HIGH |
| TST-SIGMA-007 | base64 modifier | Encode and recurse | HIGH |
| TST-SIGMA-008 | base64offset modifier | 3 variants generated | HIGH |
| TST-SIGMA-009 | contains modifier | `"i*value*"` | HIGH |
| TST-SIGMA-010 | endswith modifier | `"i*value"` | HIGH |
| TST-SIGMA-011 | startswith modifier | `"ivalue*"` | HIGH |
| TST-SIGMA-012 | re modifier | `"?pattern"` | HIGH |
| TST-SIGMA-013 | all modifier | `all(field)` wrapper | HIGH |
| TST-SIGMA-014 | Unsupported modifier | Error returned | MEDIUM |
| TST-SIGMA-015 | Nested wildcards | Fallback to regex | MEDIUM |
| TST-SIGMA-016 | "all of them" | AND identifiers | HIGH |
| TST-SIGMA-017 | "1 of them" | OR identifiers | HIGH |
| TST-SIGMA-018 | "all of prefix*" | AND matching | HIGH |
| TST-SIGMA-019 | "1 of prefix*" | OR matching | HIGH |
| TST-SIGMA-020 | Unsupported condition | Error returned | HIGH |
| TST-SIGMA-021 | Aggregation parsing | count/fields extracted | MEDIUM |
| TST-SIGMA-022 | LogSource fields | category/product/service | LOW |
| TST-SIGMA-023 | Document.find | Metadata access | LOW |
| TST-SIGMA-024 | Identifier sequence | Split into C_0, C_1 | HIGH |
| TST-SIGMA-025 | Identifier mapping | AND fields | HIGH |
| TST-SIGMA-026 | Optimisation applied | Tau passes after conversion | MEDIUM |
| TST-SIGMA-027 | rule_solve | Detection matching | HIGH |
| TST-SIGMA-028 | rule_types | Returns Unknown | LOW |

### 5.3. Golden Tests via RUN-*

| RUN-ID | Description | Coverage |
|--------|-------------|----------|
| | hunt with sigma rule | Rule loading, solve |
| | hunt with sigma --jsonl | Rule loading, output |
| | hunt with multiple rules | Sigma + Chainsaw |
| | lint --kind sigma | Sigma lint |

## 6) S1–S4 Assessment

| Dimension | Rating | Notes |
|-----------|--------|-------|
| S1 (Complexity) | **Medium** | Multiple transformation passes, Rule Collections |
| S2 (Risk) | **Low** | 16 upstream tests provide excellent coverage |
| S3 (Dependencies) | **Low** | SLICE-008, SLICE-009 Done |
| S4 (Effort) | **Medium** | Complex condition parsing, modifier handling |

**Итого:** S1=Medium, S2=Low, S3=Low, S4=Medium — допустимо без дробления (не более одной High).

## 7) Unknowns & Risks

| Risk | Description | Mitigation | Status |
|------|-------------|------------|--------|
| RISK-SIGMA-001 | yaml-cpp multi-doc API | Использовать `YAML::LoadAll` | Open |
| RISK-SIGMA-002 | Base64 без OpenSSL | Inline реализация (simple) | Open |
| RISK-SIGMA-003 | Regex validation | `std::regex` для проверки | Open |

## 8) References

- **sigma.rs:** `upstream/chainsaw/src/rule/sigma.rs`
- **mod.rs:** `upstream/chainsaw/src/rule/mod.rs`
- **convert tests:** `upstream/chainsaw/tests/convert.rs`
- **TOBE-0001:** MOD-0008 specification
- **SLICE-008:** Tau Engine (dependency)
- **SLICE-009:** Chainsaw Rules (dependency)
- **Sigma spec:** `https://github.com/SigmaHQ/sigma-specification`

---

## Appendix A: Example Sigma Rule (Simple)

**Input:** `sigma_simple.yml`
```yaml
---
title: simple
id: simple
status: experimental
description: A simple rule for testing
author: Alex Kornitzer
date: 1970/01/01
references:
detection:
 search:
 CommandLine|contains:
 - ' -Nop '
 condition: search
```

**Output:** `sigma_simple_output.yml`
```yaml
---
title: simple
description: A simple rule for testing
status: experimental
id: simple
references:
authors:
 - Alex Kornitzer
level: info
detection:
 search:
 CommandLine:
 - i* -Nop *
 condition: search
true_negatives:
true_positives:
```

## Appendix B: Example Sigma Rule Collection

**Input:** `sigma_collection.yml`
```yaml
---
title: collection
id: collection
status: experimental
description: A collection rule for testing
author: Alex Kornitzer
date: 1970/01/01
references:
action: global
detection:
 base:
 Image|contains:
 - \powershell.exe
 condition: search
---
detection:
 search:
 CommandLine|contains:
 - ' -Nop '
 condition: search and base
---
detection:
 search:
 CommandLine|contains:
 - ' -encodedcommand '
 condition: search and base
```

**Output:** `sigma_collection_output.yml`
```yaml
---
title: collection
description: A collection rule for testing
status: experimental
id: collection
references:
authors:
 - Alex Kornitzer
level: info
detection:
 base:
 Image:
 - 'i*\powershell.exe*'
 search:
 CommandLine:
 - 'i* -Nop *'
 condition: search and base
true_negatives:
true_positives:
---
title: collection
description: A collection rule for testing
status: experimental
id: collection
references:
authors:
 - Alex Kornitzer
level: info
detection:
 base:
 Image:
 - 'i*\powershell.exe*'
 search:
 CommandLine:
 - 'i* -encodedcommand *'
 condition: search and base
true_negatives:
true_positives:
```

## Appendix C: Modifier Examples

| Sigma Field | Value | Tau Output |
|-------------|-------|------------|
| `field` | `"test"` | `"itest"` |
| `field\|contains` | `"test"` | `"i*test*"` |
| `field\|endswith` | `".exe"` | `"i*.exe"` |
| `field\|startswith` | `"C:\\"` | `"iC:\\*"` |
| `field\|re` | `"test.*"` | `"?test.*"` |
| `field\|base64` | `"test"` | `"idGVzdA=="` |
| `field\|contains\|all` | `["a", "b"]` | `all(field): ["i*a*", "i*b*"]` |

---

## UnitReady Checklist

- [x] Micro-spec links to primary sources (code/tests)
- [x] All FACTS numbered and sourced
- [x] C++ port specification complete
- [x] Test matrix defined (16 upstream + 28 new)
- [x] S1-S4 assessed (Medium/Low/Low/Medium)
- [x] Risks identified and mitigated
- [x] Dependencies verified (SLICE-008 Done, SLICE-009 Verified)
- [x] No S1-S4 axis is High

**UnitReady: PASS**
