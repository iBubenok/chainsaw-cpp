# Chainsaw C++ Port

Порт инструмента [Chainsaw](https://github.com/WithSecureLabs/chainsaw) с Rust на C++.

## О проекте

**Chainsaw** — CLI-инструмент для анализа Windows Event Logs (EVTX) с использованием Sigma-правил для threat hunting и выявления атак.

Этот проект выполняет **полное портирование** Chainsaw с Rust на C++:
- эквивалентность поведения 1:1
- кроссплатформенность (Windows/Linux/macOS)
- полный перенос тестов

## Документация

### Архитектура

| Документ | Описание |
|----------|----------|
| [TASK-0001](docs/requirements/TASK-0001-problem-statement.md) | Постановка задачи |
| [AR-0001](docs/requirements/AR-0001-architectural-requirements.md) | Архитектурные требования (FR/NFR/SEC/OPS) |
| [TOBE-0001](docs/architecture/TOBE-0001-cpp-to-be-architecture.md) | To-Be архитектура C++ |
| [ARCH-0002](docs/architecture/ARCH-0002-cpp-technology-options-matrix.md) | Матрица технологических решений |

### Архитектурные решения (ADR)

| ADR | Тема |
|-----|------|
| [ADR-0001](docs/adr/ADR-0001-build-system-and-cpp-standard.md) | Система сборки и стандарт C++ |
| [ADR-0002](docs/adr/ADR-0002-dependency-management-and-vendoring.md) | Управление зависимостями |
| [ADR-0003](docs/adr/ADR-0003-json-library.md) | Библиотека JSON |
| [ADR-0004](docs/adr/ADR-0004-yaml-library.md) | Библиотека YAML |
| [ADR-0005](docs/adr/ADR-0005-regex-engine.md) | Движок регулярных выражений |
| [ADR-0006](docs/adr/ADR-0006-cli-and-user-visible-output.md) | CLI и пользовательский вывод |
| [ADR-0007](docs/adr/ADR-0007-string-formatting-and-logging.md) | Форматирование строк и логирование |
| [ADR-0008](docs/adr/ADR-0008-unit-test-framework.md) | Фреймворк unit-тестов |
| [ADR-0009](docs/adr/ADR-0009-forensic-parsers-strategy.md) | Стратегия форензик-парсеров |
| [ADR-0010](docs/adr/ADR-0010-filesystem-and-path-encoding.md) | Файловая система и кодировки путей |
| [ADR-0011](docs/adr/ADR-0011-concurrency-and-determinism.md) | Конкурентность и детерминизм |

### Управление проектом

| Документ | Описание |
|----------|----------|
| [GOV-0001](docs/governance/GOV-0001-traceability-and-sot.md) | Схема идентификаторов и трассируемость |
| [GOV-0002](docs/governance/GOV-0002-equivalence-criteria.md) | Критерии эквивалентности 1:1 |
| [GOV-0003](docs/governance/GOV-0003-architecture-approval-package.md) | Пакет согласования архитектуры |
| [GUIDE-0001](docs/guide/GUIDE-0001-developer-guide.md) | Руководство разработчика |
| [WBS-0001](docs/planning/WBS-0001-work-breakdown-and-estimates.md) | План работ и оценки |

### As-Is анализ (Rust)

| Документ | Описание |
|----------|----------|
| [CLI-0001](docs/as_is/CLI-0001-chainsaw-cli-contract.md) | CLI-контракт Chainsaw |
| [FEAT-0001](docs/as_is/FEAT-0001-feature-inventory.md) | Инвентаризация фич |
| [ARCH-0001](docs/as_is/ARCH-0001-rust-as-is-architecture.md) | As-Is архитектура Rust |
| [DATA-0001](docs/as_is/DATA-0001-data-formats-and-transformations.md) | Форматы данных |
| [DEPS-0001](docs/as_is/DEPS-0001-rust-dependencies.md) | Зависимости Rust |

### Лицензирование

| Документ | Описание |
|----------|----------|
| [POL-0001](docs/licensing/POL-0001-third-party-policy.md) | Политика third-party |
| [LIC-0001](docs/licensing/LIC-0001-upstream-licenses.md) | Лицензии upstream |
| [LIC-0002](docs/licensing/LIC-0002-cpp-dependencies.md) | Лицензии C++ зависимостей |

## Структура репозитория

```
├── docs/                  # Документация
│   ├── adr/               # Architecture Decision Records
│   ├── architecture/      # Архитектурные описания
│   ├── as_is/             # Анализ Rust-версии
│   ├── decisions/         # Проектные решения
│   ├── governance/        # Стандарты проекта
│   ├── guide/             # Руководства
│   ├── licensing/         # Лицензии
│   ├── planning/          # Планирование
│   ├── reports/           # Отчёты
│   ├── requirements/      # Требования
│   ├── standards/         # Стандарты документации
│   └── tests/             # Тестовые матрицы
├── cpp/                   # Исходный код C++ порта
│   ├── include/           # Публичные заголовки
│   ├── src/               # Исходный код
│   ├── tests/             # Тесты (GoogleTest)
│   └── cmake/             # CMake модули
├── tools/ci/              # Скрипты сборки и CI
├── third_party/           # Вендорированные зависимости (GoogleTest)
└── .github/workflows/     # CI конфигурации (GitHub Actions)
```

## Сборка

### Требования

- CMake >= 3.16
- C++17 компилятор: GCC 8+, Clang 7+, MSVC 2017+
- GoogleTest (вендорирован в third_party/)

### Linux / macOS

```bash
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Windows

```bash
cmake -S cpp -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Статус проекта

Текущая фаза: **Инженерный фундамент — CI и качество**

- [x] Анализ As-Is (Rust)
- [x] Архитектурные требования
- [x] To-Be архитектура
- [x] Architecture Decision Records
- [x] Согласование архитектуры
- [x] Базовый C++ скелет и CLI
- [x] CI кроссплатформенный (Windows/Linux/macOS)
- [ ] Портирование функциональности
- [ ] Тестирование и верификация

## Лицензия

Проект следует политике лицензирования, описанной в [POL-0001](docs/licensing/POL-0001-third-party-policy.md).

## Связанные проекты

- [Chainsaw (Rust)](https://github.com/WithSecureLabs/chainsaw) — оригинальный проект
- [Sigma Rules](https://github.com/SigmaHQ/sigma) — правила детектирования
- [EVTX-ATTACK-SAMPLES](https://github.com/sbousseaden/EVTX-ATTACK-SAMPLES) — тестовые данные
