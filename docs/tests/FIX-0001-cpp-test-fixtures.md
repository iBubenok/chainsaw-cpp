# FIX-0001 — Реестр тестовых фикстур C++ порта

## Статус
- Версия: 2
- Статус: Active
- Назначение: Single Source of Truth для тестовых фикстур C++ порта

## Связь с политикой
- `docs/licensing/POL-0003-test-data-policy.md` — правила категоризации и хранения

## Сводка

| Категория | Количество файлов | Размер | В репозитории |
|-----------|------------------|--------|---------------|
| A (INCLUDE) | 10 | ~82 KB | Да |
| B (REFERENCE-ONLY) | 5 | ~88 MB | Нет |
| C (EXTERNAL) | N/A | ~150 MB | Нет |

---

## Категория A: Включённые фикстуры

### EVTX фикстуры (`cpp/tests/fixtures/evtx/`)

| FIX-ID | Файл | Размер | Тесты | Лицензия | Upstream путь |
|--------|------|--------|-------|----------|---------------|
| FIX-A001 | `security_sample.evtx` | 68 KB | TST-0001..TST-0004 | GPL v3 | `tests/evtx/security_sample.evtx` |
| FIX-A002 | `clo_search_qj_simple_string.txt` | 2.7 KB | TST-0001 | GPL v3 | `tests/evtx/clo_search_qj_simple_string.txt` |
| FIX-A003 | `clo_search_q_jsonl_simple_string.txt` | 2.7 KB | TST-0002 | GPL v3 | `tests/evtx/clo_search_q_jsonl_simple_string.txt` |
| FIX-A004 | `clo_search_q_simple_string.txt` | 2.9 KB | TST-0003 | GPL v3 | `tests/evtx/clo_search_q_simple_string.txt` |
| FIX-A005 | `clo_hunt_r_any_logon.txt` | 1.8 KB | TST-0004 | GPL v3 | `tests/evtx/clo_hunt_r_any_logon.txt` |
| FIX-A006 | `rule-any-logon.yml` | 0.6 KB | TST-0004 | GPL v3 | `tests/evtx/rule-any-logon.yml` |

### Sigma фикстуры (`cpp/tests/fixtures/sigma/`)

| FIX-ID | Файл | Размер | Тесты | Лицензия | Upstream путь |
|--------|------|--------|-------|----------|---------------|
| FIX-A007 | `sigma_simple.yml` | 0.2 KB | TST-0007 | DRL 1.1 | `tests/convert/sigma_simple.yml` |
| FIX-A008 | `sigma_simple_output.yml` | 0.3 KB | TST-0007 | GPL v3 | `tests/convert/sigma_simple_output.yml` |
| FIX-A009 | `sigma_collection.yml` | 0.5 KB | TST-0008 | DRL 1.1 | `tests/convert/sigma_collection.yml` |
| FIX-A010 | `sigma_collection_output.yml` | 0.7 KB | TST-0008 | GPL v3 | `tests/convert/sigma_collection_output.yml` |

---

## Категория B: Reference-only фикстуры

### SRUM фикстуры (не включены)

| FIX-ID | Файл | Размер | Тесты | Лицензия | Upstream путь |
|--------|------|--------|-------|----------|---------------|
| FIX-B001 | `SOFTWARE` | 71 MB | TST-0005, TST-0006 | GPL v3 | `tests/srum/SOFTWARE` |
| FIX-B002 | `SRUDB.dat` | 1.8 MB | TST-0005, TST-0006 | GPL v3 | `tests/srum/SRUDB.dat` |
| FIX-B003 | `analysis_srum_database_table_details.txt` | 5 KB | TST-0005 | GPL v3 | `tests/srum/analysis_srum_database_table_details.txt` |
| FIX-B004 | `analysis_srum_database_json.txt` | 3.4 MB | TST-0006 | GPL v3 | `tests/srum/analysis_srum_database_json.txt` |

**Получение:** см. `cpp/tests/fixtures/srum/README.md`

### Shimcache фикстуры (`cpp/tests/fixtures/shimcache/`)

| FIX-ID | Файл | Размер | Тесты | Лицензия | Источник |
|--------|------|--------|-------|----------|----------|
| FIX-B005 | `SYSTEM.hive` | 12 MB | TST-SHIM-001..020 | N/A (собственные данные) | Windows 11 VM (`reg save HKLM\SYSTEM`) |

**Получение:**
```bash
# На Windows VM:
reg save HKLM\SYSTEM C:\Users\user\SYSTEM.hive /y
# Скопировать на хост:
scp user@windows-vm:C:/Users/user/SYSTEM.hive cpp/tests/fixtures/shimcache/
```

---

## Категория C: Внешние датасеты

| ID | Датасет | Размер | Baseline | Лицензия | Расположение |
|----|---------|--------|----------|----------|--------------|
| DS-001 | Sigma rules | 84 MB | `2952d630a` | DRL 1.1 | `upstream/sigma/` |
| DS-002 | EVTX-ATTACK-SAMPLES | 67 MB | `4ceed2f4` | GPL v3 | `upstream/EVTX-ATTACK-SAMPLES/` |

---

## Трассировка: Фикстуры → Тесты

| Тест | Описание | Фикстуры |
|------|----------|----------|
| TST-0001 | search -jq | FIX-A001, FIX-A002 |
| TST-0002 | search -q --jsonl | FIX-A001, FIX-A003 |
| TST-0003 | search -q | FIX-A001, FIX-A004 |
| TST-0004 | hunt -r | FIX-A001, FIX-A005, FIX-A006 |
| TST-0005 | analyse srum --stats-only | FIX-B001, FIX-B002, FIX-B003 |
| TST-0006 | analyse srum (full) | FIX-B001, FIX-B002, FIX-B004 |
| TST-0007 | sigma::load (simple) | FIX-A007, FIX-A008 |
| TST-0008 | sigma::load (collection) | FIX-A009, FIX-A010 |
| TST-SHIM-001..020 | analyse shimcache | FIX-B005 |

---

## История изменений

| Дата | Версия | Изменения |
|------|--------|-----------|
| 2026-01-13 | 2 | Добавлена shimcache фикстура FIX-B005 (RISK-0022 закрыт) |
| 2026-01-09 | 1 | Первоначальный реестр |
