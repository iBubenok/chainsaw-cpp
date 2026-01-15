# Тестовые фикстуры C++ порта Chainsaw

## Происхождение

Фикстуры скопированы из upstream Chainsaw (Rust):
- **Upstream:** https://github.com/WithSecureLabs/chainsaw
- **Baseline commit:** `c692405ecbc9703d190a67fd0679e48961a99c3a` (`v2.13.1-4-gc692405`)
- **Источник в upstream:** `chainsaw/tests/`

## Лицензии

### EVTX фикстуры (`evtx/`)
- **Лицензия:** GNU GPL v3
- **Источник:** `chainsaw/tests/evtx/`
- **Использование:** TST-0001..TST-0004 (CLI integration tests)

### Sigma фикстуры (`sigma/`)
- **Входные Sigma-правила** (`sigma_simple.yml`, `sigma_collection.yml`):
  - **Лицензия:** Detection Rule License (DRL) 1.1
  - Поле `author` сохранено без изменений (требование DRL)
- **Expected outputs** (`*_output.yml`):
  - **Лицензия:** GNU GPL v3 (часть Chainsaw test suite)
- **Использование:** TST-0007, TST-0008 (Sigma conversion tests)

### SRUM фикстуры (`srum/`)
- **Статус:** Хранятся через Git LFS (размер > 1 MB)
- **Лицензия:** GNU GPL v3 (часть Chainsaw test suite)
- При клонировании: `git lfs pull` (если не скачались автоматически)
- См. `srum/README.md` для подробностей

## Структура

```
fixtures/
├── LICENSE                    # GNU GPL v3
├── README.md                  # Этот файл
├── evtx/
│   ├── security_sample.evtx   # 68 KB — EVTX для search/hunt
│   ├── clo_*.txt              # Expected stdout для CLI тестов
│   └── rule-any-logon.yml     # Chainsaw rule для hunt
├── sigma/
│   ├── sigma_simple.yml       # Sigma rule (DRL 1.1)
│   ├── sigma_simple_output.yml
│   ├── sigma_collection.yml   # Sigma collection (DRL 1.1)
│   └── sigma_collection_output.yml
└── srum/
    └── README.md              # Инструкция по получению
```

## Обновление фикстур

При обновлении upstream Chainsaw:

1. Проверить изменения:
   ```bash
   diff -r upstream/chainsaw/tests/evtx cpp/tests/fixtures/evtx
   diff -r upstream/chainsaw/tests/convert cpp/tests/fixtures/sigma
   ```

2. Обновить файлы при необходимости

3. Обновить baseline в этом README

4. Обновить golden runs если затронуто

## Политика

См. `docs/licensing/POL-0003-test-data-policy.md` для полных правил:
- Категория A (INCLUDE): файлы < 1 MB
- Категория B (REFERENCE-ONLY): файлы >= 1 MB
- Категория C (EXTERNAL): внешние датасеты
