# SPEC-SLICE-002 — Micro-spec Output Layer (Writer)

## Статус
- Версия: 1
- Дата: 2026-01-09
- Статус: UnitReady
- Слайс: SLICE-002
- Модуль: MOD-0003 (`output`)
- Фаза: (Слайс-анализ)

## Входы анализа (источники)
- Upstream Rust: `chainsaw/src/write.rs` (265 строк)
- Upstream Rust: `chainsaw/src/cli.rs` (1248 строк — функции вывода)
- TOBE-0001 секция 4.3: MOD-0003 контракт
- ADR-0003: RapidJSON для JSON
- ADR-0006: собственный слой CLI/output
- ADR-0007: fmt для форматирования
- Текущий скелет: `cpp/src/output/output.cpp`, `cpp/include/chainsaw/output.hpp`
- BACKLOG-0001: SLICE-002 Output Layer
- FEAT-0009: Вывод/форматы

---

## 1. FACTS (извлечённые из upstream)

### 1.1. Writer структура (write.rs:6-28)
**FACT-W01**: Глобальная структура `Writer` содержит поля:
- `format: Format` — формат вывода (Std, Csv, Json)
- `output: Option<File>` — опциональный файл для записи (при `--output`)
- `path: Option<PathBuf>` — путь к файлу/директории вывода
- `quiet: bool` — подавление информационных сообщений
- `verbose: u8` — уровень подробности (0..2+)

```rust
// write.rs:14-20
pub enum Format {
 #[default]
 Std,
 Csv,
 Json,
}
```

### 1.2. Макросы вывода (write.rs:63-194)

**FACT-W02**: Макрос `cs_print!` — условная печать в stdout или file:
```rust
// write.rs:63-76
macro_rules! cs_print {
 ($($arg:tt)*) => ({
 match $crate::writer.output.as_ref {
 Some(mut f) => f.write_all(format!($($arg)*).as_bytes),
 None => print!($($arg)*),
 }
 })
}
```

**FACT-W03**: Макрос `cs_println!` — аналогично, но добавляет `\n` (write.rs:78-103).

**FACT-W04**: Макрос `cs_debug!` — печать в stderr только при `verbose > 0` (write.rs:106-112):
```rust
macro_rules! cs_debug {
 ($($arg:tt)*) => ({
 if $crate::writer.verbose > 0 { eprintln!($($arg)*); }
 })
}
```

**FACT-W05**: Макрос `cs_trace!` — печать в stderr только при `verbose > 1` (write.rs:114-121).

**FACT-W06**: Макрос `cs_eprintln!` — печать в stderr если не `quiet` (write.rs:123-130):
```rust
macro_rules! cs_eprintln {
 ($($arg:tt)*) => ({
 if!$crate::writer.quiet { eprintln!($($arg)*); }
 })
}
```

### 1.3. Сериализация JSON/YAML (write.rs:132-183)

**FACT-W07**: Макрос `cs_print_json!` — потоковая JSON сериализация через `serde_json::to_writer` (write.rs:132-147):
```rust
macro_rules! cs_print_json {
 ($value:expr) => {{
 match $crate::writer.output.as_ref {
 Some(mut f) => {::serde_json::to_writer(f, $value)?; f.flush }
 None => {::serde_json::to_writer(std::io::stdout, $value)?; stdout.flush }
 }
 }};
}
```

**FACT-W08**: Макрос `cs_print_json_pretty!` — pretty JSON через `to_writer_pretty` (write.rs:149-164).

**FACT-W09**: Макрос `cs_print_yaml!` — YAML сериализация через `serde_yaml::to_writer` + newline (write.rs:166-183).

### 1.4. Таблицы (write.rs:185-194)

**FACT-W10**: Макрос `cs_print_table!` — условный вывод таблицы:
```rust
macro_rules! cs_print_table {
 ($table:ident) => {
 match $crate::writer.output.as_ref {
 Some(mut f) => $table.print(&mut f),
 None => $table.printstd,
 }
 };
}
```

### 1.5. Цветной вывод (write.rs:196-264)

**FACT-W11**: Макрос `cs_greenln!` — зелёный текст в stdout с crossterm (write.rs:196-216):
- При записи в файл — простой текст без ANSI
- При выводе в терминал — использует `crossterm::style::SetForegroundColor(Color::Green)`

**FACT-W12**: Макросы для stderr: `cs_egreenln!`, `cs_eyellowln!`, `cs_eredln!` (write.rs:218-264):
- Все подавляются при `quiet`
- Используют crossterm для цвета

### 1.6. Платформозависимые символы (cli.rs:25-35)

**FACT-W13**: Константа `RULE_PREFIX` различается по платформам:
```rust
#[cfg(not(windows))]
pub const RULE_PREFIX: &str = "‣"; // U+2023 TRIANGULAR BULLET

#[cfg(windows)]
pub const RULE_PREFIX: &str = "+";
```

**FACT-W14**: Константа `TICK_SETTINGS` для progress bar:
```rust
#[cfg(not(windows))]
const TICK_SETTINGS: (&str, u64) = ("⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏ ", 80); // Braille dots

#[cfg(windows)]
const TICK_SETTINGS: (&str, u64) = (r"-\|/-", 200); // ASCII
```

### 1.7. Progress bar (cli.rs:37-71)

**FACT-W15**: Функция `init_progress_bar` создаёт indicatif ProgressBar:
- При `verbose` или `quiet` — скрытый (`ProgressDrawTarget::hidden`)
- Иначе — в stderr (`ProgressDrawTarget::stderr`)
- Template: `{msg}[+] {prefix} [{bar:40}] {pos}/{len} {spinner} [{elapsed_precise}]`
- Progress chars: `=>-`

### 1.8. Форматирование таблиц (cli.rs:367-383)

**FACT-W16**: Таблицы используют Unicode box-drawing characters:
```rust
let format = format::FormatBuilder::new
.column_separator('│')
.borders('│')
.separators(&[LinePosition::Top], LineSeparator::new('─', '┬', '┌', '┐'))
.separators(&[LinePosition::Intern], LineSeparator::new('─', '┼', '├', '┤'))
.separators(&[LinePosition::Bottom], LineSeparator::new('─', '┴', '└', '┘'))
.padding(1, 1)
.build;
```

### 1.9. Форматирование полей (cli.rs:73-97)

**FACT-W17**: Функция `format_field_length`:
- Удаляет `\n`, `\r`, `\t`
- Заменяет двойные пробелы на одинарные
- Разбивает на chunks по `col_width` символов
- При `!full_output` и длине > 496 — обрезает с добавлением `"...\n(use --full to show all content)"`

### 1.10. Вывод JSON/JSONL (cli.rs:1011-1228)

**FACT-W18**: Функция `print_json` (cli.rs:1011-1075):
- Собирает detections в массив
- Сортирует по timestamp
- Вызывает `cs_print_json!(&detections)?`

**FACT-W19**: Функция `print_jsonl` (cli.rs:1077-1228):
- Для каждого detection: `cs_print_json!(&det)?; cs_println!;`
- Поддерживает cache-to-disk mode с seek по offset

### 1.11. CSV вывод (cli.rs:776-978)

**FACT-W20**: Функция `print_csv`:
- Создаёт директорию `--output`
- Для каждой группы: файл `{group}.csv` (lowercase, пробелы → `_`)
- Использует `prettytable::csv::Writer`
- Печатает `[+] Created {filename}` через `cs_eprintln!`

---

## 2. Текущее состояние скелета C++

### 2.1. Реализовано (output.hpp, output.cpp)

1. **OutputConfig**: `quiet`, `verbose`, `no_banner`
2. **Stream enum**: `Stdout`, `Stderr`
3. **Writer class**:
 - `write(Stream, string_view)` — базовая запись
 - `write_line(Stream, string_view)` — с newline
 - `info/warn/error/debug` — с префиксами `[+]`, `[!]`, `[x]`, `[*]`
 - `flush` — fflush stdout/stderr

### 2.2. Отсутствует (нужно реализовать)

1. **Format enum**: `Std`, `Csv`, `Json`, `Jsonl` (отличие от Rust!)
2. **Output file support**: запись в файл при `--output`
3. **Path storage**: хранение пути вывода
4. **JSON сериализация**: интеграция с RapidJSON (ADR-0003)
5. **YAML сериализация**: интеграция с yaml-cpp
6. **Table formatting**: Unicode box-drawing, column width
7. **CSV output**: csv writer
8. **Progress bar**: базовая реализация (минимум для 1:1)
9. **Цветной вывод**: ANSI escape codes с детектом TTY
10. **Платформозависимые символы**: RULE_PREFIX, TICK_SETTINGS

---

## 3. Критерии закрытия (UnitDone)

### 3.1. API соответствует TOBE-0001/4.3

| Требование | Критерий проверки |
|------------|-------------------|
| Writer API | `write(Stream, bytes)`, `write_line`, `flush` ✓ (уже есть) |
| Output file | Запись в файл при config.output_path задан |
| Progress | `progress_begin/tick/end` методы |
| Format selection | Enum Format с Std/Csv/Json/Jsonl |

### 3.2. Форматы вывода

| Формат | Критерий |
|--------|----------|
| JSON | `write_json(value)` с RapidJSON, детерминированный вывод |
| JSONL | Строка JSON + newline |
| YAML | `write_yaml(value)` с yaml-cpp (для dump) |
| Table | Unicode box-drawing, настраиваемая ширина столбцов |
| CSV | CSV writer с headers |

### 3.3. Цветной вывод и TTY

| Аспект | Критерий |
|--------|----------|
| TTY detection | `is_tty_stdout`, `is_tty_stderr` из platform |
| ANSI colors | Green/Yellow/Red для stderr |
| No color in file | При записи в файл — без ANSI codes |

### 3.4. Платформозависимость

| Аспект | Критерий |
|--------|----------|
| RULE_PREFIX | `‣` на Unix, `+` на Windows |
| Progress chars | Braille на Unix, ASCII на Windows |

### 3.5. Unit-тесты

- TST-OUTPUT-001: write/write_line корректно пишут байты
- TST-OUTPUT-002: info/warn/error/debug форматирование
- TST-OUTPUT-003: quiet подавляет info
- TST-OUTPUT-004: verbose контролирует debug/trace
- TST-OUTPUT-005: JSON сериализация детерминирована
- TST-OUTPUT-006: Table formatting Unicode
- TST-OUTPUT-007: Output file creates и пишет
- TST-OUTPUT-008: format_field_length truncation

---

## 4. Оценка S1-S4

| Ось | Оценка | Обоснование |
|-----|--------|-------------|
| S1 (ширина) | Low | Один модуль MOD-0003 |
| S2 (сложность проверки) | Medium | Требует сравнения байтового вывода |
| S3 (unknowns) | Low | ADR-0003, ADR-0006, ADR-0007 приняты |
| S4 (платформенность) | Medium | ANSI/Unicode различия Windows |

**Максимум: Medium** — слайс допустим к работе (одна High отсутствует).

---

## 5. Зависимости

### 5.1. От SLICE-001 (Done ✓)
- `platform::is_tty_stdout`, `is_tty_stderr` — уже реализовано
- `platform::path_to_utf8` — для вывода путей

### 5.2. Внешние библиотеки
- **RapidJSON** (ADR-0003) — JSON сериализация
- **yaml-cpp** — YAML сериализация (для dump)
- **fmt** (ADR-0007) — форматирование строк

---

## 6. Риски

### 6.1. RISK-0011: Детерминизм вывода
- **Контроль**: JSON порядок ключей, table column order
- **Митигация**: явная сортировка ключей при сериализации

### 6.2. Новый риск (если применимо)
- **RISK-0036**: Windows console Unicode support
- **Влияние**: Medium
- **Митигация**: fallback на ASCII при отсутствии поддержки

---

## 7. План реализации

### 7.1. Расширение OutputConfig
```cpp
struct OutputConfig {
 bool quiet = false;
 int verbose = 0;
 bool no_banner = false;
 // NEW:
 std::optional<std::filesystem::path> output_path;
 Format format = Format::Std;
};
```

### 7.2. Расширение Writer
```cpp
class Writer {
 // Existing...

 // NEW:
 void write_json(const rapidjson::Value& value);
 void write_json_line(const rapidjson::Value& value); // JSONL
 void write_yaml(const YAML::Node& value);

 void green_line(std::string_view msg); // stdout, green
 void yellow_line(std::string_view msg); // stderr, yellow (warn)
 void red_line(std::string_view msg); // stderr, red (error)

 void progress_begin(std::string_view label, size_t total);
 void progress_tick(size_t current);
 void progress_end;
};
```

### 7.3. Table formatter
```cpp
namespace chainsaw::output {
 class Table {
 public:
 void add_header(const std::vector<std::string>& headers);
 void add_row(const std::vector<std::string>& cells);
 void print(Writer& w); // Форматирует с Unicode box-drawing
 };
}
```

### 7.4. Платформозависимые константы
```cpp
#ifdef _WIN32
constexpr const char* RULE_PREFIX = "+";
constexpr const char* TICK_CHARS = "-\\|/-";
constexpr int TICK_MS = 200;
#else
constexpr const char* RULE_PREFIX = "‣";
constexpr const char* TICK_CHARS = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏";
constexpr int TICK_MS = 80;
#endif
```

---

## 8. Ссылки

- TOBE-0001 секция 4.3: `docs/architecture/TOBE-0001-cpp-to-be-architecture.md`
- ADR-0003: `docs/adr/ADR-0003-json-library.md`
- ADR-0006: `docs/adr/ADR-0006-cli-and-user-visible-output.md`
- ADR-0007: `docs/adr/ADR-0007-string-formatting-and-logging.md`
- BACKLOG-0001: `docs/backlog/BACKLOG-0001-porting-backlog.md`
- FEAT-0009: `docs/as_is/FEAT-0001-feature-inventory.md`
- GOV-0002: `docs/governance/GOV-0002-equivalence-criteria.md`
- Upstream write.rs: `upstream/chainsaw/src/write.rs`
- Upstream cli.rs: `upstream/chainsaw/src/cli.rs`
