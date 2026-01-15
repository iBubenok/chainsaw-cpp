# REP-0001 — Эталонные прогоны Chainsaw (Rust) (golden runs): протокол и результаты

## Статус
- Версия: 1
- Статус: Выполнено (native Linux x86_64, release 2.13.1; Windows/macOS прогоны выполнены ранее)
- Baseline: `BASELINE-0001` (см. `docs/baseline/BASELINE-0001-upstream.md`)
- Связанные критерии 1:1: `GOV-0002` (C1–C7)
- Дата подготовки протокола: 2025-12-25

## Цель
Зафиксировать **воспроизводимые** эталонные запуски upstream Chainsaw (Rust) для последующего:
- автоматического **дифф‑сравнения** с C++ портом (/33);
- подтверждения спорных мест CLI‑контракта (unknowns из `CLI-0001`);
- выявления недетерминизма/платформенных отличий Rust (в пределах ОС).

## Ограничения выполнения (текущее состояние)
На момент подготовки протокола запуск upstream Chainsaw в окружении Projects **невозможен** из‑за отсутствия Rust toolchain (`cargo`/`rustc`).

Факт подтверждения см. в (репорт итерации) и в `RISK-0021`.

## Требования к воспроизводимости (GOV-0002)
1) Любой RUN‑сценарий обязан сохранять:
 - `stdout` и `stderr` как **байт‑потоки**;
 - `exit code`;
 - все выходные файлы (если есть `--output` или побочные артефакты);
 - метаданные среды (ОС/архитектура/версии toolchain/локаль/таймзона/cwd).
2) Для сценариев, используемых как проверка детерминизма (C7), требуется минимум **3 повторения**.
3) По умолчанию **нормализация запрещена** (см. GOV-0002/раздел 3).

---

## 1) Подготовка окружения

### 1.1. Минимальные требования
- Rust toolchain (рекомендуемо: stable): `cargo`, `rustc`
- `git` (для фиксации commit/describe)
- Python 3.10+ (для запуска `tools/golden_runs/run_rust_golden_runs.py`)

### 1.2. Управление окружением (рекомендация для стабильности)
Перед запуском сценариев установить:
- Unix/macOS:
 - `export LANG=C`
 - `export LC_ALL=C`
 - `export TZ=UTC`
- Windows:
 - `setx LANG C`
 - `setx TZ UTC` (если применимо)

> Важно: любые изменения окружения, влияющие на stdout/stderr, являются частью наблюдаемого поведения. Поэтому значения переменных фиксируются в `env.json`.

---

## 2) Входные данные и контрольные суммы

### 2.1. Архивы baseline
Контрольные суммы архивов см. в `BASELINE-0001`.

### 2.2. Фикстуры upstream tests (Chainsaw repo)
Эти файлы используются как минимальный стабильный набор для RUN‑сценариев и тест‑к‑тесту.

| Путь (внутри `chainsaw/`) | Размер (байт) | SHA-256 |
|---|---:|---|
| `tests/evtx/security_sample.evtx` | 69632 | `f6a166616d7463c29ff49210bb9323a948a9f109102ba0ab8f1bbca992390d4c` |
| `tests/evtx/rule-any-logon.yml` | 588 | `83798250fa93647d805928cf5a0fb4aeef0898983922c15c3fd778830e543746` |
| `tests/evtx/clo_search_qj_simple_string.txt` | 2741 | `9ec201add80b6bcac8ad21cc8c31ca5210def59c871adcbe9de0fb8b290c0d28` |
| `tests/evtx/clo_search_q_jsonl_simple_string.txt` | 2739 | `920e025e5abe00de6953ce937ff8d672c844289442a7236d8664696cefe5c14a` |
| `tests/evtx/clo_search_q_simple_string.txt` | 2951 | `fec4317d39ad3808db4eed7246bbc0ec3e6203f3a4a4492c4225a6055c51b973` |
| `tests/evtx/clo_hunt_r_any_logon.txt` | 1803 | `ee4ae8b0bce9f9253fd8d2e767e1649f1b62ca6492541a192ed6a10968cb6824` |
| `tests/srum/SRUDB.dat` | 1835008 | `fb3b913c8a94fae7d73f6d5641af9dd1a0040133744e07927082214a436d5c00` |
| `tests/srum/SOFTWARE` | 73924608 | `72a58b5e23ad484227fb92bc6ff0c1e2b0c4a72e6563273192b87088a7f374f1` |
| `tests/srum/analysis_srum_database_table_details.txt` | 5249 | `58a480f7ebbd5c537d9929799a1a2b5677d9b1d3d58f97819ee6e375ddc88488` |
| `tests/srum/analysis_srum_database_json.txt` | 3533274 | `237aadae3f29e7a8919206fa86003c846c642edc515f250a54a937e5019b4042` |
| `mappings/sigma-event-logs-all.yml` | 23865 | `7a8d08a56665520d9db22fa5399079571ba32f640e4ddbd61a824168b5061b71` |
| `mappings/sigma-event-logs-legacy.yml` | 7855 | `ab375bbd9644b3ccdbb67a171cf1747b09920e89e126511b25b485a8681564c6` |

### 2.3. Выбранные файлы из EVTX-ATTACK-SAMPLES (проверка путей с пробелами)
| Путь (внутри `EVTX-ATTACK-SAMPLES/`) | Размер (байт) | SHA-256 |
|---|---:|---|
| `UACME_59_Sysmon.evtx` | 69632 | `1de3775180143fd5d956bae48f572367534ae8c10f6ffbe98dd4b4e0e47e0fa1` |
| `Command and Control/DE_RDP_Tunneling_4624.evtx` | 69632 | `2668d3d7f0505ad453affcb9e2b7206617cf1ec1e5a212bd8f191eb8523c0ab1` |
| `Command and Control/bits_openvpn.evtx` | 1118208 | `9dc80ef8dd521d443016559ee5b0e55837a59bfcc9d790b20b72c38a9eddc40e` |

### 2.4. Выбранное Sigma правило (из sigma.zip) для проверки `lint --kind sigma -t`
| Путь (внутри `sigma/`) | Размер (байт) | SHA-256 |
|---|---:|---|
| `rules/windows/builtin/application/esent/win_esent_ntdsutil_abuse.yml` | 994 | `6fec3879ccc84431df9786aa0d951c1fda8afaf17244cf02e1b7c5ee2854c97a` |

---

## 3) Формат хранения результатов RUN

Результаты каждого RUN‑сценария сохраняются в выделенную директорию. Формат (SoT для структуры):

```
<OUT_DIR>/
 env.json
 manifest.json/
 run_01/
 cmdline.txt
 cwd.txt
 env_overrides.json
 exit_code.txt
 stdout.bin
 stderr.bin
 out_files/ # если сценарий генерирует файлы
 sha256.json
 run_02/... # если repeats > 1/...
```

> `stdout.bin`/`stderr.bin` сохраняются как bytes без перекодирования.

---

## 4) Набор RUN‑сценариев

### 4.1. CLI meta/help/version (подтверждение C1/C2/C3)
- ****: `chainsaw --help`
- ****: `chainsaw --version`
- ****: `chainsaw` (без аргументов)

### 4.2. CLI ошибки (clap/валидация)
- ****: `chainsaw no_such_cmd`
- ****: `chainsaw dump` (ожидается ошибка отсутствия обязательного `path`)
- ****: `chainsaw lint --help`
- ****: `chainsaw lint --kind stalker <chainsaw_rules_dir>` (проверка RISK-0013)

### 4.3. `search` (проверка C2/C3/C7 + подготовка дифф‑harness)
- ****: `chainsaw search 4624 <security_sample.evtx> -q` (repeats: 3)
- ****: `chainsaw search 4624 <security_sample.evtx> -jq` (repeats: 3)
- ****: `chainsaw search 4624 <security_sample.evtx> -q --jsonl` (repeats: 3)
- ****: `chainsaw --num-threads 1 search 4624 <EVTX-ATTACK-SAMPLES/Command and Control/DE_RDP_Tunneling_4624.evtx> -q --jsonl` (проверка путей с пробелами)

### 4.4. `hunt` (проверка C2/C3/C4/C5)
- ****: `chainsaw hunt <security_sample.evtx> -r <rule-any-logon.yml>`
- ****: `chainsaw hunt <security_sample.evtx> -r <rule-any-logon.yml> --jsonl -q`
- ****: `chainsaw hunt <security_sample.evtx> -r <rule-any-logon.yml> --jsonl --cache-to-disk -q` (проверка RISK-0014)

### 4.5. `dump` (проверка C2/C3/C5)
- ****: `chainsaw dump <security_sample.evtx> -q`
- ****: `chainsaw dump <security_sample.evtx> -q --json`

### 4.6. `analyse srum` (проверка C2/C3/C5)
- ****: `chainsaw analyse srum --software <SOFTWARE> <SRUDB.dat> --stats-only -q`
- ****: `chainsaw analyse srum --software <SOFTWARE> <SRUDB.dat> -q`

### 4.7. `lint` для Sigma (проверка конвертации/taulogic)
- ****: `chainsaw lint --kind sigma -t <sigma_rule_file>`

> Примечание: сценарии для `analyse shimcache` не включены из-за отсутствия входных данных (SYSTEM hive) в предоставленных материалах. См. `RISK-0022`.

---

## 5) Как запускать (runbook)

### 5.1. Подготовить директории
Рекомендуемая структура на машине исполнителя:
- `<WORK>/chainsaw/` — распакованный `chainsaw.zip` (baseline commit, см. `BASELINE-0001`)
- `<WORK>/sigma/` — распакованный `sigma.zip`
- `<WORK>/evtx/` — распакованный `EVTX-ATTACK-SAMPLES.zip`
- `<WORK>/out/` — каталог для результатов

### 5.2. Запуск через скрипт
Скрипт: `tools/golden_runs/run_rust_golden_runs.py` (в репозитории порта).

Пример (Linux/macOS):
```bash
python3 tools/golden_runs/run_rust_golden_runs.py \
 --chainsaw-src "<WORK>/chainsaw" \
 --sigma-src "<WORK>/sigma" \
 --evtx-samples "<WORK>/evtx" \
 --out "<WORK>/out/rust_golden_runs" \
 --repeats 3
```

На Windows аналогично (PowerShell):
```powershell
python tools\golden_runs\run_rust_golden_runs.py `
 --chainsaw-src "<WORK>\chainsaw" `
 --sigma-src "<WORK>\sigma" `
 --evtx-samples "<WORK>\evtx" `
 --out "<WORK>\out\rust_golden_runs" `
 --repeats 3
```

### 5.3. Упаковка результатов
Упаковать каталог результатов целиком:
- `rust_golden_runs_<OS>_<arch>_<date>.zip`

И прикрепить архив в Projects.

---

## 6) Результаты (заполняется после выполнения)

| RUN-ID | Статус | Exit code | Повторы | Примечания |
|---|---|---:|---:|---|
| | OK | 0 | 1 | chainsaw --help (linux x86_64) |
| | OK | 0 | 1 | chainsaw --version (linux x86_64) |
| | OK (ожидаемая ошибка) | 2 | 1 | chainsaw без аргументов → clap usage |
| | OK (ожидаемая ошибка) | 2 | 1 | неизвестная команда |
| | OK (ожидаемая ошибка) | 1 | 1 | dump без path |
| | OK | 0 | 1 | lint --help |
| | OK (ожидаемая ошибка) | 2 | 1 | lint --kind stalker |
| | OK | 0 | 3 | search 4624 yaml quiet |
| | OK | 0 | 3 | search 4624 json quiet |
| | OK | 0 | 3 | search 4624 jsonl quiet |
| | OK | 0 | 1 | search 4624 (EVTX-ATTACK-SAMPLES path with spaces) |
| | OK | 0 | 1 | hunt table |
| | OK | 0 | 1 | hunt --jsonl -q |
| | OK | 0 | 1 | hunt --jsonl --cache-to-disk -q |
| | OK | 0 | 1 | dump yaml -q |
| | OK | 0 | 1 | dump json -q |
| | OK | 0 | 1 | analyse srum --stats-only -q |
| | OK | 0 | 1 | analyse srum full -q |
| | OK | 0 | 1 | lint --kind sigma -t win_esent_ntdsutil_abuse.yml |

Архивы результатов (загружать в Projects, не коммитить):
- `rust_golden_runs_linux_x86_64_20260101.zip` — SHA-256 `5b7ce321923c08545b55a2636784a7de2044e11a2db0472b81207c499d75d16f` — содержит `env.json`, `build.json`, `manifest.json`, `RUN-*` (linux native).
- Windows/macOS архивы подготовлены и приложены ранее в Projects (SHA-256 см. проектные вложения); структура ожидается аналогичная согласно разделу 3.
