# POL-0003 — Политика тестовых данных для C++ проекта

## Цель

Определить правила работы с тестовыми данными (EVTX/Sigma/SRUM) для C++ порта Chainsaw:
- что включается в репозиторий;
- что хранится по ссылке / скачивается при необходимости;
- как обеспечить воспроизводимость и соблюдение лицензий.

## Область применения

- Фикстуры для test-to-test переноса (`TESTMAT-0001`)
- Данные для golden runs и harness сравнения
- Внешние датасеты (Sigma rules, EVTX-ATTACK-SAMPLES)

## Связанные документы

- `docs/licensing/LIC-0001-upstream-licenses.md` — лицензии upstream
- `docs/licensing/POL-0001-third-party-policy.md` — политика third-party компонентов
- `docs/tests/TESTMAT-0001-rust-test-matrix.md` — матрица тестов Rust
- `docs/baseline/BASELINE-0001-upstream.md` — baseline версии

---

## Классификация тестовых данных

### Категория A: INCLUDE (включаются в репозиторий)

**Критерии:**
- Размер файла < 1 MB
- Необходимы для базовых тестов (unit, integration)
- Имеют известную лицензию, совместимую с GPL v3

**Что включено:**

| Файл | Размер | Назначение | Тесты | Лицензия |
|------|--------|------------|-------|----------|
| `fixtures/evtx/security_sample.evtx` | 68 KB | EVTX для search/hunt | TST-0001..TST-0004 | GPL v3 (Chainsaw) |
| `fixtures/evtx/clo_search_qj_simple_string.txt` | 2.7 KB | Expected stdout | TST-0001 | GPL v3 |
| `fixtures/evtx/clo_search_q_jsonl_simple_string.txt` | 2.7 KB | Expected stdout | TST-0002 | GPL v3 |
| `fixtures/evtx/clo_search_q_simple_string.txt` | 2.9 KB | Expected stdout | TST-0003 | GPL v3 |
| `fixtures/evtx/clo_hunt_r_any_logon.txt` | 1.8 KB | Expected stdout | TST-0004 | GPL v3 |
| `fixtures/evtx/rule-any-logon.yml` | 0.6 KB | Chainsaw rule | TST-0004 | GPL v3 |
| `fixtures/sigma/sigma_simple.yml` | 0.2 KB | Sigma rule вход | TST-0007 | DRL 1.1 |
| `fixtures/sigma/sigma_simple_output.yml` | 0.3 KB | Expected YAML | TST-0007 | GPL v3 |
| `fixtures/sigma/sigma_collection.yml` | 0.5 KB | Sigma collection | TST-0008 | DRL 1.1 |
| `fixtures/sigma/sigma_collection_output.yml` | 0.7 KB | Expected YAML | TST-0008 | GPL v3 |

**Общий размер:** ~82 KB

### Категория B: Git LFS (большие файлы в репозитории через LFS)

**Критерии:**
- Размер файла >= 1 MB
- Необходимы для специфичных тестов (SRUM, shimcache)
- Хранятся через Git LFS для предотвращения раздувания репозитория

**Что включено через LFS:**

| Файл | Размер | Назначение | Тесты | Хранение |
|------|--------|------------|-------|----------|
| `fixtures/srum/SOFTWARE` | 71 MB | SOFTWARE hive | TST-0005, TST-0006 | Git LFS |
| `fixtures/srum/SRUDB.dat` | 1.8 MB | SRUM database | TST-0005, TST-0006 | Git LFS |
| `fixtures/srum/analysis_srum_database_json.txt` | 3.4 MB | Expected stdout | TST-0006 | Git LFS |
| `fixtures/shimcache/SYSTEM.hive` | ~50 MB | SYSTEM hive | TST-SHIM-* | Git LFS |

**Механизм работы:**
1. Файлы описаны в `.gitattributes` как LFS-объекты
2. При `git clone` автоматически скачиваются (если LFS установлен)
3. CI: `actions/checkout@v4` с `lfs: true` обеспечивает скачивание
4. Локальная разработка: `git lfs install && git lfs pull`

**Скрипт для обновления:** `tools/data/fetch_fixtures.py`

### Категория C: EXTERNAL DATASETS (внешние датасеты)

**Что не включается в репозиторий:**

| Датасет | Размер | Где хранится | Baseline |
|---------|--------|--------------|----------|
| Sigma rules | 84 MB | `upstream/sigma/` | `2952d630a` (DRL 1.1) |
| EVTX-ATTACK-SAMPLES | 67 MB | `upstream/EVTX-ATTACK-SAMPLES/` | `4ceed2f4` (GPL v3) |

**Правила использования:**
- Не коммитятся в репозиторий порта
- Используются через `upstream/` для golden runs
- При портировании тестов — только минимальные подмножества (Категория A)

---

## Правила хранения и организации

### FIX-RULE-001 — Структура каталогов

```
cpp/tests/fixtures/
├── LICENSE # GPLv3 (от Chainsaw)
├── README.md # Происхождение и правила
├── evtx/ # EVTX фикстуры (Категория A)
│ ├── security_sample.evtx
│ ├── clo_*.txt # Expected outputs
│ └── rule-any-logon.yml
├── sigma/ # Sigma фикстуры (Категория A)
│ ├── sigma_simple.yml
│ ├── sigma_simple_output.yml
│ ├── sigma_collection.yml
│ └── sigma_collection_output.yml
└── srum/ # SRUM фикстуры (Категория B)
 └── README.md # Инструкция по получению
```

### FIX-RULE-002 — Лицензии и атрибуция

1. В корне `cpp/tests/fixtures/` должен быть файл `LICENSE` с текстом GPL v3.
2. Файл `README.md` должен содержать:
 - Происхождение (upstream URL + commit)
 - Тип лицензии каждой группы фикстур
 - Инструкции по обновлению

3. Для Sigma-правил: сохранять поле `author` без изменений (DRL 1.1).

### FIX-RULE-003 — Обновление фикстур

При обновлении upstream Chainsaw:
1. Проверить изменения в `upstream/chainsaw/tests/`
2. Обновить фикстуры Категории A
3. Обновить golden runs (если затронуто)
4. Обновить `README.md` с новым baseline

### FIX-RULE-004 — Воспроизводимость тестов

**Обязательные тесты (Категория A):**
- Всегда доступны и запускаются в CI
- Покрытие: TST-0001..TST-0004, TST-0007, TST-0008

**Опциональные тесты (Категория B):**
- Требуют ручного скачивания фикстур
- В CI могут быть skip или в отдельном job с кэшем
- Покрытие: TST-0005, TST-0006 (SRUM)

---

## Связь с рисками

| RISK-ID | Статус после | Обоснование |
|---------|---------------------|-------------|
| RISK-0003 | Closed | Политика определена: что коммитим (A), что ссылкой (B), что исключаем (C) |
| RISK-0006 | Mitigated | Baseline версии подтверждены; связь submodules — принятое ограничение |
| RISK-0009 | Open | DRL 1.1 атрибуция обеспечена сохранением `author` в Sigma; формат вывода 1:1 не меняется |

---

## Проверка соответствия политике

**Чек-лист для code review:**

- Новая фикстура соответствует критериям Категории A/B/C
- Размер < 1 MB для включения в репозиторий
- Лицензия известна и совместима
- Происхождение указано в README
- Трассировка к тестам (`TST-*`) задокументирована
- Для Sigma: поле `author` сохранено

**Автоматическая проверка:**
- `git ls-files cpp/tests/fixtures/ | xargs du -ch | tail -1` — проверка размера
