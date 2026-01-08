# INV-0001 — Инвентаризация входных материалов (Step 2)

## Цель
Зафиксировать состав и объём входных источников (Chainsaw Rust, Sigma rules, EVTX samples) и дать навигационную карту по ключевым каталогам/файлам **без интерпретации поведения**.

## Связанные артефакты проекта
- ART-0004 — `chainsaw.zip` (исходники Chainsaw на Rust)
- ART-0005 — `sigma.zip` (набор Sigma правил)
- ART-0006 — `EVTX-ATTACK-SAMPLES.zip` (набор EVTX примеров)

## Методика инвентаризации (воспроизводимо)
1) Распаковать архивы в отдельные каталоги.
2) Зафиксировать:
   - размер архива (bytes),
   - размер распакованного дерева,
   - количество файлов/каталогов,
   - ключевые файлы верхнего уровня,
   - структуру ключевых каталогов (до уровня навигации).

> Примечание: абсолютные пути распаковки зависят от окружения. Для повторения результатов достаточно распаковать архивы «как есть» и сравнивать **относительные** пути/структуру.

## Сводная таблица объёма и типов артефактов

| Артефакт | Размер архива | Размер распакованного дерева | Файлов | Каталогов | Типы артефактов (наблюдаемо по расширениям/расположению) |
|---|---:|---:|---:|---:|---|
| ART-0004 `chainsaw.zip` | 33308570 bytes | ~93 MiB | 202 | 57 | исходники Rust (`*.rs`), тесты (`tests/*.rs`), правила/маппинги (`*.yml`/`*.yaml`), фикстуры (в т.ч. `*.evtx`, `*.dat`), документация (`*.md`), метаданные Git |
| ART-0005 `sigma.zip` | 60394184 bytes | ~71 MiB | 4389 | 704 | правила Sigma (`*.yml`/`*.yaml`), документация (`*.md`), тесты/регрессионные данные (`tests/`, `regression_data/`), метаданные Git |
| ART-0006 `EVTX-ATTACK-SAMPLES.zip` | 12530028 bytes | ~67 MiB | 344 | 34 | EVTX-логи (`*.evtx`), документы (`README.md`), лицензия, изображения, скрипты (`*.ps1`), метаданные/утилиты (в т.ч. `*.ipynb`, `*.py`), метаданные Git |

---

## ART-0004 — Chainsaw (Rust) (`chainsaw.zip`)

### Ключевые файлы верхнего уровня (наблюдаемо)
- `Cargo.toml`, `Cargo.lock`
- `README.md`
- `LICENCE`
- `flake.nix`, `flake.lock`
- `.git/`, `.github/`, `.gitignore`, `.gitmodules`

### Наблюдаемая структура верхнего уровня
- `src/`
- `tests/`
- `rules/`
- `mappings/`
- `analysis/`
- `images/`

### Навигационная карта `src/` (файлы до 2 уровней)
- `src/main.rs`
- `src/lib.rs`
- `src/cli.rs`
- `src/analyse/`: `mod.rs`, `shimcache.rs`, `srum.rs`
- `src/ext/`: `mod.rs`, `tau.rs`
- `src/file/`: `mod.rs`, `evtx.rs`, `json.rs`, `mft.rs`, `xml.rs`
- `src/rule/`: `mod.rs`, `chainsaw.rs`, `sigma.rs`
- Прочие файлы в `src/`: `hunt.rs`, `search.rs`, `value.rs`, `write.rs`

### Навигационная карта `tests/`
- Rust-тесты: `tests/clo.rs`, `tests/common.rs`, `tests/convert.rs`
- Фикстуры:
  - `tests/convert/` — входные/выходные YAML (`*.yml`)
  - `tests/evtx/` — `security_sample.evtx`, YAML-правило и текстовые эталоны (`*.txt`)
  - `tests/srum/` — `SOFTWARE`, `SRUDB.dat` и текстовые эталоны (`*.txt`)

### Навигационная карта встроенных правил и маппингов
- `rules/evtx/` — YAML-файлы правил (`*.yml`/`*.yaml`) в подкаталогах
- `rules/mft/` — YAML-файлы правил (`*.yml`)
- `mappings/` — `sigma-event-logs-all.yml`, `sigma-event-logs-legacy.yml`

### Git submodules (факт структуры репозитория)
Файл `.gitmodules` содержит 2 submodule-записи:
- `sigma_rules` → `https://github.com/SigmaHQ/sigma`
- `evtx_attack_samples` → `https://github.com/sbousseaden/EVTX-ATTACK-SAMPLES`

---

## ART-0005 — Sigma rules (`sigma.zip`)

### Ключевые файлы верхнего уровня (наблюдаемо)
- `README.md`, `Releases.md`
- `LICENSE`
- `CONTRIBUTING.md`, `.yamllint`, `.gitattributes`, `.github/`

### Наблюдаемая структура верхнего уровня
- `rules/` — основной набор правил
- `rules-compliance/`, `rules-emerging-threats/`, `rules-threat-hunting/`, `rules-placeholder/`
- `deprecated/`
- `unsupported/`
- `tests/`
- `regression_data/`
- `documentation/`, `images/`, `other/`

### Количественные показатели по YAML-правилам (по каталогам верхнего уровня)
- `rules/`: 3083 файла `*.yml`/`*.yaml`
- `rules-emerging-threats/`: 436 YAML
- `rules-threat-hunting/`: 131 YAML
- `rules-compliance/`: 3 YAML
- `rules-placeholder/`: 14 YAML
- `unsupported/`: 87 YAML
- `deprecated/`: 165 YAML

### Навигационная карта `rules/` (каталоги до 2 уровней)
- `rules/application/`: `bitbucket`, `django`, `github`, `jvm`, `kubernetes`, `nodejs`, `opencanary`, `python`, `rpc_firewall`, `ruby`, `spring`, `sql`, `velocity`
- `rules/category/`: `antivirus`, `database`
- `rules/cloud/`: `aws`, `azure`, `gcp`, `m365`
- `rules/identity/`: `cisco_duo`, `okta`, `onelogin`
- `rules/linux/`: `auditd`, `builtin`, `file_event`, `network_connection`, `process_creation`
- `rules/macos/`: `file_event`, `process_creation`
- `rules/network/`: `cisco`, `dns`, `firewall`, `fortinet`, `huawei`, `juniper`, `zeek`
- `rules/web/`: `product`, `proxy_generic`, `webserver_generic`
- `rules/windows/`: `builtin`, `create_remote_thread`, `create_stream_hash`, `dns_query`, `driver_load`, `file`, `image_load`, `network_connection`, `pipe_created`, `powershell`, `process_access`, `process_creation`, `process_tampering`, `raw_access_thread`, `registry`, `sysmon`, `wmi_event`

---

## ART-0006 — EVTX-ATTACK-SAMPLES (`EVTX-ATTACK-SAMPLES.zip`)

### Ключевые файлы верхнего уровня (наблюдаемо)
- `README.md`
- `LICENSE.GPL`
- `evtx_data.csv`
- `temp-plot.html`
- `Evtx-to-Xml.ps1`, `Winlogbeat-Bulk-Read.ps1`
- `winlogbeat_example.yml`
- Изображения: `AIEvent.jpg`, `EVTX_DataSet_Stats.PNG`, `HeatMap.PNG`, `mitre_evtx_repo_map.png`

### Наблюдаемая структура верхнего уровня
Каталоги (на уровне верхнего каталога):
- `AutomatedTestingTools/`
- `Command and Control/`
- `Credential Access/`
- `Defense Evasion/`
- `Discovery/`
- `Execution/`
- `Lateral Movement/`
- `Persistence/`
- `Privilege Escalation/`
- `Other/`
- `EVTX_ATT&CK_Metadata/`

### Количественные показатели по EVTX
- Всего `*.evtx` файлов в датасете: **278**
- Распределение `*.evtx` по верхним каталогам:
  - `AutomatedTestingTools/`: 8
  - `Command and Control/`: 6
  - `Credential Access/`: 39
  - `Defense Evasion/`: 36
  - `Discovery/`: 11
  - `Execution/`: 34
  - `Lateral Movement/`: 47
  - `Persistence/`: 22
  - `Privilege Escalation/`: 66
  - `Other/`: 8

### Навигационная карта `EVTX_ATT&CK_Metadata/` (до 2 уровней)
- `EVTX_Metadata.ipynb`
- `README.md`
- `Evtx/` — каталог с Python файлами:
  - `BinaryParser.py`, `Evtx.py`, `Nodes.py`, `Views.py`, `__init__.py`
- Изображения: `image_bar.png`, `image_barh.png`, `image_pie.png`, `sankey.png`
