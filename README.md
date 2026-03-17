# Driscord

Голосовой чат (аналог Discord) на C++. WebRTC для peer-to-peer аудио, Dear ImGui для интерфейса.

## Архитектура

```
┌─────────────┐   WebSocket    ┌────────────────┐   WebSocket    ┌─────────────┐
│   Client A  │ ◄────────────► │  Signal Server │ ◄────────────► │   Client B  │
│  (ImGui UI) │                │  (Boost.Beast) │                │  (ImGui UI) │
└──────┬──────┘                └────────────────┘                └──────┬──────┘
       │                                                                │
       │              WebRTC DataChannel (P2P, UDP)                     │
       └────────────────────────────────────────────────────────────────┘
                           Opus-encoded audio
```

- **Сервер** — relay для WebSocket-сигналинга (обмен SDP/ICE)
- **Клиент** — захват микрофона (miniaudio), кодирование (Opus), передача через WebRTC DataChannel (libdatachannel)

## Зависимости

| Компонент | Зависимости |
|-----------|-------------|
| Сервер | Boost >= 1.78, OpenSSL, nlohmann/json |
| Клиент | GLFW, Dear ImGui, libdatachannel, Opus, miniaudio, nlohmann/json |

Клиентские зависимости скачиваются автоматически через CMake FetchContent.

Для сервера нужны системные: `brew install boost openssl` (macOS).

## Сборка

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Запуск

Терминал 1 — сервер:
```bash
./build/server/driscord_server 8080
```

В GUI указать `ws://localhost:8080` и нажать «Подключиться».

## Стек

- **C++20**
- **Boost.Beast** — WebSocket-сервер
- **libdatachannel** — WebRTC (ICE/DTLS/SCTP) + WebSocket-клиент
- **Opus** — аудиокодек (48kHz, mono, 64kbps)
- **miniaudio** — кроссплатформенный захват/воспроизведение аудио
- **Dear ImGui** + GLFW + OpenGL — UI
