# Handoff — (Golden runs CLI, cross-platform)

## Что сделано
- Собрана Chainsaw (Rust) в debug/release на native Linux; прогнаны unit/integration тесты.
- Перегенерированы linux goldens через `chainsaw/tools/golden_run.py` (NO_COLOR, release).
- Выполнены rust golden runs (full manifest/env) на native Linux x86_64 через `tools/golden_runs/run_rust_golden_runs.py`, сформирован архив для Projects.
- Обновлены отчёты/реестры (`REP-0001`, `00_*`) и подготовлен список артефактов для.

## Команды (ключевые)
- `cargo build` / `cargo build --release`
- `cargo test`
- `python3 chainsaw/tools/golden_run.py --os linux --bin target/release/chainsaw`
- `python3 tools/golden_runs/run_rust_golden_runs.py --chainsaw-src chainsaw --sigma-src sigma --evtx-samples EVTX-ATTACK-SAMPLES --out out/rust_golden_runs --repeats 3`
- `python3 - <<'PY'... zip rust_golden_runs_linux_x86_64_20260101.zip... PY`

## Артефакты/вложения
- `rust_golden_runs_linux_x86_64_20260101.zip` — SHA-256: 5b7ce321923c08545b55a2636784a7de2044e11a2db0472b81207c499d75d16f (прикрепить в Projects; не коммитить).
- Обновлённые `golden/linux/*` (native Linux, release, NO_COLOR).
- `docs/reports/REP-0001-rust-golden-runs.md` — статусы..0601.
- `tools/golden_runs/run_rust_golden_runs.py`, `tools/golden_runs/README.md`.
- ZIP-снимок репозитория: ambrella-chainsaw-cpp_step13.zip — SHA-256: 90bf364067a7241c1a83d08f8f80198dcaf03d1576d23d862bbb4ad1ca7a3bc0.

## Что нужно сделать в Projects
- Установить =14, загрузить ZIP `ambrella-chainsaw-cpp_step13.zip`.
- Прикрепить golden run архивы (Windows/MacOS/новый Linux) в Projects.
- Провести проверку входов по /.
