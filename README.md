# Driscord

Голосовой/видео-чат в духе Discord. Медиа идёт по схеме **клиент → сервер → клиенты** (SFU): сервер сам терминирует WebRTC и разводит потоки, а REST API отвечает за авторизацию, серверы и каналы.

Подробная документация по архитектуре, разработке, REST API и развёртыванию
находится в [`docs/`](docs/README.md).

## Архитектура

```
┌──────────────┐                    ┌──────────────────┐                    ┌──────────────┐
│   Client A   │ ─── WebSocket ───► │ Signaling server │ ◄─── WebSocket ─── │   Client B   │
│   (Qt6/QML)  │   комната, SDP/ICE │  (Boost.Beast +  │ комната, SDP/ICE   │   (Qt6/QML)  │
│              │                    │  libdatachannel) │                    │              │
│              │ ══ DataChannel ══► │                  │ ══ DataChannel ══► │              │
│              │      (UDP/SCTP)    │   SFU: разводит  │      (UDP/SCTP)    │              │
│              │ ◄══════════════════│   медиа по комнате═══════════════════►│              │
└──────┬───────┘  Opus-аудио (48 kHz) · H.264/H.265-видео · служебка └──────┬───────┘
       │                                                                    │
       │   HTTPS REST (JWT)                                                 │
       └──────────────────────► ┌──────────────────┐ ◄─────────────────────┘
                                │    API server    │
                                │ (FastAPI + PG)   │
                                └──────────────────┘
```

- **Сигналинг-сервер** — держит комнаты, обменивается с каждым клиентом SDP/ICE и
  терминирует его `PeerConnection`. Медиа не декодирует: разбирает только метку
  канала и решает, кому переслать пакет. Голос и служебка уходят всем в комнате,
  экран — только тем, кто нажал «смотреть».
- **API-сервер** — FastAPI + PostgreSQL: регистрация/логин (JWT), серверы, каналы, инвайты, обновления.
- **Ядро (C++20)** — захват, кодирование, транспорт; собирается в статическую либу `driscord_core`.
- **UI-клиент** — Qt6/QML, линкуется с `driscord_core` напрямую.

Каждый клиент держит ровно одно медиасоединение — к серверу. Между клиентами
WebRTC не устанавливается, поэтому STUN/TURN (coturn) не нужен: сервер публично
доступен и отвечает host-кандидатами, а клиент всегда подключается наружу.

## Компоненты

| Слой | Путь | Описание |
|------|------|----------|
| Core | `core/` | C++20 ядро: аудио (miniaudio + Opus), видео (экран → FFmpeg H.264/H.265), транспорт на libdatachannel, пайплайн кодеков/джиттера. |
| Сигналинг | `backend/signaling_server/` | Boost.Beast (WebSocket, порт 9001) + libdatachannel (SFU, UDP-диапазон для ICE). |
| API | `backend/api/` | Python/FastAPI + SQLAlchemy (asyncpg) + JWT. |
| Client Qt | `client-qt/` | Qt6/QML. Линкуется с `driscord_core` напрямую. |

## Стек

- **C++20**, CMake ≥ 3.20
- **Qt6** (Quick / Network / Widgets / QuickDialogs2) — клиент
- **Boost.Beast** — WebSocket-сервер и клиент
- **libdatachannel** v0.22.5 — WebRTC (ICE / DTLS / SCTP) на обеих сторонах: клиент и SFU
- **Opus** v1.5.2 — аудиокодек (48 kHz, mono, 64 kbps голос / 128 kbps system audio)
- **FFmpeg** — видеокодек H.264/H.265
- **miniaudio** — кроссплатформенный захват/воспроизведение аудио
- **nlohmann/json** v3.11.3, **fmt** v10.2.1
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

# --- Тесты и бенчмарки ядра ---
./scripts/build.sh --test
./scripts/build.sh --bench

# --- Windows тесты ядра (MinGW + Wine) ---
./scripts/build.sh --windows --test
```

Артефакты складываются в `.builds/`:
- `.builds/cmake/qt-{release,debug}/client-qt/driscord_client` — Qt-бинарь
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

- `config.json` в корне (или рядом с бинарём клиента) — адрес сигналинга, API, видео-битрейт:
  ```json
  {
      "server": "wss://driscord.homelab.ceooptimizator.tech",
      "api": "https://driscord.homelab.ceooptimizator.tech",
      "video_bitrate_kbps": 8000
  }
  ```
  Полные `wss://`/`https://` URL рекомендуются для production. Значения вида
  `host:port` остаются совместимыми и используют `ws://`/`http://`.
  TURN-серверов в конфиге больше нет — при SFU они не нужны.
- Сигналинг-сервер: `DRISCORD_PORT` (по умолчанию 9001) и UDP-диапазон для ICE —
  `DRISCORD_ICE_PORT_MIN` / `DRISCORD_ICE_PORT_MAX` (по умолчанию 49160–49200).
  Этот диапазон надо пробросить на файрволе/в Docker.
- `backend/api/.env` — настройки API (PostgreSQL URL, JWT secret, порт). Шаблон: `backend/api/.env.example`.

## Зависимости

Системно (Linux):
- Boost ≥ 1.89 (headers + ASIO/Beast)
- OpenSSL
- FFmpeg (libavcodec/libavformat/libavutil/libswscale/libswresample)
- Qt6 (Quick, Network, Widgets, QuickDialogs2)
- Python 3 (для API)

Автоматически через CMake FetchContent:
- libdatachannel v0.22.5, Opus v1.5.2, miniaudio, GLFW, fmt, nlohmann/json

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
