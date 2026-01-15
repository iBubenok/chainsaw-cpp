# ARCH-0001 — Архитектурная карта Chainsaw (Rust) (As‑Is)

## Статус
- Версия: 1
- Статус: Draft (As‑Is)
- Назначение: зафиксировать **As‑Is архитектуру** upstream Chainsaw (Rust) на уровне компонентов/границ/потоков данных.
- Single Source of Truth для As‑Is архитектуры на фазе As‑Is.

## Метод и источники
FACTS берутся из:
- upstream исходников `chainsaw.zip`, прежде всего `chainsaw/src/*`.
- (опционально) тестов `chainsaw/tests/*` — только как подтверждение потоков вызовов и точек наблюдаемости.

Связанные документы (не дублируются):
- CLI‑контракт: `docs/as_is/CLI-0001-chainsaw-cli-contract.md`.
- Реестр функциональности: `docs/as_is/FEAT-0001-feature-inventory.md`.
- Критерии 1:1: `docs/governance/GOV-0002-equivalence-criteria.md`.
- Baseline upstream: `docs/baseline/BASELINE-0001-upstream.md`.

## Область охвата
В документе фиксируется:
- декомпозиция на основные модули и их ответственность;
- ключевые потоки данных по основным CLI командам (dump/hunt/search/analyse);
- точки платформенной специфики (**Windows vs non‑Windows**);
- кросс‑сечения (вывод, параллелизм, обработка ошибок) на уровне, достаточном для портирования.

В документе **не** фиксируется:
- To‑Be архитектура порта;
- выбор C++ библиотек;
- оптимизации/улучшения поведения.

---

## 1) Layout пакета: бинарь + библиотека

### 1.1. Библиотечный crate (`chainsaw/src/lib.rs`)
`lib.rs` экспортирует ключевые подсистемы как публичный API:
- анализаторы `ShimcacheAnalyser` / `SrumAnalyser`;
- слой чтения/типизации артефактов: `file::{get_files, Reader, Document, Kind}`;
- движки `Hunter/HunterBuilder` и `Searcher/SearcherBuilder`;
- слой правил `rule::{load, lint, sigma, Filter, Kind/Level/Status}`;
- глобальный writer и формат вывода `write::{Writer, Format, set_writer, writer}`.

Доказательства:
- `chainsaw/src/lib.rs:6–15` (публичные `pub use...`) и `chainsaw/src/lib.rs:16–26` (список модулей).

### 1.2. Бинарный crate (`chainsaw/src/main.rs`)
`main.rs`:
- подключает макросы вывода через `#[macro_use] extern crate chainsaw;`;
- парсит CLI (clap derive), инициализирует глобальные настройки (`--num-threads`, banner, writer);
- маршрутизирует выполнение по подкомандам (`dump`, `hunt`, `search`, `lint`, `analyse...`).

Доказательства:
- `chainsaw/src/main.rs:1–20` (подключение crate + imports);
- `chainsaw/src/main.rs:401–409` (точка входа `run`, `Args::parse`, настройка rayon thread pool);
- `chainsaw/src/main.rs:408+` (dispatch `match args.cmd {... }`).

---

## 2) Основные компоненты и границы ответственности

Ниже — карта модулей уровня “компонент” (в терминах Rust‑модулей), без выбора будущих реализаций в C++.

### 2.1. `file/*` — слой чтения и парсинга артефактов
Ответственность:
- рекурсивный сбор файлов из путей (`get_files`);
- определение типа артефакта по расширению;
- выдача документов (records) как итератора.

Ключевые типы/контракты:
- `enum Document { Evtx, Hve, Json, Xml, Mft, Esedb }` — унифицированное представление одной “единицы документа” для downstream (`hunt/search/dump`).
- `struct Reader` и `Reader::load(path, load_unknown, skip_errors)` — выбирает парсер по расширению и возвращает reader.
- `Reader::documents` — поток документов (`Documents`), который может отдавать ошибки по мере итерации.

Доказательства:
- `chainsaw/src/file/mod.rs:58–79` (Document enum + Reader + Documents);
- `chainsaw/src/file/mod.rs:150–220` (логика `Reader::load`, выбор парсера по расширению);
- `chainsaw/src/file/mod.rs:307–394` (логика `get_files` и рекурсивный обход директорий).

Граница ответственности:
- `file/*` **не** знает про правила и детекты; он только читает/парсит и типизирует документы.

### 2.2. `rule/*` + `ext/tau.rs` — загрузка правил и Tau‑выражения
Ответственность:
- загрузка YAML‑правил (Chainsaw и Sigma);
- (для Sigma) трансформация в Tau IR и оптимизация выражений;
- линтинг правил;
- предоставление метода `Rule::solve(document)` для проверки срабатывания.

Ключевые моменты:
- `enum Rule { Chainsaw(..), Sigma(..) }` — единый контейнер;
- `load(kind, path, kinds/levels/statuses)` фильтрует правила по метаданным;
- `Rule::solve` делегирует в `tau_engine::solve`/`tau_engine::core::solve`.

Доказательства:
- `chainsaw/src/rule/mod.rs:23–88` (Rule enum + методы доступа + `solve`);
- `chainsaw/src/rule/mod.rs:206–268` (функция `load`, ветвление Chainsaw/Sigma + optimisers);
- `chainsaw/src/ext/tau.rs:142–205` (парсинг `key: value` в Tau‑Expression и тип‑enforcement).

Граница ответственности:
- `rule/*` оперирует правилами и их логикой, но **не** управляет обходом файлов/документов и выводом.

### 2.3. `hunt.rs` — движок детекта (Hunt)
Ответственность:
- объединить: правила (`rule`) + mapping‑файлы + слой чтения документов (`file::Reader`);
- вычислить срабатывания (hits) и агрегированные детекты;
- реализовать фильтрацию по времени (from/to/timestamp), preprocess (BETA), skip_errors и т.д.

Ключевые типы:
- `HunterBuilder` → `Hunter` (настройка правилами/мэппингами/флагами);
- `Detections { hits, kind }` и `Kind::{Individual, Aggregate, Cached}`.

Особенность данных для вывода:
- `hunt::Document` хранит полезную нагрузку как `Vec<u8>` (bincode‑серилизация внутреннего `value::Value`), а при сериализации в JSON декодирует обратно и конвертирует в `serde_json::Value`.
- При `--cache-to-disk` используется `Kind::Cached`: данные документа кладутся в временный файл как “сырой JSON”, а в структуре хранятся `offset/size` (они не сериализуются).

Доказательства:
- `chainsaw/src/hunt.rs:76–132` (Detections/Kind/Document/RawDocument и сериализация);
- `chainsaw/src/main.rs:666–808` (инициализация Hunter и общий цикл `for file in &files { hunter.hunt(...) }` + выбор формата вывода);
- `chainsaw/src/hunt.rs:736–770` (обработка документов и распределение по hunts/rules);
- `chainsaw/src/hunt.rs:1022–1054` (формирование `Kind::Cached` vs `Kind::Individual` + запись JSON в cache‑file);
- `chainsaw/src/hunt.rs:1062–1100` (агрегации; сортировка timestamps перед выбором первого).

Граница ответственности:
- `hunt.rs` вычисляет детекты и возвращает структуру данных; форматирование/печать результатов вынесены в `cli.rs`.

### 2.4. `search.rs` — поисковый движок по документам
Ответственность:
- построение набора regex‑паттернов (`RegexSet`) и/или Tau‑выражений (`-t/--tau`);
- чтение документов через `Reader` и фильтрация (по regex/Tau/времени);
- выдача результатов как итератора `Hits`.

Ключевые моменты:
- builder собирает `RegexSet` и выражение `Expression::BooleanGroup(And/Or,...)` в зависимости от `--match-any`.
- при включённом фильтре времени парсит timestamp поля в разных форматах (EVTX vs JSON‑подобные документы), затем применяет `from/to` в UTC.

Доказательства:
- `chainsaw/src/search.rs:152–257` (SearcherBuilder::build: RegexSet + сбор Tau выражений + локализация from/to);
- `chainsaw/src/search.rs:38–145` (итерация по документам, фильтр времени и поиск по Tau/regex).

Граница ответственности:
- `search.rs` возвращает “сырые” найденные документы (`serde_json::Value`), а не “детекты”.

### 2.5. `analyse/*` — специализированный анализ (shimcache, srum)
Ответственность:
- отдельные аналитические пайплайны, не зависящие от `hunt/search`:
 - `ShimcacheAnalyser` строит таймлайн shimcache (опционально обогащая amcache).
 - `SrumAnalyser` парсит SRUM ESEDB + SOFTWARE hive и формирует таблицу/JSON.

Доказательства:
- `chainsaw/src/analyse/mod.rs:1–2` (экспорт модулей);
- `chainsaw/src/analyse/shimcache.rs:63–96` (загрузка hive файлов и печать путей);
- `chainsaw/src/analyse/srum.rs:97–117` (загрузка SRUDB.dat и SOFTWARE hive).

### 2.6. `cli.rs` + `write.rs` — вывод, форматирование, прогресс
Ответственность:
- `write.rs`: глобальная настройка writer (формат/quiet/verbose/вывод в файл) и набор макросов печати `cs_print*`, `cs_eprintln*` и т.п.
- `cli.rs`: форматирование “пользовательских” результатов и вспомогательные функции (таблицы/CSV/JSON/JSONL/log/progress bar).

Ключевые особенности:
- writer хранится в `static mut WRITER`, доступ через unsafe (глобальное состояние).
- `cli.rs` содержит OS‑условную компиляцию (Windows vs non‑Windows) для части символов вывода.

Доказательства:
- `chainsaw/src/write.rs:6–13` (глобальный `static mut WRITER`);
- `chainsaw/src/write.rs:63–165` (макросы печати, переключение stdout vs file);
- `chainsaw/src/cli.rs:28–42` (cfg(windows) `RULE_PREFIX` и `TICK_STRINGS`).

---

## 3) Потоки данных по основным командам (end‑to‑end)

### 3.1. Общий контур для `dump/hunt/search`

```text
CLI (clap) → сбор файлов (get_files) → Reader::load → Documents iterator
 │
 ├─ dump: печать каждого документа
 ├─ search: фильтр/матч → печать найденных документов
 └─ hunt: детект/агрегация → печать детекций
```

Базовые функции сбора файлов и чтения документов общие (`file/mod.rs`).

### 3.2. `dump`

Поток:
1) CLI: `Command::Dump {.. }` собирает список файлов через `get_files` и оценивает суммарный размер (`metadata.len`).
2) Для каждого файла вызывает `Reader::load(..)` и итерирует `reader.documents`.
3) В зависимости от `--json/--jsonl` печатает JSON массив/JSONL или YAML‑подобный вывод.

Доказательства:
- `chainsaw/src/main.rs:409–517` (ветка `Command::Dump`).

### 3.3. `hunt`

Поток:
1) CLI загружает правила (Chainsaw + Sigma) через `get_files` + `rule::load` с фильтрами по kind/level/status.
2) Строит `Hunter` через builder (правила + mapping + флаги).
3) Вычисляет набор расширений входных файлов (из правил/мэппингов), затем собирает файлы `get_files`.
4) Для каждого входного файла:
 - (опционально) создаёт `tempfile` для `--cache-to-disk`;
 - вызывает `hunter.hunt(file, &cache)`;
 - накапливает `Detections` и печатает (в режиме `--jsonl` печать делается по мере обработки файла).
5) Финальная печать результата в одном из форматов (табличный/CSV/JSON/log).

Доказательства:
- `chainsaw/src/main.rs:570–809` (загрузка правил, построение Hunter, сбор файлов, цикл обработки и печать);
- `chainsaw/src/hunt.rs:720–770` (применение hunts/rules к документам);
- `chainsaw/src/hunt.rs:1022–1054` + `chainsaw/src/cli.rs:1146–1177` (реализация `--cache-to-disk` и чтение cached JSON при печати jsonl).

### 3.4. `search`

Поток:
1) CLI формирует список путей (включая особую логику “pattern как путь” при `-e/--regex` или `-t/--tau`) и собирает файлы `get_files`.
2) Строит `Searcher` (RegexSet + Tau, фильтры времени и TZ).
3) Параллельно обрабатывает файлы через `files.par_iter.try_for_each(...)`.
4) Каждый hit печатается синхронизированно через `Mutex`, чтобы корректно сформировать JSON массив и обеспечить согласованность счётчика.

Доказательства:
- `chainsaw/src/main.rs:895–1043` (ветка `Command::Search`, включая `files.par_iter` и печать под mutex).

### 3.5. `analyse shimcache` / `analyse srum`

Поток `analyse` выделен:
- вместо `get_files`/`Reader`, подкоманды напрямую работают с конкретными файлами артефактов (hive/ESEDB) и формируют собственный вывод.

Доказательства:
- `chainsaw/src/main.rs:1044+` (ветка `Command::Analyse`);
- `chainsaw/src/analyse/shimcache.rs` / `chainsaw/src/analyse/srum.rs` (реализация анализаторов).

---

## 4) Платформенная специфичность (Windows vs cross‑platform)

### 4.1. Явная OS‑условная компиляция (cfg)
В upstream исходниках условная компиляция по OS обнаружена в `cli.rs`:
- `RULE_PREFIX`:
 - non‑Windows: `"‣"`
 - Windows: `"+"`
- `TICK_STRINGS` для progress spinner:
 - non‑Windows использует Unicode/box‑drawing символы
 - Windows использует ASCII (`"-\\|/"`).

Доказательства:
- `chainsaw/src/cli.rs:28–42`.

### 4.2. Прочие платформо‑зависимые факторы (без cfg)
Даже без `cfg`, вывод и поведение могут зависеть от платформы через:
- форматирование путей (`Path::display`), разделители и абсолютные пути;
- порядок обхода директорий (`fs::read_dir`) — может отличаться между ОС/ФС.

Доказательства:
- `chainsaw/src/file/mod.rs:324–373` (использование `fs::read_dir` без сортировки результатов).

---

## 5) Потенциальные источники недетерминизма (As‑Is наблюдение)

Этот раздел не является “дефектом”; он фиксирует поведение/риски для будущего доказательства 1:1.

1) **Порядок файлов**:
 - `get_files` рекурсивно обходит директории через `fs::read_dir`, не сортируя список `PathBuf`.
 - Следствие: порядок обработки файлов (и, соответственно, порядок результатов) может зависеть от ОС/ФС.

 Доказательство: `chainsaw/src/file/mod.rs:324–373`.

2) **Параллельная обработка `search`**:
 - файлы обрабатываются параллельно (`files.par_iter`);
 - печать синхронизирована mutex’ом, но порядок прихода результатов зависит от планировщика.

 Доказательство: `chainsaw/src/main.rs:988–1034`.

3) **Параллельная обработка документов внутри `hunt`**:
 - внутри `Hunter::hunt` используется `documents.par_bridge`, т.е. документы одного файла могут обрабатываться параллельно.

 Доказательство: `chainsaw/src/hunt.rs:792–798` (использование `par_bridge` в обработке `reader.documents`).

Связанный риск: RISK-0011 в.

---

## 6) UNKNOWN/требует подтверждения запуском

На уровне фиксируются места, где “точный внешний эффект” должен быть подтверждён golden runs, потому что зависит от runtime/окружения:
- точные строки прогресс‑бара/спиннера и их поведение на разных терминалах/ОС;
- фактическая стабильность порядка вывода (см. раздел 5) и наличие/отсутствие нормализации внутри cli‑печати;
- точные форматы timestamp‑парсинга на реальных данных (EVTX/JSON), включая ошибки и режим `--skip-errors`.

---

## 7) Ссылки
- CLI‑контракт: `docs/as_is/CLI-0001-chainsaw-cli-contract.md`.
- Feature inventory: `docs/as_is/FEAT-0001-feature-inventory.md`.
- Критерии 1:1: `docs/governance/GOV-0002-equivalence-criteria.md`.
- Baseline: `docs/baseline/BASELINE-0001-upstream.md`.
