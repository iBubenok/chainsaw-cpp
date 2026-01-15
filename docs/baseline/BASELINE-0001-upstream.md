# BASELINE-0001 — Baseline-версии и происхождение upstream-источников

## Цель
Зафиксировать **что именно портируем** (версии/коммиты/происхождение) для всех входных upstream-источников, предоставленных в проект.

## Объём (Scope)
Фиксируются baseline-метаданные для:
-: Chainsaw (Rust)
-: Sigma rules
-: EVTX-ATTACK-SAMPLES

Не фиксируются (в рамках ):
- наблюдаемое поведение/CLI-контракт (это +);
- результаты прогонов (это );
- выбор библиотек/архитектуры C++ (это после завершения As-Is, ).

## Метод извлечения baseline (воспроизводимо)
1) Проверка целостности предоставленных архивов (SHA-256).
2) Распаковка архивов «как есть».
3) Если внутри есть `.git/`, извлечь git-метаданные:
 - `git rev-parse HEAD`
 - `git rev-parse --abbrev-ref HEAD`
 - `git config --get remote.origin.url`
 - `git describe --tags --always --dirty`
 - `git log -1 --date=iso-strict --format='<format>'`
4) Для Chainsaw дополнительно извлечь значения из `Cargo.toml` (`[package]`).

> Примечание: абсолютные пути распаковки зависят от окружения. Для воспроизводимости сравниваются только **содержимое** и **git-метаданные**.

## Результаты baseline

### Chainsaw (Rust) — (`chainsaw.zip`)

**Целостность архива**
- SHA-256: `39282fb45fec6867a684d9e571599cbd36046618688bd16affce060bfff4e8eb`

**Происхождение (git)**
- Remote (origin): `https://github.com/WithSecureLabs/chainsaw`
- Branch: `master`
- Commit (HEAD): `c692405ecbc9703d190a67fd0679e48961a99c3a`
- Commit date (HEAD): `2025-10-12T04:16:07-07:00`
- Describe: `v2.13.1-4-gc692405`
- Working tree: clean (нет локальных изменений)

**Метаданные пакета (Cargo)**
- `name`: `chainsaw`
- `version`: `2.13.1`
- `repository`: `https://github.com/WithSecureLabs/chainsaw`
- `license`: `GPL3`
- `edition`: `2024`

**Примечание**
- Git describe указывает, что HEAD находится на **4 коммита после** тега `v2.13.1`, при этом версия в `Cargo.toml` остаётся `2.13.1`. Для однозначной идентификации baseline следует использовать **commit SHA** и/или **describe**, а не только номер версии.

---

### Sigma rules — (`sigma.zip`)

**Целостность архива**
- SHA-256: `24d9c89d94ffbc52b86d5c084921e4b1384b5d10f8bc66cd2a747e57bba04b95`

**Происхождение (git)**
- Remote (origin): `https://github.com/SigmaHQ/sigma`
- Branch: `master`
- Commit (HEAD): `2952d630a413a820fcd8b6033dbc14ac3566e554`
- Commit date (HEAD): `2025-12-21T18:07:30+01:00`
- Describe: `r2025-12-01-20-g2952d630a`
- Working tree: clean (нет локальных изменений)

---

### EVTX-ATTACK-SAMPLES — (`EVTX-ATTACK-SAMPLES.zip`)

**Целостность архива**
- SHA-256: `58035cf7435e86b0926920f3641751d979728ce0a48050cb42647e9250191e26`

**Происхождение (git)**
- Remote (origin): `https://github.com/sbousseaden/EVTX-ATTACK-SAMPLES`
- Branch: `master`
- Commit (HEAD): `4ceed2f4706daf601c212a8f91c113dd85349a2c`
- Commit date (HEAD): `2023-01-24T12:02:50+00:00`
- Describe: `4ceed2f`
- Working tree: clean (нет локальных изменений)

## Наблюдаемые ограничения и неопределённости

### BL-UNKN-0001 — связь версий submodules Chainsaw ↔ предоставленные `sigma.zip`/`EVTX-ATTACK-SAMPLES.zip`
Наблюдения:
- В Chainsaw присутствует файл `.gitmodules`, декларирующий submodules `sigma_rules` и `evtx_attack_samples`.
- При этом на ревизии `HEAD` (`c692405...`) в дереве git **нет** gitlink-entries на пути `sigma_rules`/`evtx_attack_samples` (каталоги отсутствуют и не перечисляются в `git ls-tree HEAD`).

Следствие:
- Невозможно доказательно установить «ожидаемые» ревизии submodules, т.е. нельзя формально утверждать, что предоставленные в проект `sigma.zip` и `EVTX-ATTACK-SAMPLES.zip` соответствуют конкретной синхронизации upstream Chainsaw.

Принятое действие:
- Оставить как риск RISK-0006 до получения дополнительных доказательств (см. ).

## Связанные риски
- RISK-0001 — закрыт: baseline Chainsaw теперь фиксируется commit SHA + describe.
- RISK-0006 — остаётся открытым: вопрос синхронизации submodules.

## Приложение A — команды для воспроизведения

> Ниже приведён минимальный набор команд, которыми можно воспроизвести извлечение baseline из предоставленных архивов.

### A.1. Хэши архивов
```bash
sha256sum chainsaw.zip sigma.zip EVTX-ATTACK-SAMPLES.zip
```

### A.2. Извлечение git-метаданных (пример для Chainsaw)
```bash
unzip -q chainsaw.zip -d chainsaw_src

git -C chainsaw_src rev-parse HEAD
git -C chainsaw_src rev-parse --abbrev-ref HEAD
git -C chainsaw_src config --get remote.origin.url
git -C chainsaw_src describe --tags --always --dirty
git -C chainsaw_src log -1 --date=iso-strict --format='%H%n%cd%n%s'
```

### A.3. Извлечение версии Cargo (Chainsaw)
```bash
# вручную из Cargo.toml, секция [package]
# либо через утилиты разбора TOML (если доступны в окружении)
```
