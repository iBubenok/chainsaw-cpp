# Chainsaw C++ Port

Порт инструмента Chainsaw на C++ для быстрой работы с криминалистическими артефактами.

## Статус

**Step 24**: Минимально собираемый скелет проекта с базовой CLI-поверхностью.

## Требования

- CMake >= 3.16
- C++17 компилятор:
  - GCC >= 8
  - Clang >= 7
  - MSVC >= 2017 (Visual Studio 15.7+)

## Сборка

### Linux / macOS (Ninja / Unix Makefiles)

```bash
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Windows (Visual Studio)

```bash
cmake -S cpp -B build
cmake --build build --config Release
```

## Тестирование

```bash
# Linux / macOS
ctest --test-dir build --output-on-failure

# Windows
ctest --test-dir build -C Release --output-on-failure
```

## Запуск

```bash
# Показать справку
./build/chainsaw --help

# Показать версию
./build/chainsaw --version

# Команда dump (заглушка)
./build/chainsaw dump /path/to/file.evtx
```

## Структура проекта

```
cpp/
├── CMakeLists.txt          # Корневой файл сборки
├── include/
│   └── chainsaw/           # Публичные заголовки
│       ├── cli.hpp         # MOD-0002: CLI парсинг
│       ├── output.hpp      # MOD-0003: Пользовательский вывод
│       └── platform.hpp    # MOD-0004: Платформенные абстракции
├── src/
│   ├── app/
│   │   └── main.cpp        # MOD-0001: Точка входа
│   ├── cli/
│   │   ├── cli.cpp         # Парсинг аргументов
│   │   └── commands.cpp    # Реализация команд (заглушки)
│   ├── output/
│   │   └── output.cpp      # Writer для stdout/stderr
│   └── platform/
│       └── platform.cpp    # Платформенные утилиты
└── tests/
    ├── CMakeLists.txt
    ├── test_cli_basic.cpp
    └── test_platform_basic.cpp
```

## Модули (MOD-*)

| MOD-ID   | Модуль     | Описание                            |
|----------|------------|-------------------------------------|
| MOD-0001 | app        | Точка входа, dispatch подкоманд     |
| MOD-0002 | cli        | Парсинг argv, help/version/errors   |
| MOD-0003 | output     | Единый слой вывода stdout/stderr    |
| MOD-0004 | platform   | Платформенные абстракции, пути, TTY |

## CLI-контракт

Поддерживаемые команды (согласно CLI-0001):

- `dump` - дамп артефактов в YAML/JSON/JSONL
- `hunt` - применение правил детекта
- `lint` - проверка правил
- `search` - поиск по артефактам
- `analyse shimcache` - timeline из shimcache
- `analyse srum` - парсинг SRUM database

## Документация

- To-Be архитектура: `docs/architecture/TOBE-0001-cpp-to-be-architecture.md`
- ADR (решения): `docs/adr/ADR-0001...ADR-0011`
- Developer Guide: `docs/guide/GUIDE-0001-developer-guide.md`
- CLI-контракт: `docs/as_is/CLI-0001-chainsaw-cli-contract.md`
