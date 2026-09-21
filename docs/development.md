# Локальный запуск и разработка

## Зависимости

Для client/core нужны Linux, CMake 3.25+, Clang/lld, Boost 1.89+, Qt 6,
GnuTLS/NSS, X11 extensions и PulseAudio. Для standalone SFU нужен OpenSSL. Для
API нужны Python 3 и PostgreSQL. Часть C++-зависимостей CMake загружает через
`FetchContent`.

Перед первой client/core сборкой создайте pinned Google WebRTC artifact:

```bash
./scripts/build_google_webrtc.sh
```

Артефакт пока Linux-only; Windows/macOS core build намеренно отключён.

## PostgreSQL и API

Создайте БД и конфигурацию:

```bash
cp backend/api/.env.example backend/api/.env
```

Пример `backend/api/.env` для согласованного с клиентом порта:

```dotenv
DATABASE_URL=postgresql+asyncpg://driscord:strong-password@127.0.0.1:5432/driscord
# Generate with: openssl rand -hex 32
SECRET_KEY=replace-with-at-least-32-random-bytes
API_PORT=9002
DATA_DIR=/var/lib/driscord
ACCESS_TOKEN_EXPIRE_MINUTES=30
REFRESH_TOKEN_EXPIRE_DAYS=7
```

Для локальной разработки `DATA_DIR` можно не задавать. Затем:

```bash
./scripts/run.sh --api      # создаст venv по requirements.txt при первом запуске
curl http://127.0.0.1:9002/health
```

Swagger UI доступен на `http://127.0.0.1:9002/docs`, OpenAPI JSON — на
`/openapi.json`. Таблицы создаются автоматически при первом старте.

## Signaling

```bash
cmake --workflow --preset server
DRISCORD_PORT=9001 ./.builds/server/backend/signaling_server/driscord_server
curl http://127.0.0.1:9001/presence
```

У бинаря порт также можно передать первым позиционным аргументом. Обычный
launcher не пересылает собственные флаги в сервер:

```bash
./scripts/run.sh --server
```

## Клиент

Dev-defaults клиента — `ws://localhost:9001` и `http://localhost:9002`, поэтому
для стандартного локального окружения достаточно:

```bash
cmake --workflow --preset client
./scripts/run.sh --qt
```

Другие endpoint'ы задаются на configure и становятся частью бинаря:

```bash
cmake --preset client \
  -DDRISCORD_CLIENT_SIGNALING_URL=wss://signal.example.org \
  -DDRISCORD_CLIENT_API_URL=https://api.example.org
cmake --build --preset client
```

Пользовательские настройки клиент сначала ищет в
`~/.config/driscord/config.json` на Linux или
`%LOCALAPPDATA%/driscord/config.json` на Windows, затем — в `config.json` или
`driscord.json` текущего каталога. JSON не может менять адреса сервисов; сейчас
он поддерживает `screen_fps`, `stun_servers` и `turn_servers`.

Bitrate ограничивается Google WebRTC session policy.

### Прокси

Клиент читает `HTTPS_PROXY`, `HTTP_PROXY`, `ALL_PROXY` и `NO_PROXY` (обе
раскладки регистра) при старте, так что отдельный выход в интернет задаётся
префиксом:

```bash
HTTP_PROXY=http://127.0.0.1:3128 ./driscord_client
```

Для https/wss-цели порядок: `HTTPS_PROXY`, `ALL_PROXY`, `HTTP_PROXY`. Откат на
`HTTP_PROXY` для TLS-цели отличается от поведения curl и сделан намеренно: все
endpoint'ы Driscord — https/wss, и без этого команда выше не давала бы ничего.
`NO_PROXY` перекрывает всё; пустое значение считается незаданным, поэтому
`HTTPS_PROXY= ./driscord_client` отключает прокси, экспортированный в шелле.
Некорректное значение не молча пропускается, а выключает прокси целиком —
иначе трафик ушёл бы туда, куда никто не просил.

Через прокси идут REST API, аватары, канал обновлений и signaling-WebSocket.
**Медиа не идёт**: ICE и SRTP — это UDP, а в запиненной ревизии Google WebRTC
клиентского proxy-API нет вовсе. Для signaling libdatachannel умеет только
HTTP CONNECT без аутентификации, поэтому socks5 или прокси с логином там
приводят к явной ошибке подключения, а не к тихому обходу.

Если нужен именно разный выход в интернет для медиа, прокси не подходит —
нужен сетевой namespace с собственным маршрутом по умолчанию.

## Тесты

```bash
cmake --workflow --preset core-tests     # core + client ↔ SFU media path, реальный WebRTC
cmake --workflow --preset server-tests   # сервер и SFU, без WebRTC-артефакта
cmake --workflow --preset client-tests   # клиент и его тесты (QT_QPA_PLATFORM=offscreen)
cd backend/api && tox                    # API
```

`client-tests` включает headless Qt QuickTest-сценарии для переходов login,
валидации форм, диалогов создания сущностей, ошибок и отказа screen capture.

Список конфигураций — `cmake --list-presets`; они же используются в CI, отдельных
сборочных команд у пайплайна нет.

API-тесты используют SQLite в памяти, а не production PostgreSQL. Это ускоряет
тесты, но не проверяет специфическое поведение PostgreSQL.

## NixOS

На NixOS те же пресеты, только через dev shell из `flake.nix`:

```bash
nix develop -c cmake --workflow --preset client
nix develop -c cmake --workflow --preset core-tests
nix develop -c ./scripts/run.sh --qt
```

Для ручного входа — `nix develop`, дальше команды как обычно.

Dev shell выставляет `DRISCORD_BUILD_TAG=nixos-`, поэтому NixOS-артефакты лежат в
`.builds/nixos-<preset>/` и не смешиваются с хостовой сборкой: у двух тулчейнов
не должно быть одного CMake-кэша.

Отдельно стоит `scripts/nixos-webrtc.sh` — он не обёртка над CMake, а инъекция
путей nixpkgs cc-wrapper в чистый Chromium clang.
