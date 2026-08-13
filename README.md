# Driscord

Голосовой/видео-чат в духе Discord. Медиа идёт по схеме **клиент → сервер → клиенты** (SFU): сервер сам терминирует WebRTC и разводит потоки, а REST API отвечает за авторизацию, серверы и каналы.

Подробная документация по архитектуре, разработке, REST API и развёртыванию
находится в [`docs/`](docs/README.md).

## Архитектура

```
┌──────────────┐   WebSocket: rooms, SDP/ICE   ┌──────────────────┐
│   Qt client  │ ─────────────────────────────► │ Signaling + SFU  │
│ Google WebRTC│ ◄───────────────────────────── │ libdatachannel   │
│              │ ══ ICE/DTLS/SRTP: RTP/RTCP ═► │ Track routers    │
│ voice + screen PeerConnections               │ no media decode  │
└──────┬───────┘                                └────────┬─────────┘
       │                                                                    │
       │   HTTPS REST (JWT)                                                 │
       └──────────────────────► ┌──────────────────┐ ◄─────────────────────┘
                                │    API server    │
                                │ (FastAPI + PG)   │
                                └──────────────────┘
```

- **Сигналинг-сервер** держит комнаты и два media PeerConnection на сессию:
  `voice` и `screen`. Он не декодирует медиа, а переназначает RTP-пакеты между
  стабильными subscriber slots, обслуживает hop-local RTCP/NACK и посылает PLI
  screen publisher при необходимости ключевого кадра.
- **API-сервер** — FastAPI + PostgreSQL: регистрация/логин (JWT), серверы, каналы, инвайты, обновления.
- **Ядро (C++20)** — захват, кодирование, транспорт; собирается в статическую либу `driscord_core`.
- **UI-клиент** — Qt6/QML, линкуется с `driscord_core` напрямую.

Между клиентами WebRTC не устанавливается. Каждый клиент держит два логических
медиасоединения только с публичным SFU: одно для голоса и одно для screen video
с system audio. Один listener может одновременно принимать несколько voice и
screen tracks без отдельного PeerConnection на каждого участника.

## Компоненты

| Слой | Путь | Описание |
|------|------|----------|
| Core | `core/` | C++20 coordinator и адаптеры Google WebRTC: native ADM, DesktopCapturer, tracks/sinks и signaling-only WebSocket transport. |
| Сигналинг | `backend/signaling_server/` | Boost.Beast (WebSocket, порт 9001) + libdatachannel (SFU, UDP-диапазон для ICE). |
| API | `backend/api/` | Python/FastAPI + SQLAlchemy (asyncpg) + JWT. |
| Client Qt | `client-qt/` | Qt6/QML. Линкуется с `driscord_core` напрямую. |

## Стек

- **C++20**, CMake ≥ 3.20
- **Qt6** (Quick / Network / Widgets / QuickDialogs2) — клиент
- **Google WebRTC**, pinned revision — client capture/APM, codecs, RTP/RTCP,
  NetEq, jitter buffers, audio mixing и video decode
- **Boost.Beast** — WebSocket-сервер
- **libdatachannel** v0.24.5 — client WebSocket и server-side Track SFU
- **miniaudio** — вывод сведённого system audio screen tracks
- **nlohmann/json** v3.11.3, **fmt** v12.2.0
- **FastAPI**, **SQLAlchemy** (asyncpg), **python-jose**, **passlib** — API

## Сборка

Конфигурации описаны в `CMakePresets.json`. Одна команда делает configure, build
и (где есть тесты) прогон:

```bash
cmake --workflow --preset client         # Qt6/QML-клиент
cmake --workflow --preset client-tests   # клиент + его тесты
cmake --workflow --preset client-debug   # то же в Debug

cmake --workflow --preset server         # сигналинг-сервер
cmake --workflow --preset server-tests   # + тесты сервера (без Google WebRTC)
cmake --workflow --preset core-tests     # core + SFU media path на реальном WebRTC

cd backend/api && tox                    # тесты API
```

Список всегда можно спросить у самого CMake: `cmake --list-presets`,
`cmake --list-presets=test`. Отдельные шаги — `cmake --preset <name>`,
`cmake --build --preset <name>`, `ctest --preset <name>`.

Артефакты складываются в `.builds/<preset>/`:
- `.builds/client/client-qt/driscord_client` — Qt-бинарь
- `.builds/server/backend/signaling_server/driscord_server` — сигналинг

Qt, Boost и артефакт Google WebRTC ищутся через окружение (`CMAKE_PREFIX_PATH`,
`BOOST_ROOT`, `DRISCORD_WEBRTC_SDK_ROOT`), поэтому одни и те же пресеты работают
на рабочей машине, в nix-шелле и в CI-образе.

## Запуск

```bash
./scripts/run.sh                    # Qt-клиент (release)
./scripts/run.sh --debug            # Qt-клиент (debug)
./scripts/run.sh --gdb              # Qt-клиент под GDB

./scripts/run.sh --server           # сигналинг-сервер
./scripts/run.sh --api              # API-сервер
```

## Конфигурация

- Адреса signaling и API встраиваются в клиент при configure:
  `DRISCORD_CLIENT_SIGNALING_URL` и `DRISCORD_CLIENT_API_URL`. Dev-defaults —
  `ws://localhost:9001` и `http://localhost:9002`; release-сборка должна явно
  передавать production `wss://`/`https://` URL.
- Пользовательский `config.json` хранит только настройки приложения. Основной
  путь на Linux — `~/.config/driscord/config.json`; поддержаны `screen_fps`,
  `stun_servers` и `turn_servers`. Адреса сервисов из JSON не читаются.
- Сигналинг-сервер: `DRISCORD_PORT` (по умолчанию 9001) и UDP-диапазон для ICE —
  `DRISCORD_ICE_PORT_MIN` / `DRISCORD_ICE_PORT_MAX` (по умолчанию 49160–49200).
  Этот диапазон надо пробросить на файрволе/в Docker.
- `backend/api/.env` — настройки API (PostgreSQL URL, JWT secret, порт). Шаблон: `backend/api/.env.example`.

## Зависимости

Системно (Linux):
- Boost ≥ 1.89 (headers + ASIO/Beast)
- Clang и lld
- OpenSSL (server-only build), GnuTLS и NSS (совместный client/SFU test build)
- X11/XComposite/XDamage/XFixes/XRandR/XTest и PulseAudio
- Qt6 (Quick, Network, Widgets, QuickDialogs2)
- Python 3 (для API)

Автоматически через CMake FetchContent:
- libdatachannel v0.24.5, miniaudio, fmt, nlohmann/json

Google WebRTC собирается отдельно командой `scripts/build_google_webrtc.sh` из
revision в `third_party/google_webrtc_revision.txt`. Текущий pinned artifact —
только Linux x86_64; другие архитектуры, Windows и macOS client build намеренно
завершаются понятной ошибкой.

## NixOS

Для NixOS есть dev shell из `flake.nix`; отдельных сборочных скриптов больше нет —
пресеты те же:

```bash
nix develop -c cmake --workflow --preset client
nix develop -c cmake --workflow --preset server-tests
nix develop -c ./scripts/run.sh --qt
```

Dev shell выставляет `DRISCORD_BUILD_TAG=nixos-`, поэтому артефакты NixOS-сборки
лежат отдельно (`.builds/nixos-client/` и т.д.) и не конфликтуют с хостовой
сборкой за один и тот же CMake-кэш.

Исключение — `scripts/nixos-webrtc.sh`: он не обёртка над CMake, а инъекция путей
nixpkgs cc-wrapper в чистый Chromium clang, без которой WebRTC на NixOS не
собирается.
