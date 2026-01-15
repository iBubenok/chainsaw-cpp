# SPEC-SLICE-003 — Micro-spec для CLI Parser & Help/Version

## Метаданные
- **SLICE-ID:** SLICE-003
- **MOD-*:** MOD-0002 (CLI Layer)
- **TST-*:** — (нет upstream тестов для CLI parser непосредственно; покрытие через RUN-*)
- **RUN-*:**,,,..
- **REQ-*:** REQ-FR-01.1 (CLI-интерфейс 1:1), REQ-FR-05 (коды возврата), REQ-FR-06 (сообщения об ошибках)
- **RISK-*:** RISK-0031 (byte-to-byte help/errors)

---

## Наблюдаемое поведение (FACTS)

### Структура CLI

**FACT-CLI-001**: CLI использует `clap` v4 с derive-макросами (`Parser`, `Subcommand`, `ArgAction`).
- Источник: `/src/main.rs:16` — `use clap::{ArgAction, Parser, Subcommand};`

**FACT-CLI-002**: Имя бинаря: `chainsaw`; поле `about`: `"Rapidly work with Forensic Artefacts"`.
- Источник: `/src/main.rs:24-27`

**FACT-CLI-003**: Глобальные опции (применимы ко всем подкомандам):
- `--no-banner` — скрыть ASCII-баннер
- `--num-threads <NUM_THREADS>` — ограничить количество потоков (default: num of CPUs)
- `-v` — повторяемый (`ArgAction::Count`), уровень подробности вывода
- Источник: `/src/main.rs:44-56`

**FACT-CLI-004**: 5 подкоманд первого уровня:
1. `dump` — Dump artefacts into a different format
2. `hunt` — Hunt through artefacts using detection rules for threat detection
3. `lint` — Lint provided rules to ensure that they load correctly
4. `search` — Search through forensic artefacts for keywords or patterns
5. `analyse` — Perform various analyses on artefacts (с вложенными: `shimcache`, `srum`)
- Источник: `/src/main.rs:58-323`, enum `Command`

**FACT-CLI-005**: Подкоманда `help` генерируется `clap` автоматически.
- Источник: поведение clap v4 по умолчанию

**FACT-CLI-006**: Стандартные флаги `-h`/`--help` и `-V`/`--version` генерируются `clap`.
- Источник: `#[clap(..., version)]` в `/src/main.rs:42`

### Exit Codes

**FACT-CLI-007**: Exit codes:
- `0` — успешное выполнение
- `1` — runtime error (ошибка выполнения, через `std::process::exit(1)` после печати `[x]...`)
- `2` — clap parsing/usage error (неверные аргументы, неизвестная команда)
- Источник: `/src/main.rs:1152-1160` (exit 1), clap defaults (exit 2)

### Golden Runs Верификация

**FACT-CLI-008**: `--help`:
- stdout: текст справки (см. golden run)
- stderr: пусто
- exit code: `0`
- Источник: `golden/rust_golden_runs_linux_x86_64//run_01/`

**FACT-CLI-009**: `--version`:
- stdout: `chainsaw 2.13.1\n`
- stderr: пусто
- exit code: `0`
- Источник: `golden/rust_golden_runs_linux_x86_64//run_01/`

**FACT-CLI-010**: Без аргументов:
- stdout: пусто
- stderr: текст справки (идентичен `--help`)
- exit code: `2`
- Источник: `golden/rust_golden_runs_linux_x86_64//run_01/`

**FACT-CLI-011**: Неизвестная подкоманда `no_such_cmd`:
- stdout: пусто
- stderr:
 ```
 error: unrecognized subcommand 'no_such_cmd'

 Usage: chainsaw [OPTIONS] <COMMAND>

 For more information, try '--help'.
 ```
- exit code: `2`
- Источник: `golden/rust_golden_runs_linux_x86_64//run_01/`

**FACT-CLI-012**: `dump` без обязательного path:
- stdout: пусто
- stderr: ASCII banner + `[x] No compatible files were found in the provided paths` (с ANSI-кодами)
- exit code: `1` (runtime error, не parsing error)
- Источник: `golden/rust_golden_runs_linux_x86_64//run_01/`

**FACT-CLI-013**: `lint --help`:
- stdout: текст справки подкоманды lint
- stderr: пусто
- exit code: `0`
- Источник: `golden/rust_golden_runs_linux_x86_64//run_01/`

**FACT-CLI-014**: `lint --kind stalker...`:
- stdout: пусто
- stderr:
 ```
 error: invalid value 'stalker' for '--kind <KIND>': unknown kind, must be: chainsaw, or sigma

 For more information, try '--help'.
 ```
- exit code: `2`
- Источник: `golden/rust_golden_runs_linux_x86_64//run_01/`

### Детали CLI структуры

**FACT-CLI-015**: `after_help` содержит примеры использования (Examples:).
- Источник: `/src/main.rs:28-41`

**FACT-CLI-016**: Структура `Args` содержит поля: `no_banner`, `num_threads`, `verbose`, `cmd` (Command enum).
- Источник: `/src/main.rs:44-56`

**FACT-CLI-017**: `Command` enum содержит варианты: `Dump`, `Hunt`, `Lint`, `Search`, `Analyse` (с вложенным `AnalyseCommand`).
- Источник: `/src/main.rs:58-277`

**FACT-CLI-018**: `lint --kind` принимает только `chainsaw` или `sigma`; `stalker` не поддерживается (исторический артефакт в help-строке).
- Источник: `/src/rule/mod.rs:127-137` (enum `RuleKind`)

**FACT-CLI-019**: `clap` `ArgGroup` используется для mutual exclusion форматов (--json/--jsonl/--csv/--log) в подкомандах.
- Источник: `/src/main.rs` — `#[arg(group = "format",...)]`

**FACT-CLI-020**: ASCII banner выводится через `print_title` в stderr (через `cs_eprintln!`).
- Источник: `/src/main.rs:325-337`

---

## Ожидаемые входы/выходы

### Вход
- `argv` — аргументы командной строки

### Выход
- Структура `Args` с распарсенными значениями (при успехе)
- Ошибка парсинга с сообщением в stderr + exit code (при ошибке)

### CLI Help Format (golden)
```
Rapidly work with Forensic Artefacts

Usage: chainsaw [OPTIONS] <COMMAND>

Commands:
 dump Dump artefacts into a different format
 hunt Hunt through artefacts using detection rules for threat detection
 lint Lint provided rules to ensure that they load correctly
 search Search through forensic artefacts for keywords or patterns
 analyse Perform various analyses on artefacts
 help Print this message or the help of the given subcommand(s)

Options:
 --no-banner Hide Chainsaw's banner
 --num-threads <NUM_THREADS> Limit the thread number (default: num of CPUs)
 -v... Print verbose output
 -h, --help Print help
 -V, --version Print version

Examples:

 Hunt with Sigma and Chainsaw Rules:
./chainsaw hunt evtx_attack_samples/ -s sigma/ --mapping mappings/sigma-event-logs-all.yml -r rules/

 Hunt with Sigma rules and output in JSON:
./chainsaw hunt evtx_attack_samples/ -s sigma/ --mapping mappings/sigma-event-logs-all.yml --json

 Search for the case-insensitive word 'mimikatz':
./chainsaw search mimikatz -i evtx_attack_samples/

 Search for Powershell Script Block Events (EventID 4014):
./chainsaw search -t 'Event.System.EventID: =4104' evtx_attack_samples/
```

### Version Format (golden)
```
chainsaw 2.13.1
```

### Exit Codes
| Условие | Exit Code |
|---------|-----------|
| Успешное выполнение | 0 |
| Runtime error (после начала выполнения команды) | 1 |
| Usage/parsing error (неверные аргументы, неизвестная команда) | 2 |

---

## Критерий закрытия слайса (UnitDone)

### Обязательные проверки

1. **`--help` byte-to-byte**:
 - stdout C++ == stdout Rust (golden)
 - stderr C++ пусто
 - exit code C++ == 0

2. **`--version` byte-to-byte**:
 - stdout C++ == stdout Rust (golden)
 - stderr C++ пусто
 - exit code C++ == 0

3. **Без аргументов**:
 - stdout C++ пусто
 - stderr C++ == stdout (help text)
 - exit code C++ == 2

4. **Неизвестная подкоманда**:
 - stdout C++ пусто
 - stderr C++ содержит "error: unrecognized subcommand" (формат clap)
 - exit code C++ == 2

5. **Невалидное значение аргумента**:
 - stdout C++ пусто
 - stderr C++ содержит "error: invalid value" (формат clap)
 - exit code C++ == 2

6. **Help подкоманды**:
 - stdout C++ == stdout Rust (golden)
 - stderr C++ пусто
 - exit code C++ == 0

### Допустимые различия (нормализация)

Согласно `GOV-0002` и `ADR-0006`:
- Версия в `--version` может отличаться (C++ порт — своя версия)
- Trailing newlines нормализуются при сравнении
- ANSI escape codes должны совпадать для цветного вывода

### Критерии по GOV-0002
- **C1 (CLI-контракт)**: подкоманды, опции, аргументы
- **C2 (stdout/stderr)**: byte-to-byte для help/version/errors
- **C3 (exit codes)**: 0/1/2 по семантике

---

## Зависимости

| SLICE-* | Статус | Блокер? |
|---------|--------|---------|
| SLICE-001 (Platform Layer) | Done | Нет |
| SLICE-002 (Output Layer) | Done | Нет |

**Вывод**: все зависимости закрыты, слайс можно брать в работу.

---

## Архитектурные решения (релевантные)

- **ADR-0006**: CLI и user-visible output — собственный CLI слой без сторонних библиотек
- **ADR-0010**: Кроссплатформенные абстракции

---

## Риски

| RISK-* | Описание | Влияние на слайс |
|--------|----------|------------------|
| RISK-0031 | Byte-to-byte совпадение help/errors | Критичен — требует точного воспроизведения формата clap |

---

## Ссылки

- Rust CLI код: `upstream/chainsaw/src/main.rs` (строки 24-323, 401-1160)
- Rust platform-specific CLI: `upstream/chainsaw/src/cli.rs`
- CLI контракт: `docs/as_is/CLI-0001-chainsaw-cli-contract.md`
- Golden runs: `golden/rust_golden_runs_linux_x86_64//`.. `/`
- To-Be архитектура (MOD-0002): `docs/architecture/TOBE-0001-cpp-to-be-architecture.md`
- ADR-0006: `docs/adr/ADR-0006-cli-and-user-visible-output.md`
- Критерии 1:1: `docs/governance/GOV-0002-equivalence-criteria.md`
