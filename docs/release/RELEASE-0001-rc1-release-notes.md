# RELEASE-0001 — Release Notes: chainsaw-cpp 0.1.0 RC1

## Версия
- **Версия**: 0.1.0-rc1
- **Дата**: 2026-01-16
- **Статус**: Release Candidate 1

---

## Обзор

Первый Release Candidate (RC1) C++ порта Chainsaw — инструмента для forensic-анализа Windows Event Logs с использованием Sigma и Chainsaw правил.

### Ключевые характеристики

- **Полная функциональная эквивалентность** с upstream Chainsaw (Rust) версии 2.13.1
- **Кроссплатформенность**: Windows x64, Linux x86_64, macOS arm64
- **505 unit-тестов** с 100% pass rate на всех платформах
- **22 upstream теста** портированы с сохранением поведения
- **Критерии 1:1 (GOV-0002) C1-C7**: все PASS

---

## Возможности

### Команды

| Команда | Описание | Статус |
|---------|----------|--------|
| `search` | Поиск по EVTX файлам с regex/tau-паттернами | ✅ Реализовано |
| `hunt` | Обнаружение угроз с Sigma/Chainsaw правилами | ✅ Реализовано |
| `dump` | Дамп EVTX/JSON/XML файлов в JSON/YAML формат | ✅ Реализовано |
| `lint` | Валидация Sigma/Chainsaw правил | ✅ Реализовано |
| `analyse srum` | Анализ SRUM базы данных | ✅ Реализовано (Windows) |
| `analyse shimcache` | Анализ Shimcache из Registry | ✅ Реализовано |

### Поддерживаемые форматы

| Формат | Чтение | Записьп |
|--------|--------|--------|
| EVTX | ✅ | - |
| JSON/JSONL | ✅ | ✅ |
| XML | ✅ | - |
| YAML | ✅ | - |
| HVE (Registry) | ✅ | - |
| ESEDB (SRUM) | ✅ (Windows) | - |
| MFT | ✅ | - |

### Sigma поддержка

- **Полная поддержка Sigma 2.0** правил
- **Модификаторы**: contains, endswith, startswith, re, base64, base64offset, all
- **Rule Collections**: multi-document YAML

---

## Платформы

### Целевые платформы

| Платформа | Архитектура | Компилятор | Статус |
|-----------|-------------|------------|--------|
| Linux | x86_64 | GCC 14.2+ | ✅ Verified |
| macOS | arm64 | AppleClang 17+ | ✅ Verified |
| Windows | x64 | MSVC 19.44+ | ✅ Verified |

### Результаты верификации RC1

| Платформа | Unit-тесты | Время | Версия |
|-----------|------------|-------|--------|
| Linux x86_64 (GCC 14.2) | 505/505 PASS | 1.40s | 2.13.1 |
| macOS arm64 (AppleClang 17) | 505/505 PASS | 1.86s | 2.13.1 |
| Windows x64 (MSVC 19.44) | 505/505 PASS | 6.82s | 2.13.1 |

---

## Зависимости

### Vendored библиотеки

| Компонент | Версия | Лицензия |
|-----------|--------|----------|
| RapidJSON | - | MIT |
| pugixml | 1.14 | MIT |
| yaml-cpp | 0.8.0 | MIT |
| GoogleTest | 1.15.2 | BSD-3-Clause (только тесты) |

### Системные требования

- **C++ Standard**: C++17 (сборка использует C++20 features где доступны)
- **CMake**: 3.16+
- **Сборка**: Ninja или Make (Unix), MSBuild (Windows)

---

## Известные ограничения

### Платформозависимые функции

1. **ESEDB Parser (SRUM)**: работает только на Windows (из-за зависимости от libesedb)
2. **MFT Extract**: некоторые тесты скипаются на не-Windows платформах
3. **Symlink/Junction**: тесты безопасности скипаются где не применимо

### Документированные различия

1. **EventData flatten**: формат соответствует upstream после ADR-0012
2. **YAML dump**: выводит JSON (не нативный YAML) для совместимости

---

## Сборка из исходников

### Linux/macOS

```bash
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/chainsaw --version
```

### Windows

```cmd
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
build\Release\chainsaw.exe --version
```

---

## Лицензия

- **chainsaw-cpp**: GNU GPL v3
- **Зависимости**: MIT/BSD-3-Clause (совместимы с GPL)
- **Sigma Rules**: Detection Rule License 1.1 (DRL)

---

## Связанные документы

| Документ | Путь |
|----------|------|
| Parity Audit | `docs/reports/REP-0002-parity-audit.md` |
| E2E Validation | `docs/reports/REP-0008-e2e-validation.md` |
| Consistency Audit | `docs/reports/REP-0009-final-consistency-audit.md` |
| Security Hardening | `docs/reports/REP-0006-security-hardening.md` |
| Performance | `docs/reports/REP-0004-performance-profiling.md` |
| Packaging Spec | `docs/packaging/SPEC-0005-packaging-specification.md` |

---

## Контакты

- **Author**: Yan Bubenok
- **Email**: yan@bubenok.com
- **GitHub**: @iBubenok

---

> Документ создан: 2026-01-16 ( — Release Candidate)
