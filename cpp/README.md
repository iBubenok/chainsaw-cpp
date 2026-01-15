# Chainsaw C++ Port

Порт инструмента Chainsaw на C++ для быстрой работы с криминалистическими артефактами.

## Статус

**Текущий прогресс:** Реализованы основные парсеры форензик-артефактов.

| Компонент | Статус | Тесты |
|-----------|--------|-------|
| CLI Framework | Done | 2 |
| Platform Layer | Done | 1 |
| Reader Framework | Done | - |
| EVTX Parser | Done | 30+ |
| HVE Parser (Registry) | Done | 20+ |
| ESEDB Parser (ESE DB) | **Done** | 21 |
| SRUM Analyser | Done | 24 |
| MFT Parser | Done | 20+ |
| Shimcache Analyser | Done | 25+ |

**Всего тестов:** 473 (все проходят)

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

# Команда dump
./build/chainsaw dump /path/to/file.evtx

# Анализ SRUM
./build/chainsaw analyse srum --software SOFTWARE SRUDB.dat
```

## Структура проекта

```
cpp/
├── CMakeLists.txt          # Корневой файл сборки
├── include/
│   └── chainsaw/           # Публичные заголовки
│       ├── cli.hpp         # MOD-0002: CLI парсинг
│       ├── output.hpp      # MOD-0003: Пользовательский вывод
│       ├── platform.hpp    # MOD-0004: Платформенные абстракции
│       ├── reader.hpp      # MOD-0005: Reader framework
│       ├── evtx.hpp        # MOD-0006: EVTX parser
│       ├── esedb.hpp       # MOD-0007: ESEDB parser (native)
│       ├── hve.hpp         # MOD-0008: HVE (registry) parser
│       ├── mft.hpp         # MOD-0009: MFT parser
│       ├── srum.hpp        # MOD-0014: SRUM analyser
│       └── shimcache.hpp   # MOD-0015: Shimcache analyser
├── src/
│   ├── app/
│   │   └── main.cpp        # MOD-0001: Точка входа
│   ├── cli/
│   │   ├── cli.cpp         # Парсинг аргументов
│   │   └── commands.cpp    # Реализация команд
│   ├── io/
│   │   ├── evtx.cpp        # EVTX parser
│   │   ├── esedb.cpp       # ESEDB parser (native ESE implementation)
│   │   ├── hve.cpp         # HVE parser
│   │   └── mft.cpp         # MFT parser
│   ├── analyse/
│   │   ├── srum.cpp        # SRUM analyser
│   │   └── shimcache.cpp   # Shimcache analyser
│   ├── output/
│   │   └── output.cpp      # Writer для stdout/stderr
│   └── platform/
│       └── platform.cpp    # Платформенные утилиты
└── tests/
    ├── CMakeLists.txt
    ├── test_cli_basic.cpp
    ├── test_platform_basic.cpp
    ├── test_evtx_gtest.cpp
    ├── test_esedb_gtest.cpp
    ├── test_hve_gtest.cpp
    ├── test_mft_gtest.cpp
    ├── test_srum_gtest.cpp
    └── test_shimcache_gtest.cpp
```

## Модули (MOD-*)

| MOD-ID   | Модуль     | Описание                                | Статус |
|----------|------------|----------------------------------------|--------|
| MOD-0001 | app        | Точка входа, dispatch подкоманд        | Done |
| MOD-0002 | cli        | Парсинг argv, help/version/errors      | Done |
| MOD-0003 | output     | Единый слой вывода stdout/stderr       | Done |
| MOD-0004 | platform   | Платформенные абстракции, пути, TTY    | Done |
| MOD-0005 | reader     | Reader framework для форензик-файлов   | Done |
| MOD-0006 | evtx       | Windows Event Log parser               | Done |
| MOD-0007 | esedb      | ESE Database parser (native impl)      | Done |
| MOD-0008 | hve        | Windows Registry hive parser           | Done |
| MOD-0009 | mft        | NTFS MFT parser                        | Done |
| MOD-0014 | srum       | SRUM database analyser                 | Done |
| MOD-0015 | shimcache  | Shimcache analyser                     | Done |

## ESEDB Parser

Собственная кроссплатформенная реализация парсера ESE Database (JET Blue):

- **Не требует внешних зависимостей** (libesedb не нужен)
- Работает на Windows, Linux, macOS
- Поддерживает все типы колонок ESE
- Парсит SruDbIdMapTable для SRUM

**Тесты:** 21 тест (20 PASS, 1 SKIP)

**Производительность:** SRUDB.dat (1.8MB, 3590 записей) → ~130ms

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
- ESEDB Spec: `docs/slices/SPEC-SLICE-016-esedb-parser.md`
- SRUM Spec: `docs/slices/SPEC-SLICE-017-analyse-srum.md`
