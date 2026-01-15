# DEPS-0001 — Реестр зависимостей Chainsaw (Rust) и их роль (As‑Is)

## Статус
- Версия: 1
- Статус: Draft (As‑Is)
- Назначение: зафиксировать **наблюдаемые зависимости Rust‑версии Chainsaw** (прямые runtime/dev зависимости из `Cargo.toml`, с разрешёнными версиями из `Cargo.lock`) и их роль в поведении.
- Single Source of Truth для As‑Is карты зависимостей на фазе As‑Is.

## Метод и источники
FACTS берутся из:
- `chainsaw/Cargo.toml` — декларации зависимостей и ключевых features.
- `chainsaw/Cargo.lock` — фактические версии и источники (crates.io / git).
- исходников `chainsaw/src/*` и тестов `chainsaw/tests/*` — места использования (файлы/строки).

Связанные документы (не дублируются):
- CLI‑контракт: `docs/as_is/CLI-0001-chainsaw-cli-contract.md`.
- Реестр форматов данных: `docs/as_is/DATA-0001-data-formats-and-transformations.md`.
- As‑Is архитектура: `docs/as_is/ARCH-0001-rust-as-is-architecture.md`.

## Область охвата
Документ фиксирует:
- прямые runtime зависимости из `Cargo.toml` и их роль;
- прямые dev‑зависимости (тестовый harness) и их роль;
- особые случаи: `[patch.crates-io]` (git‑переопределения) и заметные эффекты на воспроизводимость/портирование.

Документ **не** фиксирует:
- выбор библиотек/подходов C++ (To‑Be);
- изменения поведения (в т.ч. «улучшения» ошибок/форматов вывода).

---

## Легенда: значимость и категория портирования

### Значимость (IMPACT)
- **H** — влияет на наблюдаемое поведение: детекты/семантика правил/парсинг форматов/вывод/ошибки.
- **M** — влияет в основном на UX/производительность/детерминизм, но может проявляться в наблюдаемом поведении (например, порядок/прогресс‑бар).
- **L** — вспомогательная/в использовании не обнаружена на базовом срезе.

### Категория портирования (CAT)
Категория — это **класс задачи** для порта (без выбора конкретных C++ библиотек):
- **CAT-A (Парсеры форматов)**: EVTX/HVE/MFT/ESEDB/XML и др.
- **CAT-B (Движок правил/выражений)**: tau‑engine, Sigma‑конверсия, поиск/матчинг.
- **CAT-C (CLI/вывод/терминал)**: CLI‑парсинг, форматирование таблиц/логов/прогресс‑бар.
- **CAT-D (Производительность/детерминизм/кэш)**: параллелизм, внутренние кэши/сериализация.
- **CAT-E (Утилиты/поддержка)**: небольшие вспомогательные функции/структуры.
- **CAT-T (Только тесты)**: зависимости, используемые только в Rust‑тестах.

---

## 1) Прямые runtime‑зависимости (Cargo.toml → Cargo.lock)

Источник списка: `chainsaw/Cargo.toml` (L12–L43) + `[patch.crates-io]` (L52–L55).

> Примечание: версии в колонке **Resolved** — из `Cargo.lock` и отражают фактическую сборку upstream.

| Зависимость (crate) | Resolved (Cargo.lock) | IMPACT | Роль в продукте | Где используется (примеры) | Риски/примечания | CAT |
|---|---|:---:|---|---|---|:---:|
| aho-corasick | 1.1.3 (crates.io) | M | Быстрый множественный поиск подстрок; используется в расширениях tau‑парсера для `contains` (case-insensitive). | `chainsaw/src/ext/tau.rs` L3, L256–L332 | Семантика `contains` для tau‑идентификаторов зависит от построения автомата (DFA + ASCII‑case‑insensitive). | B |
| anyhow | 1.0.100 (crates.io) | H | Единый формат ошибок/контекстов; макросы `anyhow!`, `bail!`, `.context(...)` формируют сообщения об ошибках. | `chainsaw/src/lib.rs` L1–L5; многочисленные `anyhow::bail!` в коде | Формулировки ошибок — часть наблюдаемого поведения (stdout/stderr + exit code), особенно в CLI. | C/E |
| base64 | 0.22.1 (crates.io) | H | Реализация Sigma modifiers `base64`/`base64offset` при конвертации Sigma→tau‑выражения. | `chainsaw/src/rule/sigma.rs` L309–L331 | Важны детали: STANDARD base64 + подрезка строк для `base64offset` (S/E массивы). | B |
| bincode | 2.0.1 (crates.io) | M | Внутренняя сериализация `crate::value::Value` (память/кэш) при `hunt`, в т.ч. для агрегаций; декодирование при выводе. | `chainsaw/src/hunt.rs` L83–L105; `chainsaw/src/cli.rs` L147–L165 | Внешне не является форматом интерфейса, но влияет на режим `--cache-to-disk` и пути обработки (см. DATA-0001). | D |
| bytesize | 2.1.0 (crates.io) | L/M | Человекочитаемое отображение размеров (например, суммарный размер загруженных артефактов). | `chainsaw/src/main.rs` L11–L12, L732–L749 | Наблюдаемо влияет на текстовые сообщения в stderr при загрузке артефактов. | C |
| chrono | 0.4.42 (crates.io) | H | Даты/время: парсинг/сравнение/форматирование RFC3339 в `search`/`hunt`/`analyse`; UTC/Local конверсия. | `chainsaw/src/search.rs` L3–L4, L52–L137; `chainsaw/src/cli.rs` L7 | Важна точность/форматирование `to_rfc3339` и парсинг входных timestamp‑строк (форматы в `search.rs`). | B |
| chrono-tz | 0.10.4 (crates.io) | H | Таймзоны для `--timezone` и локализации timestamp (в `search`/`hunt` вывод). | `chainsaw/src/search.rs` L4, L165–L238; `chainsaw/src/cli.rs` L8 | Ошибки локализации (`failed to localise timestamp`) — наблюдаемы; список таймзон определяется crate. | B |
| clap | 4.5.48 (crates.io) | H | CLI‑парсинг/валидация/генерация help; определяет ошибки аргументов и help‑текст. | `chainsaw/src/main.rs` L16–L319 (derive Parser/Subcommand) | Для 1:1 критично воспроизведение поведения CLI (см. CLI-0001) независимо от реализации. | C |
| crossterm | 0.29.0 (crates.io) | M | Управление выводом в терминал (цвет/стиль) в макросах `cs_*`. | `chainsaw/src/write.rs` L196–L263 | Платформенные различия терминалов; также влияет на то, что часть сообщений окрашивается. | C |
| evtx | 0.8.5 (crates.io) | H | Парсинг EVTX/EVT → JSON, включая `separate_json_attributes(true)` и структуру `_attributes`. | `chainsaw/src/file/evtx.rs` L4–L30 | Критическая зона портирования: структура JSON влияет на поиск/детекты (см. DATA-0001). | A |
| hex | 0.4.3 (crates.io) | L | Прямая роль в коде upstream **не обнаружена** в `chainsaw/src/*` и `chainsaw/tests/*` на текущем срезе. | (нет находок по `hex::` в `src/` и `tests/`) | Возможно, «хвост» после рефакторинга или используется только транзитивно (но тогда не нужен в `[dependencies]`). Требует уточнения. | E |
| indicatif | 0.18.0 (crates.io) | M | Прогресс‑бар для `hunt/search` (в основном stderr); скрывается при `--verbose`/`--quiet`. | `chainsaw/src/cli.rs` L9–L70 | На Windows и не‑Windows разные `tick_chars` и `RULE_PREFIX` (`cfg(windows)`). Может осложнить дифф stdout/stderr. | C |
| lazy_static | 1.5.0 (crates.io) | M | Ленивая инициализация статических структур (в т.ч. regex) для разбора shimcache и Sigma‑модификаторов. | `chainsaw/src/rule/sigma.rs` L265+; `chainsaw/src/file/hve/shimcache.rs` L298+ | Влияет на производительность/инициализацию; функционально обычно прозрачно. | D/E |
| libesedb | 0.2.7 (crates.io) | H | Парсинг ESEDB (SRUM) через libesedb API; конверсия типов (OleTime → RFC3339). | `chainsaw/src/file/esedb/mod.rs` L4–L145 | `libesedb-sys` собирается через `cc` (Cargo.lock содержит `libesedb-sys` + build deps) — повышенный риск кроссплатформенной сборки. | A |
| mft | 0.6.1 (git, ffd9511…) | H | Парсинг NTFS $MFT (через `MftParser`), извлечение data streams и побочные эффекты записи файлов. | `chainsaw/src/file/mft.rs` L6–L213 | Источник зависимости — git через `[patch.crates-io]`. Дополнительно: имена файлов data streams включают `rand::random` (недетерминизм). | A |
| notatin | 1.0.1 (git, 61b2d3d…) | H | Парсинг Windows Registry hives (HVE), включая работу с transaction logs (`.LOG/.LOG1/.LOG2`) и режим recover_deleted. | `chainsaw/src/file/hve/mod.rs` L6–L82 | Источник зависимости — git через `[patch.crates-io]`. Ошибки восстановления печатаются в stderr и влияют на поведение `--skip-errors`. | A |
| once_cell | 1.21.3 (crates.io) | M | Однократная инициализация кэшей в hot‑path `hunt` (map контейнеров для mapped fields). | `chainsaw/src/hunt.rs` L12, L560–L695 | Влияет на производительность/память; функционально должно быть эквивалентно. | D/E |
| prettytable-rs | 0.10.0 (crates.io) | H | Форматирование таблиц и CSV‑вывода (часть CLI‑контракта: таблицы, статистика SRUM, и т.п.). | `chainsaw/src/cli.rs` L10–L70; `chainsaw/src/write.rs` L185–L193 | Для 1:1 требуется воспроизвести формат таблиц/CSV (включая ширины/разделители) там, где они наблюдаемы. | C |
| quick-xml | 0.38.3 (crates.io) | H | Десериализация XML → JSON (команда `dump/search/hunt` по XML файлам). | `chainsaw/src/file/xml.rs` L19–L24 | В сборке присутствует также `quick-xml 0.36.2` (как зависимость `evtx`). Возможны семантические различия между версиями. | A |
| rayon | 1.11.0 (crates.io) | M | Параллельная обработка документов при `hunt` (`par_bridge`, thread pool). | `chainsaw/src/hunt.rs` L13, L796–L820; `chainsaw/src/main.rs` L4, L404+ | Риск недетерминизма порядка/таймингов (см. RISK-0011). Для 1:1 нужен контроль сортировок и устойчивых порядков в выводе. | D |
| rand | 0.9.2 (crates.io) | M | Генерация случайной строки для имён файлов при извлечении MFT data streams (побочный эффект). | `chainsaw/src/file/mft.rs` L170–L173 | Недетерминируемые имена файлов осложняют воспроизводимость и дифф harness при включённом выводе streams. | C/D |
| regex | 1.11.3 (crates.io) | H | RegexSet/Regex для `search`, фильтров и конвертации Sigma (включая ограничения на regex‑синтаксис). | `chainsaw/src/search.rs` L5, L149–L159; `chainsaw/src/rule/sigma.rs` (модификаторы) | Семантика regex (case-insensitive, наборы) критична для 1:1. | B |
| rustc-hash | 2.1.1 (crates.io) | M | Быстрые HashMap/Hasher (FxHashMap/FxHasher) в Value и пайплайне детектов. | `chainsaw/src/value.rs` L1–L18; `chainsaw/src/hunt.rs` L14–L15 | Влияет на производительность и потенциально на порядок ключей (если не сортируется). В коде часто сортируют ключи перед выводом. | D |
| serde | 1.0.228 (crates.io) | H | (Де)сериализация правил/структур; derive‑макросы используются повсеместно. | напр.: `chainsaw/src/rule/mod.rs` L1–L18; `chainsaw/src/hunt.rs` L15–L18 | Семантика (де)сериализации YAML/JSON влияет на трактовку конфигов и формирование вывода. | B |
| serde_json | 1.0.145 (crates.io) | H | JSON DOM, `RawValue` для JSONL/кэша, сериализация результатов (`--json/--jsonl`). | `chainsaw/src/cli.rs` L13; `chainsaw/src/hunt.rs` L19; `chainsaw/src/file/evtx.rs` L6 | Критично для 1:1 форматов вывода (см. DATA-0001). | B |
| serde_yaml | 0.9.34+deprecated (crates.io) | H | YAML парсинг/сериализация: Chainsaw rules, Sigma rules, mappings; также YAML‑вывод (`cs_print_yaml!`). | `chainsaw/src/rule/mod.rs` L5–L18; `chainsaw/src/write.rs` L166–L183 | В Cargo.lock версия помечена `+deprecated`. Нужна фиксация семантики парсинга YAML через тесты/эталонные прогоны. | B |
| smallvec | 1.15.1 (crates.io) | M | Оптимизация хранения небольших наборов hits (`SmallVec<[Hit; 1]>`). | `chainsaw/src/hunt.rs` L20, L90–L96 | Производительность/память; функционально прозрачно. | D |
| tau-engine | 1.14.1 (crates.io) | H | Парсер и исполнение выражений фильтрации/детектов (Chainsaw/Sigma). | `chainsaw/src/rule/mod.rs` L6–L18; `chainsaw/src/search.rs` L7–L9; `chainsaw/src/hunt.rs` L21–L24 | Одна из главных зон сложности 1:1: семантика выражений, типизация, модификаторы, совместимость с Sigma. | B |
| tempfile | 3.23.0 (crates.io) | M | Создание временного файла для режима cache-to-disk при `hunt`. | `chainsaw/src/main.rs` L757–L767 | Влияет на ресурсное поведение и ошибки (`Failed to create cache on disk`). | D |
| terminal_size | 0.4.3 (crates.io) | M | Определение ширины терминала → выбор `column_width` по диапазонам. | `chainsaw/src/main.rs` L339–L354 | Прямо влияет на формат табличного вывода/обрезку строк. | C |
| uuid | 1.18.1 (crates.io) | M | Внутренние идентификаторы правил/групп/документов в `hunt`. | `chainsaw/src/hunt.rs` L25, L150–L189; `chainsaw/src/cli.rs` L15 | В наблюдаемом выводе UUID напрямую не печатаются, но используются как ключи для сопоставления hits→rules. | E |

---

## 2) Dev‑зависимости (тестовый harness)

Источник списка: `chainsaw/Cargo.toml` (L46–L49) + тесты `chainsaw/tests/*`.

| Dev‑зависимость | Resolved (Cargo.lock) | Роль | Где используется | CAT |
|---|---|---|---|:---:|
| assert_cmd | 2.0.17 (crates.io) | Запуск бинарника `chainsaw` в интеграционных тестах и ассерты по exit code/stdout/stderr. | `chainsaw/tests/clo.rs` (например, L1–L24) | T |
| predicates | 3.1.3 (crates.io) | Предикаты для сравнения stdout со «золотыми» файлами (`eq_file`). | `chainsaw/tests/clo.rs` (например, L1–L24) | T |
| paste | 1.0.15 (crates.io) | Макро‑генерация имён тестов (удобство написания). | `chainsaw/tests/convert.rs` (macro `convert_sigma!`) | T |

---

## 3) Особые случаи и зоны сложности (для порта)

### 3.1. Парсеры форензик‑форматов (CAT‑A)
Критические зависимости, определяющие структуру «документа» и, как следствие, совпадение детектов:
- `evtx` (EVTX → JSON, `_attributes`, алиасы полей) — `chainsaw/src/file/evtx.rs`.
- `notatin` (Registry Hive + transaction logs) — `chainsaw/src/file/hve/mod.rs`.
- `mft` (MFT + data streams side effects) — `chainsaw/src/file/mft.rs`.
- `libesedb` (ESEDB/SRUM) — `chainsaw/src/file/esedb/mod.rs`.
- `quick-xml` (XML → JSON) — `chainsaw/src/file/xml.rs`.

### 3.2. Движок правил и матчинг (CAT‑B)
Зависимости, которые определяют семантику фильтров и детектов:
- `tau-engine` + расширения `chainsaw/src/ext/tau.rs`.
- `regex`/`aho-corasick` (поиск/модификаторы).
- `serde_yaml`/`serde_json` (трактовка входных правил/конфигов).

### 3.3. CLI/вывод/терминал (CAT‑C)
Зависимости, влияющие на наблюдаемый вывод:
- `clap` (help/валидация аргументов/ошибки).
- `prettytable-rs` (таблицы и CSV).
- `crossterm` (цвет и стиль сообщений).
- `indicatif` + `terminal_size` (прогресс‑бар, ширины колонок, платформенные `cfg(windows)` символы).

### 3.4. Производительность, кэш и детерминизм (CAT‑D)
- `rayon` (параллелизм) — риск стабильности порядка и воспроизводимости.
- `bincode`, `tempfile` (внутренний кэш и cache‑to‑disk).
- `rustc-hash`, `smallvec`, `once_cell`, `lazy_static` (оптимизации hot‑path).

---

## 4) Замечания по воспроизводимости upstream

### 4.1. Git‑переопределения crates.io
Upstream явно переопределяет crates.io для двух зависимостей:
- `mft` → git `https://github.com/alexkornitzer/mft.git` (commit `ffd95113…`) — `Cargo.toml` L52–L55.
- `notatin` → git `https://github.com/alexkornitzer/notatin.git` (commit `61b2d3dc…`) — `Cargo.toml` L52–L55.

Это означает, что наблюдаемое поведение Rust‑версии (и baseline для 1:1) закреплено не только версиями, но и **конкретными git‑коммитами**.

---

## 5) Открытые вопросы (для закрытия в следующих шагах)

1) `hex` присутствует в runtime‑зависимостях, но прямых мест использования в `src/` и `tests/` на текущем срезе не найдено.
 - План верификации: при /13 (инвентаризация тестов / golden runs) проверить, не задействуется ли `hex` в опциональных ветках CLI или через feature‑flags; дополнительно проверить `cargo tree -e features` при наличии toolchain.

2) В сборке присутствуют две версии `quick-xml` (`0.38.x` напрямую и `0.36.x` транзитивно через `evtx`).
 - План верификации: отдельные golden runs для `dump/search/hunt` по XML и EVTX; при несовпадениях зафиксировать как отдельный риск.

Связанные записи реестра:
- RISK-0026 — отсутствует coverage по XML‑веткам + потенциальные различия из‑за двух `quick-xml`.
- RISK-0027 — допущение о фактической неиспользуемости `hex` (требует верификации).
