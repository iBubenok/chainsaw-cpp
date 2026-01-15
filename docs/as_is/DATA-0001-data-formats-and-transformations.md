# DATA-0001 — Реестр форматов данных и трансформаций Chainsaw (Rust) (As‑Is)

## Статус
- Версия: 1
- Статус: Draft (As‑Is)
- Назначение: зафиксировать **форматы входов/выходов** и ключевые **инварианты трансформаций** в upstream Chainsaw (Rust), влияющие на наблюдаемое поведение.
- Single Source of Truth для As‑Is описания форматов данных на фазе As‑Is.

## Метод и источники
FACTS берутся из:
- upstream исходников `chainsaw.zip`, прежде всего `chainsaw/src/*`.
- upstream тестов/фикстур `chainsaw/tests/*` — как **наблюдаемое поведение** (golden‑подобные примеры) форматов вывода и сериализации.
- датасетов `sigma.zip` и `EVTX-ATTACK-SAMPLES.zip` — как подтверждение реальных расширений/структур входных данных.

Связанные документы (не дублируются):
- CLI‑контракт: `docs/as_is/CLI-0001-chainsaw-cli-contract.md`.
- Реестр функциональности: `docs/as_is/FEAT-0001-feature-inventory.md`.
- As‑Is архитектура: `docs/as_is/ARCH-0001-rust-as-is-architecture.md`.
- Критерии 1:1: `docs/governance/GOV-0002-equivalence-criteria.md`.

## Область охвата
В документе фиксируется:
- поддерживаемые **форматы форензик‑артефактов** (EVTX/JSON/JSONL/XML/HVE/MFT/ESEDB) и правила их выбора;
- форматы **правил** (Chainsaw YAML / Sigma YAML) и **конфигураций** (mappings YAML, regex‑файл);
- форматы **вывода** (таблица/лог/JSON/JSONL/YAML/CSV) и ключевые инварианты сериализации;
- внутренние представления/кэши, влияющие на наблюдаемое поведение (bincode, cache‑to‑disk).

В документе **не** фиксируется:
- выбор C++ библиотек/форматов To‑Be;
- «улучшения» форматов, нормализация вывода или изменение сообщений об ошибках.

---

## 1) Термины и внутренние представления

### 1.1. Document / Kind (тип артефакта)
В Rust коде `chainsaw/src/file/mod.rs` определены:
- `file::Document` — перечисление реальных «документов» после парсинга (EVTX/HVE/JSON/MFT/XML/ESEDB).
- `file::Kind` — тип входного артефакта и основа для выбора расширений.

Ключевой факт: **выбор парсера завязан на расширение файла**, без `libmagic`.
- Комментарий в коде: «assume that the file extensions are correct» (`chainsaw/src/file/mod.rs`, `Reader::load`, L111–L113).

### 1.2. Value (внутреннее JSON‑подобное представление)
Chainsaw вводит собственный тип `crate::value::Value`:
- объект хранится в `FxHashMap<String, Value>`;
- поддерживает `to_string` для строковых/числовых/булевых/`null` значений;
- реализует конверсию из/в `serde_json::Value`.

Источник: `chainsaw/src/value.rs`.

### 1.3. Tau (движок условий)
Логика фильтров/детектов опирается на `tau_engine`:
- `tau_engine::Document::find(path)` используется для доступа к полям по строковому пути;
- выражения применяются как к EVTX‑объектам (через Wrapper), так и к JSON‑подобным документам.

Источники: `chainsaw/src/hunt.rs`, `chainsaw/src/search.rs`, `chainsaw/src/cli.rs`.

---

## 2) Входные форматы форензик‑артефактов (dump/hunt/search)

### 2.1. Реестр форматов (сводная таблица)

| Формат | Kind | Расширения, которые считает «валидными» | Как выбирается парсер | Выход парсера (документ) |
|---|---:|---|---|---|
| Windows Event Log | Evtx | `.evt`, `.evtx` | по расширению (`Reader::load`) | `serde_json::Value` (структура `Evtx { data, timestamp }`) |
| JSON | Json | `.json` | по расширению | `serde_json::Value` (объект или элементы массива) |
| JSON Lines | Jsonl | `.jsonl` | по расширению | **внутри `Document::Json`** (каждая строка → JSON) |
| XML | Xml | `.xml` | по расширению | `serde_json::Value` (результат XML→JSON десериализации) |
| Registry Hive | Hve | `.hve` | по расширению | `serde_json::Value` (каждая запись hive сериализуется в JSON) |
| NTFS MFT | Mft | `.mft`, `.bin`, имя файла `$MFT` | по расширению или edge‑case `$MFT` | `serde_json::Value` (записи MFT) |
| ESEDB | Esedb | `.dat`, `.edb` | по расширению | `serde_json::Value` (каждая строка таблицы → объект + поле `Table`) |
| Неизвестный | Unknown | — | `Parser::Unknown` | итератор пуст (нет документов) |

Источник расширений: `chainsaw/src/file/mod.rs`, `Kind::extensions`, L51–L67.

### 2.2. Правила выбора парсера и ошибки (Reader::load)

#### 2.2.1. Extension-first + load_unknown fallback
`Reader::load(file, load_unknown, skip_errors,...)`:
- сначала выбирает парсер **по расширению** (`evt|evtx`, `json`, `jsonl`, `bin|mft`, `xml`, `hve`, `dat|edb`).
- если расширение неизвестно и `load_unknown=true`, пытается парсить в порядке:
 1) EVTX
 2) MFT
 3) JSON
 4) XML
 5) HVE
 6) ESEDB

Источник: `chainsaw/src/file/mod.rs`, L103–L304.

#### 2.2.2. Поведение skip_errors
Если парсер не смог загрузить файл и `skip_errors=true`:
- печатает предупреждение в stderr формата:
 - `[!] failed to load file '<path>' - <error>`
- возвращает `Parser::Unknown`, что приводит к **отсутствию документов**.

Источник: `chainsaw/src/file/mod.rs`, L115–L176 (EVTX/JSON/JSONL), L181–L206 (MFT), L207–L272 (XML/HVE/ESEDB).

Если `skip_errors=false`:
- ошибка пробрасывается (`anyhow::bail!(e)`), что завершает команду.

#### 2.2.3. Сообщения об ошибке для неизвестных типов
При `load_unknown=true` и невозможности распознать тип:
- при `skip_errors=true`: печатается предупреждение
 - `[!] file type is not currently supported - <path>` (если расширение есть)
 - `[!] file type is not known - <path>` (если расширения нет)
- при `skip_errors=false`: команда завершается ошибкой
 - `file type is not currently supported - <path>, use --skip-errors to continue...`
 - `file type is not known - <path>, use --skip-errors to continue...`

Источник: `chainsaw/src/file/mod.rs`, L273–L376.

### 2.3. Формат EVTX/Evt (Windows Event Log)

#### 2.3.1. Структура EVTX → JSON
EVTX парсится через `evtx` crate с параметром `separate_json_attributes(true)`.
Это проявляется в структуре JSON:
- элементы XML‑атрибутов уходят в поля вида `<Element>_attributes`.

Наблюдаемое подтверждение:
- `chainsaw/tests/evtx/clo_search_q_simple_string.txt` — YAML вывод `search -q` содержит поля `Provider_attributes`, `TimeCreated_attributes`, `Event_attributes`.

Код‑источник:
- `chainsaw/src/file/evtx.rs`, `EvtxParser::load` — `ParserSettings::default.separate_json_attributes(true)`.

#### 2.3.2. Обёртки для поиска/фильтрации (Wrapper/WrapperLegacy)
Для доступа к полям по путям используются две обёртки:
- `crate::evtx::Wrapper(Value)` — для `crate::value::Value`.
- `crate::evtx::WrapperLegacy(serde_json::Value)` — для JSON в `search`.

Особенность: алиасы ключей:
- `Event.System.Provider` → `Event.System.Provider_attributes.Name`
- `Event.System.TimeCreated` → `Event.System.TimeCreated_attributes.SystemTime`

Источник: `chainsaw/src/file/evtx.rs`, `WrapperLegacy::find`, L50–L62.

#### 2.3.3. Regex‑поиск по EVTX
EVTX regex‑поиск работает по сериализованной строке JSON:
- документ сериализуется `serde_json::to_string(&self.data)`;
- затем выполняется `replace("\\\\", "\\")`;
- затем `RegexSet` проверяет `match_any` (any/all).

Источник: `chainsaw/src/file/evtx.rs`, `impl Searchable for SerializedEvtxRecord<Json>`, L64–L76.

### 2.4. JSON и JSONL

#### 2.4.1. JSON (.json)
- файл читается целиком в строку;
- парсится `serde_json::from_str`;
- если корень — массив, итератор возвращает каждый элемент как отдельный документ;
- иначе возвращает один документ.

Источник: `chainsaw/src/file/json.rs`, `Parser::load`/`parse`, L8–L46.

#### 2.4.2. JSONL (.jsonl)
- при `load` выполняется «грубая» проверка: читается только **первая строка** и пытается распарситься как JSON;
- затем reader перематывается в начало;
- `parse` читает файл построчно и каждую строку парсит как JSON.

Источник: `chainsaw/src/file/json.rs`, модуль `lines`, L49–L108.

Важная граница: `Reader::load(load_unknown=true)` **не использует** JSONL‑парсер как fallback («слишком generic»).
Источник: `chainsaw/src/file/mod.rs`, L363–L365.

### 2.5. XML (.xml)
XML трансформируется в JSON через `quick_xml::de::from_reader`.
- результат — `serde_json::Value`.

Источник: `chainsaw/src/file/xml.rs`, `Parser::load`/`parse`, L8–L33.

### 2.6. Registry Hive (.hve)

- Hive открывается через `notatin` (`Hive::new`), с опцией `recover_deleted`.
- Если рядом с hive найдены транзакционные логи (`*.LOG`, `*.LOG1`, `*.LOG2`), включается восстановление и печатается сообщение:
 - `[+] Loading the hive <path> with the transaction logs...`
- Если создание Hive с recovery не удалось — печатается предупреждение и повторяется открытие без recovery.

Источник: `chainsaw/src/file/hve/mod.rs`, L35–L108.

Трансформация:
- итератор `hive.iter`;
- каждая запись сериализуется в JSON (`serde_json::to_value(r)`).

Источник: `chainsaw/src/file/hve/mod.rs`, `parse`, L110–L120.

Граница: формат `Kind::Hve` по расширениям включает **только** `.hve`.
Для hive без расширения (например `SOFTWARE`, `SYSTEM`) в `dump/hunt/search` требуется `--load-unknown`.
Источник: `chainsaw/src/file/mod.rs`, `Kind::extensions`, L55.

### 2.7. MFT (.mft/.bin/$MFT)

- Парсинг через `mft` crate.
- Edge case: файл **без расширения** с именем `$MFT` распознаётся отдельно.

Источник: `chainsaw/src/file/mod.rs`, L327–L334.

Опционально: извлечение ADS/streams в файлы.
Ключевые инварианты:
- если выходной путь уже существует — команда завершается ошибкой:
 - `Data stream output path already exists: <path>. Exiting out of precaution.`
- имя файла stream включает:
 - исходный путь с заменой разделителей на `_`,
 - **случайный** суффикс из 6 байт в hex,
 - номер и имя потока,
 - расширение `.disabled`.

Источник: `chainsaw/src/file/mft.rs`, `write_stream_to_file`, L45–L120.

### 2.8. ESEDB (.dat/.edb)

- Парсинг через `libesedb`;
- итерация по таблицам/колонкам/записям;
- каждая запись превращается в `HashMap<String, serde_json::Value>` и дополняется полем `Table`.

Источник: `chainsaw/src/file/esedb/mod.rs`, `parse`, L12–L131.

Инварианты преобразования типов:
- `DateTime` → RFC3339 в UTC с точностью до секунд (`SecondsFormat::Secs`, `use_z=true`).
Источник: `chainsaw/src/file/esedb/mod.rs`, L54–L61.

Наблюдаемое подтверждение формата вывода:
- `chainsaw/tests/srum/analysis_srum_database_json.txt` — JSON массив объектов с полями `Table`, `TableName`, `TimeStamp` и др.

---

## 3) Форматы правил и конфигураций

### 3.1. Общие правила загрузки rules (расширения)
Rules загружаются функцией `rule::load(path, kind)`:
- поддерживаются только расширения `.yml` и `.yaml`;
- иначе — ошибка: `rule file is invalid, only yaml is supported`.

Источник: `chainsaw/src/rule/mod.rs`, `load`, L65–L76.

### 3.2. Chainsaw rules (YAML)

Формат:
- YAML документ со структурой `rule::chainsaw::Rule`.
- ключевые поля: `name`, `group`, `kind`, `level`, `status`, `fields`, `filter`.
- `fields` описывают отображение: откуда читать (`from`), имя/видимость, каст/контейнер (JSON или KV‑строка).

Источник: `chainsaw/src/rule/chainsaw.rs` (структуры `Rule`, `Field`, `Container`, `Format`).

Наблюдаемый пример:
- `chainsaw/rules/mft/mimikatz_mft.yml` — содержит `fields` и `filter`.

### 3.3. Sigma rules (YAML)

#### 3.3.1. Разделение multi-doc
Sigma rule файл делится regex‑ом `---\s*\n` на «части».
- каждая часть парсится в `Sigma` через `serde_yaml::from_str`;
- части, которые не распарсились, **пропускаются** (`filter_map(|p|....ok)`).

Источник: `chainsaw/src/rule/sigma.rs`, `load`, L780–L792.

Подтверждение через тест:
- `chainsaw/tests/convert.rs` использует тот же regex для сравнения expected output.

#### 3.3.2. Rule collections
Если в первом документе присутствует `action` — правило трактуется как «коллекция»:
- первая часть задаёт базовый контекст (`base` detection);
- последующие части добавляют/переопределяют `detection` и condition.

Источник: `chainsaw/src/rule/sigma.rs`, `load`, L814–L854.

Наблюдаемый пример:
- вход: `chainsaw/tests/convert/sigma_collection.yml`
- выход: `chainsaw/tests/convert/sigma_collection_output.yml` (2 документа результата).

#### 3.3.3. Трансформация паттернов
Пример из теста `sigma_simple`:
- вход: `CommandLine|contains: [' -Nop ']`
- выход: `CommandLine: ['i* -Nop *']` (добавлен `i*` префикс и `*` wildcard‑суффикс).

Наблюдаемое подтверждение:
- `chainsaw/tests/convert/sigma_simple.yml` → `chainsaw/tests/convert/sigma_simple_output.yml`.

### 3.4. Mapping files (YAML) для Sigma (hunt)

Mapping файл — YAML, который загружается в `hunt::load_mappings(mapping_path)`:
- парсится как `Mapping` через `serde_yaml::from_reader`;
- при ошибке парсинга: `Provided mapping file is invalid - <err>`;
- при ошибке чтения файла: `Error loading specified mapping file - <err>`.

Источник: `chainsaw/src/hunt.rs`, `load_mappings`, L157–L196.

Ключевые элементы mapping:
- `rules: sigma` (chainsaw rules mappings не поддерживаются: ошибка `Chainsaw rules do not support mappings`).
- `kind: evtx|json|...` (если `jsonl`, то приводится к `json`).
- `groups:` — список групп (имя группы + отображаемые поля + фильтр и timestamp‑поле).
- `extensions:` — список расширений с возможными preconditions.

Источник: `chainsaw/src/hunt.rs`, `load_mappings`, L204–L256.

Наблюдаемые примеры:
- `chainsaw/mappings/sigma-event-logs-all.yml`
- `chainsaw/mappings/sigma-event-logs-legacy.yml`

### 3.5. Regex file (analyse shimcache)

`analyse shimcache` может принимать файл regex‑паттернов.
Формат:
- текстовый файл;
- каждый паттерн — отдельная строка;
- читается `BufReader::lines`.

Источник: `chainsaw/src/main.rs`, `AnalyseCommand::Shimcache`, L1061–L1076.

---

## 4) Внутренние форматы и кэширование

### 4.1. Bincode сериализация Value (hunt)
В `hunt` данные документа хранятся как `Vec<u8>`:
- `bincode::serde::encode_to_vec(&value, bincode::config::standard)`.

Источник: `chainsaw/src/hunt.rs`, L1046–L1052.

Декодирование выполняется, например, при формировании CSV/лог‑вывода:
- `bincode::serde::decode_from_slice::<Value, _>(...)`.

Источник: `chainsaw/src/cli.rs`, `print_csv` L896–L915, `print_log` L314–L332.

### 4.2. Cache-to-disk (hunt --jsonl --cache-to-disk)

При включённом `cache_to_disk`:
- создаётся временный файл (`NamedTempFile` в `main.rs`, см. CLI-0001);
- для каждого документа, у которого есть `hits`, сериализуется JSON‑строка (`serde_json::to_string(&Json::from(value))`)
- строка **пишется без разделителя/перевода строки** в файл;
- для документа запоминаются `offset` и `size`.

Источник: `chainsaw/src/hunt.rs`, L1022–L1041.

Инвариант формата кэша: файл = **конкатенация** JSON объектов, доступ к ним только по (offset,size).

---

## 5) Форматы вывода (наблюдаемое поведение)

> Детали CLI флагов (какие команды поддерживают какие форматы) — в `CLI-0001`.

### 5.1. YAML (stdout)
Используется в `dump` и `search` по умолчанию:
- перед каждым документом печатается строка `---`.

Наблюдаемое подтверждение:
- `chainsaw/tests/evtx/clo_search_q_simple_string.txt`.

Код‑источник (search):
- `chainsaw/src/main.rs`, `Command::Search`, печать `---` + `cs_print_yaml!`.

### 5.2. JSON (stdout)

#### 5.2.1. dump/search
- печатается один JSON массив (`[`... `]`), элементы разделяются запятыми.

Наблюдаемое подтверждение:
- `chainsaw/tests/evtx/clo_search_qj_simple_string.txt`.

Источник (search): `chainsaw/src/main.rs`, L984–L1038.

#### 5.2.2. hunt --json
`hunt` выводит JSON массив элементов типа `Detection`.
`Detection` содержит:
- `group`, `id`, `name`, `authors`, `description`, `level`, `status`, `references`, `tags`, `attacks`...
- `kind` (flatten): `individual|aggregate|cached` + `document(s)`.

Источник: `chainsaw/src/cli.rs`, `Detection`/`Kind`, L980–L1128.

### 5.3. JSONL (stdout)
- каждый документ/детекция печатается как JSON на отдельной строке.

Наблюдаемое подтверждение (search):
- `chainsaw/tests/evtx/clo_search_q_jsonl_simple_string.txt`.

Ограничение (hunt): `--cache-to-disk` допустим только при `--jsonl`.
Источник: `chainsaw/src/main.rs`, `Command::Hunt`, L615–L620.

### 5.4. Табличный вывод (stdout)
`hunt` по умолчанию печатает таблицы (prettytable) по группам.
Наблюдаемое подтверждение:
- `chainsaw/tests/evtx/clo_hunt_r_any_logon.txt`.

Особенность: в stdout присутствуют ANSI escape‑последовательности для цвета.

Источник: `chainsaw/src/cli.rs`, `print_detections` + `cs_print_table!` (см. архитектуру `ARCH-0001`).

### 5.5. log‑формат (stdout)
`hunt --log` печатает строки вида:
- `<timestamp> | <c|σ> | <rule name> | <count> | <values...>`

Источник: `chainsaw/src/cli.rs`, `print_log`, L244–L352.

### 5.6. CSV (файлы)

#### 5.6.1. hunt --csv --output <dir>
- создаётся директория output;
- создаётся **отдельный CSV файл на группу**;
- имя файла: `<group>.csv`, где пробелы заменены на `_`, и всё в нижнем регистре.
- первая строка: `timestamp,detections,path,<fields...>`.
- если для группы нет `fields`, то вместо полей добавляется одна колонка `data` (сырой JSON документа).

Источник: `chainsaw/src/cli.rs`, `print_csv`, L783–L855.

#### 5.6.2. analyse shimcache
`analyse shimcache` печатает CSV (или таблицу в stdout с усечением колонок).
Источник: `chainsaw/src/cli.rs`, `print_shimcache_analysis_csv`, L569–L773.

#### 5.6.3. analyse srum
`analyse srum`:
- в режиме `--stats-only` печатает таблицу (ascii) со сводкой.
- без `--stats-only` печатает JSON массив.

Наблюдаемое подтверждение:
- `chainsaw/tests/srum/analysis_srum_database_table_details.txt`
- `chainsaw/tests/srum/analysis_srum_database_json.txt`

---

## 6) Границы и «острые углы», влияющие на 1:1

1) **Опора на расширения** (без libmagic): неверное расширение → другой парсер/ошибка/тишина.
 - Источник: `chainsaw/src/file/mod.rs`, L111–L113.

2) **Не‑детерминизм порядка вывода** возможен там, где используется параллелизм (например, `search` обходит `files.par_iter`).
 - Источник: `chainsaw/src/main.rs`, `Command::Search`, L990+.

3) **cache-to-disk**: кэш‑файл без разделителей между JSON объектами.
 - Источник: `chainsaw/src/hunt.rs`, L1023–L1041.

4) **MFT data streams**: имена файлов включают случайный суффикс.
 - Источник: `chainsaw/src/file/mft.rs`, `generate_random_hex_string`, L39–L43.
 - Связанный риск: `RISK-0025`.

5) **Фильтрация по времени в `search` зависит от формата timestamp**:
 - для `Kind::Evtx` ожидается строка вида `%Y-%m-%dT%H:%M:%S%.6fZ` (микросекунды),
 - для остальных — `%Y-%m-%dT%H:%M:%S%.fZ`.
 Диапазон задаётся как *(from,to)*, границы отсекаются нестрого:
 - если `timestamp <= from` — документ пропускается,
 - если `timestamp >= to` — документ пропускается.
 Источник: `chainsaw/src/search.rs`, `Iter::next`, L48–L108.

6) **Расширение `.dat` трактуется как ESEDB**, что конфликтует с практикой хранения некоторых hive в `.dat`.
 Для hive в `dump/hunt/search` требуется либо `.hve`, либо `--load-unknown`.
 Источник: `chainsaw/src/file/mod.rs`, L229–L272 (hve/edb/dat выбор), L55.

7) **Sigma loader пропускает нераспарсившиеся YAML‑секции без ошибки**, что может приводить к «пустому набору правил».
 Источник: `chainsaw/src/rule/sigma.rs`, `load`, L780–L792.

8) **Некоторые парсеры используют `expect(...)`**, что при ошибке приводит к panic (аварийному завершению).
 Пример: ESEDB парсер активно использует `.expect("could not...")`.
 Источник: `chainsaw/src/file/esedb/mod.rs` (множественные `expect`).

