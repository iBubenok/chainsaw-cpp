# FEAT-0001 — Реестр функциональности Chainsaw (Rust) (As‑Is)

## Статус
- Версия: 1
- Статус: Draft (As‑Is)
- Источник истины для раздела «Feature inventory» на фазе As‑Is.

## Метод извлечения
FACTS берутся из:
- upstream исходников `chainsaw.zip` (каталоги `src/`, `tests/`).
- CLI‑контракта `docs/as_is/CLI-0001-chainsaw-cli-contract.md`.

Ограничение среды: на не выполнялись реальные прогоны бинаря; наблюдаемое поведение фиксируется по тестам Rust и по коду CLI/логики. Сценарии, требующие подтверждения запуском, помечены как **UNKNOWN** и выносятся в план.

## Область охвата
Реестр покрывает:
- CLI команды и их назначение (не повторяя CLI‑контракт — только ссылки).
- Набор «смысловых фич» (pipeline чтения артефактов, правила, поиск, вывод).
- Аналитические подкоманды (shimcache, srum).

## Таблица фич (feature inventory)

> Колонки:
> - **FEAT-ID** — идентификатор фичи для трассировки.
> - **Поверхность** — где наблюдается (CLI/API/файлы).
> - **Наблюдаемое поведение** — что именно делает (без предположений о мотивах).
> - **Доказательства** — первичные источники: путь + привязка к фрагменту.
> - **Проверка** — как доказывать 1:1 на будущих шагах (тест/запуск/дифф).

| FEAT-ID | Название | Поверхность | Наблюдаемое поведение (As‑Is) | Доказательства (код/тесты) | Проверка 1:1 (план) |
|---|---|---|---|---|---|
| FEAT-0001 | Глобальные флаги и инициализация | CLI | Поддерживаются глобальные опции `--no-banner`, `--num-threads`, `-v` (count). При отсутствии `--no-banner` печатается ASCII‑баннер. При `--num-threads` настраивается глобальный пул rayon. | `chainsaw/src/main.rs`: структура `Args` + `print_title` + `ThreadPoolBuilder::new.num_threads(...)` в `run` (см. `match args.cmd`). | Golden run: `chainsaw --help`, `chainsaw -v...`. Для баннера — сравнение stdout/stderr (/27). |
| FEAT-0002 | Команда `dump` — выгрузка артефактов | CLI | Загружает поддерживаемые артефакты из путей, парсит документы и печатает содержимое в выбранном формате: YAML‑подобный `Std` (дефолт), `--json`, `--jsonl`. Поддерживает фильтрацию по расширению, `--load-unknown`, `--skip-errors`, `-q`. Для MFT есть опции декодирования data streams и каталога вывода. При `--json` выводится JSON массив, при `--jsonl` — одна строка JSON на документ. | `chainsaw/src/main.rs`: ветка `Command::Dump {.. }` (инициализация writer, `Reader::load(..)`, обход `reader.documents`, ветвление `json/jsonl/std`). Типы документов: `Document::{Evtx,Hve,Json,Xml,Mft,Esedb}`. | Добавить/портировать тесты (отсутствуют в upstream tests). Golden run на выборочных файлах + дифф harness (/27). |
| FEAT-0003 | Команда `hunt` — детект на артефактах по правилам | CLI | Загружает файлы, грузит правила (Chainsaw и/или Sigma) и применяет их к документам. Поддерживает: mapping для Sigma (`--mapping` требуется при `--sigma`), дополнительные правила (`-r/--rule`), фильтры правил по `--kind/--level/--status`, фильтрацию входных файлов по расширениям, окна времени `--from/--to` (по timestamp), настройку TZ (`--local` или `--timezone`), режимы вывода (`--json/--jsonl`, табличный дефолт, `--log`, `--csv`), `--output`, `--preprocess` (BETA), `--cache-to-disk` (только при jsonl), `--column-width`, `--full`, `--metadata`, `--skip-errors`, `--load-unknown`, `-q`. | CLI: `chainsaw/src/main.rs` ветка `Command::Hunt {.. }` (опции и конфликты/requirements). Минимальная «наблюдаемость» по тесту: `chainsaw/tests/clo.rs::hunt_r_any_logon` + фикстуры `tests/evtx/rule-any-logon.yml`, `tests/evtx/security_sample.evtx`, ожидаемый stdout `tests/evtx/clo_hunt_r_any_logon.txt`. | Test‑to‑test: перенос `hunt_r_any_logon` в C++. Golden runs с Sigma+mapping + harness сравнения. |
| FEAT-0004 | Загрузка правил Chainsaw YAML | CLI/API | Поддерживаются YAML правила типа “Chainsaw”; правила имеют уровень/статус/фильтр на основе Tau (detection/expression). | `chainsaw/src/rule/mod.rs`: `enum Kind { Chainsaw, Sigma }`, `load(kind, path,...)` ветка `Kind::Chainsaw` вызывает `chainsaw::load(path)`; `Rule::solve` для Chainsaw использует `tau_engine::{solve, core::solve}`. | Порт тестов, которые используют `-r` (см. FEAT-0003). Дополнительно — unit‑тесты на парсинг/валидацию правил (/циклы). |
| FEAT-0005 | Загрузка Sigma правил и компиляция в Tau | CLI/API | Sigma правила грузятся функцией `sigma::load(path)` и затем десериализуются в `rule::Sigma`. При загрузке применяется оптимизация Tau выражения (`optimiser::coalesce/shake/rewrite/matrix`). | `chainsaw/src/rule/mod.rs`: ветка `Kind::Sigma` внутри `load(..)` + вызовы optimiser; `chainsaw/tests/convert.rs` проверяет, что `sigma::load` даёт ожидаемый YAML (сравнение с `tests/convert/*_output.yml`). | Test‑to‑test: перенести `tests/convert.rs` как unit/integration тест (в C++ — golden YAML/IR). Golden run `hunt -s sigma --mapping...`. |
| FEAT-0006 | Команда `lint` — проверка правил | CLI | Команда `lint` принимает путь и `--kind` (из `RuleKind`) и опционально `--tau` (вывод Tau логики). Поведение при ошибках загрузки/линтинга определяется `chainsaw::lint`/`chainsaw::load`. | CLI: `chainsaw/src/main.rs` ветка `Command::Lint {.. }` + импорт `lint as lint_rule, load as load_rule`. **Примечание:** help‑строка в CLI говорит про `stalker`, но `RuleKind` определён только как `{chainsaw,sigma}` (`chainsaw/src/rule/mod.rs`). | Добавить прогоны/тесты на lint (в upstream tests отсутствуют). На — зафиксировать `chainsaw lint --help` и проверку ошибок на заведомо неправильных правилах. |
| FEAT-0007 | Команда `search` — поиск по документам | CLI | Поддерживает поиск по: (1) простому `pattern`, (2) доп. regex `-e/--regex`, (3) Tau выражениям `-t/--tau` (с AND по умолчанию и `--match-any`), фильтры по расширениям, `--ignore-case`, формат вывода `--json/--jsonl`, `--timestamp` (поле времени) + `--from/--to`, TZ (`--local`/`--timezone`), `--output`, `--skip-errors`, `--load-unknown`, `-q`. | CLI: `chainsaw/src/main.rs` ветка `Command::Search {.. }`. Наблюдаемость по тестам: `chainsaw/tests/clo.rs::{search_jq_simple_string, search_q_simple_string, search_q_jsonl_simple_string}` + ожидаемые stdout файлы `tests/evtx/clo_search_*.txt`. | Test‑to‑test: перенести 3 теста `search_*` и сравнивать stdout (golden). Добавить сценарии regex/tau (на основе upstream). |
| FEAT-0008 | Чтение/идентификация артефактов и документов | CLI/API | При обработке путей `get_files` собирает совместимые файлы (с опцией фильтра по расширению). `Reader::load` пытается определить тип файла; опционально разрешает `--load-unknown`. Документы итеративно выдаются через `reader.documents`. | `chainsaw/src/main.rs` использует `get_files` и `Reader::load` в `dump/hunt/search`; типы документов видны в `dump` через `Document` enum. Реализация readers: `chainsaw/src/file/*` (evtx/json/xml/mft). | На — golden runs на “пустые/несовместимые каталоги” и на корректные файлы. На /циклах — тест‑к‑тесту на парсинг EVTX (используя `tests/evtx/security_sample.evtx`). |
| FEAT-0009 | Вывод/форматы (табличный/JSON/JSONL/CSV/log) и writer | CLI/API | `Writer` управляет форматом вывода, quiet/verbose и направлением вывода (stdout/file). Для некоторых команд `--output` может означать файл или каталог (для CSV в hunt). | `chainsaw/src/main.rs`: `init_writer(..)` + ветки `hunt` (проверка “CSV output must be a folder”), `dump` (json array/jsonl/std). Реализация writer: `chainsaw/src/write.rs`. | На зафиксировать эталонные форматы для ключевых команд; затем в сравнение через harness с нормализацией (если допустимо по GOV-0002). |
| FEAT-0010 | Команда `analyse shimcache` — таймлайн shimcache | CLI | Подкоманда читает SYSTEM hive (shimcache), строит таймлайн исполнения; опционально обогащает данными из Amcache (`-a/--amcache`) и поддерживает детект «insertion time» через regex (`-e/--regex` или `-r/--regexfile`), а также режим `--tspair` (требует amcache). Вывод по умолчанию — CSV/таблица (точный формат требует golden run). | CLI: `chainsaw/src/main.rs` ветка `AnalyseCommand::Shimcache {.. }`. Анализатор: `chainsaw/src/analyse/shimcache.rs` экспортируется как `ShimcacheAnalyser` (`chainsaw/src/lib.rs`). | Добавить тесты (upstream tests отсутствуют). На — эталонные прогоны на подготовленных hives (нужно подобрать/добавить фикстуры; если нет — оформить риск). |
| FEAT-0011 | Команда `analyse srum` — анализ SRUM базы | CLI | Подкоманда анализирует SRUM ESEDB (`SRUDB.dat`) совместно с `SOFTWARE` hive. Поддерживает режим “только статистика” (`--stats-only`), `-q` и `-o/--output`. | CLI: `chainsaw/src/main.rs` ветка `AnalyseCommand::Srum {.. }`. Наблюдаемость по тестам: `chainsaw/tests/clo.rs::{analyse_srum_database_table_details, analyse_srum_database_json}` + фикстуры `tests/srum/SRUDB.dat`, `tests/srum/SOFTWARE` + ожидаемые stdout `tests/srum/analysis_srum_database_*.txt`. | Test‑to‑test: перенести 2 теста SRUM (stdout golden). На — зафиксировать дополнительные прогоны без `--stats-only` и с `-o` (если нужно для покрытия). |
## Неясности/UNKNOWN, выявленные на
1) `lint --kind stalker` упомянут в help‑тексте подкоманды `lint`, но в `RuleKind` отсутствует значение `stalker`.
 - Возможные объяснения: устаревший help‑текст или функциональность вынесена/удалена.
 - План верификации: на зафиксировать вывод `chainsaw lint --help` + попытку `chainsaw lint --kind stalker...` и ожидаемую ошибку/валидацию clap.

## Связи и ссылки
- CLI‑контракт: `docs/as_is/CLI-0001-chainsaw-cli-contract.md`.
- Критерии 1:1: `docs/governance/GOV-0002-equivalence-criteria.md`.
