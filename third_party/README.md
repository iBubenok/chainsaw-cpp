# third_party

Каталог предназначен для хранения текстов лицензий и уведомлений (NOTICE), необходимых для соблюдения условий лицензирования сторонних компонентов, а также vendored зависимостей.

## Vendored зависимости

- `rapidjson/` — RapidJSON (MIT License) — JSON парсинг и сериализация (ADR-0003)
  - Источник: https://github.com/Tencent/rapidjson
  - Версия: master branch (2026-01-09), необходима для совместимости с GCC 14
  - Используется: SLICE-002 (Output Layer)

## Лицензии

- `licenses/DRL-1.1.txt` — текст Detection Rule License (DRL) 1.1, применяемой к Sigma-правилам.

GPL v3 для проекта порта зафиксирована в корневом файле `LICENSE`.
