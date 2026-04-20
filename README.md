# Driscord

P2P голосовой/видео-чат в духе Discord. WebRTC DataChannel для аудио и демонстрации экрана между пирами, отдельный сигналинг-сервер для обмена SDP/ICE и REST API для авторизации, серверов и каналов.

## Архитектура

```
┌──────────────┐      WebSocket       ┌──────────────────┐      WebSocket      ┌──────────────┐
│   Client A   │ ───────────────────► │ Signaling server │ ◄─────────────────── │   Client B   │
│  (Compose    │ ◄─── SDP / ICE ────  │  (Boost.Beast)   │  ─── SDP / ICE ────► │   или Qt)    │
│   или Qt)    │                      └──────────────────┘                      │              │
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
- **Ядро (C++20)** — захват, кодирование, транспорт; собирается в статическую либу `driscord_core` + при необходимости в JNI-библиотеку `libcore.so` / `core.dll`.
- **UI-клиенты** — два варианта, оба поверх одного и того же `driscord_core`.

## Компоненты

| Слой | Путь | Описание |
|------|------|----------|
| Core | `core/` | C++20 ядро: аудио (miniaudio + Opus), видео (экран → FFmpeg H.264/H.265), транспорт на libdatachannel, пайплайн кодеков/джиттера. |
| JNI-шин | `core/jni/` | Биндинги ядра для JVM-клиента. |
| Сигналинг | `backend/signaling_server/` | Boost.Beast WebSocket-relay на порту 9001. |
| API | `backend/api/` | Python/FastAPI + SQLAlchemy (asyncpg) + JWT. |
| Client Compose | `client-compose/` | Kotlin + Compose Desktop. Ядро подключается через JNI. Собирается в fat-JAR + AppImage / Windows portable zip. |
| Client Qt | `client-qt/` | Qt6/QML. Линкуется с `driscord_core` напрямую, без JNI. |
| Launcher | `launcher/` | Нативный Windows-launcher (находит встроенную JRE и стартует JVM). |

## Стек

- **C++20**, CMake ≥ 3.20
- **Boost.Beast** — WebSocket-сервер и клиент
- **libdatachannel** v0.22.5 — WebRTC (ICE / DTLS / SCTP)
- **Opus** v1.5.2 — аудиокодек (48 kHz, mono, 64 kbps голос / 128 kbps system audio)
- **FFmpeg** — видеокодек H.264/H.265
- **miniaudio** — кроссплатформенный захват/воспроизведение аудио
- **nlohmann/json** v3.11.3, **fmt** v10.2.1
- **Kotlin** 2.1.20 + **Compose Desktop** 1.8.0 — один из клиентов
- **Qt6** (Quick / Network / Widgets / QuickDialogs2) — альтернативный клиент
- **FastAPI**, **SQLAlchemy** (asyncpg), **python-jose**, **passlib** — API

## Сборка

Все сборочные сценарии делаются через `./scripts/build.sh` (таргет × действие — независимые оси):

```bash
# --- Клиенты ---
./scripts/build.sh                    # Kotlin/Compose, release: libcore.so + driscord.jar
./scripts/build.sh --debug            # Kotlin/Compose, debug
./scripts/build.sh --package          # Linux AppImage

./scripts/build.sh --qt               # Qt6/QML клиент, release
./scripts/build.sh --qt --debug       # Qt6/QML, debug

./scripts/build.sh --windows          # Windows-клиент (MinGW cross-compile, dev-сборка)
./scripts/build.sh --windows --package # Windows portable zip (JRE + EXE-launcher + JAR)

# --- Сервер ---
./scripts/build.sh --server           # сигналинг-сервер, release
./scripts/build.sh --server --debug
./scripts/build.sh --server --test    # тесты сервера

# --- API ---
./scripts/build.sh --api              # создаёт venv, ставит зависимости

# --- Тесты и бенчмарки ядра ---
./scripts/build.sh --test
./scripts/build.sh --bench
```

Артефакты складываются в `.builds/`:
- `.builds/client/linux/{release,debug}/` — JAR + libcore.so (или AppImage после `--package`)
- `.builds/client/windows/release/` — core.dll + driscord.jar (+ driscord.exe и JRE для `--package`)
- `.builds/cmake/qt-{release,debug}/client-qt/driscord_client` — Qt-бинарь
- `.builds/server/{release,debug}/driscord_server` — сигналинг

## Запуск

```bash
./scripts/run.sh                    # Compose-клиент (release → AppImage)
./scripts/run.sh --debug            # Compose-клиент через Gradle
./scripts/run.sh --gdb              # то же под GDB

./scripts/run.sh --qt               # Qt6-клиент
./scripts/run.sh --qt --debug

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
- JDK 21 (для JNI-сборки и Kotlin-клиента)
- Python 3 (для API)
- Qt6 (Quick, Network, Widgets, QuickDialogs2) — только для Qt-клиента

Автоматически через CMake FetchContent:
- libdatachannel v0.22.5, Opus v1.5.2, miniaudio, GLFW, fmt, nlohmann/json

Для Windows-кросскомпиляции:
- MinGW (`x86_64-w64-mingw32-g++`)
- FFmpeg Windows-сборка (скрипт `third_party/fetch_win_deps.sh`)
