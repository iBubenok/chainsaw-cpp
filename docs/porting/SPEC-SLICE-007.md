# SPEC-SLICE-007 — EVTX Parser

## Статус
- Версия: 3
- Дата: 2026-01-10
- Слайс: SLICE-007
- Статус: **Verified** ( Verification PASS)
- Step: 33
- Verification Report: `docs/reports/VERIFY-SLICE-007-2026-01-10.md`

## Назначение
Micro-spec поведения EVTX парсера из upstream Chainsaw для портирования в C++ (MOD-0007 `formats/evtx`).

EVTX (Windows Event Log) — **критически важный** формат для Chainsaw, являющийся основным источником данных для команд `search`, `hunt`, `dump`.

## Метаданные

| Поле | Значение |
|------|----------|
| SLICE-ID | SLICE-007 |
| MOD-* | MOD-0007 (formats/evtx) |
| TST-* | TST-0001, TST-0002, TST-0003, TST-0004 (косвенно через search/hunt) |
| RUN-* |,,,,,,, |
| REQ-* | REQ-FR-0002, REQ-FR-0004, REQ-NFR-0010 |
| RISK-* | RISK-0030 (pending C++ парсер), RISK-0011 (детерминизм) |

## Источники (FACTS)

### Первичные источники

| Компонент | Файл | Строки | Описание |
|-----------|------|--------|----------|
| EVTX Parser | `upstream/chainsaw/src/file/evtx.rs` | 1-78 | Парсинг EVTX файлов |
| EVTX тип | `upstream/chainsaw/src/file/evtx.rs` | 12 | `pub type Evtx = SerializedEvtxRecord<Json>` |
| Reader (EVTX ветка) | `upstream/chainsaw/src/file/mod.rs` | 114-136 | Выбор EvtxParser по расширению |
| Reader (fallback) | `upstream/chainsaw/src/file/mod.rs` | 275-278 | EVTX в fallback позиции 1 |
| Cargo.toml | `upstream/chainsaw/Cargo.toml` | 22 | `evtx = { version = "0.8", default-features = false }` |
| Wrapper алиасы | `upstream/chainsaw/src/file/evtx.rs` | 32-62 | Алиасы для tau_engine Document |

### Вторичные источники (тесты/golden runs)

| Тест/RUN | Файл | Описание |
|----------|------|----------|
| search_jq_simple_string | `upstream/chainsaw/tests/clo.rs:7-25` | search 4624 -jq |
| search_q_jsonl_simple_string | `upstream/chainsaw/tests/clo.rs:27-50` | search 4624 --jsonl |
| search_q_simple_string | `upstream/chainsaw/tests/clo.rs:51-70` | search 4624 -q |
| hunt_r_any_logon | `upstream/chainsaw/tests/clo.rs:72-94` | hunt с Chainsaw правилом |
| security_sample.evtx | `upstream/chainsaw/tests/evtx/` | Тестовая EVTX фикстура |
| clo_search_qj_simple_string.txt | `upstream/chainsaw/tests/evtx/` | Ожидаемый JSON вывод |

---

## Контракт EVTX Parser (evtx.rs)

### Тип Evtx (evtx.rs:12)
```rust
pub type Evtx = SerializedEvtxRecord<Json>;
```

**FACT-001:** EVTX документ — это `SerializedEvtxRecord<Json>` из crate `evtx`.
**FACT-002:** `SerializedEvtxRecord` содержит поля:
- `data: T` (здесь T = serde_json::Value) — JSON представление события
- `timestamp: chrono::DateTime<Utc>` — временная метка события
- `record_id: u64` — ID записи в логе

### Parser struct (evtx.rs:14-16)
```rust
pub struct Parser {
 pub inner: EvtxParser<File>,
}
```

**FACT-003:** Parser хранит EvtxParser из crate `evtx`, привязанный к File.

### Parser::load (evtx.rs:18-23)
```rust
pub fn load(file: &Path) -> crate::Result<Self> {
 let settings = ParserSettings::default.separate_json_attributes(true);
 let parser = EvtxParser::from_path(file)?.with_configuration(settings);
 Ok(Self { inner: parser })
}
```

**FACT-004:** `ParserSettings::default.separate_json_attributes(true)` — **критическая настройка**.
**FACT-005:** `separate_json_attributes(true)` означает, что XML атрибуты выносятся в отдельные поля `*_attributes`.
**FACT-006:** При ошибке парсинга возвращается ошибка evtx crate (через anyhow).

### Parser::parse (evtx.rs:25-30)
```rust
pub fn parse(
 &mut self,
) -> impl Iterator<Item = Result<SerializedEvtxRecord<serde_json::Value>, EvtxError>> + '_ {
 self.inner.records_json_value
}
```

**FACT-007:** `records_json_value` возвращает итератор записей с JSON представлением.
**FACT-008:** Итератор возвращает `Result` — ошибки могут возникать при итерации (повреждённые записи).

---

## Структура JSON документа EVTX

### Формат с separate_json_attributes=true

**FACT-009:** Наблюдаемая структура JSON из `clo_search_qj_simple_string.txt`:

```json
{
 "Event": {
 "EventData": {
 "AuthenticationPackageName": "Negotiate",
 "LogonType": 5,
 "TargetUserName": "SYSTEM",
...
 },
 "System": {
 "Channel": "Security",
 "Computer": "DESKTOP-JK4Q86I",
 "Correlation_attributes": {"ActivityID": "..."},
 "EventID": 4624,
 "EventRecordID": 31794,
 "Execution_attributes": {"ProcessID": 688, "ThreadID": 736},
 "Keywords": "0x8020000000000000",
 "Level": 0,
 "Opcode": 0,
 "Provider_attributes": {"Guid": "...", "Name": "Microsoft-Windows-Security-Auditing"},
 "Security": null,
 "Task": 12544,
 "TimeCreated_attributes": {"SystemTime": "2022-10-11T19:26:52.154080Z"},
 "Version": 2
 }
 },
 "Event_attributes": {"xmlns": "http://schemas.microsoft.com/win/2004/08/events/event"}
}
```

**FACT-010:** XML атрибуты преобразуются в поля с суффиксом `_attributes`:
- `Provider` → `Provider_attributes: {Name, Guid}`
- `TimeCreated` → `TimeCreated_attributes: {SystemTime}`
- `Correlation` → `Correlation_attributes: {ActivityID}`
- `Execution` → `Execution_attributes: {ProcessID, ThreadID}`
- `Event` → `Event_attributes: {xmlns}`

**FACT-011:** Timestamp в формате ISO 8601 с микросекундами: `%Y-%m-%dT%H:%M:%S%.6fZ`

---

## Wrapper алиасы для tau_engine (evtx.rs:32-62)

### Wrapper struct (evtx.rs:32-45)
```rust
pub struct Wrapper<'a>(pub &'a Value);
impl Document for Wrapper<'_> {
 fn find(&self, key: &str) -> Option<Tau<'_>> {
 match key {
 "Event.System.Provider" => self.0.find("Event.System.Provider_attributes.Name"),
 "Event.System.TimeCreated" => self
.0
.find("Event.System.TimeCreated_attributes.SystemTime"),
 _ => self.0.find(key),
 }
 }
}
```

**FACT-012:** Алиасы для удобства в правилах детекции:
- `Event.System.Provider` → `Event.System.Provider_attributes.Name`
- `Event.System.TimeCreated` → `Event.System.TimeCreated_attributes.SystemTime`

**FACT-013:** WrapperLegacy (evtx.rs:49-62) — аналогичный wrapper для `serde_json::Value` (используется в search).

### Searchable trait (evtx.rs:64-77)
```rust
impl Searchable for SerializedEvtxRecord<Json> {
 fn matches(&self, regex: &RegexSet, match_any: &bool) -> bool {
 if *match_any {
 regex.is_match(&self.data.to_string.replace(r"\\", r"\"))
 } else {
 regex
.matches(&self.data.to_string.replace(r"\\", r"\"))
.into_iter
.collect::<Vec<_>>
.len
 == regex.len
 }
 }
}
```

**FACT-014:** Regex поиск выполняется по JSON-stringify документа.
**FACT-015:** Перед поиском выполняется замена `\\\\` → `\` (нормализация escape-последовательностей).
**FACT-016:** `match_any=true` — любой паттерн должен совпасть; `match_any=false` — все паттерны.

---

## Контракт Reader (для EVTX)

### Выбор по расширению (mod.rs:114-136)
```rust
"evt" | "evtx" => {
 let parser = match EvtxParser::load(file) {
 Ok(parser) => parser,
 Err(e) => {
 if skip_errors {
 cs_eyellowln!("[!] failed to load file '{}' - {}\n", file.display, e);
 return Ok(Self { parser: Parser::Unknown });
 } else {
 anyhow::bail!(e);
 }
 }
 };
 Ok(Self { parser: Parser::Evtx(parser) })
}
```

**FACT-017:** Расширения `.evt` и `.evtx` выбирают EvtxParser.
**FACT-018:** Формат ошибки: `[!] failed to load file '<path>' - <error>\n`
**FACT-019:** При `skip_errors=true` возвращается Parser::Unknown, итератор будет пустым.

### Fallback порядок (mod.rs:275-278)
```rust
if let Ok(parser) = EvtxParser::load(file) {
 return Ok(Self { parser: Parser::Evtx(parser) });
}
```

**FACT-020:** При `load_unknown=true` EVTX пробуется **первым** (позиция 1).

### Kind::extensions (mod.rs:54)
```rust
Kind::Evtx => Some(vec!["evt".to_string, "evtx".to_string]),
```

**FACT-021:** Kind::Evtx поддерживает расширения `evt`, `evtx` (без точки).

### Reader::documents для EVTX (mod.rs:387-392)
```rust
Parser::Evtx(parser) => Box::new(
 parser
.parse
.map(|r| r.map(Document::Evtx).map_err(|e| e.into)),
)
```

**FACT-022:** Документы оборачиваются в `Document::Evtx`.
**FACT-023:** Ошибки парсинга записей преобразуются в `anyhow::Error`.

---

## Критичные инварианты

### INV-001: separate_json_attributes=true
- Все XML атрибуты выносятся в отдельные поля `*_attributes`
- Это влияет на структуру JSON и работу правил детекции
- **Без этой настройки детекты не будут работать корректно**

### INV-002: Алиасы полей
- `Event.System.Provider` → `Event.System.Provider_attributes.Name`
- `Event.System.TimeCreated` → `Event.System.TimeCreated_attributes.SystemTime`
- Алиасы должны работать в tau_engine Document::find

### INV-003: Формат timestamp
- ISO 8601 с микросекундами: `2022-10-11T19:26:52.154080Z`
- Используется для фильтрации по времени в search/hunt

### INV-004: Extension-first выбор
- `.evt`, `.evtx` → EvtxParser
- Без magic bytes анализа

### INV-005: Fallback позиция
- При load_unknown=true EVTX на позиции 1 (первым пробуется)

---

## Зависимости слайса

### Входные зависимости
| ID | Название | Статус |
|----|----------|--------|
| SLICE-001 | Platform Layer | Done |
| SLICE-005 | Reader Framework + JSON | Done |
| SLICE-006 | XML Parser | Done |

### Исходящие зависимости (использует SLICE-007)
| ID | Название | Статус |
|----|----------|--------|
| SLICE-011 | Search Command | Backlog |
| SLICE-012 | Hunt Command | Backlog |
| SLICE-013 | Dump Command | Backlog |

---

## Риски

### RISK-0030: Отсутствие готового C++ стека
- **Статус:** Open
- **Влияние:** High — выбор парсера определяет структуру JSON
- **Варианты:**
 1. `libevtx` (libyal) — C библиотека, используется в криминалистике
 2. Собственная реализация на основе документации формата
 3. Портирование логики из Rust crate `evtx`
- **Критерий выбора (ADR-0009):** воспроизвести структуру JSON с `_attributes` полями

### RISK-0039 (новый): Совместимость libevtx JSON формата
- **Статус:** Open
- **Влияние:** High
- **Вероятность:** Medium
- **Описание:** libevtx может формировать JSON в другом формате, без `_attributes` разделения
- **План:** Spike: проверить вывод libevtx, сравнить с Rust evtx crate

---

## Выбор EVTX библиотеки для C++

### Контекст
ADR-0009 оставляет выбор EVTX библиотеки как **Pending**.

### Вариант 1: libevtx (libyal)
**Плюсы:**
- Зрелая библиотека, широко используется в форензике
- Поддержка различных версий EVTX
- C API, легко интегрируется

**Минусы:**
- Формат JSON может отличаться от Rust evtx crate
- Требует проверки `separate_json_attributes` семантики
- Дополнительная зависимость для сборки

### Вариант 2: Собственная реализация
**Плюсы:**
- Полный контроль над форматом JSON
- Можно точно воспроизвести `_attributes` семантику

**Минусы:**
- Большой объём работы
- Риск ошибок в парсере бинарного формата

### Рекомендация
**Spike-подход:** сначала проверить libevtx на фикстуре `security_sample.evtx`, сравнить JSON структуру с эталоном. Если формат совместим — использовать libevtx. Если нет — реализовать слой преобразования или собственный парсер.

---

## Требуемые тесты

### Upstream тесты (test-to-test)

| ID | Тест | Файл | Описание |
|----|------|------|----------|
| TST-0001 | search_jq_simple_string | clo.rs:7-25 | search 4624 JSON вывод |
| TST-0002 | search_q_jsonl_simple_string | clo.rs:27-50 | search 4624 JSONL вывод |
| TST-0003 | search_q_simple_string | clo.rs:51-70 | search 4624 YAML вывод |
| TST-0004 | hunt_r_any_logon | clo.rs:72-94 | hunt с Chainsaw правилом |

**Примечание:** Эти тесты — интеграционные (CLI), требуют SLICE-011/012. Для SLICE-007 создаём unit-тесты на парсер.

### Unit-тесты (новые для C++)

| ID | Описание | Приоритет |
|----|----------|-----------|
| TST-EVTX-001 | Загрузка security_sample.evtx успешна | High |
| TST-EVTX-002 | Количество записей соответствует эталону | High |
| TST-EVTX-003 | Структура JSON содержит `_attributes` поля | High |
| TST-EVTX-004 | Provider_attributes.Name извлекается корректно | High |
| TST-EVTX-005 | TimeCreated_attributes.SystemTime в ISO 8601 | High |
| TST-EVTX-006 | EventID извлекается как число | High |
| TST-EVTX-007 | EventRecordID монотонно возрастает | Medium |
| TST-EVTX-008 | EventData поля присутствуют | High |
| TST-EVTX-009 | Невалидный EVTX файл → ошибка load | High |
| TST-EVTX-010 | Reader::open выбирает EVTX по расширению.evtx | High |
| TST-EVTX-011 | Reader::open выбирает EVTX по расширению.evt | High |
| TST-EVTX-012 | Reader::kind возвращает Evtx | High |
| TST-EVTX-013 | skip_errors=true → пустой Reader при ошибке | Medium |
| TST-EVTX-014 | load_unknown=true fallback (EVTX на позиции 1) | Medium |
| TST-EVTX-015 | Алиас Event.System.Provider работает | High |
| TST-EVTX-016 | Алиас Event.System.TimeCreated работает | High |

### Golden runs (RUN-*)

| RUN-ID | Команда | Покрытие |
|--------|---------|----------|
| | search 4624 -q | EVTX parsing + YAML output |
| | search 4624 -jq | EVTX parsing + JSON output |
| | search 4624 --jsonl | EVTX parsing + JSONL output |
| | hunt -r rule.yml | EVTX parsing + rule matching |

---

## C++ API (To-Be контракт)

### Из существующей архитектуры (reader.hpp)

```cpp
namespace chainsaw::io {
 // DocumentKind::Evtx уже определён

 /// EVTX запись (аналог SerializedEvtxRecord<Json>)
 struct EvtxRecord {
 Value data; ///< JSON представление события
 std::string timestamp; ///< ISO 8601 timestamp
 uint64_t record_id; ///< ID записи
 };

 /// EvtxReader — парсер EVTX файлов
 class EvtxReader: public Reader {
 public:
 explicit EvtxReader(std::filesystem::path path);
 bool load;

 bool next(Document& out) override;
 bool has_next const override;
 DocumentKind kind const override;
 const std::filesystem::path& path const override;
 const std::optional<ReaderError>& last_error const override;
 };

 /// Создать EVTX Reader
 std::unique_ptr<Reader> create_evtx_reader(
 const std::filesystem::path& path,
 bool skip_errors);
}
```

### Wrapper для tau_engine

```cpp
namespace chainsaw::evtx {
 /// Wrapper с алиасами для поиска по полям
 /// @param value JSON документ EVTX события
 /// @param key Путь к полю (с поддержкой алиасов)
 /// @return Значение поля или nullopt
 std::optional<Value> find_with_aliases(const Value& value, std::string_view key);
}
```

---

## S1-S4 Оценка

| Ось | Оценка | Обоснование |
|-----|--------|-------------|
| S1 (ширина контуров) | Low | Один модуль formats/evtx |
| S2 (сложность проверки) | Medium | Есть golden runs и фикстуры для сравнения |
| S3 (unknowns) | High | Выбор библиотеки pending (ADR-0009), совместимость JSON формата |
| S4 (платформенность) | Low | libevtx кроссплатформенный |

**Итог:** S3=High — допустимо к работе (только одна ось High), но требуется spike для проверки libevtx.

---

## Решение по дроблению

Согласно BACKLOG-0001, слайс имеет S3=High. По правилу S1-S4:
> если High по 2+ осям — дробить

У SLICE-007 только одна ось High (S3), поэтому **дробление не требуется**.

**План снижения S3:**
1. Spike: проверить libevtx JSON формат
2. Если совместим — принять решение по libevtx в ADR-0009
3. Если не совместим — спланировать слой преобразования или альтернативу

---

## Критерии UnitReady

- [x] Micro-spec ссылается на первичные источники (код Rust evtx.rs, mod.rs)
- [x] Определён полный набор проверок (TST-EVTX-001..016 +*/030*)
- [x] Dependencies/unknowns оценены (SLICE-005 Done, библиотека pending)
- [x] Правило гранулярности соблюдено (max одна ось High)

**Статус UnitReady:** PASS (с условием spike по libevtx)

---

## Критерий закрытия слайса (UnitDone)

1. EvtxReader реализован и компилируется на 3 платформах
2. JSON структура с `_attributes` полями воспроизведена
3. Алиасы Event.System.Provider/TimeCreated работают
4. Unit-тесты TST-EVTX-* проходят на 3 платформах
5. Интеграция в Reader::open по расширениям.evt/.evtx
6. Fallback позиция 1 при load_unknown=true
7. Нет регрессий по SLICE-001..006

---

## Ссылки

- Rust код EVTX: `upstream/chainsaw/src/file/evtx.rs`
- Rust код Reader: `upstream/chainsaw/src/file/mod.rs:114-136, 275-278`
- Тесты upstream: `upstream/chainsaw/tests/clo.rs`
- Фикстура: `cpp/tests/fixtures/evtx/security_sample.evtx`
- Golden output: `cpp/tests/fixtures/evtx/clo_search_qj_simple_string.txt`
- ADR парсеры: `docs/adr/ADR-0009-forensic-parsers-strategy.md`
- BACKLOG: `docs/backlog/BACKLOG-0001-porting-backlog.md` (SLICE-007)
- RISK:
- DATA formats: `docs/as_is/DATA-0001-data-formats-and-transformations.md`
- C++ Reader: `cpp/src/io/reader.cpp`, `cpp/include/chainsaw/reader.hpp`
