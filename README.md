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

Все сборочные сценарии делаются через `./scripts/build.sh` (таргет × действие — независимые оси):

```bash
# --- Клиент ---
./scripts/build.sh                    # Qt6/QML, release
./scripts/build.sh --debug            # Qt6/QML, debug

# --- Сервер ---
./scripts/build.sh --server           # сигналинг-сервер, release
./scripts/build.sh --server --debug
./scripts/build.sh --server --test    # тесты сервера

# --- API ---
./scripts/build.sh --api              # создаёт venv, ставит зависимости

# --- Тесты core и реального client ↔ SFU media path ---
./scripts/build.sh --test
```

Артефакты складываются в `.builds/`:
- `.builds/cmake/qt-webrtc-{release,debug}/client-qt/driscord_client` — Qt-бинарь
- `.builds/server/{release,debug}/driscord_server` — сигналинг

## Запуск

```bash
./scripts/run.sh                    # Qt-клиент (release)
./scripts/run.sh --debug            # Qt-клиент (debug)
./scripts/run.sh --gdb              # Qt-клиент под GDB

./scripts/run.sh --server           # сигналинг-сервер
./scripts/run.sh --api              # API-сервер
```

## Конфигурация

- `config.json` в корне (или рядом с бинарём клиента) — адрес сигналинга, API и FPS захвата:
  ```json
  {
      "server": "wss://driscord.homelab.ceooptimizator.tech",
      "api": "https://driscord.homelab.ceooptimizator.tech",
      "screen_fps": 60
  }
  ```
  Полные `wss://`/`https://` URL рекомендуются для production. Значения вида
  `host:port` остаются совместимыми и используют `ws://`/`http://`.
  TURN сейчас не настроен: клиенты используют публичный host candidate SFU и
  исходящий UDP. Для сетей, блокирующих UDP, перед production-релизом нужен
  TURN/TCP/TLS или эквивалентный fallback.
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

Для NixOS есть отдельный dev shell и отдельные скрипты, чтобы обе тестовые
машины собирали проект в одинаковом окружении:

```bash
./scripts/nixos-build.sh --qt
./scripts/nixos-build.sh --server
./scripts/nixos-build.sh --test

./scripts/nixos-run.sh --server 9001
./scripts/nixos-run.sh --api
./scripts/nixos-run.sh --qt
```

Если скрипты запущены вне `nix develop`, они сами зайдут в dev shell из
`flake.nix`. Артефакты NixOS-сборки лежат отдельно в `.builds/nixos/`.
