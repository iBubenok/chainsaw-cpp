# История проекта (детальный архив)

> **Назначение**: Этот файл содержит полную детальную историю всех завершённых шагов проекта.
> Для текущего состояния проекта см..
>
> **Обновляется**: автоматически при архивировании завершённых итераций.

---

## Фаза G: Quality, Hardening & Release (-44)

###: План сопровождения и стратегия обновлений (2026-01-16)

**ПРОЕКТ ПОРТИРОВАНИЯ ЗАВЕРШЁН**

- **MAINT-0001 создан**: `docs/maintenance/MAINT-0001-maintenance-`
- Определены воспроизводимые процессы:
 1. Обновление Sigma-правил (DRL 1.1 compliance)
 2. Обновление датасетов EVTX (категории A/B/C)
 3. Обновление C++ зависимостей (vendoring)
 4. Синхронизация с upstream Chainsaw
 5. Сохранение доказательной базы 1:1 (GOV-0002)
- Чек-листы для каждого типа обновления
- Роли: Maintainer, Security Officer, Release Manager
- **DoD: PASS** — определён воспроизводимый процесс обновлений

###: Release Candidate package + аудит лицензий (2026-01-16)

- **RC1 сборка на 3 платформах**:
 - Linux (GCC 14.2.0): 505/505 PASS, версия 2.13.1
 - macOS (AppleClang 17.0.0): 505/505 PASS, версия 2.13.1
 - Windows (MSVC 19.44): 505/505 PASS, версия 2.13.1
- **Аудит лицензий**: LIC-0002 v2 финализирован
- **Артефакты**:
 - `RELEASE-0001`: Release Notes RC1
 - `REP-0010`: RC1 Verification Protocol
- **DoD: PASS**

###: Финальный аудит консистентности документов (2026-01-16)

- **REP-0009 создан**: `docs/reports/REP-0009-final-consistency-audit.md`
- 30/30 REQ-* прослеживаются
- 12/12 ADR реализованы
- Противоречий не выявлено
- **DoD: PASS**

###: Кроссплатформенная e2e-валидация (2026-01-16)

- **REP-0008 создан**: `docs/reports/REP-0008-e2e-validation.md`
- 505/505 тестов PASS на 3 платформах
- CLI сценарии RUN-* проверены
- Критерии GOV-0002 C1-C7 PASS
- **DoD: PASS**

###: Верификация пакетирования и воспроизводимости (2026-01-15)

- **REP-0007 создан**: `docs/reports/REP-0007-packaging-verification.md`
- Кроссплатформенная сборка: 3/3 PASS
- Лицензионный комплаенс: PASS
- Воспроизводимость (bit-exact): PASS
- Air-gapped сборка: PASS
- **DoD: PASS**

###: Security review и аудит кода (2026-01-15)

- **REP-0006 создан**: `docs/reports/REP-0006-security-review.md`
- ASan/UBSan: 0 issues
- Input validation: PASS
- Path traversal protection: PASS
- **DoD: PASS**

###: Профилирование производительности (2026-01-15)

- **REP-0005 создан**: `docs/reports/REP-0005-performance-profile.md`
- Benchmark на 3 платформах
- Bottlenecks: regex compilation (acceptable)
- **DoD: PASS**

###: Расширенное тестирование (2026-01-15)

- **TEST-EXPAND-0001**: `docs/tests/TEST-EXPAND-0001-test-expansion-`
- 46 новых тестов добавлено
- Coverage: GAP-001, detection, robustness, security, CLI
- **DoD: PASS**

###: GAP-001 resolution + extended tests (2026-01-15)

- **ADR-0012 реализован**: EVTX JSON EventData flatten
- 46 unit-тестов добавлено (TST-GAP-*, TST-DET-*, TST-ROB-*, TST-SEC-*, TST-CLI-*)
- 505 unit-тестов итого
- **DoD: PASS**

###: Fuzzing/robustness testing (2026-01-14)

- Robustness тесты: 12 сценариев
- ASan/UBSan: PASS
- **DoD: PASS**

###: Parity audit (2026-01-14)

- **REP-0002 создан**: `docs/reports/REP-0002-parity-audit.md`
- GAP-001 выявлен (EVTX EventData format)
- 19/19 слайсов verified
- **DoD: PASS**

---

## Фаза F: Цикл портирования (-33)

### SLICE-019: Analyse Shimcache Command

** (SLICE-019 Implementation) — 2026-01-13:**
- **SLICE-019 Analyse Shimcache Command — Verified PASS**
- Реализованы файлы:
 - `cpp/include/chainsaw/shimcache.hpp` — API парсера и analyser
 - `cpp/src/analyse/shimcache.cpp` — реализация парсера и ShimcacheAnalyser
 - `cpp/tests/test_shimcache_gtest.cpp` — 25 unit-тестов (TST-SHIM-001..020 + 5 доп.)
- Исправления HVE парсера:
 - Format detection для NK cell (name_size offset 72 vs 76)
 - Subkeys list offset fallback (offset 28 vs 32)
 - Windows 11 shimcache format detection (поиск "10ts" signature)
- Кроссплатформенная верификация (3-VM протокол):
 - **Linux** (GCC 14.2): сборка ✓, 473/473 тестов ✓, 22 shimcache тестов PASSED, 3 SKIPPED
 - **macOS** (AppleClang 17.0): сборка ✓, 473/473 тестов ✓, 22 shimcache тестов PASSED, 3 SKIPPED
 - **Windows** (MSVC 19.44): сборка ✓, все тесты ✓, 22 shimcache тестов PASSED, 3 SKIPPED
- Закрытие рисков:
 - **RISK-0022**: SYSTEM hive — ✅ CLOSED (получен с Windows 11)
 - **RISK-SHIM-001**: Windows Vista/XP — ACCEPTED (документировано)
 - **RISK-SHIM-002**: Amcache integration — OPEN (тесты skipped, нужна Amcache.hve)
- Всего C++ unit-тестов: **473** (+25 от SLICE-019)

** (SLICE-019 UnitReady) — 2026-01-13:**
- **SLICE-019 Analyse Shimcache Command — UnitReady (Conditional) PASS**
- Micro-spec создан: `docs/slices/SPEC-SLICE-019-analyse-shimcache.md`
- UnitReady критерии из PLAYBOOK-0001 (5/5 с условием):
 1. ✅ Micro-spec создан
 2. ✅ Поведение описано на основе FACTS (32 факта)
 3. ⚠️ Определён полный набор проверок (20 unit-тестов, но нет тестовых данных)
 4. ✅ Зависимости оценены
 5. ✅ Оценка S1–S4 корректна (Low/High/High/Low)

---

### SLICE-018: MFT Parser

** (SLICE-018 Verification) — 2026-01-13:**
- **SLICE-018 MFT Parser — UnitDone/Verified PASS**
- Кроссплатформенная верификация (3-VM протокол):
 - **Linux** (GCC 14.2): сборка ✓, 448/448 тестов ✓, 17 MFT тестов PASSED
 - **macOS** (AppleClang 17.0): сборка ✓, 448/448 тестов ✓, 17 MFT тестов PASSED
 - **Windows** (MSVC 19.44): сборка ✓, 448/448 тестов ✓, 17 MFT тестов PASSED
- Сравнение с golden runs/0404/0405)
- Закрытие рисков RISK-MFT-001..004
- Отчёт: `docs/reports/VERIFY-SLICE-018-2026-01-13.md`

** (SLICE-018 Implementation) — 2026-01-12:**
- **SLICE-018 MFT Parser — UnitDone PASS**
- Реализован собственный MFT парсер (ADR-0009: custom parser вместо libfsntfs)
- Созданы файлы:
 - `cpp/include/chainsaw/mft.hpp` — API парсера
 - `cpp/src/io/mft.cpp` — реализация парсера
 - `cpp/tests/test_mft_gtest.cpp` — 21 unit-тест
- Всего C++ unit-тестов: **448** (+21 от SLICE-018)

---

### SLICE-017: Analyse SRUM Command

** (SLICE-017 Verification) — 2026-01-12:**
- **SLICE-017 Analyse SRUM Command — UnitDone/Verified PASS**
- Кроссплатформенная верификация (3-VM протокол):
 - **Linux**: 427/427 тестов ✓, 24 SRUM тестов ✓, 21 ESEDB тестов ✓
 - **macOS**: 427/427 тестов ✓, 24 SRUM тестов ✓, 21 ESEDB тестов ✓
 - **Windows**: 427/427 тестов ✓, 24 SRUM тестов ✓, 4 ESEDB тестов ✓ (17 skipped)
- Закрытие рисков RISK-ESEDB-001..004, RISK-SRUM-001..003
- Всего C++ unit-тестов: **427** (+24 от SLICE-017)
- Отчёт: `docs/reports/VERIFY-SLICE-017-2026-01-12.md`

** (SLICE-017 UnitReady) — 2026-01-12:**
- Micro-spec: `docs/slices/SPEC-SLICE-017-analyse-srum.md`
- S1–S4 Assessment: Complexity=LOW, Risk=LOW, Dependencies=LOW, Effort=MEDIUM

---

### SLICE-016: ESEDB Parser

** (SLICE-016 Verification) — 2026-01-12:**
- **SLICE-016 ESEDB Parser — UnitDone/Verified PASS**
- Кроссплатформенная верификация:
 - **Linux**: 403/403 тестов ✓, libesedb v20251215 ✓
 - **macOS**: 403/403 тестов ✓, libesedb v20251215 ✓
 - **Windows**: 403/403 тестов ✓, graceful degradation ✓
- Закрытие рисков RISK-ESEDB-001..004
- Отчёт: `docs/reports/VERIFY-SLICE-016-2026-01-12.md`

** (SLICE-016 Implementation) — 2026-01-12:**
- Реализовано:
 - `cpp/include/chainsaw/esedb.hpp` — API парсера
 - `cpp/src/io/esedb.cpp` — условная компиляция (Linux/macOS: libesedb, Windows: stub)
 - `cpp/tests/test_esedb_gtest.cpp` — 21 unit-тест
- Всего C++ unit-тестов: **403** (+21 от SLICE-016)

** (SLICE-016 UnitReady) — 2026-01-12:**
- Micro-spec: `docs/slices/SPEC-SLICE-016-esedb-parser.md`

---

### SLICE-015: HVE Parser

** (SLICE-015 Verification) — 2026-01-12:**
- **SLICE-015 HVE Parser — UnitDone/Verified PASS**
- 382/382 тестов на всех платформах
- 19 HVE-специфичных тестов
- Закрытие рисков RISK-HVE-001..004
- Отчёт: `docs/reports/VERIFY-SLICE-015-2026-01-12.md`

** (SLICE-015 Implementation) — 2026-01-12:**
- Реализован полный REGF парсер (~1100 строк)
- Transaction Log Support (HVLE парсинг)
- 19 unit-тестов
- ADR-0009 обновлён: HVE = Accepted

** (SLICE-015 UnitReady) — 2026-01-12:**
- Micro-spec: `docs/slices/SPEC-SLICE-015-hve-parser.md`
- S1–S4: Complexity=HIGH, Risk=HIGH, Dependencies=MEDIUM, Effort=HIGH

---

### SLICE-014: Lint Command

** (SLICE-014 Verification) — 2026-01-12:**
- **SLICE-014 Lint Command — UnitDone/Verified PASS**
- 363/363 тестов на всех платформах
- Реализовано: detection_to_yaml, expression_to_yaml, pattern_to_string
- 5 unit-тестов TST-LINT-*

---

### SLICE-013: Dump Command

** (SLICE-013 Verification) — 2026-01-12:**
- **SLICE-013 Dump Command — Verified PASS**
- 358/358 тестов на всех платформах
- 28 unit-тестов TST-DUMP-001..028
- Отчёт: `docs/reports/VERIFY-SLICE-013-2026-01-12.md`

---

### SLICE-012: Hunt Command

** (SLICE-012 Verification) — 2026-01-11:**
- **SLICE-012 Hunt Command — Verified PASS**
- 330/330 тестов на всех платформах
- API соответствует SPEC-SLICE-012 (33/33 FACTS PASS)
- 27 unit-тестов TST-HUNT-*
- Отчёт: `docs/reports/VERIFY-SLICE-012-2026-01-11.md`

---

### SLICE-011: Search Command

** (SLICE-011 Verification) — 2026-01-11:**
- **SLICE-011 Search Command — Verified PASS**
- 303/303 тестов на всех платформах
- 21 unit-тест TST-SEARCH-*
- Исправлен баг Binary XML template caching (RISK-EVTX-001 CLOSED)
- Отчёт: `docs/reports/VERIFY-SLICE-011-2026-01-11.md`

** (SLICE-011 UnitReady) — 2026-01-11:**
- Micro-spec: `docs/slices/SPEC-SLICE-011-search-command.md`

---

### SLICE-010: Sigma Rules

** (SLICE-010 Verification) — 2026-01-11:**
- **SLICE-010 Sigma Rules — Verified PASS**
- 282/282 тестов на всех платформах
- Исправлен race condition в тестах SigmaLoadTest
- Отчёт: `docs/reports/VERIFY-SLICE-010-2026-01-11.md`

** (SLICE-010 UnitReady) — 2026-01-11:**
- Micro-spec: `docs/slices/SPEC-SLICE-010-sigma-rules.md`

---

### SLICE-009: Chainsaw Rules

** (SLICE-009 Verification) — 2026-01-11:**
- **SLICE-009 Chainsaw Rules — Verified PASS**
- 260/260 тестов на всех платформах
- Закрыты риски RISK-CSRULE-001, RISK-CSRULE-002
- Отчёт: `docs/reports/VERIFY-SLICE-009-2026-01-11.md`

** (SLICE-009 Implementation) — 2026-01-11:**
- yaml-cpp 0.8.0 вендорирован
- 32 unit-теста TST-CSRULE-*

** (SLICE-009 UnitReady) — 2026-01-11:**
- Micro-spec: `docs/slices/SPEC-SLICE-009-chainsaw-rules.md`

---

### SLICE-008: Tau Engine

** (SLICE-008 Verification) — 2026-01-11:**
- **SLICE-008 Tau Engine — Verified PASS**
- 228/228 тестов на всех платформах
- Отчёт: `docs/reports/VERIFY-SLICE-008-2026-01-11.md`

** (SLICE-008 Implementation) — 2026-01-11:**
- Полная реализация Tau Engine
- 32 unit-теста TST-TAU-*

** (SLICE-008 UnitReady) — 2026-01-10:**
- Micro-spec: `docs/slices/SPEC-SLICE-008-tau-engine.md`

---

### SLICE-007: EVTX Parser

** (SLICE-007 Verification) — 2026-01-10:**
- **SLICE-007 EVTX Parser — Verification PASS**
- 196/196 тестов на всех платформах
- Отчёт: `docs/reports/VERIFY-SLICE-007-2026-01-10.md`

** (SLICE-007 Implementation) — 2026-01-10:**
- Полная реализация Binary XML парсера
- 16 unit-тестов TST-EVTX-*

** (SLICE-007 UnitReady) — 2026-01-10:**
- Micro-spec: `docs/porting/SPEC-SLICE-007.md`

---

### SLICE-006: XML Parser

** (SLICE-006 Verification) — 2026-01-10:**
- **SLICE-006 XML Parser — Verification PASS**
- 180/180 тестов на всех платформах
- Отчёт: `docs/reports/VERIFY-SLICE-006-2026-01-10.md`

** (SLICE-006 Implementation) — 2026-01-10:**
- pugixml v1.14 вендорирован
- 16 unit-тестов TST-XML-*

** (SLICE-006 UnitReady) — 2026-01-09:**
- Micro-spec: `docs/porting/SPEC-SLICE-006.md`

---

### SLICE-005: Reader Framework + JSON Parser

** (SLICE-005 Verification) — 2026-01-09:**
- **SLICE-005 — Verification PASS**
- 164/164 тестов на всех платформах
- Исправлен race condition в тестах
- Отчёт: `docs/reports/VERIFY-SLICE-005-2026-01-09.md`

** (SLICE-005 Implementation) — 2026-01-09:**
- Value class, Reader API
- 28 unit-тестов

** (SLICE-005 UnitReady) — 2026-01-09:**
- Micro-spec: `docs/porting/SPEC-SLICE-005.md`

---

### SLICE-004: File Discovery

** (SLICE-004 Verification) — 2026-01-09:**
- **SLICE-004 — Verification PASS**
- 136/136 тестов на всех платформах
- Отчёт: `docs/reports/VERIFY-SLICE-004-2026-01-09.md`

** (SLICE-004 Implementation) — 2026-01-09:**
- discover_files API
- 21 unit-тест

** (SLICE-004 UnitReady) — 2026-01-09:**
- Micro-spec: `docs/porting/SPEC-SLICE-004.md`

---

### SLICE-003: CLI Parser

** (SLICE-003 Implementation) — 2026-01-09:**
- render_help, render_version, parse
- 22 unit-теста CLI
- 115/115 тестов на всех платформах

** (SLICE-003 UnitReady) — 2026-01-09:**
- Micro-spec: `docs/porting/SPEC-SLICE-003.md`

---

### SLICE-002: Output Layer

** (SLICE-002 Implementation) — 2026-01-09:**
- Writer, Table, Format, Color
- RapidJSON вендорирован
- 102/102 тестов на всех платформах

** (SLICE-002 UnitReady) — 2026-01-09:**
- Micro-spec: `docs/porting/SPEC-SLICE-002.md`

---

### SLICE-001: Platform Layer

** (SLICE-001 Implementation) — 2026-01-09:**
- make_temp_file реализован
- 60/60 тестов на всех платформах

** (SLICE-001 UnitReady) — 2026-01-09:**
- Micro-spec: `docs/porting/SPEC-SLICE-001.md`

---

## Фаза E: Подготовка к портированию (-28)

### — Iteration Playbook
- **2026-01-09**: Playbook создан: `docs/porting/PLAYBOOK-0001-iteration-playbook.md`

### — Porting Backlog
- **2026-01-09**: Бэклог создан: `docs/backlog/BACKLOG-0001-porting-backlog.md`
- 19 слайсов, 22 upstream теста распределены

### — Test Data Policy
- **2026-01-09**: Политика: `docs/licensing/POL-0003-test-data-policy.md`
- Фикстуры: `cpp/tests/fixtures/` (132 KB)

### — Harness
- **2026-01-09**: Harness: `tools/harness/compare_behavior.py`
- Отчёт: `docs/reports/REP-0002-harness-comparison-baseline.md`

### — CI
- **2026-01-09**: GitHub Actions: `.github/workflows/ci.yml`

### — C++ Skeleton
- **2026-01-09**: Скелет C++ проекта создан

### — Golden Runs Generation
- **2026-01-09**: Golden runs Rust создан автоматически

---

##
- **2026-01-09**: PASS
- Отчёт: `docs/reports/Gate_E_F_2026-01-09.md`

---

## Предыдущие фазы

### (2026-01-03)
- Отчёт: `docs/reports/Gate_D_E_2026-01-03.md`

### (2026-01-02)
- Отчёт: `docs/reports/Gate_B_D_2026-01-02.md`

### (2025-12-25)
- Отчёт: `docs/reports/Gate_A_B_2025-12-25.md`

---

---

## Фаза G: Quality (-36)

### — Реализация расширенных тестов
- **2026-01-15**: завершён
- **ADR-0012 реализован**: flatten EventData в evtx.cpp и reader.cpp
- **test_extended_gtest.cpp**: 27 новых тестов
 - TST-DET-001..003: Determinism (JSON, EVTX, XML)
 - TST-ROB-001..012: Robustness (corrupted inputs, unicode paths)
 - TST-SEC-001..004: Security (path traversal, symlinks)
 - TST-CLI-001..003: CLI parsing
 - TST-DISC-001..002: File discovery
- **Sanitizer builds**: ASan, UBSan настроены в CMakeLists.txt
- **Результаты тестирования**:
 - Linux (GCC 14.2): 505/505 PASS ✅
 - macOS (AppleClang 17.0): 505/505 PASS ✅
 - Windows (MSVC 19.44): 505/505 PASS ✅
 - Linux + ASan: 505/505 PASS ✅
 - Linux + UBSan: 505/505 PASS ✅
- **Закрыто**: RISK-0040 (GAP-001) — CLOSED

### — План расширения тестов
- **2026-01-15**: завершён
- **TEST-EXPAND-0001**: план 45 тестов по 7 категориям
- **ADR-0012**: решение по GAP-001 (flatten EventData)

### — Parity-аудит
- **2026-01-15**: завершён
- **REP-0002**: parity-аудит
- 11/11 фич, 22/22 тестов, 478 unit-тестов
- GAP-001 задокументирован

---

##
- **2026-01-14**: PASS
- Отчёт: `docs/reports/Gate_F_G_2026-01-14.md`

---

> Архив обновлён: 2026-01-15 ( — 505 тестов на 3 платформах + ASan/UBSan)
