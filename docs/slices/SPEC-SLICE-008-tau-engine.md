# SPEC-SLICE-008 — Tau Engine (IR + Solver)

## Meta

| Field | Value |
|-------|-------|
| Slice ID | SLICE-008 |
| MOD-* | MOD-0009 |
| Status | UnitReady |
| Priority | 8 |
| Dependencies | SLICE-005 (Value/Document - Done) |
| Created | 2026-01-10 |

## 1) Overview

Tau Engine — это движок выражений и solver для сопоставления правил (Detection Rules) с документами (JSON-like структурами). В upstream chainsaw используется внешний crate `tau-engine` v1.0 с features: `core`, `json`, `sync`.

**Ключевая функциональность:**
1. **IR (Intermediate Representation)** — типизированное AST выражений (`Expression` enum)
2. **Solver** — древовидный решатель для вычисления Expression против Document
3. **Parser** — парсинг YAML/текста в Expression (Pratt parser)
4. **Optimiser** — оптимизации AST (coalesce, shake, rewrite, matrix)

**Использование в chainsaw:**
- `hunt.rs`: проверка документов против правил через `tau_engine::solve` и `tau_engine::core::solve`
- `search.rs`: фильтрация документов по tau-выражению через `tau_engine::core::solve`
- `rule/chainsaw.rs`: загрузка правил с применением оптимизаций через `optimiser::{coalesce, shake, rewrite, matrix}`
- `rule/sigma.rs`: конвертация Sigma→Tau через `detections_to_tau`

## 2) Primary Sources (Rust)

### 2.1. External Crate: tau-engine v1.0

**Repository:** https://github.com/WithSecureLabs/tau-engine (MIT License)
**Crates.io:** https://crates.io/crates/tau-engine

**Public API используемый chainsaw:**

```rust
// tau_engine (lib.rs)
pub use self::document::Document;
pub use self::value::Value;
pub fn solve(detection: &Detection, document: &dyn Document) -> bool;

// tau_engine::core (with feature "core")
pub use crate::rule::Detection;
pub fn solve(expression: &Expression, document: &dyn Document) -> bool;

// tau_engine::core::parser
pub use self::parser::{
 parse_identifier,
 BoolSym, Expression, IdentifierParser, MatchType, ModSym, Pattern, Search
};

// tau_engine::core::optimiser
pub fn coalesce(expr: Expression, identifiers: &HashMap<String, Expression>) -> Expression;
pub fn shake(expr: Expression) -> Expression;
pub fn rewrite(expr: Expression) -> Expression;
pub fn matrix(expr: Expression) -> Expression;
```

### 2.2. Chainsaw Extensions: `src/ext/tau.rs`

**Source:** `upstream/chainsaw/src/ext/tau.rs:1-337`

Расширения для интеграции tau-engine в chainsaw:

| Function | Purpose | Lines |
|----------|---------|-------|
| `deserialize_expression` | Десериализация YAML → Expression | 10-16 |
| `deserialize_numeric` | Десериализация числовых Pattern | 18-36 |
| `extract_fields` | Извлечение имён полей из Expression | 38-80 |
| `update_fields` | Замена имён полей в Expression (для preprocessing) | 82-128 |
| `parse_field` | Парсинг поля с cast: `int(field)`, `str(field)` | 130-140 |
| `parse_kv` | Парсинг CLI key-value в Expression | 142-337 |

### 2.3. Usage in Chainsaw

**hunt.rs:915, 977**
```rust
// Group matching
if tau_engine::core::solve(filter, &mapped) {... }

// Rule matching (Detection)
tau_engine::solve(detection, &mapped)

// Expression matching (Filter::Expression)
tau_engine::core::solve(expression, &mapped)
```

**search.rs:114, 131**
```rust
if!tau_engine::core::solve(expression, &wrapper) {
 continue;
}
```

**rule/chainsaw.rs:180-194**
```rust
detection.expression = optimiser::coalesce(detection.expression, &detection.identifiers);
detection.identifiers.clear;
detection.expression = optimiser::shake(detection.expression);
detection.expression = optimiser::rewrite(detection.expression);
detection.expression = optimiser::matrix(detection.expression);
```

## 3) Data Model (FACTS)

### 3.1. Expression Enum (IR)

```rust
pub enum Expression {
 // Boolean logic
 BooleanGroup(BoolSym, Vec<Expression>), // AND/OR группа
 BooleanExpression(Box<Expression>, BoolSym, Box<Expression>), // left op right
 Negate(Box<Expression>), // NOT

 // Field access
 Field(String), // простое поле
 Cast(String, ModSym), // int(field)/str(field)/flt(field)
 Nested(String, Box<Expression>), // вложенный объект

 // Matching
 Match(Pattern, Box<Expression>), // pattern match
 Search(Search, String, bool), // search pattern, field, cast_to_str
 Matrix(Vec<String>, Vec<(Vec<Pattern>, bool)>), // multi-field matrix

 // Literals
 Boolean(bool),
 Float(f64),
 Integer(i64),
 Null,
 Identifier(String), // named identifier reference
}
```

### 3.2. BoolSym Enum (Boolean Operators)

```rust
pub enum BoolSym {
 And,
 Or,
 Equal,
 GreaterThan,
 GreaterThanOrEqual,
 LessThan,
 LessThanOrEqual,
}
```

### 3.3. ModSym Enum (Type Modifiers)

```rust
pub enum ModSym {
 Int, // cast to integer
 Str, // cast to string
 Flt, // cast to float
}
```

### 3.4. Pattern Enum (Value Patterns)

```rust
pub enum Pattern {
 // Integer comparisons
 Equal(i64),
 GreaterThan(i64),
 GreaterThanOrEqual(i64),
 LessThan(i64),
 LessThanOrEqual(i64),

 // Float comparisons
 FEqual(f64),
 FGreaterThan(f64),
 FGreaterThanOrEqual(f64),
 FLessThan(f64),
 FLessThanOrEqual(f64),

 // String patterns
 Any, // любое значение (не null)
 Regex(CompiledRegex), // regex match
 Contains(String), // substring contains
 EndsWith(String), // suffix match
 Exact(String), // exact string match
 StartsWith(String), // prefix match
}
```

### 3.5. Search Enum (Search Strategies)

```rust
pub enum Search {
 Any, // поле существует
 Regex(CompiledRegex, bool), // regex, ignore_case
 AhoCorasick(Box<AhoCorasick>, Vec<MatchType>, bool), // DFA automaton, match_types, ignore_case
 Contains(String), // case-sensitive contains
 EndsWith(String), // case-sensitive endswith
 Exact(String), // case-sensitive exact
 StartsWith(String), // case-sensitive startswith
}
```

### 3.6. MatchType Enum (for AhoCorasick)

```rust
pub enum MatchType {
 Contains(String),
 EndsWith(String),
 Exact(String),
 StartsWith(String),
}
```

### 3.7. Detection Struct

```rust
pub struct Detection {
 pub expression: Expression,
 pub identifiers: HashMap<String, Expression>,
}
```

### 3.8. Document Trait

```rust
pub trait Document {
 fn find(&self, key: &str) -> Option<Value<'_>>;
}
```

### 3.9. Value Enum (from SLICE-005)

```rust
pub enum Value<'a> {
 Null,
 Bool(bool),
 Float(f64),
 Int(i64),
 UInt(u64),
 String(Cow<'a, str>),
 Array(&'a dyn AsValue),
 Object(&'a dyn Object),
}
```

## 4) Solver Semantics

### 4.1. Core Solve Algorithm

`tau_engine::core::solve(expression: &Expression, document: &dyn Document) -> bool`

**Алгоритм:** рекурсивный обход дерева Expression, вычисление каждого узла против Document.

**Семантика узлов:**

| Expression | Semantics |
|------------|-----------|
| `BooleanGroup(And, exprs)` | ALL exprs must be true |
| `BooleanGroup(Or, exprs)` | ANY expr must be true |
| `BooleanExpression(l, op, r)` | compare l op r (==, <, >, <=, >=) |
| `Negate(e)` | NOT e |
| `Field(f)` | document.find(f) is Some |
| `Cast(f, Int)` | document.find(f) parsed as int |
| `Cast(f, Str)` | document.find(f) to_string |
| `Search(s, f, cast)` | apply search strategy s to field f |
| `Match(p, e)` | evaluate e and match against pattern p |
| `Matrix(fields, rows)` | multi-field match matrix |
| `Nested(f, e)` | solve e against nested object at f |
| `Boolean(b)` | literal bool |
| `Integer(i)` | literal int |
| `Float(f)` | literal float |
| `Null` | null literal |
| `Identifier(id)` | lookup in identifiers map |

### 4.2. Detection Solve

`tau_engine::solve(detection: &Detection, document: &dyn Document) -> bool`

**Алгоритм:**
1. Если есть identifiers — подставить их в expression через `coalesce`
2. Вызвать `core::solve(expression, document)`

### 4.3. Search Semantics

| Search | Semantics |
|--------|-----------|
| `Any` | field exists and not null |
| `Regex(rx, ic)` | regex match (ic=ignore_case) |
| `AhoCorasick(ac, types, ic)` | DFA multi-pattern search |
| `Contains(s)` | value.contains(s) (case-sensitive) |
| `EndsWith(s)` | value.ends_with(s) (case-sensitive) |
| `Exact(s)` | value == s (case-sensitive) |
| `StartsWith(s)` | value.starts_with(s) (case-sensitive) |

**Case-insensitive matching:**
При `ignore_case=true` chainsaw использует `AhoCorasick` DFA с `ascii_case_insensitive(true)` вместо простых Contains/EndsWith/Exact/StartsWith.

### 4.4. Array Iteration

Когда Document.find возвращает Array, solver итерирует по всем элементам массива и возвращает true если хотя бы один элемент матчится.

### 4.5. Nested Object Access

Поддержка dot-notation: `field.subfield.subsubfield` через рекурсивный `Object::find`.

## 5) Optimiser Phases

### 5.1. coalesce(expr, identifiers)

**Назначение:** Подстановка именованных identifiers в expression.

**Семантика:**
- `Identifier(name)` → `identifiers[name]` (рекурсивно)
- Остальные узлы — рекурсивный обход

### 5.2. shake(expr)

**Назначение:** Tree shaking — удаление мёртвого кода, упрощение констант.

**Оптимизации:**
- `NOT NOT x` → `x`
- `true AND x` → `x`
- `false AND x` → `false`
- `true OR x` → `true`
- `false OR x` → `x`
- Flatten nested BooleanGroups с одинаковым оператором

### 5.3. rewrite(expr)

**Назначение:** Переписывание для эффективности.

**Оптимизации:**
- Сортировка элементов BooleanGroup для стабильности
- Нормализация порядка операндов в BooleanExpression

### 5.4. matrix(expr)

**Назначение:** Объединение нескольких Search на разных полях в Matrix для batch-matching.

**Семантика:**
- AND группа из Search по разным полям → Matrix
- Позволяет оптимизировать итерацию по документу

## 6) Sigma → Tau Conversion

**Source:** `upstream/chainsaw/src/rule/sigma.rs:507-771`

### 6.1. Detection Block Conversion

```yaml
# Sigma format
detection:
 selection:
 EventID: 4688
 CommandLine|contains: 'powershell'
 condition: selection
```

→

```yaml
# Tau format
detection:
 selection:
 EventID: i4688
 CommandLine: i*powershell*
 condition: selection
```

### 6.2. Modifier Mapping

| Sigma Modifier | Tau Pattern |
|----------------|-------------|
| (none) | `i{value}` (case-insensitive exact with wildcards) |
| `contains` | `i*{value}*` |
| `startswith` | `i{value}*` |
| `endswith` | `i*{value}` |
| `re` | `?{regex}` |
| `base64` | base64-encoded value |
| `base64offset` | 3 variants with offset padding |
| `all` | `all({field})` |

### 6.3. Condition Operators

| Sigma | Tau |
|-------|-----|
| `selection` | `selection` |
| `selection1 and selection2` | `selection1 and selection2` |
| `selection1 or selection2` | `selection1 or selection2` |
| `not selection` | `not selection` |
| `all of them` | `sel1 and sel2 and...` |
| `1 of them` | `sel1 or sel2 or...` |
| `all of selection*` | `(selection0 and selection1 and...)` |
| `1 of selection*` | `(selection0 or selection1 or...)` |

### 6.4. Unsupported Conditions

**Source:** `sigma.rs:182-197`

```rust
fn unsupported(&self) -> bool {
 self.contains(" | ") // aggregation pipe
 | self.contains('*') // wildcard in condition (not in value)
 | self.contains(" avg ")
 | self.contains(" of ")
 | self.contains(" max ")
 | self.contains(" min ")
 | self.contains(" near ")
 | self.contains(" sum ")
}
```

## 7) C++ Port Specification

### 7.1. Module Boundary: MOD-0009

**Namespace:** `chainsaw::tau`

**Public Interface:**

```cpp
namespace chainsaw::tau {

// === Value (from SLICE-005, MOD-0005) ===
// Используется Value из value.hpp

// === Expression IR ===
enum class BoolSym {
 And, Or, Equal, GreaterThan, GreaterThanOrEqual, LessThan, LessThanOrEqual
};

enum class ModSym { Int, Str, Flt };

enum class MatchType { Contains, EndsWith, Exact, StartsWith };

// Pattern - variant type
using Pattern = std::variant<
 PatternEqual, // int64_t
 PatternGreaterThan, // int64_t
 PatternGreaterThanOrEqual, // int64_t
 PatternLessThan, // int64_t
 PatternLessThanOrEqual, // int64_t
 PatternFEqual, // double
 PatternFGreaterThan, // double
 PatternFGreaterThanOrEqual, // double
 PatternFLessThan, // double
 PatternFLessThanOrEqual,// double
 PatternAny,
 PatternRegex, // compiled regex
 PatternContains, // string
 PatternEndsWith, // string
 PatternExact, // string
 PatternStartsWith // string
>;

// Search strategy
using Search = std::variant<
 SearchAny,
 SearchRegex, // compiled regex + ignore_case
 SearchAhoCorasick, // DFA + match_types + ignore_case
 SearchContains, // string
 SearchEndsWith, // string
 SearchExact, // string
 SearchStartsWith // string
>;

// Expression - recursive variant
struct Expression; // forward
using ExpressionPtr = std::unique_ptr<Expression>;

struct Expression {
 using Variant = std::variant<
 BooleanGroup, // BoolSym + vector<Expression>
 BooleanExpression, // left + BoolSym + right
 Negate, // Expression
 Field, // string
 Cast, // string + ModSym
 Nested, // string + Expression
 Match_, // Pattern + Expression
 Search_, // Search + string + bool(cast)
 Matrix, // vector<string> + vector<(vector<Pattern>, bool)>
 Boolean, // bool
 Float, // double
 Integer, // int64_t
 Null,
 Identifier // string
 >;
 Variant data;
};

// === Detection ===
struct Detection {
 Expression expression;
 std::unordered_map<std::string, Expression> identifiers;
};

// === Document trait ===
class Document {
public:
 virtual ~Document = default;
 virtual std::optional<Value> find(std::string_view key) const = 0;
};

// === Solver ===
bool solve(const Detection& detection, const Document& document);
bool solve(const Expression& expression, const Document& document);

// === Parser ===
std::expected<Expression, Error> parse_identifier(const YAML::Node& yaml);
std::expected<Pattern, Error> parse_numeric(const std::string& str);
std::expected<Expression, Error> parse_kv(std::string_view kv);
Expression parse_field(std::string_view key);

// === Optimiser ===
Expression coalesce(Expression expr,
 const std::unordered_map<std::string, Expression>& identifiers);
Expression shake(Expression expr);
Expression rewrite(Expression expr);
Expression matrix(Expression expr);

// === Field extraction ===
std::unordered_set<std::string> extract_fields(const Expression& expr);
Expression update_fields(Expression expr,
 const std::unordered_map<std::string, std::string>& lookup);

} // namespace chainsaw::tau
```

### 7.2. Internal Dependencies

- `MOD-0005` (SLICE-005): `Value`, `Document` trait, `Object` trait
- RE2 (`ADR-0005`): regex compilation and matching
- Aho-Corasick library: DFA string matching

### 7.3. Implementation Constraints

1. **Expression variant:** Использовать `std::variant` или tagged union с explicit memory management
2. **Recursive types:** `std::unique_ptr` для рекурсивных Expression
3. **Regex:** RE2 для Pattern::Regex и Search::Regex
4. **Aho-Corasick:** Отдельная библиотека или реализация (см. ADR-0009 pending)
5. **YAML parsing:** RapidYAML или yaml-cpp для parse_identifier

### 7.4. Behavioral 1:1 Requirements

| Behavior | Requirement | Source |
|----------|-------------|--------|
| Case-insensitive matching | ASCII-only folding via AhoCorasick | ext/tau.rs:257-268 |
| Dot-notation | Split by '.' for nested access | Document::find semantics |
| Array iteration | Match if ANY element matches | Solver semantics |
| Wildcard patterns | `*` = any chars, `?` = any char | sigma.rs:213-230 |
| Pattern prefix | `i` = ignore_case, `?` = regex | sigma.rs:230, 256 |

## 8) Test Matrix

### 8.1. Upstream TST-* Mapping (TESTMAT-0001)

Следующие upstream тесты **косвенно** тестируют tau engine через Sigma→Tau conversion:

| TST-ID | Test | Coverage |
|--------|------|----------|
| TST-0009 | test_unsupported_conditions | Condition parsing |
| TST-0010 | test_match_contains | Pattern: contains → `i*...*` |
| TST-0011 | test_match_endswith | Pattern: endswith → `i*...` |
| TST-0012 | test_match | Pattern: wildcards handling |
| TST-0013 | test_match_regex | Pattern: regex → `?...` |
| TST-0014 | test_match_startswith | Pattern: startswith → `i...*` |
| TST-0015 | test_parse_identifier | parse_identifier |
| TST-0016 | test_prepare | Detection preparation |
| TST-0017 | test_prepare_group | Detection merge |
| TST-0018 | test_detection_to_tau_0 | Full Sigma→Tau conversion |
| TST-0019 | test_detection_to_tau_all_of_them | Condition: all of them |
| TST-0020 | test_detection_to_tau_one_of_them | Condition: 1 of them |
| TST-0021 | test_detection_to_tau_all_of_selection | Condition: all of selection* |
| TST-0022 | test_detection_to_tau_one_of_selection | Condition: 1 of selection* |

Следующие upstream тесты **прямо** используют tau_engine через hunt/search:

| TST-ID | Test | tau usage |
|--------|------|-----------|
| TST-0001..TST-0003 | search tests | tau_engine::core::solve |
| TST-0004 | hunt test | tau_engine::solve |

### 8.2. New Slice Unit Tests (TST-TAU-*)

Для полного покрытия tau модуля нужны дополнительные тесты:

| Test ID | Description | Expected | Priority |
|---------|-------------|----------|----------|
| TST-TAU-001 | Expression::Field solve | true if field exists | HIGH |
| TST-TAU-002 | Expression::BooleanGroup(And) | true if ALL match | HIGH |
| TST-TAU-003 | Expression::BooleanGroup(Or) | true if ANY match | HIGH |
| TST-TAU-004 | Expression::Negate | inverts result | HIGH |
| TST-TAU-005 | Search::Exact case-sensitive | exact string match | HIGH |
| TST-TAU-006 | Search::AhoCorasick case-insensitive | ignore_case matching | HIGH |
| TST-TAU-007 | Search::Contains | substring match | MEDIUM |
| TST-TAU-008 | Search::StartsWith | prefix match | MEDIUM |
| TST-TAU-009 | Search::EndsWith | suffix match | MEDIUM |
| TST-TAU-010 | Search::Regex | regex pattern match | HIGH |
| TST-TAU-011 | BooleanExpression numeric | int comparison | MEDIUM |
| TST-TAU-012 | Cast(Int) | string→int conversion | MEDIUM |
| TST-TAU-013 | Cast(Str) | value→string conversion | MEDIUM |
| TST-TAU-014 | Nested object access | dot-notation lookup | HIGH |
| TST-TAU-015 | Array iteration | any element match | HIGH |
| TST-TAU-016 | optimiser::coalesce | identifier substitution | MEDIUM |
| TST-TAU-017 | optimiser::shake | dead code elimination | MEDIUM |
| TST-TAU-018 | optimiser::matrix | multi-field optimization | LOW |
| TST-TAU-019 | parse_kv with int | Cast expression | MEDIUM |
| TST-TAU-020 | parse_kv with not | Negate expression | MEDIUM |
| TST-TAU-021 | extract_fields | field name extraction | LOW |
| TST-TAU-022 | update_fields | field name replacement | LOW |

### 8.3. Golden Tests via RUN-*

Используются косвенно через:
- `*` (search с tau expression via --tau)
- `*` (hunt с rules → tau detection)

## 9) S1–S4 Assessment

| Dimension | Rating | Notes |
|-----------|--------|-------|
| S1 (Complexity) | HIGH | Complex recursive AST, Pratt parser, multiple optimization passes |
| S2 (Risk) | MEDIUM | External crate semantics must match exactly |
| S3 (Dependencies) | LOW | Depends on SLICE-005 (Done), RE2, Aho-Corasick |
| S4 (Effort) | HIGH | Full reimplementation of tau-engine core |

**Рекомендация:** Можно рассмотреть разбиение на подслайсы:
- SLICE-008A: Expression IR + basic solve
- SLICE-008B: Parser (parse_identifier, parse_kv)
- SLICE-008C: Optimiser passes
- SLICE-008D: Search strategies (AhoCorasick)

## 10) Unknowns & Risks

| Risk | Description | Mitigation |
|------|-------------|------------|
| RISK-TAU-001 | Точная семантика Pratt parser | Изучить tau-engine исходники детально |
| RISK-TAU-002 | Aho-Corasick library выбор | Рассмотреть aho-corasick-cpp или port |
| RISK-TAU-003 | Case folding semantics | Только ASCII lowercase, не Unicode |
| RISK-TAU-004 | Regex flavor differences | RE2 vs Rust regex — протестировать edge cases |

## 11) References

- **tau-engine source:** https://github.com/WithSecureLabs/tau-engine
- **tau-engine docs:** https://docs.rs/tau-engine/1.0.0/tau_engine/
- **chainsaw ext/tau.rs:** `upstream/chainsaw/src/ext/tau.rs`
- **chainsaw sigma.rs:** `upstream/chainsaw/src/rule/sigma.rs`
- **chainsaw hunt.rs:** `upstream/chainsaw/src/hunt.rs`
- **chainsaw search.rs:** `upstream/chainsaw/src/search.rs`
- **TOBE-0001:** MOD-0009 specification
- **ADR-0005:** RE2 for regex
- **SLICE-005:** Value/Document dependency

---

## Appendix A: Expression Examples

### A.1. Simple Field Check
```yaml
detection:
 selection:
 EventID: 4688
 condition: selection
```
→ `Expression::Search(Search::Exact("4688"), "EventID", false)`

### A.2. Contains with Ignore Case
```yaml
detection:
 selection:
 CommandLine|contains: 'powershell'
 condition: selection
```
→ `Expression::Search(Search::AhoCorasick(..., [MatchType::Contains("powershell")], true), "CommandLine", false)`

### A.3. Boolean Group
```yaml
detection:
 sel1:
 EventID: 4688
 sel2:
 CommandLine|contains: 'cmd'
 condition: sel1 and sel2
```
→ `Expression::BooleanGroup(BoolSym::And, [expr1, expr2])`

### A.4. Negation
```yaml
detection:
 selection:
 EventID: 4688
 filter:
 User: 'SYSTEM'
 condition: selection and not filter
```
→ `Expression::BooleanGroup(BoolSym::And, [selection_expr, Expression::Negate(filter_expr)])`
