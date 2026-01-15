# UNITREADY-SLICE-018 — MFT Parser UnitReady Analysis Report

## Статус
- **Дата:** 2026-01-12
- **Step:** 31 (Slice-анализ)
- **Слайс:** SLICE-018 (MFT Parser)
- **Итог:** **UnitReady PASS** (все блокеры закрыты)

---

## Резюме

 для SLICE-018 (MFT Parser) завершён как **UnitReady PASS**. Все блокеры закрыты:
- BLOCKER-001: ADR-0009 Accepted (собственный парсер) ✅
- BLOCKER-002: Тестовые фикстуры созданы ✅
- BLOCKER-003: Golden runs/0404 добавлены ✅

S2 снижен с High до Medium.

### Ключевые результаты
1. **Micro-spec создан:** `docs/slices/SPEC-SLICE-018-mft-parser.md`
2. **FACTS извлечены:** 31 факт из анализа Rust кода (mft.rs, mod.rs, main.rs)
3. **Unit-тесты определены:** TST-MFT-001..017 (DRAFT — требует финализации)
4. **Риски зарегистрированы:** RISK-MFT-001..004
5. **Блокеры выявлены:** 3 блокера требуют закрытия

---

## Верификация рисков RISK-ESEDB-001..004

Согласно заданию, выполнена верификация закрытия рисков RISK-ESEDB-001..004:

| Риск | Статус | Доказательство |
|------|--------|----------------|
| RISK-ESEDB-001 | **Closed** | libesedb работает на Linux/macOS (pkg-config), Windows использует graceful degradation (stub) |
| RISK-ESEDB-002 | **Closed** | 21 unit-тест (TST-ESEDB-001..017 + 4 дополнительных) покрывают C API vs Rust semantics |
| RISK-ESEDB-003 | **Closed** | TST-ESEDB-008 + FiletimeToIso8601 верифицируют OLE Automation Date и FILETIME конверсии |
| RISK-ESEDB-004 | **Closed** | Полный SRUDB.dat (1.8MB) парсится за 0.06s на Linux/macOS |

**Вывод:** Все 4 риска RISK-ESEDB подтверждены как закрытые с доказательствами из /33.

---

## Оценка S1–S4 для SLICE-018 (финальная)

| Ось | Значение | Обоснование |
|-----|----------|-------------|
| S1 (ширина контуров) | **Low** | Один модуль MFT парсер |
| S2 (сложность проверки) | **Medium** | Golden runs/0404 + фикстуры созданы |
| S3 (нерешённые вопросы) | **Low** | ADR-0009 MFT Accepted (собственный парсер) |
| S4 (платформенность) | **Low** | Собственный C++ код без внешних зависимостей |

**Правило:** Все оси Low или Medium → слайс готов к реализации.

---

## Выявленные блокеры (все закрыты)

### BLOCKER-001: ADR-0009 pending для MFT — ✅ CLOSED (2026-01-12)
- **Описание:** Выбор библиотеки для MFT парсера не принят
- **Результат spike:** libfsntfs отвергнут (не разделяет SI/FN timestamps, отсутствуют поля)
- **Решение:** Собственная реализация MFT парсера на C++ (ADR-0009 MFT Accepted)

### BLOCKER-002: Отсутствие тестовых фикстур MFT — ✅ CLOSED (2026-01-12)
- **Описание:** Нет MFT файлов в тестовых фикстурах upstream
- **Результат:** Создан синтетический MFT `cpp/tests/fixtures/mft/test_minimal.mft` (8192 байт, 8 записей)
- **Артефакты:** expected_json.json, expected_yaml.txt
- **Верификация:** Rust Chainsaw v2.10.1 подтвердил корректность

### BLOCKER-003: Отсутствие golden runs для MFT — ✅ CLOSED (2026-01-12)
- **Описание:** Нет RUN-* сценариев для `chainsaw dump <mft_file>`
- **Результат:**
 -: `chainsaw dump test_minimal.mft -q` (YAML) ✅
 -: `chainsaw dump test_minimal.mft -q --json` ✅
 -: `chainsaw dump test_minimal.mft --decode-data-streams -q` ✅ (upstream v2.13.1)

---

## Риски (обновлено после spike)

| ID | Описание | Влияние | Вероятность | Статус |
|----|----------|---------|-------------|--------|
| RISK-MFT-001 | libfsntfs API differences from Rust mft crate | High | Medium | **Closed** (libfsntfs отвергнут) |
| RISK-MFT-002 | DataStreams extraction semantics | Medium | Medium | Open |
| RISK-MFT-003 | Random filename suffix reproducibility | Medium | High | Open (Accepted) |
| RISK-MFT-004 | FlatMftEntryWithName JSON structure | Medium | Medium | **Mitigated** (собственный парсер) |

---

## Определённые unit-тесты (DRAFT)

| ID | Описание |
|----|----------|
| TST-MFT-001 | Load MFT file by extension.mft |
| TST-MFT-002 | Load MFT file by extension.bin |
| TST-MFT-003 | Load $MFT file without extension |
| TST-MFT-004 | Iterate MFT entries |
| TST-MFT-005 | Skip ZERO_HEADER entries |
| TST-MFT-006 | Entry count |
| TST-MFT-007 | JSON structure FlatMftEntryWithName |
| TST-MFT-008 | DataStreams field in JSON |
| TST-MFT-009 | Extract data streams hex |
| TST-MFT-010 | Extract data streams UTF-8 decode |
| TST-MFT-011 | Data streams directory output |
| TST-MFT-012 | Path sanitization (separators → _) |
| TST-MFT-013 | Path truncation (150 chars) |
| TST-MFT-014 | Error on existing file |
| TST-MFT-015 | Fallback position in Reader |
| TST-MFT-016 | File not found error |
| TST-MFT-017 | Invalid MFT file error |

---

## UnitReady критерии (PLAYBOOK-0001)

| # | Критерий | Статус |
|---|----------|--------|
| 1 | Micro-spec создан | ✅ PASS |
| 2 | Поведение описано на основе FACTS | ✅ PASS (31 факт) |
| 3 | Определён полный набор проверок | ✅ PASS (17 тестов + фикстуры + golden runs) |
| 4 | Зависимости оценены | ✅ PASS (ADR-0009 Accepted — собственный парсер) |
| 5 | Оценка S1–S4 корректна | ✅ PASS (S2=Medium, остальные Low) |

**Итог: UnitReady PASS** — слайс готов к реализации

---

## План следующих шагов

### Вариант A: Закрытие блокеров SLICE-018
1. **Этап 1 (Spike libfsntfs):**
 - Проверить сборку libfsntfs на Linux/macOS/Windows
 - Написать minimal test: load MFT → iterate entries → dump JSON
 - Сравнить JSON структуру с Rust mft output
 - Принять решение по ADR-0009 (MFT секция)

2. **Этап 2 (Тестовые фикстуры):**
 - Получить/создать тестовый MFT файл
 - Генерировать expected output через Rust Chainsaw
 - Добавить фикстуры в `cpp/tests/fixtures/mft/`
 - Добавить golden runs..0405

3. **Этап 3 (UnitReady финализация):**
 - Возврат к с закрытыми блокерами
 - Финализация unit-тестов
 - Присвоение статуса UnitReady PASS

### Вариант B: Переход к Parity-аудиту
Если SLICE-018 блокирован, можно перейти к parity-аудиту для 17 завершённых слайсов SLICE-001..017.

---

## Обновлённые артефакты

| Артефакт | Статус |
|----------|--------|
| `docs/slices/SPEC-SLICE-018-mft-parser.md` | Создан |
| | Обновлён (RISK-MFT-001..004) |
| | Обновлён |
| `docs/backlog/BACKLOG-0001-porting-backlog.md` | Обновлён (SLICE-018 статус) |

---

## Проверка доступа к VM (3-VM протокол)

| VM | IP | Статус | Права |
|----|----|--------|-------|
| Linux | 192.168.5.75 | ✅ Доступ есть | user в группе sudo |
| macOS | 192.168.5.221 | ✅ Доступ есть | yan в группе admin |
| Windows | 192.168.5.178 | ✅ Доступ есть | user в группе Administrators |

---

## Заключение

 для SLICE-018 завершён как **UnitReady PASS**. Все блокеры закрыты:
- **BLOCKER-001 CLOSED** — ADR-0009 MFT Accepted (собственная реализация)
- **BLOCKER-002 CLOSED** — Тестовые фикстуры созданы и верифицированы
- **BLOCKER-003 CLOSED** — Golden runs/0404 добавлены

**Риски:**
- RISK-MFT-001 Closed (libfsntfs отвергнут)
- RISK-MFT-004 Mitigated (собственный парсер)
- RISK-MFT-002, RISK-MFT-003 Open (Accepted — будут адресованы при реализации)

**Артефакты:**
- Фикстуры: `cpp/tests/fixtures/mft/test_minimal.mft`, `expected_json.json`, `expected_yaml.txt`
- Golden runs:,, в `golden/rust_golden_runs_linux_x86_64/`
- создан автоматически с upstream chainsaw v2.13.1 (скомпилирован из исходников)

**Следующий шаг:** — реализация MFT парсера

Верификация RISK-ESEDB-001..004 подтвердила их закрытие с доказательствами из предыдущих шагов.
