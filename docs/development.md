# Локальный запуск и разработка

## Зависимости

Для всего проекта нужны CMake 3.20+, C++20-компилятор, Boost 1.89+, OpenSSL,
Qt 6, FFmpeg и Python 3. Для API нужен PostgreSQL. Часть C++-зависимостей CMake
загружает через `FetchContent`.

## PostgreSQL и API

Создайте БД и конфигурацию:

```bash
cp backend/api/.env.example backend/api/.env
```

Пример `backend/api/.env` для согласованного с клиентом порта:

```dotenv
DATABASE_URL=postgresql+asyncpg://driscord:strong-password@127.0.0.1:5432/driscord
SECRET_KEY=replace-with-a-long-random-secret
API_PORT=9002
DATA_DIR=/var/lib/driscord
ACCESS_TOKEN_EXPIRE_MINUTES=30
REFRESH_TOKEN_EXPIRE_DAYS=7
```

Для локальной разработки `DATA_DIR` можно не задавать. Затем:

```bash
./scripts/build.sh --api
./scripts/run.sh --api
curl http://127.0.0.1:9002/health
```

Swagger UI доступен на `http://127.0.0.1:9002/docs`, OpenAPI JSON — на
`/openapi.json`. Таблицы создаются автоматически при первом старте.

## Signaling

```bash
./scripts/build.sh --server
DRISCORD_PORT=9001 ./.builds/server/release/driscord_server
curl http://127.0.0.1:9001/presence
```

У бинаря порт также можно передать первым позиционным аргументом. В текущем
`scripts/run.sh` аргумент `--server` ошибочно передаётся самому бинарю, который
пытается преобразовать его в число. Поэтому до исправления launcher используйте
прямой запуск выше.

## Клиент

Создайте `config.json` в корне проекта:

```json
{
  "server": "127.0.0.1:9001",
  "api": "127.0.0.1:9002",
  "screen_fps": 60,
  "turn_servers": []
}
```

Затем:

```bash
./scripts/build.sh --qt
./scripts/run.sh --qt
```

Клиент ищет `config.json`, затем `driscord.json` в текущем каталоге, после чего
ищет платформенный файл: `~/.config/driscord/config.json` на Linux или
`%LOCALAPPDATA%/driscord/config.json` на Windows. Если ничего не найдено,
используются `localhost:9001`, `localhost:9002` и 60 FPS.

Ключ `video_bitrate_kbps`, показанный в корневом README, текущий `AppConfig` не
читает. Настраиваемый ключ называется `screen_fps`.

## Тесты

```bash
./scripts/build.sh --test
./scripts/build.sh --server --test
./scripts/build.sh --api --test
```

API-тесты используют SQLite в памяти, а не production PostgreSQL. Это ускоряет
тесты, но не проверяет специфическое поведение PostgreSQL.

## NixOS

На NixOS используйте отдельные скрипты:

```bash
./scripts/nixos-build.sh --qt
./scripts/nixos-build.sh --server
./scripts/nixos-build.sh --api
./scripts/nixos-build.sh --test
```

Они автоматически выполняют команду через `nix develop`, если текущая shell ещё
не является dev shell проекта. Для ручного входа:

```bash
nix develop
```

Запуск:

```bash
./scripts/nixos-run.sh --server 9001
./scripts/nixos-run.sh --api
./scripts/nixos-run.sh --qt
```

NixOS-артефакты не смешиваются с обычной Linux-сборкой и кладутся в
`.builds/nixos/`.
