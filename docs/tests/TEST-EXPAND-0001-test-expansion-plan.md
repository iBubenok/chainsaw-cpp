# TEST-EXPAND-0001 — План расширения тестов (на основе рисков)

## Статус
- **Версия:** 1.1
- **Дата:** 2026-01-15
- **Шаг:**
- **Статус:** Partially Implemented

### Реализация
- **P1 тесты:** 14/14 реализованы ✅
- **P2 тесты (partial):** 13/27 реализованы ✅
- **P3 тесты:** 0/4 (отложены)
- **Артефакт:** `cpp/tests/test_extended_gtest.cpp`
- **Результаты:** 505/505 PASS на 3 платформах + ASan/UBSan

## Цель

Определить набор расширенных тестов для C++ порта Chainsaw сверх портированных upstream тестов (TST-0001..TST-0022).

**Категории расширения:**
1. **Robustness** — устойчивость к ошибочным/битым входам
2. **Security** — защита от path traversal, resource exhaustion, injection
3. **Stress** — большие файлы, много файлов, граничные случаи
4. **Determinism** — детерминизм повторных прогонов

## Принципы

1. **Каждый тест связан с риском или инвариантом** — без «тестов ради тестов»
2. **Не дублировать существующее покрытие** — 478 unit-тестов уже покрывают основную функциональность
3. **Приоритет по P1 рискам** — сначала закрываем критичные риски
4. **Кроссплатформенность** — тесты должны работать на 3 ОС

---

## 1. Анализ открытых рисков

### 1.1. Риски, требующие тестового покрытия

| RISK-ID | Описание | Приоритет | Категория теста | Статус покрытия |
|---------|----------|-----------|-----------------|-----------------|
| RISK-0011 | Детерминизм (rayon) | P1 | Determinism | Частично |
| RISK-0031 | CLI byte-to-byte | P1 | Robustness | Частично |
| RISK-0040 | EVTX JSON format (GAP-001) | P2 | Robustness | Не покрыто |
| RISK-0014 | `--cache-to-disk` FS эффекты | P2 | Security | Не покрыто |
| RISK-0025 | MFT data streams имена | P2 | Security | Не покрыто |
| RISK-0026 | XML parsing coverage | P3 | Robustness | Частично |
| RISK-0028 | `--no-banner` согласованность | P2 | Robustness | Частично |

### 1.2. Риски, не требующие тестового покрытия

| RISK-ID | Описание | Причина исключения |
|---------|----------|--------------------|
| RISK-0008 | GPLv3 совместимость | Юридический риск |
| RISK-0009 | DRL 1.1 атрибуция | Юридический риск |
| RISK-0010 | GPLv3 only vs or-later | Юридический риск |
| RISK-0027 | hex dependency (Assumption) | Допущение, не тестируемо |
| RISK-0029 | Windows golden runs git | Операционный риск |
| RISK-0032 | libyal build complexity | Операционный риск |

---

## 2. Требования безопасности (SEC)

Из AR-0001, требующие тестового покрытия:

| REQ-ID | Описание | Категория | Текущее покрытие |
|--------|----------|-----------|------------------|
| REQ-SEC-0017 | Недоверенные входы, устойчивость | Robustness | Частично (negative golden runs) |
| REQ-SEC-0018 | Контроль FS побочных эффектов | Security | Частично |
| REQ-SEC-0020 | Безопасные временные файлы | Security | Не покрыто |
| REQ-SEC-0021 | Path traversal защита | Security | Не покрыто |
| REQ-SEC-0022 | Отсутствие UB (sanitizers) | Security | Не покрыто |

---

## 3. План расширенных тестов

### 3.1. Категория: Determinism

**Цель:** Подтвердить детерминизм вывода при повторных запусках.

| TEST-ID | Описание | Связь | Приоритет |
|---------|----------|-------|-----------|
| TST-DET-001 | Hunt: 3 повтора на одном EVTX | RISK-0011, C7 | P1 |
| TST-DET-002 | Search: 3 повтора с regex | RISK-0011, C7 | P1 |
| TST-DET-003 | Dump: 3 повтора JSON output | RISK-0011, C7 | P1 |
| TST-DET-004 | Analyse srum: 3 повтора | RISK-0011, C7 | P2 |
| TST-DET-005 | Analyse shimcache: 3 повтора | RISK-0011, C7 | P2 |

**Критерий прохождения:** stdout/stderr/exit code идентичны между повторами.

**Реализация:** Harness script с 3+ повторениями и diff проверкой.

---

### 3.2. Категория: Robustness (REQ-SEC-0017)

**Цель:** Подтвердить корректную обработку битых/некорректных входов без crash.

| TEST-ID | Описание | Связь | Приоритет |
|---------|----------|-------|-----------|
| TST-ROB-001 | Truncated EVTX (обрезанный заголовок) | REQ-SEC-0017 | P1 |
| TST-ROB-002 | Corrupted EVTX (битый chunk) | REQ-SEC-0017 | P1 |
| TST-ROB-003 | Empty EVTX (0 bytes) | REQ-SEC-0017 | P1 |
| TST-ROB-004 | Non-EVTX file with.evtx extension | REQ-SEC-0017 | P2 |
| TST-ROB-005 | Invalid YAML Sigma rule | REQ-SEC-0017 | P2 |
| TST-ROB-006 | Malformed Chainsaw rule | REQ-SEC-0017 | P2 |
| TST-ROB-007 | Truncated HVE (registry hive) | REQ-SEC-0017 | P2 |
| TST-ROB-008 | Corrupted MFT entry | REQ-SEC-0017 | P2 |
| TST-ROB-009 | Invalid ESEDB header | REQ-SEC-0017 | P2 |
| TST-ROB-010 | Very long path (>260 chars Windows) | REQ-SEC-0017, RISK-0007 | P2 |
| TST-ROB-011 | Path with special characters | RISK-0007 | P2 |
| TST-ROB-012 | Unicode path names | RISK-0007 | P2 |

**Критерий прохождения:** Процесс не падает (no crash, no SEGFAULT); корректный exit code.

**Реализация:** Unit-тесты с синтетическими битыми фикстурами.

---

### 3.3. Категория: Security (REQ-SEC-0018..0022)

**Цель:** Подтвердить защиту от атак через входные данные.

| TEST-ID | Описание | Связь | Приоритет |
|---------|----------|-------|-----------|
| TST-SEC-001 | Path traversal в output path (`../../../`) | REQ-SEC-0021 | P1 |
| TST-SEC-002 | Absolute path injection в MFT stream name | REQ-SEC-0021, RISK-0025 | P1 |
| TST-SEC-003 | Null byte в пути файла | REQ-SEC-0021 | P1 |
| TST-SEC-004 | Symlink traversal (Unix) | REQ-SEC-0021 | P2 |
| TST-SEC-005 | Junction point traversal (Windows) | REQ-SEC-0021 | P2 |
| TST-SEC-006 | Large file resource limit (>1GB EVTX) | REQ-SEC-0017 | P2 |
| TST-SEC-007 | Deep recursion Sigma condition | REQ-SEC-0017 | P2 |
| TST-SEC-008 | Regex catastrophic backtracking | REQ-SEC-0017 | P2 |
| TST-SEC-009 | Temporary file cleanup on error | REQ-SEC-0020 | P2 |
| TST-SEC-010 | Temporary file permissions | REQ-SEC-0020 | P3 |

**Критерий прохождения:** Атака не приводит к записи за пределами output dir; ресурсы ограничены.

**Реализация:** Unit-тесты + integration тесты с проверкой FS состояния.

---

### 3.4. Категория: Stress (REQ-NFR-0013)

**Цель:** Подтвердить обработку граничных случаев по размеру/количеству.

| TEST-ID | Описание | Связь | Приоритет |
|---------|----------|-------|-----------|
| TST-STR-001 | Large EVTX (100MB+) | REQ-NFR-0013 | P2 |
| TST-STR-002 | Many EVTX files (1000+) | REQ-NFR-0013 | P2 |
| TST-STR-003 | Large Sigma ruleset (1000+ rules) | REQ-NFR-0013 | P2 |
| TST-STR-004 | Deep directory structure (100+ levels) | REQ-NFR-0013 | P3 |
| TST-STR-005 | EVTX with many records (1M+) | REQ-NFR-0013 | P3 |

**Критерий прохождения:** Успешное завершение без timeout/OOM в разумных пределах.

**Реализация:** Integration тесты с синтетическими большими фикстурами (генерируются, не хранятся).

---

### 3.5. Категория: Sanitizers (REQ-SEC-0022)

**Цель:** Подтвердить отсутствие UB через инструментальные проверки.

| TEST-ID | Описание | Связь | Приоритет |
|---------|----------|-------|-----------|
| TST-SAN-001 | AddressSanitizer (ASan) прогон | REQ-SEC-0022 | P1 |
| TST-SAN-002 | UndefinedBehaviorSanitizer (UBSan) прогон | REQ-SEC-0022 | P1 |
| TST-SAN-003 | MemorySanitizer (MSan) прогон (Linux) | REQ-SEC-0022 | P2 |
| TST-SAN-004 | ThreadSanitizer (TSan) прогон | REQ-SEC-0022 | P3 |

**Критерий прохождения:** Все unit-тесты проходят под sanitizers без ошибок.

**Реализация:** CI build variants с sanitizer flags.

---

### 3.6. Категория: CLI/Output (RISK-0031, RISK-0028)

**Цель:** Подтвердить соответствие CLI output upstream.

| TEST-ID | Описание | Связь | Приоритет |
|---------|----------|-------|-----------|
| TST-CLI-001 | Help message format (`--help`) | RISK-0031, C1 | P1 |
| TST-CLI-002 | Version output format (`--version`) | RISK-0031, C1 | P1 |
| TST-CLI-003 | Error message format (invalid args) | RISK-0031, C6 | P1 |
| TST-CLI-004 | Banner output (default) | RISK-0028 | P2 |
| TST-CLI-005 | No-banner output (`--no-banner`) | RISK-0028 | P2 |
| TST-CLI-006 | Subcommand help messages | RISK-0031, C1 | P2 |

**Критерий прохождения:** Byte-to-byte совпадение с golden runs (где возможно).

**Реализация:** Golden runs comparison harness.

---

### 3.7. Категория: GAP-001

**Цель:** Документировать и тестировать поведение EVTX JSON формата.

| TEST-ID | Описание | Связь | Приоритет |
|---------|----------|-------|-----------|
| TST-GAP-001 | EventData JSON structure test | RISK-0040 | P2 |
| TST-GAP-002 | Sigma rule matching with C++ EventData | RISK-0040 | P2 |
| TST-GAP-003 | Chainsaw rule matching with C++ EventData | RISK-0040 | P2 |

**Критерий прохождения:** Правила корректно матчатся с обеими структурами (после ADR решения).

**Примечание:** Требуется ADR для решения flatten vs mapping. Тесты зависят от ADR.

---

## 4. Сводка по приоритетам

### 4.1. P1 тесты (критичные)

| Категория | Количество | Связанные риски |
|-----------|------------|-----------------|
| Determinism | 3 | RISK-0011 |
| Robustness | 3 | REQ-SEC-0017 |
| Security | 3 | REQ-SEC-0021, RISK-0025 |
| Sanitizers | 2 | REQ-SEC-0022 |
| CLI/Output | 3 | RISK-0031 |

**Итого P1: 14 тестов**

### 4.2. P2 тесты (важные)

| Категория | Количество | Связанные риски |
|-----------|------------|-----------------|
| Determinism | 2 | RISK-0011 |
| Robustness | 9 | REQ-SEC-0017, RISK-0007 |
| Security | 6 | REQ-SEC-0020 |
| Stress | 3 | REQ-NFR-0013 |
| Sanitizers | 1 | REQ-SEC-0022 |
| CLI/Output | 3 | RISK-0028, RISK-0031 |
| GAP-001 | 3 | RISK-0040 |

**Итого P2: 27 тестов**

### 4.3. P3 тесты (желательные)

| Категория | Количество |
|-----------|------------|
| Security | 1 |
| Stress | 2 |
| Sanitizers | 1 |

**Итого P3: 4 теста**

---

## 5. План реализации

### 5.1. Фаза 1: P1 тесты

1. **TST-DET-001..003** — Determinism harness
2. **TST-ROB-001..003** — Corrupted EVTX fixtures
3. **TST-SEC-001..003** — Path traversal unit tests
4. **TST-SAN-001..002** — Sanitizer CI builds
5. **TST-CLI-001..003** — CLI golden runs

**Оценка:** 14 тестов

### 5.2. Фаза 2: P2 тесты ( продолжение)

1. **TST-DET-004..005** — Analyse determinism
2. **TST-ROB-004..012** — Extended robustness
3. **TST-SEC-004..010** — Extended security
4. **TST-STR-001..003** — Stress tests
5. **TST-SAN-003** — MSan
6. **TST-CLI-004..006** — Extended CLI
7. **TST-GAP-001..003** — После ADR

**Оценка:** 27 тестов

### 5.3. Фаза 3: P3 тесты (если время позволяет)

- TST-SEC-010, TST-STR-004..005, TST-SAN-004

**Оценка:** 4 теста

---

## 6. Зависимости

| Зависимость | Влияние | Статус |
|-------------|---------|--------|
| ADR для GAP-001 | TST-GAP-* зависят от решения | Pending |
| Sanitizer support в CMake | TST-SAN-* требуют build variants | Не реализовано |
| Large fixture generation | TST-STR-* требуют генератор | Не реализовано |

---

## 7. Не дублируем существующее покрытие

Существующие 478 unit-тестов покрывают:
- Базовую функциональность всех команд
- Sigma/Chainsaw rule parsing и matching
- EVTX/HVE/MFT/ESEDB parsing
- Tau engine expression evaluation
- Output formatting

**Новые тесты НЕ дублируют это**, а расширяют:
- Edge cases (битые файлы, граничные размеры)
- Security scenarios (path traversal, injection)
- Determinism (повторяемость)
- Инструментальные проверки (sanitizers)

---

## 8. Обновления документов

### 8.1. Требуемые ADR

| ADR-ID | Тема | Статус |
|--------|------|--------|
| ADR-0012 | EVTX JSON format (GAP-001 resolution) | Pending |

### 8.2. Обновления AR

Требование REQ-SEC-0022 должно быть расширено:
- Добавить конкретные sanitizer targets
- Определить coverage requirements

### 8.3. Обновления GUIDE

Developer guide должен включать:
- Инструкции по запуску sanitizer builds
- Процедуру создания robustness fixtures

---

## Связи

- AR-0001: Архитектурные требования
- GOV-0002: Критерии эквивалентности
- REP-0002: Parity-аудит
- 00_RISK_REGISTER: Реестр рисков

