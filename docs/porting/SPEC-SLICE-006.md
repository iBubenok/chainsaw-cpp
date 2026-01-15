# SPEC-SLICE-006 — XML Parser

## Статус
- Версия: 2
- Дата: 2026-01-10
- Слайс: SLICE-006
- Статус: **Verified** (UnitDone PASS)
- Step: 33
- Верификация: `docs/reports/VERIFY-SLICE-006-2026-01-10.md`

## Назначение
Micro-spec поведения XML парсера из upstream Chainsaw для портирования в C++ (MOD-0007 `formats/xml`).

## Источники (FACTS)

### Первичные источники

| Компонент | Файл | Строки | Описание |
|-----------|------|--------|----------|
| XML Parser | `upstream/chainsaw/src/file/xml.rs` | 1-56 | Парсинг XML файлов |
| Reader (XML ветка) | `upstream/chainsaw/src/file/mod.rs` | 207-227 | Выбор XmlParser по расширению |
| Reader (fallback) | `upstream/chainsaw/src/file/mod.rs` | 291-294 | XML в fallback порядке (позиция 4) |
| Cargo.toml | `upstream/chainsaw/Cargo.toml` | 31 | `quick-xml = { version = "0.38", features = ["serialize"] }` |

### Вторичные источники (использование)
| Команда | Файл | Как используется |
|---------|------|------------------|
| dump | main.rs | Reader::load → documents (XML как Document::Xml) |
| search | main.rs | Reader::load → documents → filtering |
| hunt | main.rs | Reader::load → documents → rules matching |

---

## Контракт XML Parser (xml.rs)

### Тип Xml (xml.rs:12)
```rust
pub type Xml = Json; // serde_json::Value
```

**FACT-001:** XML представляется как serde_json::Value — тот же тип что и JSON.
**FACT-002:** Document::Xml содержит serde_json::Value, а не native XML структуру.

### Parser struct (xml.rs:14-16)
```rust
pub struct Parser {
 pub inner: Option<Json>,
}
```

**FACT-003:** Parser хранит распарсенный JSON (Option для take-семантики).

### Parser::load (xml.rs:18-24)
```rust
pub fn load(path: &Path) -> crate::Result<Self> {
 let file = File::open(path)?;
 let reader = BufReader::new(file);
 let xml = quick_xml::de::from_reader(reader)?;
 Ok(Self { inner: Some(xml) })
}
```

**FACT-004:** XML файл читается целиком через BufReader.
**FACT-005:** Используется `quick_xml::de::from_reader` для десериализации XML → JSON.
**FACT-006:** Десериализация через serde — XML маппится на serde_json::Value.
**FACT-007:** При ошибке парсинга возвращается ошибка quick_xml (через anyhow).

### Parser::parse (xml.rs:26-34)
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

**FACT-008:** Если результат парсинга — JSON массив, итерируется по элементам.
**FACT-009:** Если результат — не массив, возвращается один документ.
**FACT-010:** После первого вызова `parse` — `inner` становится None (take-семантика).

### ParserIter (xml.rs:37-55)
```rust
struct ParserIter(Option<IntoIter<Json>>);

impl Iterator for ParserIter {
 type Item = Result<Json, Error>;

 fn next(&mut self) -> Option<Self::Item> {
 match &mut self.0 {
 Some(i) => i.next.map(Ok),
 None => None,
 }
 }

 fn size_hint(&self) -> (usize, Option<usize>) {
 match &self.0 {
 Some(i) => i.size_hint,
 None => (0, Some(0)),
 }
 }
}
```

**FACT-011:** Итератор всегда возвращает Ok(json) — ошибки возникают только при load.
**FACT-012:** size_hint корректно делегируется внутреннему итератору.

---

## Контракт Reader (для XML)

### Выбор по расширению (mod.rs:207-227)
```rust
"xml" => {
 let parser = match XmlParser::load(file) {
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
 Ok(Self { parser: Parser::Xml(parser) })
}
```

**FACT-013:** Расширение `.xml` выбирает XmlParser.
**FACT-014:** Формат ошибки: `[!] failed to load file '<path>' - <error>\n`
**FACT-015:** При `skip_errors=true` возвращается Parser::Unknown.

### Fallback порядок (mod.rs:291-294)
```rust
} else if let Ok(parser) = XmlParser::load(file) {
 return Ok(Self { parser: Parser::Xml(parser) });
```

**FACT-016:** При `load_unknown=true` XML пробуется на позиции 4 (после EVTX, MFT, JSON).

### Reader::documents для XML (mod.rs:401-402)
```rust
Parser::Xml(parser) => Box::new(parser.parse.map(|r| r.map(Document::Xml)))
```

**FACT-017:** Документы оборачиваются в Document::Xml.

### Reader::kind для XML (mod.rs:428)
```rust
Parser::Xml(_) => Kind::Xml,
```

**FACT-018:** Возвращает Kind::Xml.

---

## quick_xml::de — XML→JSON маппинг

### Ключевые особенности quick_xml serde deserializer

**FACT-019:** quick_xml с feature `serialize` позволяет десериализовать XML в произвольные serde-совместимые структуры.

**FACT-020:** При десериализации в serde_json::Value применяются следующие правила:
- XML элементы → JSON объекты
- XML атрибуты → поля с префиксом `@` (настраивается)
- XML текстовое содержимое → поле `$text` или `$value`
- Повторяющиеся элементы с одинаковым именем → массивы

**FACT-021:** Конкретное поведение зависит от версии quick_xml и настроек десериализатора. В Chainsaw используется дефолтный `from_reader` без дополнительных настроек.

### Комментарий в коде (xml.rs:9-10)
```rust
// NOTE: Because we just deserialize into JSON, this looks pretty much the same as the JSON
// implementation. Maybe in time we will parse it differently...
```

**FACT-022:** Авторы отмечают, что XML парсер структурно идентичен JSON парсеру — только способ загрузки отличается.

---

## Критичные инварианты

### INV-001: XML → JSON представление
- XML файл парсится и конвертируется в serde_json::Value
- Результат хранится как обычный JSON документ
- Итерация идентична JSON: массив → элементы, иначе → один документ

### INV-002: Extension-first выбор
- `.xml` расширение → XmlParser
- Без magic bytes анализа

### INV-003: Fallback позиция
- При load_unknown=true XML на позиции 4 после EVTX, MFT, JSON

### INV-004: Ошибки при load, не при итерации
- Все ошибки парсинга возникают в load
- parse всегда возвращает Ok(json)

---

## Зависимости слайса

### Входные зависимости
| ID | Название | Статус |
|----|----------|--------|
| SLICE-005 | Reader Framework + JSON | Done |

### Исходящие зависимости (использует SLICE-006)
| ID | Название | Статус |
|----|----------|--------|
| SLICE-013 | Dump Command | Backlog |

---

## Риски

### RISK-0026: Нет golden runs для XML
- **Статус:** Open
- **Влияние:** Нет эталонных данных для верификации 1:1
- **Mitigation:** Создать синтетические XML фикстуры и unit-тесты

### Новый риск: XML→JSON маппинг

**RISK-NEW-001:** Различия в XML→JSON конверсии между quick_xml и C++ библиотекой
- **Вероятность:** Medium
- **Влияние:** High (может сломать детекты на XML документах)
- **Plan:** Создать тесты на конкретных XML примерах, сравнить JSON структуру

---

## Выбор XML библиотеки для C++

### Контекст
ADR-0009 оставляет выбор XML библиотеки как **Pending**. Варианты:
- pugixml
- tinyxml2
- expat + собственная конверсия

### Рекомендация: pugixml

**Обоснование:**
1. **Популярность:** одна из самых используемых XML библиотек в C++
2. **Простота:** DOM API, легко конвертировать в Value
3. **Лицензия:** MIT (совместима с проектом)
4. **Header-only:** опционально, упрощает интеграцию
5. **Производительность:** достаточная для задач Chainsaw

### План интеграции
1. Добавить pugixml как зависимость (через CMake FetchContent или vendoring)
2. Создать функцию конверсии `pugixml::xml_node → chainsaw::Value`
3. Реализовать XmlReader аналогично JsonReader
4. Протестировать на синтетических XML файлах

---

## Требуемые тесты

### Upstream тесты
- **Нет прямых тестов** для XML parser в upstream
- Нет XML файлов в fixtures

### Unit-тесты (новые для C++)

| ID | Описание | Приоритет |
|----|----------|-----------|
| TST-XML-001 | Простой XML элемент → JSON объект | High |
| TST-XML-002 | XML с атрибутами → JSON с @-полями | High |
| TST-XML-003 | XML с вложенными элементами → вложенные объекты | High |
| TST-XML-004 | XML массив (повторяющиеся элементы) → JSON массив | High |
| TST-XML-005 | XML текстовое содержимое → $text поле | High |
| TST-XML-006 | Невалидный XML → ошибка load | High |
| TST-XML-007 | Пустой XML файл → ошибка или пустой документ | Medium |
| TST-XML-008 | Reader::open выбирает XML по расширению | High |
| TST-XML-009 | Reader::kind возвращает Xml | High |
| TST-XML-010 | XML результат-массив → несколько документов | Medium |
| TST-XML-011 | XML результат-объект → один документ | Medium |
| TST-XML-012 | skip_errors=true → пустой Reader при ошибке | Medium |
| TST-XML-013 | load_unknown=true fallback (XML на позиции 4) | Low |

### XML фикстуры для создания

| Файл | Содержимое | Тест |
|------|------------|------|
| `simple.xml` | `<root><item>value</item></root>` | TST-XML-001, TST-XML-003 |
| `attributes.xml` | `<root attr="val"><item id="1"/></root>` | TST-XML-002 |
| `array.xml` | `<root><item>a</item><item>b</item></root>` | TST-XML-004, TST-XML-010 |
| `text.xml` | `<root>text content</root>` | TST-XML-005 |
| `invalid.xml` | `<root><unclosed>` | TST-XML-006 |
| `empty.xml` | `` или `<root/>` | TST-XML-007 |
| `complex.xml` | Вложенная структура с атрибутами и массивами | TST-XML-003, TST-XML-004 |

---

## C++ API (To-Be контракт)

### Из существующей архитектуры (reader.hpp)

```cpp
namespace chainsaw::io {
 // DocumentKind::Xml уже определён

 // XmlReader — аналог JsonReader
 class XmlReader: public Reader {
 public:
 explicit XmlReader(std::filesystem::path path);
 bool load; // Парсинг XML → Value

 bool next(Document& out) override;
 bool has_next const override;
 DocumentKind kind const override;
 const std::filesystem::path& path const override;
 const std::optional<ReaderError>& last_error const override;
 };

 /// Создать XML Reader
 std::unique_ptr<Reader> create_xml_reader(
 const std::filesystem::path& path,
 bool skip_errors);
}
```

### Функция конверсии XML → Value

```cpp
namespace chainsaw::io {
 /// Конвертировать pugixml узел в Value
 /// @param node XML узел (pugixml)
 /// @return Value представление узла
 Value xml_node_to_value(const pugi::xml_node& node);

 /// Конвертировать XML документ в Value
 /// @param doc XML документ (pugixml)
 /// @return Value представление документа
 Value xml_document_to_value(const pugi::xml_document& doc);
}
```

---

## S1-S4 Оценка

| Ось | Оценка | Обоснование |
|-----|--------|-------------|
| S1 (ширина контуров) | Low | Один модуль formats/xml |
| S2 (сложность проверки) | Medium | Нет upstream тестов, нужны синтетические |
| S3 (unknowns) | Medium | Выбор библиотеки pending (ADR-0009), XML→JSON маппинг |
| S4 (платформенность) | Low | pugixml кроссплатформенный |

**Итог:** допустимо к работе (только одна ось Medium)

---

## Критерии UnitReady ✓

- [x] Micro-spec ссылается на первичные источники (код Rust xml.rs)
- [x] Определён полный набор проверок (TST-XML-001..013)
- [x] Dependencies/unknowns оценены (SLICE-005 Done, библиотека pending)
- [x] Правило гранулярности соблюдено (max одна ось High)

---

## Ссылки
- Rust код XML: `upstream/chainsaw/src/file/xml.rs`
- Rust код Reader: `upstream/chainsaw/src/file/mod.rs:207-227, 291-294`
- ADR парсеры: `docs/adr/ADR-0009-forensic-parsers-strategy.md`
- BACKLOG: `docs/backlog/BACKLOG-0001-porting-backlog.md` (SLICE-006)
- RISK:
- C++ Reader: `cpp/src/io/reader.cpp`, `cpp/include/chainsaw/reader.hpp`
