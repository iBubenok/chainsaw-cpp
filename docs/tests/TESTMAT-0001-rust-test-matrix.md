# TESTMAT-0001 — Матрица тестов Chainsaw (Rust) для test‑to‑test (As‑Is)

## Статус
- Версия: 1
- Статус: Draft (As‑Is)
- Назначение: зафиксировать полный список тестов upstream Chainsaw и их семантику как основу **test‑to‑test** переноса (+).
- Single Source of Truth для `TST-*` в рамках.

## Область охвата
Включено **всё**, что обнаружено по дереву исходников `chainsaw/` как Rust‑тесты:
- integration tests: `chainsaw/tests/*.rs`;
- unit tests: `chainsaw/src/**` (модули `#[cfg(test)]`).

Не включено:
- эталонные прогоны бинаря (golden runs) — это;
- тесты сторонних библиотек (они остаются вне контроля проекта).

## Метод инвентаризации
1) Поиск тестов по атрибуту `#[test]` по дереву `chainsaw/` и ручная проверка `chainsaw/tests/*`.
 - Команда для воспроизведения (пример): `grep -R --line-number "#\[test\]" chainsaw`
2) Для каждого теста зафиксировано:
 - точное местоположение: `файл:строки`;
 - что именно проверяется (наблюдаемое поведение/инвариант);
 - входы/фикстуры и ожидаемый результат;
 - привязка к `FEAT-*` (см. `docs/as_is/FEAT-0001-feature-inventory.md`);
 - привязка к подсистемам Rust (см. `docs/as_is/ARCH-0001-rust-as-is-architecture.md`).

## Сводка
- Всего тестов: **22**
 - CLI/integration (через `Command::cargo_bin("chainsaw")`): **6** (`chainsaw/tests/clo.rs`)
 - Integration/unit (Sigma conversion): **2** (`chainsaw/tests/convert.rs`)
 - Unit (Sigma internals): **14** (`chainsaw/src/rule/sigma.rs`)

## Инвентарь фикстур (upstream)

> Размеры приведены для понимания влияния данных на воспроизводимость/CI.

| Путь | Роль | Размер (байт) | Используется в тестах |
|---|---|---:|---|
| `chainsaw/tests/evtx/security_sample.evtx` | EVTX вход для `search`/`hunt` | 69 632 | TST-0001, TST-0002, TST-0003, TST-0004 |
| `chainsaw/tests/evtx/rule-any-logon.yml` | Chainsaw rule (YAML) для `hunt` | 588 | TST-0004 |
| `chainsaw/tests/evtx/clo_search_qj_simple_string.txt` | expected stdout для `search -jq` | 2 741 | TST-0001 |
| `chainsaw/tests/evtx/clo_search_q_jsonl_simple_string.txt` | expected stdout для `search -q --jsonl` | 2 739 | TST-0002 |
| `chainsaw/tests/evtx/clo_search_q_simple_string.txt` | expected stdout для `search -q` | 2 951 | TST-0003 |
| `chainsaw/tests/evtx/clo_hunt_r_any_logon.txt` | expected stdout для `hunt -r...` | 1 803 | TST-0004 |
| `chainsaw/tests/srum/SRUDB.dat` | SRUM ESEDB вход | 1 835 008 | TST-0005, TST-0006 |
| `chainsaw/tests/srum/SOFTWARE` | SOFTWARE hive вход для SRUM | 73 924 608 | TST-0005, TST-0006 |
| `chainsaw/tests/srum/analysis_srum_database_table_details.txt` | expected stdout (таблица) для `analyse srum --stats-only` | 5 249 | TST-0005 |
| `chainsaw/tests/srum/analysis_srum_database_json.txt` | expected stdout (JSON) для `analyse srum` | 3 533 274 | TST-0006 |
| `chainsaw/tests/convert/sigma_simple.yml` | Sigma rule (YAML) вход | 228 | TST-0007 |
| `chainsaw/tests/convert/sigma_simple_output.yml` | expected YAML (семантика) результата `sigma::load` | 258 | TST-0007 |
| `chainsaw/tests/convert/sigma_collection.yml` | Sigma rule collection (multi‑doc YAML) вход | 459 | TST-0008 |
| `chainsaw/tests/convert/sigma_collection_output.yml` | expected YAML docs результата `sigma::load` | 669 | TST-0008 |

---

## Матрица тестов

### 1) CLI / integration tests (контракт бинаря и stdout)

> Общая модель этих тестов: запускается бинарь `chainsaw` через `Command::cargo_bin("chainsaw")`, далее проверяется `stdout` на **полное совпадение** с файлом (predicate `eq_file(...).utf8.unwrap`).
>
> Важно: это проверка **stdout как байт‑потока** (см. GOV‑0002/C2). Любые управляющие последовательности (ANSI) и Unicode‑символы являются частью проверяемого вывода.

| TST-ID | Rust test (источник) | Что проверяет | Команда/аргументы (концептуально) | Входы/фикстуры | Ожидаемый результат | FEAT-* | Подсистемы (ARCH) | Примечания |
|---|---|---|---|---|---|---|---|---|
| TST-0001 | `chainsaw/tests/clo.rs:6–25` `search_jq_simple_string` | `search`: поиск по EVTX (паттерн `4624`), вывод в JSON (флаг `-j`) + quiet (`-q`), сравнение stdout с эталоном | `chainsaw search 4624 <security_sample.evtx> -jq` | `tests/evtx/security_sample.evtx` | `tests/evtx/clo_search_qj_simple_string.txt` | FEAT-0007, FEAT-0008, FEAT-0009 | `search.rs`, `file/*`, `write.rs` | stdout — JSON массив в одну строку (пример см. expected file) |
| TST-0002 | `chainsaw/tests/clo.rs:27–50` `search_q_jsonl_simple_string` | `search`: поиск по EVTX (паттерн `4624`), quiet (`-q`), формат JSONL, сравнение stdout | `chainsaw search 4624 <security_sample.evtx> -q --jsonl` | `tests/evtx/security_sample.evtx` | `tests/evtx/clo_search_q_jsonl_simple_string.txt` | FEAT-0007, FEAT-0008, FEAT-0009 | `search.rs`, `file/*`, `write.rs` | stdout — JSONL (по одному событию на строку) |
| TST-0003 | `chainsaw/tests/clo.rs:51–70` `search_q_simple_string` | `search`: поиск по EVTX (паттерн `4624`), quiet (`-q`), дефолтный формат (Std/YAML‑подобный), сравнение stdout | `chainsaw search 4624 <security_sample.evtx> -q` | `tests/evtx/security_sample.evtx` | `tests/evtx/clo_search_q_simple_string.txt` | FEAT-0007, FEAT-0008, FEAT-0009 | `search.rs`, `file/*`, `write.rs` | stdout — YAML‑подобный вывод (multi‑doc `---`), порядок/формат значим |
| TST-0004 | `chainsaw/tests/clo.rs:72–94` `hunt_r_any_logon` | `hunt`: применение 1 Chainsaw‑правила к EVTX, вывод дефолтного табличного отчёта, сравнение stdout | `chainsaw hunt <security_sample.evtx> -r <rule-any-logon.yml>` | `tests/evtx/security_sample.evtx`, `tests/evtx/rule-any-logon.yml` | `tests/evtx/clo_hunt_r_any_logon.txt` | FEAT-0003, FEAT-0004, FEAT-0008, FEAT-0009 | `hunt.rs`, `rule/*`, `cli.rs`, `write.rs` | expected stdout содержит ANSI‑escape и Unicode box‑drawing; на Windows upstream имеет `cfg(windows)` отличия (см. ARCH-0001/4.1) |
| TST-0005 | `chainsaw/tests/clo.rs:96–120` `analyse_srum_database_table_details` | `analyse srum`: режим `--stats-only`, вывод таблицы метаданных SRUM (включая временные рамки и retention), сравнение stdout | `chainsaw analyse srum --software <SOFTWARE> <SRUDB.dat> --stats-only -q` | `tests/srum/SRUDB.dat`, `tests/srum/SOFTWARE` | `tests/srum/analysis_srum_database_table_details.txt` | FEAT-0011 | `analyse/srum.rs`, ESEDB/HVE парсеры, `cli.rs`/`write.rs` | Зависит от ESEDB (`libesedb*`) и парсинга SOFTWARE hive |
| TST-0006 | `chainsaw/tests/clo.rs:122–145` `analyse_srum_database_json` | `analyse srum`: детальный вывод (без `--stats-only`), stdout — большой JSON массив; сравнение stdout | `chainsaw analyse srum --software <SOFTWARE> <SRUDB.dat> -q` | `tests/srum/SRUDB.dat`, `tests/srum/SOFTWARE` | `tests/srum/analysis_srum_database_json.txt` | FEAT-0011 | `analyse/srum.rs`, ESEDB/HVE парсеры, `cli.rs`/`write.rs` | Expected stdout ~3.5MB, чувствителен к порядку и сериализации JSON |

### 2) Integration/unit tests: Sigma → Tau/YAML (семантика конвертации)

> Эти тесты **не запускают** бинарь. Они вызывают `chainsaw::sigma::load(path)` и сравнивают получившийся результат с YAML‑эталоном как `serde_yaml::Value` (то есть **по семантике YAML‑дерева**, а не по текстовому представлению).

| TST-ID | Rust test (источник) | Что проверяет | Входы/фикстуры | Ожидаемый результат | FEAT-* | Подсистемы (ARCH) | Примечания |
|---|---|---|---|---|---|---|---|
| TST-0007 | `chainsaw/tests/convert.rs:10–45` (macro) + `convert_sigma!("simple")` (`chainsaw/tests/convert.rs:44`) → `solve_simple` | `sigma::load` корректно преобразует одиночное Sigma правило в YAML‑представление Chainsaw (authors/level/detection/condition/true_*), включая трансформацию `CommandLine|contains` в шаблон `i*...*` | `tests/convert/sigma_simple.yml` | `tests/convert/sigma_simple_output.yml` (YAML‑семантика) | FEAT-0005 | `rule/sigma.rs` | Тест разбивает expected по `---\n` regex и сравнивает YAML objects 1‑к‑1 по порядку |
| TST-0008 | `chainsaw/tests/convert.rs:10–45` (macro) + `convert_sigma!("collection")` (`chainsaw/tests/convert.rs:45`) → `solve_collection` | `sigma::load` корректно обрабатывает **коллекцию** Sigma (multi‑doc): базовый документ задаёт metadata+base‑detection, последующие документы расширяют detection/condition; результат — несколько YAML правил, каждое включает base + search | `tests/convert/sigma_collection.yml` | `tests/convert/sigma_collection_output.yml` (2 YAML doc) | FEAT-0005 | `rule/sigma.rs` | В expected кавычки/escape значимы на уровне YAML‑парсинга; порядок выдачи правил должен совпадать |

### 3) Unit tests: внутренности `rule/sigma.rs`

> Эти тесты фиксируют точную семантику преобразования отдельных конструкций Sigma в Tau/YAML.

| TST-ID | Rust test (источник) | Что проверяет (инвариант) | Входы (встроенные в тест) | Ожидаемый результат | FEAT-* | Подсистемы (ARCH) |
|---|---|---|---|---|---|---|
| TST-0009 | `chainsaw/src/rule/sigma.rs:874–885` `test_unsupported_conditions` | `Condition::unsupported` возвращает `true` для строк условий с конструкциями: `" | "`, `*`, и `" of "` (на примерах) | 3 строки условий | `assert!(...unsupported)` | FEAT-0005 | `rule/sigma.rs` |
| TST-0010 | `chainsaw/src/rule/sigma.rs:886–890` `test_match_contains` | `as_contains` добавляет префикс `i*` и суффикс `*` | `"foobar"` | `"i*foobar*"` | FEAT-0005 | `rule/sigma.rs` |
| TST-0011 | `chainsaw/src/rule/sigma.rs:892–896` `test_match_endswith` | `as_endswith` добавляет префикс `i*` | `"foobar"` | `"i*foobar"` | FEAT-0005 | `rule/sigma.rs` |
| TST-0012 | `chainsaw/src/rule/sigma.rs:898–917` `test_match` | `as_match` добавляет `i` и допускает `*` только как ведущий/замыкающий wildcard; вложенные wildcard (`foo*bar`, `foo?bar`) → `None` | строки `foobar`, `*foobar`, `foobar*`, `*foobar*`, `foo*bar`, `foo?bar` | соответствующие строки / `None` | FEAT-0005 | `rule/sigma.rs` |
| TST-0013 | `chainsaw/src/rule/sigma.rs:919–923` `test_match_regex` | `as_regex(convert=false)` возвращает `?`‑префикс для regex‑матча | `"foobar"` | `"?foobar"` | FEAT-0005 | `rule/sigma.rs` |
| TST-0014 | `chainsaw/src/rule/sigma.rs:925–929` `test_match_startswith` | `as_startswith` добавляет `i` и суффикс `*` | `"foobar"` | `"ifoobar*"` | FEAT-0005 | `rule/sigma.rs` |
| TST-0015 | `chainsaw/src/rule/sigma.rs:931–960` `test_parse_identifier` | `parse_identifier` добавляет `i`‑префикс для всех строковых значений в YAML (в т.ч. в массивах и mapping), оставляя числа без изменений | YAML с array/mapping/number/string | YAML, где все строки имеют `i...` | FEAT-0005 | `rule/sigma.rs` |
| TST-0016 | `chainsaw/src/rule/sigma.rs:962–980` `test_prepare` | `prepare(detection, None)` возвращает detection без изменений (базовый сценарий) | YAML detection | detection == expected | FEAT-0005 | `rule/sigma.rs` |
| TST-0017 | `chainsaw/src/rule/sigma.rs:982–1008` `test_prepare_group` | `prepare(base, Some(ext))` объединяет base‑детекцию и extension‑детекцию в общий объект (включая condition) | YAML base + YAML ext | объединённая detection структура | FEAT-0005 | `rule/sigma.rs` |
| TST-0018 | `chainsaw/src/rule/sigma.rs:1010–1069` `test_detection_to_tau_0` | `detections_to_tau` выполняет комплексную конвертацию: `parse_identifier`, преобразование модификаторов (`|contains/|endswith/|re/|startswith`), развёртку списков в `C_0..C_n`, и переписывание condition (включая `all(B)` и скобки) | YAML detection (A,B,C, condition: `A and B and C`) | YAML (tau) структура с `detection:...`, `true_*: ` и переписанным `condition` | FEAT-0005 | `rule/sigma.rs` |
| TST-0019 | `chainsaw/src/rule/sigma.rs:1071–1096` `test_detection_to_tau_all_of_them` | Condition `all of them` переписывается в `A and B` (и строки получают `i`‑префикс) | YAML detection с `condition: all of them` | YAML с `condition: A and B` | FEAT-0005 | `rule/sigma.rs` |
| TST-0020 | `chainsaw/src/rule/sigma.rs:1098–1123` `test_detection_to_tau_one_of_them` | Condition `1 of them` переписывается в `A or B` (и строки получают `i`‑префикс) | YAML detection с `condition: 1 of them` | YAML с `condition: A or B` | FEAT-0005 | `rule/sigma.rs` |
| TST-0021 | `chainsaw/src/rule/sigma.rs:1125–1154` `test_detection_to_tau_all_of_selection` | Condition `all of selection*` переписывается в `A and (selection0 and selection1)` | YAML detection с `selection0/selection1` | YAML с переписанным `condition` | FEAT-0005 | `rule/sigma.rs` |
| TST-0022 | `chainsaw/src/rule/sigma.rs:1156–1185` `test_detection_to_tau_one_of_selection` | Condition `1 of selection*` переписывается в `A and (selection0 or selection1)` | YAML detection | YAML с переписанным `condition` | FEAT-0005 | `rule/sigma.rs` |

---

## Сводка покрытия фич (по upstream тестам)

> Это **не полный coverage проекта**, а только фактическое покрытие тем, что уже присутствует в upstream тестах.

| FEAT-ID | Покрытие тестами `TST-*` |
|---|---|
| FEAT-0003 (hunt) | TST-0004 |
| FEAT-0004 (Chainsaw rules YAML) | TST-0004 |
| FEAT-0005 (Sigma load/convert) | TST-0007…TST-0022 |
| FEAT-0007 (search) | TST-0001…TST-0003 |
| FEAT-0008 (read/identify artifacts) | TST-0001…TST-0004 |
| FEAT-0009 (writer/formats) | TST-0001…TST-0004 |
| FEAT-0011 (analyse srum) | TST-0005…TST-0006 |

Фичи без покрытия upstream тестами (требуют golden runs и/или новых тестов в порте):
- FEAT-0001 (глобальные флаги/banner/threadpool), FEAT-0002 (dump), FEAT-0006 (lint), FEAT-0010 (analyse shimcache) — см. `FEAT-0001` раздел «Проверка 1:1 (план)».

## Примечания о воспроизводимости и платформенных различиях
- Тесты TST-0001…TST-0006 сравнивают stdout **байт‑в‑байт** с файлами‑эталонами. Любые платформенные отличия upstream (например, `cfg(windows)` в `cli.rs` для префикса правил и tick‑символов) потенциально означают необходимость **платформо‑специфичных expected outputs** для кроссплатформенной матрицы. См. `ARCH-0001/4.1`.
- Фикстура `tests/srum/SOFTWARE` (~74MB) и большой expected JSON (`analysis_srum_database_json.txt` ~3.5MB) влияют на размер репозитория/артефактов и время CI. Политика хранения и минимизация набора данных —.

## Связи
- Критерии 1:1: `docs/governance/GOV-0002-equivalence-criteria.md`.
- Реестр фич: `docs/as_is/FEAT-0001-feature-inventory.md`.
- As‑Is архитектура: `docs/as_is/ARCH-0001-rust-as-is-architecture.md`.
