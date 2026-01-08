# SRUM фикстуры (Категория B — Reference-only)

## Статус

Эти фикстуры **НЕ включены** в репозиторий из-за размера:
- `SOFTWARE` — 71 MB (SOFTWARE registry hive)
- `SRUDB.dat` — 1.8 MB (SRUM database)
- `analysis_srum_database_table_details.txt` — 5 KB (expected stdout)
- `analysis_srum_database_json.txt` — 3.4 MB (expected stdout)

## Использование

Эти фикстуры необходимы для тестов TST-0005 и TST-0006 (`analyse srum`).

## Получение фикстур

### Вариант 1: Копирование из upstream

```bash
cp -r upstream/chainsaw/tests/srum/* cpp/tests/fixtures/srum/
```

### Вариант 2: Скачивание (при наличии скрипта)

```bash
python3 tools/data/fetch_srum_fixtures.py
```

## Проверка целостности

После получения фикстур проверьте хэши:

```bash
sha256sum cpp/tests/fixtures/srum/SOFTWARE
sha256sum cpp/tests/fixtures/srum/SRUDB.dat
```

Ожидаемые значения (из upstream baseline `c692405`):
- `SOFTWARE`: (вычислить при первом использовании)
- `SRUDB.dat`: (вычислить при первом использовании)

## CI интеграция

В CI эти тесты могут быть:
1. **Skip** — если фикстуры отсутствуют
2. **В отдельном job** — с кэшированием артефактов
3. **Вручную** — на локальной машине разработчика

## Лицензия

Фикстуры являются частью тестового набора Chainsaw (GNU GPL v3).
