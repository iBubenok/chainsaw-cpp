# SPEC-SLICE-005 — Reader Framework + JSON Parser

## Статус
- Версия: 1
- Дата: 2026-01-09
- Слайс: SLICE-005
- Статус: UnitReady
- Step: 31

## Назначение
Micro-spec поведения Reader Framework и JSON парсера из upstream Chainsaw для портирования в C++ (MOD-0006 `io::reader`, MOD-0007 `formats/json`).

## Источники (FACTS)

### Первичные источники

| Компонент | Файл | Строки | Описание |
|-----------|------|--------|----------|
| Document enum | `upstream/chainsaw/src/file/mod.rs` | 24-32 | Типы документов (Evtx/Hve/Json/Mft/Xml/Esedb) |
| Kind enum | `upstream/chainsaw/src/file/mod.rs` | 38-49 | Типы входных файлов (+ Jsonl, Unknown) |
| Kind::extensions | `upstream/chainsaw/src/file/mod.rs` | 51-68 | Расширения для каждого Kind |
| Parser enum | `upstream/chainsaw/src/file/mod.rs` | 87-97 | Внутренние парсеры |
| Reader struct | `upstream/chainsaw/src/file/mod.rs` | 99-433 | Основной API чтения файлов |
| JSON Parser | `upstream/chainsaw/src/file/json.rs` | 1-68 | Парсинг JSON файлов |
| JSONL Parser | `upstream/chainsaw/src/file/json.rs` | 70-125 | Парсинг JSON Lines |
| Value | `upstream/chainsaw/src/value.rs` | 1-84 | Внутренняя модель документа |

### Вторичные источники (использование)
| Команда | Файл | Как используется |
|---------|------|------------------|
| dump | main.rs | Reader::load → documents → output |
| search | main.rs | Reader::load → documents → filtering |
| hunt | main.rs | Reader::load → documents → rules matching |

---

## Контракт Document/Kind

### Document enum (mod.rs:24-32)
```rust
pub enum Document {
 Evtx(Evtx),
 Hve(Hve),
 Json(Json), // serde_json::Value
 Mft(Mft),
 Xml(Xml),
 Esedb(Esedb),
}
```

**FACT-001:** Document представляет распарсенный документ одного из поддерживаемых форматов.
**FACT-002:** JSONL документы представлены как Document::Json (тот же тип что и JSON).

### Kind enum (mod.rs:38-49)
```rust
pub enum Kind {
 Evtx, Hve, Json, Jsonl, Mft, Xml, Esedb, Unknown
}
```

**FACT-003:** Kind — тип входного файла, определяет какой парсер использовать.
**FACT-004:** Kind::Jsonl существует отдельно от Kind::Json, но документы оба → Document::Json.

### Kind::extensions (mod.rs:51-68)

| Kind | Расширения |
|------|------------|
| Evtx | `evt`, `evtx` |
| Hve | `hve` |
| Json | `json` |
| Jsonl | `jsonl` |
| Mft | `mft`, `bin`, `$MFT` |
| Xml | `xml` |
| Esedb | `dat`, `edb` |
| Unknown | None |

**FACT-005:** Расширения задаются БЕЗ точки.
**FACT-006:** `$MFT` — специальный случай (имя файла, не расширение).

---

## Контракт Reader

### Сигнатура (mod.rs:104-110)
```rust
impl Reader {
 pub fn load(
 file: &Path,
 load_unknown: bool,
 skip_errors: bool,
 decode_data_streams: bool,
 data_streams_directory: Option<PathBuf>,
 ) -> crate::Result<Self>
}
```

### Алгоритм выбора парсера (mod.rs:111-383)

#### 1. Получение расширения
```
extension = file.extension.and_then(|e| e.to_str)
```

#### 2. Выбор по расширению (extension-first)

| Расширение | Парсер |
|------------|--------|
| `evt`, `evtx` | EvtxParser |
| `json` | JsonParser |
| `jsonl` | JsonlParser |
| `bin`, `mft` | MftParser |
| `xml` | XmlParser |
| `hve` | HveParser |
| `dat`, `edb` | EsedbParser |
| другое | см. fallback |

**FACT-007:** Выбор парсера по расширению — **первичный** механизм (без magic bytes).

#### 3. Обработка ошибок загрузки (mod.rs:116-131)
```
IF parser.load fails THEN
 IF skip_errors THEN
 PRINT stderr: "[!] failed to load file '{path}' - {error}\n"
 RETURN Ok(Reader { parser: Parser::Unknown })
 ELSE
 RETURN Err(error)
```

**FACT-008:** Формат ошибки: `[!] failed to load file '<path>' - <error>\n`
**FACT-009:** При `skip_errors=true` возвращается Parser::Unknown (итератор пуст).

#### 4. Fallback для неизвестных расширений (mod.rs:273-323)

При `load_unknown=true` и неизвестном расширении — попытка парсинга в порядке:
1. EVTX
2. MFT
3. JSON
4. XML
5. HVE
6. ESEDB

**FACT-010:** JSONL НЕ используется в fallback ("слишком generic", mod.rs:363-365).

**FACT-011:** При неудаче всех парсеров:
- `skip_errors=true`: `[!] file type is not currently supported - {path}\n`
- `skip_errors=false`: `file type is not currently supported - {path}, use --skip-errors to continue...`

#### 5. Edge case: $MFT без расширения (mod.rs:327-334)
```
IF file.file_name == Some("$MFT") THEN
 TRY MftParser::load(file)
```

**FACT-012:** Файл с именем `$MFT` (без расширения) распознаётся как MFT.

### Reader::documents (mod.rs:385-419)

```rust
pub fn documents<'a>(&'a mut self) -> Documents<'a>
```

**FACT-013:** Возвращает итератор `Documents`, который оборачивает итератор конкретного парсера.
**FACT-014:** Для Parser::Unknown итератор всегда пуст.

### Reader::kind (mod.rs:421-432)

**FACT-015:** Возвращает Kind, соответствующий текущему парсеру.

---

## Контракт JSON Parser

### JSON Parser (json.rs:12-53)

#### Parser::load
```rust
pub fn load(path: &Path) -> crate::Result<Self> {
 let file = File::open(path)?;
 let reader = BufReader::new(file);
 let json = serde_json::from_reader(reader)?;
 Ok(Self { inner: Some(json) })
}
```

**FACT-016:** Файл читается целиком в память и парсится как JSON.
**FACT-017:** При ошибке парсинга — возвращается ошибка serde_json.

#### Parser::parse
```rust
pub fn parse(&mut self) -> impl Iterator<Item = Result<Json, Error>> + '_ {
 if let Some(json) = self.inner.take {
 return match json {
 Json::Array(array) => ParserIter(Some(array.into_iter)),
 _ => ParserIter(Some(vec![json].into_iter)),
 };
 }
 ParserIter(None)
}
```

**FACT-018:** Если корень JSON — массив, итерирует по элементам массива.
**FACT-019:** Если корень — не массив, возвращает один документ (весь JSON).
**FACT-020:** После первого вызова `parse` — `inner` становится None.

### JSONL Parser (json.rs:70-125)

#### Parser::load
```rust
pub fn load(path: &Path) -> crate::Result<Self> {
 let file = File::open(path)?;
 let mut reader = BufReader::new(file);
 // Crude check: read first line and try to parse as JSON
 let mut line = String::new;
 reader.read_line(&mut line)?;
 let _ = serde_json::from_str::<Json>(&line)?;
 reader.rewind?;
 Ok(Self { inner: Some(reader) })
}
```

**FACT-021:** При load читается только первая строка для валидации.
**FACT-022:** Если первая строка не парсится как JSON — ошибка.

#### Parser::parse
```rust
pub fn parse(&mut self) -> impl Iterator<Item = Result<Json, Error>> + '_ {
 if let Some(file) = self.inner.take {
 return ParserIter(Some(file.lines));
 }
 ParserIter(None)
}
```

**FACT-023:** Читает файл построчно, каждая строка парсится как отдельный JSON документ.
**FACT-024:** Пустые строки и ошибки парсинга — ошибки итератора.

---

## Контракт Value (canonical document model)

### Value enum (value.rs:8-18)
```rust
pub enum Value {
 Null,
 Bool(bool),
 Float(f64),
 Int(i64),
 UInt(u64),
 String(String),
 Array(Vec<Value>),
 Object(FxHashMap<String, Value>),
}
```

**FACT-025:** Value явно разделяет Int/UInt/Float (в отличие от serde_json::Number).
**FACT-026:** Object использует FxHashMap (быстрый хеш, неупорядоченный).

### Конверсия Json → Value (value.rs:20-41)

**FACT-027:** Number конвертируется в порядке приоритета: UInt → Int → Float.
**FACT-028:** Если число не помещается ни в один тип — `unreachable!` (panic).

### Конверсия Value → Json (value.rs:43-58)

**FACT-029:** Float конвертируется через `Number::from_f64`.
**FACT-030:** При невозможности конвертации Float → `.expect("could not return to float")` (panic).

---

## Критичные инварианты

### INV-001: Extension-first выбор парсера
- Парсер выбирается по расширению файла БЕЗ анализа содержимого
- Комментарий в коде: "we assume that the file extensions are correct"

### INV-002: Fallback порядок
При `load_unknown=true`:
1. EVTX → 2. MFT → 3. JSON → 4. XML → 5. HVE → 6. ESEDB
- JSONL **не** используется в fallback

### INV-003: JSON Array развёртка
- Если JSON — массив, каждый элемент становится отдельным документом
- Если не массив — весь JSON как один документ

### INV-004: Value типизация чисел
- Порядок: `as_u64` → `as_i64` → `as_f64`
- Это влияет на tau matching и сериализацию

### INV-005: Потоковая итерация
- JSON читается целиком в память (non-streaming)
- JSONL читается построчно (streaming)

### INV-006: Формат сообщений об ошибках
- `[!] failed to load file '<path>' - <error>\n`
- `[!] file type is not currently supported - <path>\n`
- `[!] file type is not known - <path>\n`

---

## Зависимости слайса

### Входные зависимости
| ID | Название | Статус |
|----|----------|--------|
| SLICE-001 | Platform Layer | Done |

### Исходящие зависимости (использует SLICE-005)
| ID | Название | Статус |
|----|----------|--------|
| SLICE-006 | XML Parser | Backlog |
| SLICE-007 | EVTX Parser | Backlog |
| SLICE-008 | Tau Engine | Backlog |
| SLICE-011 | Search Command | Backlog |
| SLICE-012 | Hunt Command | Backlog |
| SLICE-013 | Dump Command | Backlog |
| SLICE-015 | HVE Parser | Backlog |
| SLICE-016 | ESEDB Parser | Backlog |
| SLICE-018 | MFT Parser | Backlog |

---

## Риски

### Нет новых рисков
- SLICE-005 использует RapidJSON (ADR-0003) — решение принято
- Зависимости от парсеров (EVTX/HVE/MFT/ESEDB) отложены на другие слайсы

---

## Требуемые тесты (test-to-test)

### Upstream тесты
- **Нет прямых тестов** для JSON parser в upstream
- Косвенное покрытие через TST-0001..TST-0003 (search с EVTX)

### Unit-тесты (новые для C++)

| ID | Описание | Приоритет |
|----|----------|-----------|
| TST-RDR-001 | DocumentKind enum соответствует upstream | High |
| TST-RDR-002 | Kind::extensions возвращает правильные расширения | High |
| TST-RDR-003 | Reader::open выбирает JSON по расширению | High |
| TST-RDR-004 | Reader::open выбирает JSONL по расширению | High |
| TST-RDR-005 | Reader::kind возвращает правильный Kind | High |
| TST-JSON-001 | JSON массив → несколько документов | High |
| TST-JSON-002 | JSON объект → один документ | High |
| TST-JSON-003 | JSON примитив → один документ | High |
| TST-JSON-004 | Невалидный JSON → ошибка | High |
| TST-JSONL-001 | JSONL — построчная итерация | High |
| TST-JSONL-002 | JSONL первая строка невалидна → ошибка load | High |
| TST-JSONL-003 | JSONL строка посередине невалидна → ошибка next | Medium |
| TST-VALUE-001 | Value::from(Json) — числа UInt | High |
| TST-VALUE-002 | Value::from(Json) — числа Int (отрицательные) | High |
| TST-VALUE-003 | Value::from(Json) — Float | High |
| TST-VALUE-004 | Value to Json round-trip | Medium |
| TST-RDR-006 | skip_errors=true → Parser::Unknown | Medium |
| TST-RDR-007 | skip_errors=false → ошибка | Medium |
| TST-RDR-008 | load_unknown=true fallback порядок | Medium |
| TST-RDR-009 | $MFT edge case | Low |

---

## C++ API (To-Be контракт)

### Из TOBE-0001/4.6

```cpp
namespace chainsaw::io {
 // Типы документов (аналог Rust Document enum)
 enum class DocumentKind { Evtx, Hve, Json, Xml, Mft, Esedb, Unknown };

 // Canonical document model (JSON-like)
 // Использует RapidJSON DOM (ADR-0003)
 class Value {
 public:
 // Типы: Null, Bool, Int64, UInt64, Double, String, Array, Object
 // Конверсия из/в rapidjson::Value
 };

 // Документ с метаданными
 struct Document {
 DocumentKind kind;
 Value data; // Canonical content
 std::string source; // Source file path (UTF-8)
 // Дополнительные мета: record_id, timestamp (если применимо)
 };

 // Reader — унифицированный интерфейс чтения
 class Reader {
 public:
 // Открыть файл и создать Reader
 static std::expected<Reader, std::string> open(
 const std::filesystem::path& file,
 bool load_unknown = false,
 bool skip_errors = false);

 // Получить следующий документ
 // Возвращает false когда документы закончились
 bool next(Document& out);

 // Текущий тип файла
 DocumentKind kind const;
 };
}
```

### Отличия от Rust API
1. Использует `std::expected` вместо `Result` (C++23)
2. Value на базе RapidJSON DOM (ADR-0003)
3. `decode_data_streams` и `data_streams_directory` — отложены до SLICE-018 (MFT)

---

## S1-S4 Оценка

| Ось | Оценка | Обоснование |
|-----|--------|-------------|
| S1 (ширина контуров) | Low | io::reader + formats/json — тесно связаны |
| S2 (сложность проверки) | Low | unit-тесты, без e2e |
| S3 (unknowns) | Low | RapidJSON решение принято (ADR-0003) |
| S4 (платформенность) | Low | JSON кроссплатформенный |

**Итог:** допустимо к работе (все оси Low)

---

## Критерии UnitReady ✓

- [x] Micro-spec ссылается на первичные источники (код/тесты/запуски)
- [x] Определён полный набор проверок для доказательства 1:1
- [x] Dependencies/unknowns оценены
- [x] Правило гранулярности соблюдено (все оси Low)

---

## Ссылки
- Rust код Reader: `upstream/chainsaw/src/file/mod.rs:24-433`
- Rust код JSON: `upstream/chainsaw/src/file/json.rs`
- Rust код Value: `upstream/chainsaw/src/value.rs`
- To-Be архитектура: `docs/architecture/TOBE-0001-cpp-to-be-architecture.md` (раздел 4.6)
- ADR JSON: `docs/adr/ADR-0003-json-library.md`
- DATA formats: `docs/as_is/DATA-0001-data-formats-and-transformations.md`
- Backlog: `docs/backlog/BACKLOG-0001-porting-backlog.md` (SLICE-005)
