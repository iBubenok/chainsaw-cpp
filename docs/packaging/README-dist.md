# Chainsaw C++ Port

Кроссплатформенный порт инструмента Chainsaw для работы с криминалистическими артефактами Windows.

## Возможности

- **Разбор EVTX**: анализ журналов событий Windows (.evtx)
- **Поиск**: поиск по паттернам и регулярным выражениям
- **Детекция**: обнаружение по правилам Sigma и Chainsaw
- **Анализ артефактов**: SRUM, Shimcache, MFT, Registry Hives

## Использование

```bash
# Справка
./chainsaw --help

# Дамп EVTX в JSON
./chainsaw dump <evtx_file> --json

# Поиск по паттерну
./chainsaw search <evtx_file> -e "mimikatz"

# Детекция по правилам Sigma
./chainsaw hunt <evtx_path> -s <sigma_rules> --mapping <mapping.yml>

# Анализ SRUM
./chainsaw analyse srum --software <SOFTWARE_hive> <SRUDB.dat>

# Анализ Shimcache
./chainsaw analyse shimcache <SYSTEM_hive>
```

## Поддерживаемые платформы

- Linux (x86_64, glibc 2.35+)
- macOS (arm64, macOS 15+)
- Windows (x64, Windows 10/11)

## Лицензия

GNU General Public License v3.0 — см. файл LICENSE.

Сторонние компоненты распространяются под собственными лицензиями — см. каталог `third_party/licenses/`.

## Исходный код

Репозиторий: https://github.com/iBubenok/chainsaw-cpp

## Атрибуция

Порт основан на оригинальном проекте [Chainsaw](https://github.com/WithSecureLabs/chainsaw) от WithSecure Labs.
