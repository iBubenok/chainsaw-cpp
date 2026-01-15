# SRUM фикстуры (Категория B — Git LFS)

## Статус

Эти фикстуры хранятся через **Git LFS** для предотвращения раздувания репозитория:

| Файл | Размер | Назначение |
|------|--------|------------|
| `SOFTWARE` | 71 MB | SOFTWARE registry hive |
| `SRUDB.dat` | 1.8 MB | SRUM database |
| `analysis_srum_database_json.txt` | 3.4 MB | Expected stdout (JSON output) |
| `analysis_srum_database_table_details.txt` | 5 KB | Expected stdout (table details) |

## Использование

Эти фикстуры необходимы для тестов TST-0005 и TST-0006 (`analyse srum`).

## Получение фикстур

### После клонирования репозитория

Если Git LFS установлен, файлы скачиваются автоматически при `git clone`.

Если файлы не скачались (видите текстовые pointer-файлы):

```bash
git lfs install
git lfs pull
```

### Установка Git LFS

- **Linux:** `sudo apt install git-lfs && git lfs install`
- **macOS:** `brew install git-lfs && git lfs install`
- **Windows:** Скачайте с https://git-lfs.github.com/

### Обновление из upstream

При обновлении upstream Chainsaw используйте скрипт:

```bash
python3 tools/data/fetch_fixtures.py --srum
```

Или вручную:

```bash
cp -r upstream/chainsaw/tests/srum/* cpp/tests/fixtures/srum/
```

## Проверка целостности

```bash
sha256sum cpp/tests/fixtures/srum/SOFTWARE
sha256sum cpp/tests/fixtures/srum/SRUDB.dat
```

Ожидаемые значения (из upstream baseline `c692405`):
- `SOFTWARE`: `72a58b5e23ad484227fb92bc6ff0c1e2b0c4a72e6563273192b87088a7f374f1`
- `SRUDB.dat`: `fb3b913c8a94fae7d73f6d5641af9dd1a0040133744e07927082214a436d5c00`

## CI интеграция

CI использует `actions/checkout@v4` с `lfs: true`, что обеспечивает автоматическое скачивание LFS-объектов.

## Лицензия

Фикстуры являются частью тестового набора Chainsaw (GNU GPL v3).

## Связанные документы

- `docs/licensing/POL-0003-test-data-policy.md` — политика тестовых данных
- `.gitattributes` — конфигурация Git LFS
