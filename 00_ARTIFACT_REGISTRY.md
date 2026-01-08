# 00_ARTIFACT_REGISTRY

Реестр значимых артефактов проекта (см. `docs/governance/GOV-0001-traceability-and-sot.md`).

| ID | Название | Тип | Статус | Где лежит | Шаг | Коротко что внутри |
|---|---|---|---|---|---|---|
| ART-0003 | Профиль компании «Амбрелла» | Документ | Imported | `docs/context/Ambrella_company_profile.docx` | 0 | Контекст ИБ‑домена (используется при формировании требований AR) |
| ART-0004 | Upstream Chainsaw (Rust) — исходники | Входные данные | External | upstream/chainsaw | 0 | Репозиторий WithSecureLabs/chainsaw |
| ART-0005 | Sigma rules dataset | Входные данные | External | upstream/sigma | 0 | Репозиторий SigmaHQ/sigma |
| ART-0006 | EVTX-ATTACK-SAMPLES dataset | Входные данные | External | upstream/EVTX-ATTACK-SAMPLES | 0 | Репозиторий sbousseaden/EVTX-ATTACK-SAMPLES |
| ART-0007 | Решение: стратегия репозитория и синхронизации с upstream | Решение (DEC) | Created | `docs/decisions/DEC-0001-repo-strategy.md` | 5 | Разделение `upstream/` и `cpp/`, правила фиксации материалов |
| ART-0008 | Решение: лицензирование и third-party policy | Решение (DEC) | Created | `docs/decisions/DEC-0002-licensing-and-third-party-policy.md` | 4 | Политика комплаенса, регистрация лицензий/NOTICE |
| ART-0009 | README репозитория порта | Документ | Created | `README.md` | 1 | Навигация по SoT и структуре репозитория |
| ART-0010 | Инвентаризация входных материалов | Документ | Created | `docs/inventory/INV-0001-input-materials.md` | 1 | Список входов + целевые окружения + ограничения |
| ART-0011 | Gate A→B review | Отчёт (gate) | Created | `docs/reports/Gate_A_B_2025-12-25.md` | Gate A→B | Результаты перехода A→B + риски на переход |
| ART-0012 | Baseline upstream (что именно портируем) | Документ | Created | `docs/baseline/BASELINE-0001-upstream.md` | 3 | Фиксация commit/describe, источников и ограничений |
| ART-0013 | Реестр лицензий upstream | Документ | Created | `docs/licensing/LIC-0001-upstream-licenses.md` | 4 | Таблица лицензий и обязательств (provenance) |
| ART-0014 | Политика third-party компонентов и NOTICE | Документ | Created | `docs/licensing/POL-0001-third-party-policy.md` | 4 | Правила включения лицензий/NOTICE в порт |
| ART-0016 | Лицензия репозитория порта | Лицензия | Added | `LICENSE` | 4 | Текст лицензии репозитория порта |
| ART-0017 | Текст DRL 1.1 (Sigma) | Лицензия | Added | `third_party/licenses/DRL-1.1.txt` | 4 | Лицензия Sigma Detection Rule License 1.1 |
| ART-0018 | GOV: ID scheme / SoT / traceability | Документ | Created | `docs/governance/GOV-0001-traceability-and-sot.md` | 5 | Правила идентификаторов, SoT и трассируемости |
| ART-0020 | GOV: критерии эквивалентности 1:1 | Документ | Created | `docs/governance/GOV-0002-equivalence-criteria.md` | 6 | Определение «1:1» и допустимые доказательства |
| ART-0023 | As‑Is: CLI‑контракт Chainsaw | Документ | Created | `docs/as_is/CLI-0001-chainsaw-cli-contract.md` | 7 | Наблюдаемое поведение CLI (опции/ошибки/форматы) |
| ART-0025 | As‑Is: инвентарь фич Chainsaw | Документ | Created | `docs/as_is/FEAT-0001-feature-inventory.md` | 8 | Функциональные режимы, флаги, зависимости |
| ART-0028 | As‑Is: архитектура Rust (границы/модули) | Документ | Created | `docs/as_is/ARCH-0001-rust-as-is-architecture.md` | 9 | Map модулей Rust и платформенные особенности |
| ART-0029 | As‑Is: форматы данных и преобразования | Документ | Created | `docs/as_is/DATA-0001-data-formats-and-transformations.md` | 10 | Пайплайны форматов и преобразований |
| ART-0030 | As‑Is: зависимости Rust и их роль | Документ | Created | `docs/as_is/DEPS-0001-rust-dependencies.md` | 11 | Карта crates/lib и их влияние на поведение |
| ART-0031 | Test matrix Rust (test-to-test) | Документ | Created | `docs/tests/TESTMAT-0001-rust-test-matrix.md` | 12 | SoT для `TST-*` и связей с upstream тестами |
| ART-0034 | Скрипт golden run (минимальный) | Артефакт (script) | Created | `tools/golden_run_simple.py` | 13 | Запуск набора команд и сохранение stdout/stderr/exit code |
| ART-0035 | Отчёт Rust golden runs | Отчёт | Created | `docs/reports/REP-0001-rust-golden-runs.md` | 13 | Протокол `RUN-*`, команды, результаты, замечания |
| ART-0036 | Скрипт полного набора golden runs | Артефакт (script) | Created | `tools/golden_runs_full/run_rust_golden_runs.py` | 13 | Сбор `RUN-*` + метаданные среды + упаковка |
| ART-0037 | Runbook полного набора golden runs | Документ | Created | `tools/golden_runs_full/README.md` | 13 | Команды запуска, структура выходных данных |
| ART-0041 | Итоговый отчёт Step 13 | Документ | Created | `docs/reports/REP-Step13-golden-runs-summary.md` | 13 | Итоги сбора эталонов и протоколов |
| ART-0044 | Gate B→D review | Отчёт (gate) | Created | `docs/reports/Gate_B_D_2026-01-02.md` | Gate B→D | Результаты перехода B→D + риски на переход |
| ART-0046 | Решение: инструменты для сборки Rust golden runs | Решение (DEC) | Created | `docs/decisions/DEC-0003-golden-runs-tooling.md` | 13 | Формализует стандартный набор инструментов для `RUN-*` |
| ART-0047 | Решение: временные non-native эталоны и пересъём | Решение (DEC) | Created | `docs/decisions/DEC-0004-non-native-golden-runs-temporary.md` | 13 | Разрешает временную фиксацию, но требует пересъём native |
| ART-0048 | Решение: нормализация путей и сохранение stdout | Решение (DEC) | Created | `docs/decisions/DEC-0005-golden-runs-paths-and-stdout.md` | 13 | Устраняет платформенные ошибки путей и искажения stdout |
| ART-0049 | Решение: Python 3.9 совместимость и macOS пересъём | Решение (DEC) | Created | `docs/decisions/DEC-0006-python39-and-macos-regeneration.md` | 13 | Обеспечивает воспроизводимость на macOS |
| ART-0050 | Решение: финализация Linux native golden runs | Решение (DEC) | Created | `docs/decisions/DEC-0007-linux-native-golden-runs.md` | 13 | Закрепляет Linux эталон как источник истины |
| ART-0051 | Решение: исключение `.local/` из репозитория и снапшотов | Решение (DEC) | Created | `docs/decisions/DEC-0008-exclude-local-from-repo.md` | 13 | Снижает риск попадания локального состояния в артефакты |
| ART-0052 | Постановка задачи: портирование Chainsaw (Rust) → C++ | Документ | Created | `docs/requirements/TASK-0001-problem-statement.md` | 15 | Проверяемая постановка: границы/сценарии/артефакты/DoD; трассировка к As‑Is/тестам/эталонам |
| ART-0054 | Архитектурные требования (AR) | Документ | Created | `docs/requirements/AR-0001-architectural-requirements.md` | 16 | Реестр требований `REQ-*` (FR/NFR/SEC/OPS) с критериями приёмки/верификацией |
| ART-0056 | Матрица вариантов технологий/зависимостей для C++ | Документ | Created | `docs/architecture/ARCH-0002-cpp-technology-options-matrix.md` | 17 | Options matrix по ключевым подсистемам (EVTX/YAML/Sigma/CLI/logging/tests/packaging/OS) + критерии выбора |
| ART-0058 | Решение: система сборки и стандарт C++ | Решение (ADR) | Created | `docs/adr/ADR-0001-build-system-and-cpp-standard.md` | 18 | CMake + C++17 как базовая платформа сборки |
| ART-0059 | Решение: управление зависимостями C++ (vendoring, офлайн) | Решение (ADR) | Created | `docs/adr/ADR-0002-dependency-management-and-vendoring.md` | 18 | Vendoring с фиксацией версий и без сети по умолчанию |
| ART-0060 | Решение: библиотека JSON | Решение (ADR) | Created | `docs/adr/ADR-0003-json-library.md` | 18 | RapidJSON + собственная каноникализация формата вывода |
| ART-0061 | Решение: библиотека YAML | Решение (ADR) | Created | `docs/adr/ADR-0004-yaml-library.md` | 18 | yaml-cpp + собственная валидация/нормализация |
| ART-0062 | Решение: regex-движок | Решение (ADR) | Created | `docs/adr/ADR-0005-regex-engine.md` | 18 | RE2 как базовый движок для regex |
| ART-0063 | Решение: CLI и пользовательский вывод | Решение (ADR) | Created | `docs/adr/ADR-0006-cli-and-user-visible-output.md` | 18 | Собственный CLI слой для byte-to-byte help/errors/таблиц |
| ART-0064 | Решение: форматирование строк и логирование | Решение (ADR) | Created | `docs/adr/ADR-0007-string-formatting-and-logging.md` | 18 | fmt для форматирования, без обязательного стороннего логгера |
| ART-0065 | Решение: фреймворк модульных тестов | Решение (ADR) | Created | `docs/adr/ADR-0008-unit-test-framework.md` | 18 | GoogleTest как основа test-to-test переноса |
| ART-0066 | Решение: стратегия парсеров форензик-форматов | Решение (ADR) | Created | `docs/adr/ADR-0009-forensic-parsers-strategy.md` | 18 | ESEDB=libesedb; EVTX/HVE/MFT/XML=pending с критериями выбора |
| ART-0067 | Реестр лицензий C++ зависимостей | Документ | Created | `docs/licensing/LIC-0002-cpp-dependencies.md` | 18 | Таблица выбранных/планируемых зависимостей + статусы Planned/Vendored/Verified |
| ART-0069 | Решение: пути, файловая система и кодировки | Решение (ADR) | Created | `docs/adr/ADR-0010-filesystem-and-path-encoding.md` | 18 | std::filesystem + единая политика преобразования path ⇄ UTF‑8 |
| ART-0070 | Решение: параллелизм и детерминизм | Решение (ADR) | Created | `docs/adr/ADR-0011-concurrency-and-determinism.md` | 18 | Детерминизм как приоритет; параллелизм только с стабильным упорядочиванием |
| ART-0071 | To‑Be архитектура C++ (детальная) | Документ | Created | `docs/architecture/TOBE-0001-cpp-to-be-architecture.md` | 19 | Границы `MOD-*`, интерфейсы/потоки данных, платформенные абстракции, карта As‑Is → To‑Be |
| ART-0073 | WBS и оценка трудозатрат (с трассировкой) | Документ | Created | `docs/planning/WBS-0001-work-breakdown-and-estimates.md` | 20 | WBS: работа → REQ/FEAT/TST/RUN/RISK/MOD + сводная оценка и буфер |
| ART-0075 | Практическое руководство разработчика | Документ | Created | `docs/guide/GUIDE-0001-developer-guide.md` | 21 | Нормы разработки/тестов/безопасности/кроссплатформенности; правила проверки соответствия |
| ART-0077 | Пакет согласования архитектуры | Документ | Created | `docs/governance/GOV-0003-architecture-approval-package.md` | 22 | Чек‑лист согласования + шаблон протокола + критерии «готово к старту реализации» |
| ART-0079 | Аудит консистентности документов №1 | Отчёт | Created | `docs/reports/REP-0002-consistency-audit-1.md` | 23 | Найденные несогласованности в Step 15–22 и применённые исправления |
| ART-0081 | Gate D→E review | Отчёт (gate) | Created | `docs/reports/Gate_D_E_2026-01-03.md` | Gate D→E | Результаты перехода D→E + риски на переход |
| ART-0082 | Протокол согласования архитектуры (To‑Be) — 2026-01-03 | Отчёт | Created | `docs/reports/Architecture_Approval_2026-01-03.md` | Gate D→E | Итог применения `GOV-0003` и решение о старте Step 24 |
| ART-0084 | Рамки публичной публикации и аудит (PUBLIC) | Документ (governance) | Created | `docs/governance/GOV-0004-public-repository-scope-and-audit.md` | Gate D→E | Scope PUBLIC для Gate D→E и дальнейшей публикации; критерии очистки |
| ART-0086 | Решение: механизм PUBLIC/PRIVATE и протокол подготовки public‑коммита | Решение (DEC) | Created | `docs/decisions/DEC-0009-public-mirror-mechanism.md` | Gate D→E | Выбор механизма двух репозиториев + export/audit по allowlist |
| ART-0087 | Стандарт ведения публичного репозитория | Документ (governance) | Created | `docs/governance/GOV-0005-public-repo-operations.md` | Gate D→E | Ветки/коммиты/PR + протокол экспорт → аудит → публикация |
| ART-0100 | Финальный аудит PUBLIC‑пакета и механизма публикации | Отчёт | Created | `docs/reports/REP-0003-public-package-audit.md` | Gate D→E | Итоги проверки экспорта/аудита PUBLIC + чек‑лист и runbook публикации |
| ART-0121 | Стандарт документации | Документ (standards) | Created | `docs/standards/STD-0001-documentation-standards.md` | Gate D→E | Стандарт ведения проектной документации на основе best practices |
