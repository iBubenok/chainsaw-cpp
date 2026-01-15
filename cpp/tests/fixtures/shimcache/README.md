# Shimcache фикстуры (Категория B — Reference-only)

## Статус

Эти фикстуры **НЕ включены** в репозиторий из-за размера:
- `SYSTEM.hive` — 12 MB (Windows Registry SYSTEM hive)
- `Amcache.hve` — 3.9 MB (Windows Amcache registry hive)

**RISK-0022 CLOSED (2026-01-13)**: SYSTEM.hive получен.
**RISK-SHIM-002 CLOSED (2026-01-13)**: Amcache.hve получен.

## Использование

- `SYSTEM.hive` — необходим для тестов TST-SHIM-001..013 (`analyse shimcache`)
- `Amcache.hve` — необходим для тестов TST-SHIM-014..016 (Amcache integration)

## Содержимое

### SYSTEM.hive
- Windows Registry SYSTEM hive (формат REGF)
- AppCompatCache (shimcache) данные
- Источник: Windows 11 VM

### Amcache.hve
- Windows Registry Amcache hive (формат REGF)
- Информация о запускавшихся программах
- Используется для enrichment shimcache timestamps
- Источник: Windows 11 VM

## Получение фикстур

### SYSTEM.hive

```powershell
# На Windows (требуются права администратора):
reg save HKLM\SYSTEM C:\Users\user\SYSTEM.hive /y
```

### Amcache.hve

Файл заблокирован системой, использовать esentutl с VSS:

```powershell
# На Windows (требуются права администратора):
esentutl.exe /y "C:\Windows\appcompat\Programs\Amcache.hve" /vss /d "C:\Users\user\Amcache.hve"
```

Затем скопировать на хост:
```bash
scp user@windows-vm:C:/Users/user/SYSTEM.hive cpp/tests/fixtures/shimcache/
scp user@windows-vm:C:/Users/user/Amcache.hve cpp/tests/fixtures/shimcache/
```

## Проверка целостности

После получения фикстуры:

```bash
# Проверка размера
ls -la cpp/tests/fixtures/shimcache/SYSTEM.hive
# Ожидаемый размер: ~12 MB

# Проверка типа файла
file cpp/tests/fixtures/shimcache/SYSTEM.hive
# Ожидаемый результат: MS Windows registry file, NT/2000 or above

# Проверка наличия shimcache
strings -a cpp/tests/fixtures/shimcache/SYSTEM.hive | grep -i "AppCompatCache"
# Ожидаемый результат: должны быть найдены строки
```

## CI интеграция

В CI эти тесты могут быть:
1. **Skip** — если фикстура отсутствует
2. **В отдельном job** — с кэшированием артефактов
3. **Вручную** — на локальной машине разработчика

## Лицензия

Фикстура является производным продуктом Windows Registry.
Используется только для тестирования в рамках проекта.

## История

| Дата | Изменение |
|------|-----------|
| 2026-01-13 | SYSTEM.hive получен с Windows 11 VM (RISK-0022 CLOSED) |
| 2026-01-13 | Amcache.hve получен с Windows 11 VM через esentutl /vss (RISK-SHIM-002 CLOSED) |
