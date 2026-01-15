# TOBE-0001 — Архитектура порта Chainsaw на C++ (To‑Be)

## Статус
- Версия: 1
- Статус: Draft (To‑Be)
- Назначение: зафиксировать **To‑Be архитектуру** порта Chainsaw на C++ на уровне компонентов/границ/интерфейсов/потоков данных и платформенных абстракций.
- Single Source of Truth для:
 - `MOD-*` (границы модулей для трассируемости, см. `GOV-0001`);
 - “As‑Is → To‑Be” соответствия компонентов (см. `ARCH-0001`).

## Входы (источники истины)
- Постановка задачи: `docs/requirements/TASK-0001-problem-statement.md`.
- Архитектурные требования (`REQ-*`): `docs/requirements/AR-0001-architectural-requirements.md`.
- Критерии 1:1: `docs/governance/GOV-0002-equivalence-criteria.md`.
- As‑Is архитектура Rust: `docs/as_is/ARCH-0001-rust-as-is-architecture.md`.
- Форматы данных As‑Is: `docs/as_is/DATA-0001-data-formats-and-transformations.md`.
- Test‑matrix (test‑to‑test): `docs/tests/TESTMAT-0001-rust-test-matrix.md`.
- ADR baseline (ключевые решения): `docs/adr/ADR-0001...ADR-0011`.

## Область охвата
Документ описывает:
- модульную декомпозицию C++ порта (границы ответственности, `MOD-*`);
- ключевые интерфейсы между модулями;
- end‑to‑end потоки для команд CLI (`dump`, `search`, `hunt`, `lint`, `analyse srum`, `analyse shimcache`);
- точки платформенной специфики и требования к абстракциям;
- карту соответствия As‑Is → To‑Be.

Документ **не** фиксирует:
- выбор конкретных EVTX/HVE/MFT/XML парсеров (см. pending в `ADR-0009`);
- WBS/план реализации;
- инженерные гайды/стандарты кодирования;
- детали CI/harness (–27).

---

## 1) Архитектурные драйверы (constraints)

### 1.1. Трассируемость и 1:1 как первичный драйвер
Архитектура должна поддерживать инвариант трассировки `REQ-* → (ADR-*/DEC-*) → MOD-* → TST-* → REP-*/RUN-*` (см. `AR-0001/REQ-NFR-0014`, `GOV-0001/4.2`).

Байт‑в‑байт критерии stdout/stderr и exit codes считаются частью наблюдаемого поведения (см. `GOV-0002/C1–C7`, `AR-0001/REQ-NFR-0015`).

### 1.2. Кроссплатформенность и платформенная истина
Сравнение 1:1 выполняется **внутри платформы** (Windows↔Windows, Linux↔Linux, macOS↔macOS), и различия между платформами являются частью baseline‑истины (см. `GOV-0002/1.2`, а также `ARCH-0001/4.1` о Windows‑отличиях символов вывода).

### 1.3. Вывод/CLI как часть «наблюдаемой поверхности»
Решение `ADR-0006` требует собственного слоя CLI и пользовательского вывода (help/errors/таблицы/прогресс) для обеспечения byte‑to‑byte совпадения с baseline.

### 1.4. Детерминизм важнее параллелизма
`ADR-0011`: параллелизм допустим только при доказуемом сохранении наблюдаемого порядка и детерминизма вывода.

### 1.5. Пути/кодировки
`ADR-0010`: единая политика `std::filesystem::path` + явные преобразования `path ⇄ UTF‑8` для стабильного поведения и безопасности.

---

## 2) Общая структура: бинарь + внутренние библиотеки

Целевой артефакт исполнения — бинарь `chainsaw` (имя соответствует baseline CLI).

To‑Be структура кода (ожидаемая, уточняется в при создании скелета):
- `cpp/` — корень C++ части;
- `cpp/src/` — реализация модулей `MOD-*`;
- `cpp/include/` — публичные интерфейсы между модулями;
- `cpp/tests/` — тесты (gtest) по `TESTMAT-0001` и расширения;
- `third_party/` — vendored зависимости и тексты лицензий (см. `ADR-0002`, `POL-0001`).

> Примечание: конкретные CMake targets/структура каталогов — предмет, но модульные границы (`MOD-*`) фиксируются здесь как независимый от build‑системы контракт.

---

## 3) Модульная декомпозиция (MOD-*)

Ниже `MOD-*` — устойчивые якоря трассируемости. На уровне реализации рекомендуется:
- включать `MOD-XXXX` в комментарий верхнего уровня файла(ов) модуля и в имена CMake targets/пакетов тестов;
- в тестах включать `TST-XXXX` в имя тест‑кейса или в имя набора.

### 3.1. Таблица модулей

| MOD-ID | Имя модуля (логическое) | Ответственность (кратко) | Основные входы/выходы |
|---|---|---|---|
| MOD-0001 | `app` | Точка входа, сбор конфигурации, dispatch подкоманд | argv/env → выполнение команды → exit code |
| MOD-0002 | `cli` | Парсинг argv + генерация `--help/--version` + диагностические ошибки CLI | argv → `Command`/`CliError`/help text |
| MOD-0003 | `output` | Единый слой пользовательского вывода (stdout/stderr/file), таблицы/CSV/JSON/JSONL, прогресс | structured results → bytes(stdout/stderr)/files |
| MOD-0004 | `platform` | Платформенные абстракции (paths/encoding, fs helpers, tty/env, temp files) | OS APIs → унифицированные утилиты |
| MOD-0005 | `io::discovery` | Сбор входных файлов/директорий (аналог `get_files`) + детерминизация порядка (политика) | input paths → список файлов |
| MOD-0006 | `io::reader` | Унифицированный интерфейс чтения артефактов: `Reader` и поток `Document` | file → stream<Document>/errors |
| MOD-0007 | `formats` | Набор парсеров форматов (EVTX/ESEDB/HVE/MFT/XML/JSON) и адаптеры | bytes(file) → canonical document model |
| MOD-0008 | `rules` | Загрузка/линт/фильтрация правил (Chainsaw+Sigma), sigma→tau подготовка | rule files → RuleSet / lint output |
| MOD-0009 | `tau` | Tau IR, парсер выражений и solver над document model | tau expr + document → bool/matches |
| MOD-0010 | `engine::dump` | Реализация команды `dump` (pipeline чтения + сериализация документов) | input → output |
| MOD-0011 | `engine::search` | Реализация команды `search` (regex/tau, фильтр времени) | input + query → hits → output |
| MOD-0012 | `engine::hunt` | Реализация команды `hunt` (rules+mappings, детекты, cache-to-disk) | input + rules → detections → output |
| MOD-0013 | `engine::lint` | Реализация `lint` (валидация/фильтры правил) | rules → lint report + exit |
| MOD-0014 | `analyse::srum` | `analyse srum` (ESEDB+SOFTWARE hive, таблицы/JSON) | SRUDB+SOFTWARE → output |
| MOD-0015 | `analyse::shimcache` | `analyse shimcache` (Registry hives, timeline) | hives → output |

### 3.2. Слои и допустимые зависимости

Для тестируемости и минимизации циклов зависимостей вводится правило направленности зависимостей:

```text
(app)
 ↓
(cli) ────────────────┐
 ↓ │
(engines/analyse) │
 ↓ │
(rules / tau) │
 ↓ │
(io::reader) │
 ↓ │
(formats) │
 ↓ │
(platform) ───────────┘

(output) — используется app/engines/analyse, но не зависит от них
```

Нормы:
- `platform` не зависит от остальных модулей.
- `formats` зависит от `platform` и выбранных внешних парсеров.
- `io::reader` зависит от `formats` и `platform`.
- `rules/tau` не зависят от CLI/output, чтобы тестировать семантику конвертации/solver отдельно.
- `output` зависит от выбранных библиотек форматирования/JSON (см. `ADR-0003`, `ADR-0007`) и от доменных структур результатов, но не от `cli`.

---

## 4) Ключевые доменные модели и интерфейсы

Ниже определяются интерфейсы как **контракты между модулями** (псевдосигнатуры). Конкретные типы/ошибки и их реализация уточняются в (скелет) и в слайсах.

### 4.1. Общие типы (сквозные)

#### 4.1.1. `ByteBuffer`
- Назначение: хранить байтовые данные (stdout/stderr, сырой документ, фрагменты файла) без неявной перекодировки.
- Требования: `REQ-NFR-0015`, `GOV-0002/C2`.

#### 4.1.2. `Status` / `Error`
- Назначение: единый переносимый формат ошибки внутри порта.
- Требования: управляемая печать ошибок (`ADR-0006`), безопасность недоверенных входов (`REQ-SEC-0017`).

> На уровне To‑Be фиксируется только инвариант: **ошибка не должна автоматически печататься** библиотекой/побочным механизмом; пользовательский текст/формат ошибки — обязанность `output`/`cli`.

### 4.2. `MOD-0002 cli`: контракт парсинга

#### 4.2.1. Типы
- `struct CommandLine {... }` — нормализованное представление argv.
- `struct Command` — один из вариантов подкоманд (dump/search/hunt/lint/analyse...).
- `struct CliDiagnostic { exit_code, stderr_bytes }` — готовый к печати результат ошибки парсинга/валидации аргументов.

#### 4.2.2. API (псевдо)
```cpp
namespace chainsaw::cli {
 struct ParseResult {
 bool ok;
 Command command; // валиден если ok
 CliDiagnostic diag; // валиден если!ok
 };

 ParseResult parse(int argc, char** argv);
 ByteBuffer render_help(const CommandLine& spec, /*...*/);
 ByteBuffer render_version(/*...*/);
}
```

Критичное свойство: `render_help/render_version/diag` формируют **точные** байты, совпадающие с baseline (см. `ADR-0006`, `CLI-0001`, `*`).

### 4.3. `MOD-0003 output`: контракт печати

`output` — единственная подсистема, которая:
- пишет в stdout/stderr;
- пишет в файл при `--output`;
- печатает прогресс (stderr) и таблицы.

#### 4.3.1. Writer
```cpp
namespace chainsaw::output {
 enum class Stream { Stdout, Stderr };

 struct OutputConfig {
 bool quiet;
 bool verbose;
 // output_file_path (optional), format selection, no_banner,...
 };

 class Writer {
 public:
 explicit Writer(const OutputConfig& cfg);

 void write(Stream s, std::string_view bytes);
 void write_line(Stream s, std::string_view bytes); // добавляет '\n' как в baseline

 void flush;

 // Прогресс (минимальный совместимый контракт):
 void progress_begin(/*total? label?*/);
 void progress_tick(/*...*/);
 void progress_end(/*...*/);
 };
}
```

#### 4.3.2. Форматирование результатов
`output` должен уметь сериализовать:
- документы `dump` в текст/JSON/JSONL (см. `ADR-0003`);
- hits `search` в форматах baseline;
- detections `hunt` (таблица/CSV/JSON/JSONL/log);
- отчёты `lint`;
- результаты `analyse`.

Сериализация должна быть детерминированной и управляемой (см. `ADR-0003`, `ADR-0006`, `REQ-NFR-0015`).

### 4.4. `MOD-0004 platform`: пути, кодировки, окружение, temp

Ключевой инвариант: **все преобразования path ⇄ string выполняются только в `platform`** (см. `ADR-0010`).

```cpp
namespace chainsaw::platform {
 std::filesystem::path path_from_utf8(std::string_view u8);
 std::string path_to_utf8(const std::filesystem::path& p); // lossy на Unix при необходимости

 bool is_tty_stdout;
 bool is_tty_stderr;

 // temp/cache
 std::filesystem::path make_temp_file(/*prefix*/);
}
```

### 4.5. `MOD-0005 io::discovery`: сбор входных файлов

Обязанность:
- реализовать эквивалент `get_files` (As‑Is `ARCH-0001/2.1`) с политикой детерминизации порядка, согласуемой с baseline.

```cpp
namespace chainsaw::io {
 struct DiscoveryOptions {
 bool recursive;
 bool follow_symlinks;
 // и другие флаги CLI
 };

 std::vector<std::filesystem::path> discover_files(
 const std::vector<std::filesystem::path>& inputs,
 const DiscoveryOptions& opt);
}
```

> Политика сортировки/стабилизации порядка выделяется как отдельная функция (чтобы при необходимости подстроиться под наблюдаемое baseline‑поведение без влияния на остальной код).

### 4.6. `MOD-0006 io::reader` и `MOD-0007 formats`: документный интерфейс

#### 4.6.1. DocumentKind
В C++ порте требуется аналог `Document` enum из Rust (`ARCH-0001/2.1`): EVTX/HVE/JSON/XML/MFT/ESEDB.

#### 4.6.2. Canonical document model
Внутренний pipeline (`rules/tau/search/hunt`) должен оперировать **каноническим представлением** документа. Минимальная форма:
- объект/карта ключ→значение;
- массивы;
- числа/строки/bool/null;
- плюс “мета” (например, источник файла, offset/record id, timestamp).

Для совместимости с Sigma/Tau рекомендуется объектная модель, близкая по семантике к JSON (с опорой на RapidJSON DOM), но окончательный тип фиксируется в реализации.

#### 4.6.3. Reader API
```cpp
namespace chainsaw::io {
 enum class DocumentKind { Evtx, Hve, Json, Xml, Mft, Esedb, Unknown };

 struct Document {
 DocumentKind kind;
 // payload: canonical object (JSON-like)
 // meta: source path, record id, timestamps...
 };

 class Reader {
 public:
 static Reader open(const std::filesystem::path& file,
 bool load_unknown,
 bool skip_errors);

 // Поток документов: на больших данных важно уметь стримить.
 // Конкретная форма итератора (pull/push) определяется в.
 bool next(Document& out, Status& err);
 };
}
```

`formats` реализует набор парсеров, подключаемых Reader’ом по kind/extension и (при необходимости) по сигнатурам.

---

## 5) Потоки данных по CLI командам (end-to-end)

В этом разделе фиксируется «как данные бегут по системе» и где находятся точки наблюдаемости (stdout/stderr/files).

### 5.1. Общий контур

```text
argv → MOD-0002(cli) → Command
 │
 ├─ help/version → MOD-0003(output) → stdout
 │
 └─ execute → MOD-0001(app)
 ↓
 MOD-0005(discovery) → files
 ↓
 MOD-0006(reader) → documents
 ↓
 (MOD-0010/0011/0012/0013/0014/0015) engines/analyse
 ↓
 MOD-0003(output) → stdout/stderr/files
 ↓
 exit code
```

### 5.2. `dump` (MOD-0010)
- Вход: пути/директории.
- Pipeline: discovery → reader → сериализация каждого документа.
- Вывод: stdout или `--output` (в точности как baseline).

Точки 1:1 сравнения: `..`, `GOV-0002/C2,C3,C5`.

### 5.3. `search` (MOD-0011)
- Вход: паттерн (regex) и/или tau‑выражение + файлы.
- Pipeline:
 - компиляция regex через RE2 (`ADR-0005`) и/или tau‑парсинг (`MOD-0009`);
 - discovery → reader → фильтрация документов;
 - выдача hits в формате baseline (JSON/JSONL/текст).

Детерминизм:
- порядок hits должен соответствовать baseline на платформе (см. `GOV-0002/C4,C7` + `RISK-0011`).
- архитектурно предусмотрена точка “детерминизации/упорядочивания” перед `output` (см. `ADR-0011`).

Точки проверки: `TST-0001..TST-0003`, `*`.

### 5.4. `hunt` (MOD-0012)
- Вход: rules (Chainsaw и/или Sigma), mappings, файлы.
- Pipeline:
 - загрузка правил (`MOD-0008`) и, при необходимости, sigma→tau (`MOD-0009`);
 - определение типов входов по правилам/мэппингам;
 - discovery → reader → применение rules → hits/detections;
 - агрегирование/сортировка (если порядок наблюдаем и стабилен в baseline);
 - печать результатов (таблица/CSV/JSON/JSONL/log) через `output`.

Особенность `--cache-to-disk`:
- побочные файлы и стратегия размещения — часть наблюдаемого поведения (`GOV-0002/C5`, `RISK-0014`);
- поэтому cache‑подсистема должна быть локализована внутри `engine::hunt`, опираясь на `platform::make_temp_file`.

Точки проверки: `TST-0004`, `*`.

### 5.5. `lint` (MOD-0013)
- Вход: директория/файлы правил.
- Pipeline: discovery → rules::load/lint → output.
- Точки проверки: `..`.

### 5.6. `analyse srum` (MOD-0014)
- Вход: `SRUDB.dat` + `SOFTWARE` hive.
- Pipeline:
 - чтение ESEDB через libesedb (см. `ADR-0009`);
 - извлечение и нормализация таблиц/полей в модель результата;
 - вывод таблицы/JSON.

Точки проверки: `TST-0005..TST-0006`, `*`.

### 5.7. `analyse shimcache` (MOD-0015)
- Вход: Registry hives и опционально amcache.
- Pipeline:
 - чтение HVE (pending парсер, см. `ADR-0009` + `RISK-0022`);
 - извлечение shimcache, построение timeline;
 - вывод.

Точки проверки: baseline сейчас недостаточны (`RISK-0022`), требуются дополнительные фикстуры/`RUN-*`.

---

## 6) Платформенные абстракции и «узкие горлышки» 1:1

### 6.1. Кодировки и печать путей
- Любая печать путей в stdout/stderr должна проходить через `platform::path_to_utf8` (см. `ADR-0010`).
- В 1:1 режиме запрещены «красивые» нормализации (например, замена разделителей) без отдельного решения (см. `REQ-NFR-0016`, `GOV-0002/3`).

### 6.2. TTY/ANSI/Unicode символы
- `output` обязан воспроизводить baseline‑символы (в т.ч. Unicode box‑drawing) и ANSI‑escape в тех сценариях, где они присутствуют в эталоне (`TESTMAT-0001/TST-0004`, `ARCH-0001/4.1`).
- Детект TTY и режимы `--quiet/--verbose` должны быть реализованы так, чтобы golden runs (non‑TTY) совпадали байт‑в‑байт.

### 6.3. Время, таймзоны и фильтры
- Модуль `engine::search` должен реализовать фильтры времени (`from/to/timestamp`), согласованные с baseline (см. `ARCH-0001/2.4`, `DATA-0001`).
- Фиксация `TZ` и влияние окружения учитываются в harness (`GOV-0002/1.3`).

### 6.4. Детерминизм порядка
- Любая параллельность не должна менять наблюдаемое поведение (`ADR-0011`).
- Архитектура предусматривает *явную точку* упорядочивания результатов перед сериализацией/печатью.

---

## 7) Карта соответствия As-Is → To-Be

Цель карты: обеспечить перенос «границ ответственности» без потери наблюдаемого поведения.

| As‑Is (Rust) | Роль | To‑Be (C++) |
|---|---|---|
| `chainsaw/src/main.rs` | entrypoint, CLI parse, dispatch | MOD-0001 (`app`) + MOD-0002 (`cli`) |
| `chainsaw/src/write.rs` + `chainsaw/src/cli.rs` | writer, таблицы/прогресс, форматирование | MOD-0003 (`output`) + частично MOD-0002 (`cli`) |
| `chainsaw/src/file/*` | get_files + Reader + Document enum | MOD-0005 (`io::discovery`) + MOD-0006 (`io::reader`) + MOD-0007 (`formats`) |
| `chainsaw/src/rule/*` | load/lint rules, sigma, фильтрация | MOD-0008 (`rules`) |
| `chainsaw/src/ext/tau.rs` + `tau_engine::*` | tau IR/solver | MOD-0009 (`tau`) |
| `chainsaw/src/search.rs` | search pipeline | MOD-0011 (`engine::search`) |
| `chainsaw/src/hunt.rs` | hunt pipeline + cache-to-disk | MOD-0012 (`engine::hunt`) |
| `chainsaw/src/analyse/srum.rs` | analyse srum | MOD-0014 (`analyse::srum`) |
| `chainsaw/src/analyse/shimcache.rs` | analyse shimcache | MOD-0015 (`analyse::shimcache`) |

Ключевое отличие To‑Be: отсутствие глобального `static mut WRITER` (Rust `write.rs`). В C++ порте writer должен быть **объектом**, передаваемым по явному каналу управления (см. `ADR-0006`, `REQ-NFR-0015`).

---

## 8) Минимальная трассировка (якоря для + и –33)

Эта секция фиксирует «замыкание цепочки» для ключевых поверхностей уже на уровне архитектуры.

| Поверхность | REQ-* (минимум) | ADR-* (ключевые) | MOD-* | TST-* / RUN-* |
|---|---|---|---|---|
| CLI help/errors | `REQ-FR-0001`, `REQ-NFR-0015`, `REQ-NFR-0016` | `ADR-0006`, `ADR-0007` | MOD-0002, MOD-0003 | `..` |
| `search` | `REQ-FR-0002`, `REQ-NFR-0015` | `ADR-0005`, `ADR-0011`, `ADR-0010` | MOD-0011, MOD-0006, MOD-0007, MOD-0003 | `TST-0001..TST-0003`, `*` |
| `hunt` | `REQ-FR-0003`, `REQ-NFR-0015` | `ADR-0011`, `ADR-0006`, `ADR-0009` | MOD-0012, MOD-0008, MOD-0009, MOD-0003 | `TST-0004`, `*` |
| `dump` | `REQ-FR-0004` | `ADR-0003`, `ADR-0010` | MOD-0010, MOD-0006, MOD-0007, MOD-0003 | `..` |
| `lint` | `REQ-FR-0005` | `ADR-0006` | MOD-0013, MOD-0008, MOD-0003 | `..`, `` |
| `analyse srum` | `REQ-FR-0006`, `REQ-NFR-0013` | `ADR-0009`, `ADR-0003` | MOD-0014, MOD-0007, MOD-0003 | `TST-0005..TST-0006`, `*` |

> Полный набор `REQ-*` и расширенные связи фиксируются в и развиваются в ходе слайсов (–33).

---

## 9) Открытые вопросы и точки риска

1) EVTX/HVE/MFT/XML парсеры остаются pending (см. `ADR-0009`). Архитектурно это изолировано в `MOD-0007 formats`.
2) `analyse shimcache` требует входных данных для закрытия 1:1 (см. `RISK-0022`).
3) Порядок вывода и детерминизм на больших данных (параллелизм) требует отдельной доказательной базы (`RISK-0011`, `ADR-0011`).

---

## 10) Ссылки
- As‑Is архитектура: `docs/as_is/ARCH-0001-rust-as-is-architecture.md`.
- AR (требования): `docs/requirements/AR-0001-architectural-requirements.md`.
- ADR baseline: `docs/adr/ADR-0001...ADR-0011`.
- GOV 1:1 критерии: `docs/governance/GOV-0002-equivalence-criteria.md`.
- Test matrix: `docs/tests/TESTMAT-0001-rust-test-matrix.md`.
