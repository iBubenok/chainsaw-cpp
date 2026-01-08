# 00_DECISION_LOG

Индекс решений проекта.

| ID | Дата | Статус | Кратко | Файл (SoT) |
|---|---|---|---|---|
| DEC-0001 | 2025-12-24 | Accepted | Стратегия репозитория и синхронизации с upstream | `docs/decisions/DEC-0001-repo-strategy.md` |
| DEC-0002 | 2025-12-25 | Accepted | Лицензирование и политика third-party | `docs/decisions/DEC-0002-licensing-and-third-party-policy.md` |
| DEC-0003 | 2025-12-30 | Accepted | Инструменты для сборки Rust golden runs | `docs/decisions/DEC-0003-golden-runs-tooling.md` |
| DEC-0004 | 2025-12-30 | Accepted | Временные golden runs вне native окружения + обязательство пересъёма | `docs/decisions/DEC-0004-non-native-golden-runs-temporary.md` |
| DEC-0005 | 2025-12-30 | Accepted | Нормализация путей и запись stdout напрямую в golden runs | `docs/decisions/DEC-0005-golden-runs-paths-and-stdout.md` |
| DEC-0006 | 2025-12-30 | Accepted | Совместимость с Python 3.9 и пересъём macOS эталонов | `docs/decisions/DEC-0006-python39-and-macos-regeneration.md` |
| DEC-0007 | 2026-01-01 | Accepted | Финализация Linux golden runs на native Linux | `docs/decisions/DEC-0007-linux-native-golden-runs.md` |
| DEC-0008 | 2026-01-02 | Accepted | Исключение `.local/` из репозитория и снапшотов | `docs/decisions/DEC-0008-exclude-local-from-repo.md` |
| DEC-0009 | 2026-01-03 | Accepted | Механизм PUBLIC/PRIVATE и протокол подготовки очищенного public-коммита | `docs/decisions/DEC-0009-public-mirror-mechanism.md` |
| DEC-0010 | 2026-01-04 | Accepted | Базовый механизм 3‑VM оркестрации по SSH (sync snapshot + exec) | `docs/decisions/DEC-0010-cli-3vm-ssh-orchestration-baseline.md` |
| ADR-0001 | 2026-01-02 | Accepted | Система сборки и стандарт C++ | `docs/adr/ADR-0001-build-system-and-cpp-standard.md` |
| ADR-0002 | 2026-01-02 | Accepted | Управление зависимостями C++ (vendoring, офлайн‑сборка) | `docs/adr/ADR-0002-dependency-management-and-vendoring.md` |
| ADR-0003 | 2026-01-02 | Accepted | Библиотека JSON (парсинг и генерация вывода) | `docs/adr/ADR-0003-json-library.md` |
| ADR-0004 | 2026-01-02 | Accepted | Библиотека YAML (Sigma/Chainsaw rules и mappings) | `docs/adr/ADR-0004-yaml-library.md` |
| ADR-0005 | 2026-01-02 | Accepted | Regex‑движок (RE2) | `docs/adr/ADR-0005-regex-engine.md` |
| ADR-0006 | 2026-01-02 | Accepted | CLI и пользовательский вывод (help/errors/таблицы/прогресс) | `docs/adr/ADR-0006-cli-and-user-visible-output.md` |
| ADR-0007 | 2026-01-02 | Accepted | Форматирование строк и внутреннее логирование | `docs/adr/ADR-0007-string-formatting-and-logging.md` |
| ADR-0008 | 2026-01-02 | Accepted | Фреймворк модульных тестов (gtest) | `docs/adr/ADR-0008-unit-test-framework.md` |
| ADR-0009 | 2026-01-02 | Accepted | Стратегия парсеров форензик‑форматов (EVTX/ESEDB/HVE/MFT/XML) | `docs/adr/ADR-0009-forensic-parsers-strategy.md` |
| ADR-0010 | 2026-01-02 | Accepted | Файловая система, пути и кодировки | `docs/adr/ADR-0010-filesystem-and-path-encoding.md` |
| ADR-0011 | 2026-01-02 | Accepted | Параллелизм и детерминизм результата | `docs/adr/ADR-0011-concurrency-and-determinism.md` |

## Планируемые решения
- Закрытие pending‑решений из `ADR-0009`:
  - выбор EVTX парсера;
  - выбор HVE/Registry парсера;
  - выбор MFT парсера;
  - выбор XML парсера.
- Фиксация baseline версий/коммитов и верификация лицензий для зависимостей из `LIC-0002` при фактическом vendoring.
