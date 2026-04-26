# Driscord

P2P голосовой/видео-чат в духе Discord. WebRTC DataChannel для аудио и демонстрации экрана между пирами, отдельный сигналинг-сервер для обмена SDP/ICE и REST API для авторизации, серверов и каналов.

## Архитектура

```
┌──────────────┐      WebSocket       ┌──────────────────┐      WebSocket      ┌──────────────┐
│   Client A   │ ───────────────────► │ Signaling server │ ◄─────────────────── │   Client B   │
│   (Qt6/QML)  │ ◄─── SDP / ICE ────  │  (Boost.Beast)   │  ─── SDP / ICE ────► │   (Qt6/QML)  │
│              │                      └──────────────────┘                      │              │
│              │                                                                │              │
│              │ ────────────── WebRTC DataChannel (P2P, UDP) ─────────────────►│              │
│              │        Opus-аудио (48 kHz) · H.264/H.265-видео · служебка      │              │
└──────┬───────┘                                                                └──────┬───────┘
       │                                                                               │
       │   HTTPS REST (JWT)                                                            │
       └──────────────────────────► ┌──────────────────┐ ◄──────────────────────────────┘
                                    │    API server    │
                                    │ (FastAPI + PG)   │
                                    └──────────────────┘
```

- **Сигналинг-сервер** — WebSocket-relay, не трогает медиа, только маршрутизирует SDP/ICE.
- **API-сервер** — FastAPI + PostgreSQL: регистрация/логин (JWT), серверы, каналы, инвайты, обновления.
- **Ядро (C++20)** — захват, кодирование, транспорт; собирается в статическую либу `driscord_core`.
- **UI-клиент** — Qt6/QML, линкуется с `driscord_core` напрямую.

## Компоненты

| Слой | Путь | Описание |
|------|------|----------|
| Core | `core/` | C++20 ядро: аудио (miniaudio + Opus), видео (экран → FFmpeg H.264/H.265), транспорт на libdatachannel, пайплайн кодеков/джиттера. |
| Сигналинг | `backend/signaling_server/` | Boost.Beast WebSocket-relay на порту 9001. |
| API | `backend/api/` | Python/FastAPI + SQLAlchemy (asyncpg) + JWT. |
| Client Qt | `client-qt/` | Qt6/QML. Линкуется с `driscord_core` напрямую. |

## Стек

- **C++20**, CMake ≥ 3.20
- **Qt6** (Quick / Network / Widgets / QuickDialogs2) — клиент
- **Boost.Beast** — WebSocket-сервер и клиент
- **libdatachannel** v0.22.5 — WebRTC (ICE / DTLS / SCTP)
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

- `config.json` в корне (или рядом с бинарём клиента) — адрес сигналинга, API, TURN-серверы, видео-битрейт:
  ```json
  {
      "server": "host:9001",
      "api": "host:9002",
      "video_bitrate_kbps": 8000,
      "turn_servers": [
          { "url": "turn:host:3478", "user": "...", "pass": "..." }
      ]
  }
  ```
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
