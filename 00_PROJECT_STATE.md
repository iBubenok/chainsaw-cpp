# 00_PROJECT_STATE

## Текущий шаг
- CURRENT_STEP: Step 26 — CI кроссплатформенный (Windows/Linux/macOS)
- Статус: PASS (2026-01-09)
- Последний завершённый шаг: Step 26 — CI кроссплатформенный — PASS (2026-01-09)
- Последний пройденный gate: Gate D→E — обзор готовности к переходу в фазу реализации (после Step 23) — PASS-with-risks (2026-01-03)
- Следующий шаг: Step 27 — Harness сравнения наблюдаемого поведения (Rust vs C++)

## Контекст
Фаза **E (инженерный фундамент C++)** продолжается: CI кроссплатформенный настроен и проверен на 3 платформах.

Step 26 завершён успешно:
- GitHub Actions CI workflow: `.github/workflows/ci.yml`
  - Сборка на 3 платформах (Linux/macOS/Windows)
  - Тесты с GoogleTest
  - Проверка форматирования (format-check)
  - ASan и UBSan прогоны на Linux/macOS
- Скрипты локальной сборки:
  - Unix: `tools/ci/build.sh` (Linux/macOS)
  - Windows: `tools/ci/build.ps1` (PowerShell)
- Кроссплатформенная проверка:
  - **Linux** (GCC 14.2): сборка ✓, 48/48 тестов ✓, format-check ✓, ASan ✓
  - **macOS** (AppleClang 17.0): сборка ✓, 48/48 тестов ✓, ASan ✓
  - **Windows** (MSVC 19.44): сборка ✓, 48/48 тестов ✓
- GitHub Actions CI: https://github.com/iBubenok/chainsaw-cpp/actions
  - Все 10 jobs пройдены успешно (run #20832380950)

## Ключевые артефакты (Single Source of Truth)
- Постановка задачи (SoT Step 15): `docs/requirements/TASK-0001-problem-statement.md`
- Архитектурные требования (SoT Step 16): `docs/requirements/AR-0001-architectural-requirements.md`
- Матрица вариантов технологий/зависимостей (SoT Step 17): `docs/architecture/ARCH-0002-cpp-technology-options-matrix.md`
- ADR baseline (SoT Step 18): `docs/adr/ADR-0001...ADR-0011` + `00_DECISION_LOG.md`
- To‑Be архитектура (SoT Step 19): `docs/architecture/TOBE-0001-cpp-to-be-architecture.md`
- WBS и оценка (SoT Step 20): `docs/planning/WBS-0001-work-breakdown-and-estimates.md`
- Developer guide (SoT Step 21): `docs/guide/GUIDE-0001-developer-guide.md`
- Пакет согласования архитектуры (SoT Step 22): `docs/governance/GOV-0003-architecture-approval-package.md`
- Критерии 1:1 (SoT Step 6): `docs/governance/GOV-0002-equivalence-criteria.md`
- Политика «зелёного состояния» (SoT Step 25): `docs/governance/POL-0002-green-state-policy.md`
- CI workflow (SoT Step 26): `.github/workflows/ci.yml`

## Результаты последнего gate
- Gate‑отчёт: `docs/reports/Gate_D_E_2026-01-03.md`
- Протокол согласования To‑Be: `docs/reports/Architecture_Approval_2026-01-03.md`

## Блокеры / открытые вопросы
- Pending‑решения по парсерам (см. `ADR-0009`): EVTX / HVE(Registry) / MFT / XML.
- Политика тестовых данных (P1): будет финализирована на Step 28.
- Детерминизм и платформенные расхождения вывода (P1): контроль через Step 27 и дальнейшие дифф‑прогоны.

## Следующий шаг
- Step 27 — Harness сравнения наблюдаемого поведения (Rust vs C++): скрипты/протокол сравнения golden runs, первые результаты дифф-сравнения.

## Кроссплатформенный протокол разработки

Целевая модель работы:
- host‑оркестратор + 3 VM (Windows/Linux/macOS) + SSH;
- все платформозависимые сборки/запуски выполняются внутри соответствующей VM;
- обязательный охват 3 ОС в каждой итерации с build/test.

Статус:
- Инструментальная инфраструктура 3‑VM протокола работает;
- CI workflow настроен и проверен на всех 3 платформах.
