# SPEC-0005 — Спецификация пакетирования и дистрибуции

## Статус
- Версия: 1
- Статус: Draft
- Дата: 2026-01-16

## Цель

Определить требования, процедуры и артефакты для пакетирования и дистрибуции C++ порта Chainsaw на целевых платформах (Windows/Linux/macOS).

## Связанные документы

- `AR-0001`: Архитектурные требования (REQ-OPS-0023..0030)
- `POL-0001`: Политика сторонних компонентов
- `LIC-0002`: Реестр лицензий C++ зависимостей
- `ADR-0002`: Стратегия vendoring и офлайн-сборки
- `.github/workflows/ci.yml`: CI workflow

---

## 1) Целевые платформы и требования

### 1.1. Матрица платформ

| Платформа | Архитектура | Компилятор | Min OS Version | Формат артефакта |
|-----------|-------------|------------|----------------|------------------|
| Linux | x86_64 | GCC 13+ | glibc 2.35+ (Ubuntu 22.04+) | tar.gz |
| macOS | arm64 | AppleClang 17+ | macOS 15+ | tar.gz |
| Windows | x64 | MSVC 19.44+ | Windows 10/11 | zip |

### 1.2. Требования к артефактам (REQ-OPS-0023, REQ-OPS-0026)

1. **Standalone binary**: единственный исполняемый файл без внешних зависимостей (кроме системных библиотек)
2. **Лицензионное соответствие**: включение LICENSE и NOTICE файлов
3. **Воспроизводимость**: одинаковый исходный код → одинаковый функционал бинарника (bit-exact не требуется)
4. **Air-gapped сборка**: возможность собрать без доступа к сети (REQ-OPS-0028)

---

## 2) Структура дистрибутивного пакета

### 2.1. Содержимое пакета

```
chainsaw-cpp-<version>-<platform>/
├── chainsaw[.exe] # Исполняемый файл
├── LICENSE # GPL v3 (основная лицензия)
├── README.md # Краткая документация
└── third_party/
 └── licenses/
 ├── rapidjson-MIT.txt # RapidJSON MIT License
 ├── pugixml-MIT.txt # pugixml MIT License
 ├── yaml-cpp-MIT.txt # yaml-cpp MIT License
 └── DRL-1.1.txt # Detection Rule License
```

### 2.2. Именование артефактов

- Linux: `chainsaw-cpp-0.1.0-linux-x86_64.tar.gz`
- macOS: `chainsaw-cpp-0.1.0-macos-arm64.tar.gz`
- Windows: `chainsaw-cpp-0.1.0-windows-x64.zip`

---

## 3) Процедура сборки

### 3.1. Linux (Release build)

```bash
# Подготовка
cmake -S cpp -B build \
 -G Ninja \
 -DCMAKE_BUILD_TYPE=Release \
 -DCHAINSAW_BUILD_TESTS=OFF \
 -DCHAINSAW_WARNINGS_AS_ERRORS=ON

# Сборка
cmake --build build -j$(nproc)

# Проверка
./build/chainsaw --version
ldd./build/chainsaw # Проверка зависимостей
```

### 3.2. macOS (Release build)

```bash
# Подготовка
cmake -S cpp -B build \
 -G Ninja \
 -DCMAKE_BUILD_TYPE=Release \
 -DCHAINSAW_BUILD_TESTS=OFF \
 -DCHAINSAW_WARNINGS_AS_ERRORS=ON

# Сборка
cmake --build build -j$(sysctl -n hw.ncpu)

# Проверка
./build/chainsaw --version
otool -L./build/chainsaw # Проверка зависимостей
```

### 3.3. Windows (Release build)

```cmd
rem Подготовка (Visual Studio)
cmake -S cpp -B build ^
 -DCMAKE_BUILD_TYPE=Release ^
 -DCHAINSAW_BUILD_TESTS=OFF ^
 -DCHAINSAW_WARNINGS_AS_ERRORS=ON

rem Сборка
cmake --build build --config Release -j %NUMBER_OF_PROCESSORS%

rem Проверка
build\Release\chainsaw.exe --version
```

---

## 4) Процедура пакетирования

### 4.1. Подготовка лицензионных файлов

Перед пакетированием необходимо создать файлы лицензий сторонних компонентов:

```bash
# Создать директорию для лицензий
mkdir -p out/package/third_party/licenses

# Скопировать лицензии
cp LICENSE out/package/
cp third_party/licenses/DRL-1.1.txt out/package/third_party/licenses/

# Извлечь лицензии из vendored зависимостей
cp third_party/rapidjson/license.txt out/package/third_party/licenses/rapidjson-MIT.txt
cp third_party/pugixml/LICENSE.md out/package/third_party/licenses/pugixml-MIT.txt
cp third_party/yaml-cpp/LICENSE out/package/third_party/licenses/yaml-cpp-MIT.txt
```

### 4.2. Пакетирование (Linux/macOS)

```bash
# Создать структуру
VERSION="0.1.0"
PLATFORM="linux-x86_64" # или macos-arm64
PKG_NAME="chainsaw-cpp-${VERSION}-${PLATFORM}"

mkdir -p "out/${PKG_NAME}"
cp build/chainsaw "out/${PKG_NAME}/"
cp LICENSE "out/${PKG_NAME}/"
cp docs/packaging/README-dist.md "out/${PKG_NAME}/README.md"
cp -r out/package/third_party "out/${PKG_NAME}/"

# Создать архив
cd out && tar -czvf "${PKG_NAME}.tar.gz" "${PKG_NAME}"
```

### 4.3. Пакетирование (Windows)

```cmd
rem Создать структуру
set VERSION=0.1.0
set PLATFORM=windows-x64
set PKG_NAME=chainsaw-cpp-%VERSION%-%PLATFORM%

mkdir "out\%PKG_NAME%"
copy build\Release\chainsaw.exe "out\%PKG_NAME%\"
copy LICENSE "out\%PKG_NAME%\"
copy docs\packaging\README-dist.md "out\%PKG_NAME%\README.md"
xcopy /E /I out\package\third_party "out\%PKG_NAME%\third_party"

rem Создать архив (PowerShell)
powershell Compress-Archive -Path "out\%PKG_NAME%" -DestinationPath "out\%PKG_NAME%.zip"
```

---

## 5) Проверка воспроизводимости

### 5.1. Критерии воспроизводимости

1. **Функциональная эквивалентность**: `--version`, `--help`, и функциональные команды дают идентичный результат
2. **Тесты проходят**: все unit-тесты проходят на собранном бинарнике
3. **Лицензионное соответствие**: все обязательные файлы лицензий присутствуют

### 5.2. Процедура проверки

```bash
# 1. Проверка версии
./chainsaw --version # Ожидается: "chainsaw-cpp 0.1.0"

# 2. Проверка help
./chainsaw --help # Ожидается: вывод справки без ошибок

# 3. Базовый функциональный тест (dump)
./chainsaw dump <evtx_file> --json # Ожидается: JSON вывод без ошибок

# 4. Проверка наличия файлов лицензий
ls -la third_party/licenses/
# Ожидается: DRL-1.1.txt, rapidjson-MIT.txt, pugixml-MIT.txt, yaml-cpp-MIT.txt
```

---

## 6) Лицензионное соответствие (REQ-OPS-0026)

### 6.1. Обязательства по лицензиям

| Компонент | Лицензия | Обязательства |
|-----------|----------|---------------|
| chainsaw-cpp | GPL v3 | Исходный код доступен, LICENSE включён |
| RapidJSON | MIT | Сохранение copyright, LICENSE включён |
| pugixml | MIT | Сохранение copyright, LICENSE включён |
| yaml-cpp | MIT | Сохранение copyright, LICENSE включён |
| Sigma rules | DRL 1.1 | Атрибуция автора, DRL-1.1.txt включён |

### 6.2. Чек-лист комплаенса

- LICENSE (GPL v3) присутствует в корне пакета
- third_party/licenses/ содержит все необходимые файлы
- README.md содержит информацию о лицензии
- Исходный код доступен (публичный репозиторий)

---

## 7) Air-gapped сборка (REQ-OPS-0028)

### 7.1. Подготовка офлайн-окружения

Для сборки без доступа к сети необходимы:

1. **Исходники проекта**: полная копия репозитория
2. **Vendored зависимости**: `third_party/` с RapidJSON, pugixml, yaml-cpp, GoogleTest
3. **Инструменты**: CMake 3.16+, Ninja/Make, компилятор (GCC/Clang/MSVC)

### 7.2. Проверка офлайн-сборки

```bash
# Отключить сеть и выполнить сборку
cmake -S cpp -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/chainsaw --version
```

---

## 8) Артефакты шага

| Артефакт | Путь | Описание |
|----------|------|----------|
| SPEC-0005 | `docs/packaging/SPEC-0005-packaging-specification.md` | Данный документ |
| README-dist | `docs/packaging/README-dist.md` | README для дистрибутива |
| Пакеты | `out/chainsaw-cpp-*` | Дистрибутивные архивы |
| REP-0007 | `docs/reports/REP-0007-packaging-verification.md` | Отчёт о верификации |

---

## 9) Связь с требованиями

| Требование | Покрытие |
|------------|----------|
| REQ-OPS-0023 | Воспроизводимый цикл сборки задокументирован в разделе 3 |
| REQ-OPS-0026 | Лицензионный комплаенс описан в разделе 6 |
| REQ-OPS-0028 | Air-gapped сборка описана в разделе 7 |
| REQ-OPS-0029 | Исключение временных каталогов обеспечено.gitignore |
