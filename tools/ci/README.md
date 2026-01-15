# CI-инструменты

Скрипты и конфигурации для непрерывной интеграции проекта Chainsaw C++ Port.

## Структура

```
tools/ci/
├── README.md        # Этот файл
├── build.sh         # Скрипт сборки для Unix (Linux/macOS)
└── build.ps1        # Скрипт сборки для Windows (PowerShell)

.github/workflows/
└── ci.yml           # GitHub Actions workflow
```

## GitHub Actions CI

Workflow `.github/workflows/ci.yml` запускает:

| Job | Платформа | Компилятор | Проверки |
|-----|-----------|------------|----------|
| `linux-gcc` | Ubuntu | GCC | Сборка (Debug/Release), тесты, format-check |
| `linux-asan` | Ubuntu | GCC | ASan прогон |
| `linux-ubsan` | Ubuntu | GCC | UBSan прогон |
| `macos` | macOS | AppleClang | Сборка (Debug/Release), тесты |
| `macos-asan` | macOS | AppleClang | ASan прогон |
| `windows` | Windows | MSVC | Сборка (Debug/Release), тесты |

### Триггеры

- Push в ветки `main`, `develop`
- Pull request в ветки `main`, `develop`
- Ручной запуск (workflow_dispatch)

Workflow срабатывает только при изменениях в `cpp/`, `third_party/` или самом workflow.

## Локальные скрипты сборки

### Linux / macOS

```bash
# Базовая сборка
./tools/ci/build.sh

# Debug-сборка с тестами
./tools/ci/build.sh --build-type Debug --test

# Сборка с ASan
./tools/ci/build.sh --sanitizer address --test

# Полная проверка (как в CI)
./tools/ci/build.sh --clean --warnings-as-errors --test --format-check
```

Опции:
- `--build-type TYPE` — Debug или Release (default: Release)
- `--sanitizer SAN` — address, undefined, thread, memory, none (default: none)
- `--warnings-as-errors` — трактовать предупреждения как ошибки
- `--no-tests` — не собирать тесты
- `--clean` — очистить build/ перед сборкой
- `--test` — запустить тесты после сборки
- `--format-check` — проверить форматирование
- `--jobs N` — количество параллельных задач

### Windows

```powershell
# Базовая сборка
.\tools\ci\build.ps1

# Debug-сборка с тестами
.\tools\ci\build.ps1 -BuildType Debug -Test

# Полная проверка
.\tools\ci\build.ps1 -Clean -WarningsAsErrors -Test
```

Опции:
- `-BuildType TYPE` — Debug или Release (default: Release)
- `-WarningsAsErrors` — трактовать предупреждения как ошибки
- `-NoTests` — не собирать тесты
- `-Clean` — очистить build/ перед сборкой
- `-Test` — запустить тесты после сборки
- `-Jobs N` — количество параллельных задач

## Критерии «зелёного» состояния (POL-0002)

Обязательные критерии:
- [ ] Сборка без ошибок на Linux, macOS, Windows
- [ ] Все тесты проходят на всех платформах
- [ ] Отсутствие ошибок ASan (Linux, macOS)
- [ ] Отсутствие ошибок UBSan (Linux)

Рекомендуемые критерии:
- [ ] Код отформатирован (format-check pass)
- [ ] Нет предупреждений компилятора

## Связанные документы

- `docs/governance/POL-0002-green-state-policy.md` — политика качества
- `docs/adr/ADR-0001-build-system-and-cpp-standard.md` — система сборки
- `docs/adr/ADR-0008-unit-test-framework.md` — тестовый фреймворк
