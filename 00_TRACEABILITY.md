# 00_TRACEABILITY

Минимальная матрица трассируемости (не заменяет профильные SoT‑документы).

## 1) Верхнеуровневая матрица (что является источником истины и зачем)

| Требование / Цель | Артефакт / SoT | Примечание |
|---|---|---|
| Зафиксировать baseline "что именно портируем" | `docs/baseline/BASELINE-0001-upstream.md` + входы `upstream/chainsaw`, `upstream/sigma`, `upstream/EVTX-ATTACK-SAMPLES` | Опора для сопоставления поведения и воспроизводимости |
| Схема идентификаторов, SoT и правила трассировки | `docs/governance/GOV-0001-traceability-and-sot.md` | Правило "одна истина на класс информации" |
| Критерии эквивалентности 1:1 и доказательная база приёмки | `docs/governance/GOV-0002-equivalence-criteria.md` | Определение "совпало" для stdout/stderr/exit code/FS‑эффектов |
| As‑Is поведенческая истина Rust | `docs/as_is/*` | CLI/фичи/архитектура/форматы/зависимости |
| Test‑to‑test база (логические тесты `TST-*`) | `docs/tests/TESTMAT-0001-rust-test-matrix.md` | SoT для переноса тестов в C++ |
| Golden runs Rust (сценарии `RUN-*`) | `docs/reports/REP-0001-rust-golden-runs.md` | Основа для diff‑harness (Step 27) и e2e сравнений |
| Проверяемая постановка задачи (границы/приёмка/сценарии) | `docs/requirements/TASK-0001-problem-statement.md` | Связывает As‑Is с To‑Be, определяет "что такое готово" |
| Архитектурные требования (`REQ-*`) | `docs/requirements/AR-0001-architectural-requirements.md` | Единственный источник формулировок требований |
| ADR baseline (архитектурные решения `ADR-*`) | `docs/adr/*` + `00_DECISION_LOG.md` | Решения приняты/отложены управляемо |
| To‑Be архитектура (модули `MOD-*`, потоки, As‑Is→To‑Be) | `docs/architecture/TOBE-0001-cpp-to-be-architecture.md` | База для реализации и трассировки `REQ-* → MOD-* → код/тесты` |
| WBS и оценка трудозатрат | `docs/planning/WBS-0001-work-breakdown-and-estimates.md` | Трассировка работ к `REQ/FEAT/TST/RUN/RISK/MOD` |
| Практическое руководство разработчика | `docs/guide/GUIDE-0001-developer-guide.md` | Проверяемые инженерные нормы (без дублирования требований) |
| Пакет согласования архитектуры | `docs/governance/GOV-0003-architecture-approval-package.md` | Чек‑лист/протокол/критерии готовности к старту реализации |
| Политика third‑party и реестр лицензий | `docs/licensing/POL-0001-third-party-policy.md`, `docs/licensing/LIC-0002-cpp-dependencies.md` | Комплаенс и правила работы с зависимостями/данными |
| Риски/допущения | `00_RISK_REGISTER.md` | Любая неопределённость фиксируется как риск/assumption с планом |

---

## 2) Минимальная цепочка трассируемости для ключевых CLI поверхностей (To‑Be)

> Детали модульных границ и потоков: `docs/architecture/TOBE-0001-cpp-to-be-architecture.md`.

| Поверхность | REQ-* (минимум) | ADR-* (ключевые) | MOD-* | TST-* / RUN-* |
|---|---|---|---|---|
| CLI help/errors | `REQ-FR-0001`, `REQ-NFR-0010`, `REQ-NFR-0015`, `REQ-NFR-0016` | `ADR-0006`, `ADR-0007` | `MOD-0002`, `MOD-0003` | `RUN-0101..RUN-0104` |
| `search` | `REQ-FR-0002`, `REQ-NFR-0010`, `REQ-NFR-0015` | `ADR-0005`, `ADR-0006`, `ADR-0011` | `MOD-0011`, `MOD-0006`, `MOD-0007`, `MOD-0003` | `TST-0001..TST-0003`, `RUN-0201..RUN-0204` |
| `hunt` | `REQ-FR-0003`, `REQ-NFR-0010`, `REQ-NFR-0015` | `ADR-0011`, `ADR-0006` | `MOD-0012`, `MOD-0008`, `MOD-0009`, `MOD-0003` | `TST-0004`, `RUN-0301..RUN-0303` |
| `dump` | `REQ-FR-0004` | `ADR-0003`, `ADR-0010` | `MOD-0010`, `MOD-0006`, `MOD-0007`, `MOD-0003` | `RUN-0401..RUN-0402` |
| `lint` | `REQ-FR-0005` | `ADR-0006` | `MOD-0013`, `MOD-0008`, `MOD-0003` | `RUN-0103..RUN-0104`, `RUN-0601` |
| `analyse srum` | `REQ-FR-0006`, `REQ-NFR-0013` | `ADR-0009`, `ADR-0010` | `MOD-0014`, `MOD-0007`, `MOD-0003` | `TST-0005..TST-0006`, `RUN-0501..RUN-0502` |
| `analyse shimcache` | `REQ-FR-0007` | (pending: парсер HVE/Registry в `ADR-0009`) | `MOD-0015`, `MOD-0007`, `MOD-0003` | (нет baseline `TST-*`; требуется добавить фикстуры и проверки/`RUN-*` для shimcache — см. `RISK-0022`) |
