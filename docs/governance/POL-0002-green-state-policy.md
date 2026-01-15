# POL-0002 — Политика «зелёного состояния» (Quality Gates)

## Статус
- Версия: 1
- Статус: Active
- Дата создания: 2026-01-08
- Шаг плана:

## Связанные документы
- Требования: `AR-0001` (`REQ-SEC-0022`, `REQ-OPS-0023`)
- Developer guide: `GUIDE-0001`
- ADR: `ADR-0001` (CMake), `ADR-0008` (GoogleTest)

---

## 1) Назначение

Документ определяет:
- что считается **«зелёным» (pass)** состоянием проекта;
- какие проверки обязательны;
- как воспроизвести проверки локально и в CI;
- критерии **fail** и процедуру восстановления.

---

## 2) Инструменты качества

### 2.1. Компилятор и предупреждения

| Параметр | Описание | Обязательность |
|----------|----------|----------------|
| C++17 | Базовый стандарт языка (ADR-0001) | Обязательно |
| Строгие предупреждения | `-Wall -Wextra -Wpedantic` (GCC/Clang), `/W4 /permissive-` (MSVC) | Обязательно |
| Предупреждения как ошибки | `-DCHAINSAW_WARNINGS_AS_ERRORS=ON` | Рекомендуется в CI |

### 2.2. Форматирование кода (clang-format)

| Файл конфигурации | `cpp/.clang-format` |
|-------------------|---------------------|
| Версия | clang-format 15+ |
| Базовый стиль | Google, адаптированный для проекта |

**Команды:**
```bash
# Форматирование (изменяет файлы)
cmake --build build --target format

# Проверка без изменения
cmake --build build --target format-check
```

### 2.3. Статический анализ (clang-tidy)

| Файл конфигурации | `cpp/.clang-tidy` |
|-------------------|-------------------|
| Версия | clang-tidy 15+ |
| Фокус | bugprone, clang-analyzer, performance, modernize |

**Команды:**
```bash
# Включение при сборке
cmake -S cpp -B build -DCHAINSAW_ENABLE_CLANG_TIDY=ON

# Ручной запуск
clang-tidy cpp/src/**/*.cpp -- -I cpp/include -std=c++17
```

### 2.4. Санитайзеры (REQ-SEC-0022)

| Санитайзер | Опция CMake | Платформы | Назначение |
|------------|-------------|-----------|------------|
| Address (ASan) | `-DCHAINSAW_SANITIZER=address` | Linux, macOS, Windows (MSVC 2019+) | buffer overflow, use-after-free, memory leaks |
| Undefined (UBSan) | `-DCHAINSAW_SANITIZER=undefined` | Linux, macOS | undefined behavior |
| Thread (TSan) | `-DCHAINSAW_SANITIZER=thread` | Linux, macOS | data races |
| Memory (MSan) | `-DCHAINSAW_SANITIZER=memory` | Linux (Clang only) | uninitialized memory |

**Команды:**
```bash
# Сборка с ASan
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Debug -DCHAINSAW_SANITIZER=address
cmake --build build
ctest --test-dir build --output-on-failure
```

### 2.5. Тестирование (ADR-0008)

| Фреймворк | GoogleTest v1.15.2 |
|-----------|-------------------|
| Запуск | `ctest --test-dir build --output-on-failure` |
| Минимальное покрытие | Базовые тесты CLI, platform, output |

---

## 3) Критерии «зелёного» состояния

### 3.1. Обязательные критерии (блокирующие)

| ID | Критерий | Проверка |
|----|----------|----------|
| GS-001 | Сборка без ошибок на 3 платформах | `cmake --build build` завершается с exit code 0 |
| GS-002 | Сборка без предупреждений (при `WARNINGS_AS_ERRORS=ON`) | Все предупреждения устранены |
| GS-003 | Все тесты проходят | `ctest` завершается с exit code 0, 100% тестов pass |
| GS-004 | Отсутствие ошибок ASan | Прогон с ASan не выявляет нарушений памяти |
| GS-005 | Отсутствие ошибок UBSan | Прогон с UBSan не выявляет UB |

### 3.2. Рекомендуемые критерии (не блокирующие)

| ID | Критерий | Проверка |
|----|----------|----------|
| GS-010 | Код отформатирован | `format-check` завершается с exit code 0 |
| GS-011 | clang-tidy без критичных замечаний | Нет ошибок категории error |
| GS-012 | Документация актуальна | Нет расхождений между кодом и docs |

---

## 4) Матрица платформ

| Платформа | Компилятор | CI/Проверка | Санитайзеры |
|-----------|------------|-------------|-------------|
| Linux (x86_64) | GCC 12+ | Обязательно | ASan, UBSan, TSan |
| macOS (arm64) | AppleClang 17+ | Обязательно | ASan, UBSan |
| Windows (x86_64) | MSVC 19.44+ | Обязательно | ASan (ограниченно) |

---

## 5) Процедура проверки

### 5.1. Локальная проверка (разработчик)

```bash
# 1. Конфигурация
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Debug

# 2. Сборка
cmake --build build -j

# 3. Тесты
ctest --test-dir build --output-on-failure

# 4. Форматирование (опционально)
cmake --build build --target format-check
```

### 5.2. Полная проверка (перед коммитом)

```bash
# 1. Чистая сборка
rm -rf build && cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Debug

# 2. С предупреждениями как ошибками
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Debug -DCHAINSAW_WARNINGS_AS_ERRORS=ON
cmake --build build

# 3. Тесты
ctest --test-dir build --output-on-failure

# 4. ASan прогон (Linux/macOS)
rm -rf build && cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Debug -DCHAINSAW_SANITIZER=address
cmake --build build
ctest --test-dir build --output-on-failure
```

### 5.3. Проверка на 3 VM (3-VM протокол)

Для шагов со сборкой обязательна проверка на всех 3 платформах через SSH.

```bash
# Копирование на VM
scp -r cpp user@vm-linux:~/cpp
scp -r cpp yan@vm-mac:~/cpp
scp -r cpp user@vm-win:C:/Users/user/

# Сборка и тесты на каждой VM
ssh user@vm-linux 'cmake -S ~/cpp -B ~/cpp/build && cmake --build ~/cpp/build && ctest --test-dir ~/cpp/build'
```

---

## 6) Обработка fail

### 6.1. Блокирующий fail

1. **Сборка не проходит**: исправить ошибку компиляции до продолжения работы.
2. **Тест падает**: исправить код или тест; не переходить к следующему шагу.
3. **Санитайзер выявил ошибку**: исправить проблему памяти/UB до мержа.

### 6.2. Рекомендуемый fail

1. **Форматирование**: запустить `cmake --build build --target format`.
2. **clang-tidy**: оценить замечания, исправить критичные.

---

## 7) Интеграция с CI

CI должен выполнять:
1. Сборку на 3 платформах (Windows/Linux/macOS).
2. Запуск тестов (`ctest`).
3. Проверку форматирования (`format-check`).
4. Прогон с ASan (Linux, опционально macOS).

Отчёт CI должен содержать:
- Статус сборки для каждой платформы.
- Результаты тестов (pass/fail).
- Замечания санитайзеров (если есть).

---

## 8) Эволюция политики

| Шаг | Изменение |
|-----|-----------|
| | Базовая политика (текущий документ) |
| | Интеграция с CI |
| | Добавление diff-harness проверок |
| | Расширение security-проверок |

---

## 9) Чек-лист «зелёного» состояния

- Сборка успешна на Linux
- Сборка успешна на macOS
- Сборка успешна на Windows
- Все тесты проходят на Linux
- Все тесты проходят на macOS
- Все тесты проходят на Windows
- ASan прогон без ошибок (Linux/macOS)
- UBSan прогон без ошибок (Linux/macOS)
- Код отформатирован (format-check pass)
