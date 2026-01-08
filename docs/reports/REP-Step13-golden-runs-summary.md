# REP-Step13 — Итоговый отчёт по golden runs (Step 13)

## Статус
- Версия: 1.0
- Дата: 2026-01-01

## Краткое описание

Отчёт фиксирует результаты сбора эталонных прогонов (golden runs) Chainsaw (Rust) на Step 13.

## Выполненные работы

1. **Сборка и тестирование:**
   - Собрана Chainsaw (Rust) в debug/release на native Linux
   - Прогнаны unit/integration тесты (`cargo test`)

2. **Генерация эталонов:**
   - Перегенерированы linux goldens через `chainsaw/tools/golden_run.py` (NO_COLOR, release)
   - Выполнены rust golden runs (full manifest/env) на native Linux x86_64

3. **Обновление документации:**
   - Обновлены отчёты/реестры (`REP-0001`, `00_*`)

## Ключевые команды

```bash
cargo build
cargo build --release
cargo test
python3 chainsaw/tools/golden_run.py --os linux --bin target/release/chainsaw
python3 tools/golden_runs/run_rust_golden_runs.py \
    --chainsaw-src chainsaw \
    --sigma-src sigma \
    --evtx-samples EVTX-ATTACK-SAMPLES \
    --out out/rust_golden_runs \
    --repeats 3
```

## Артефакты

| Артефакт | Описание | SHA-256 |
|----------|----------|---------|
| `rust_golden_runs_linux_x86_64_20260101.zip` | Архив golden runs Linux | `5b7ce321923c...d75d16f` |
| `golden/linux/*` | Обновлённые эталоны (native Linux, release, NO_COLOR) | — |
| `docs/reports/REP-0001-rust-golden-runs.md` | Статусы RUN-0001..0601 | — |
| `tools/golden_runs/run_rust_golden_runs.py` | Скрипт сбора golden runs | — |
| `ambrella-chainsaw-cpp_step13.zip` | Снимок репозитория | `90bf364067a...ca7a3bc0` |

## Следующие шаги

- Верификация эталонов на целевых платформах (Windows, macOS)
- Интеграция в CI-pipeline для автоматической проверки

## Связанные документы

- `REP-0001-rust-golden-runs.md` — детальный протокол golden runs
- `DEC-0003-golden-runs-tooling.md` — решение по инструментам
- `DEC-0007-linux-native-golden-runs.md` — решение по Linux эталонам
