# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
# Build Qt client (release, default)
./scripts/build.sh
./scripts/build.sh --debug

# Build signaling server
./scripts/build.sh --server
./scripts/build.sh --server --debug

# Build API (create venv + install deps)
./scripts/build.sh --api

# Tests & benchmarks (target + action are independent axes)
./scripts/build.sh --test             # test core
./scripts/build.sh --bench            # bench core
./scripts/build.sh --server --test    # test server
./scripts/build.sh --api --test       # test API (pytest)
./scripts/build.sh --windows --test   # core tests on Windows under Wine (MinGW)

# Run Qt client
./scripts/run.sh
./scripts/run.sh --debug

# Run signaling server
./scripts/run.sh --server
./scripts/run.sh --server --debug

# Run API server
./scripts/run.sh --api

# Debug Qt client with GDB
./scripts/run.sh --gdb
```

Build outputs:
- `.builds/cmake/qt-{release,debug}/client-qt/driscord_client` — Qt client binary
- `.builds/server/{release,debug}/` — driscord_server

Runtime config is loaded from `config.json` (server host/port, API host/port, video bitrate, TURN servers). Example:
```json
{ "server": "host:9001", "api": "host:9002", "video_bitrate_kbps": 8000, "turn_servers": [...] }
```
API config is loaded from `backend/api/.env` (see `.env.example` for template).

## Architecture

Driscord is a WebRTC-based P2P voice and screen-sharing app (Discord-like) with three backend/library layers plus a Qt client:

### 1. Signaling Server (`backend/signaling_server/`)
Boost.Beast WebSocket relay — purely a message router for SDP/ICE negotiation. It never touches audio/video data. All real-time media flows P2P directly between clients.

### 2. API Server (`backend/api/`)
Python/FastAPI backend with PostgreSQL (asyncpg + SQLAlchemy). Provides user auth (JWT), channel management, and update distribution. All endpoints except `/auth/*` and `/health` require a Bearer token.

### 3. C++ Core Library (`core/src/`, built as `driscord_core` static lib)
The core has two parallel transport systems:

**Audio pipeline**: `audio_sender` → mic capture (miniaudio) → Opus encode (48kHz/mono) → DataChannel → `audio_receiver` → jitter buffer → decode → `audio_mixer` → playback

**Video pipeline**: `video_sender` → screen capture (platform-specific) → H.264/H.265 encode (FFmpeg) → chunk (max 60KB each, see `ChunkHeader`) → DataChannel → `video_receiver` → reassemble → decode → OpenGL texture

**Transport layer** (`transport.cpp`): manages the WebSocket signaling connection and all WebRTC peer connections. Each peer gets multiple DataChannels (audio, video, control, optionally system audio).

### 4. UI Client (`client-qt/`)
Qt6 / QML application. Links `driscord_core` directly as a C++ library. Enabled via `-DBUILD_QT_CLIENT=ON`; requires `Qt6::{Quick,Network,Widgets,QuickDialogs2}`. C++↔QML bridging lives in `client-qt/src/app/DriscordBridge.*`.

### Wire Protocol (`core/src/utils/protocol.hpp`)
Custom binary headers prepended to all media packets:
- `AudioHeader`: 16 bytes (seq + sender timestamp) + Opus payload
- `VideoHeader`: 24 bytes (width, height, timestamp, bitrate, frame duration)
- `ChunkHeader`: 6 bytes (frame_id, chunk_idx, total_chunks) + payload

### Platform Abstraction
- Audio I/O: miniaudio (single header, all platforms)
- Screen capture: `core/src/video/capture/` — separate `.cpp` per platform (Linux/X11+Xrandr, Windows/D3D11, macOS/ScreenCaptureKit)
- System audio capture: `core/src/audio/capture/` — same pattern (Linux/PulseAudio, Windows/Media Foundation, macOS/AudioToolbox)

### Logging
`core/src/utils/log.hpp` — thread-safe, millisecond timestamps. Use macros: `LOG_INFO()`, `LOG_WARNING()`, `LOG_ERROR()`.

### Key Config
`core/src/config.hpp` defines `Config` struct. `config.json` provides runtime values. `core/src/stream_defs.hpp` defines FPS and quality preset enums.

## Dependencies
All C++ deps except FFmpeg, Qt, and system libs are fetched at configure time via CMake FetchContent:
- libdatachannel v0.22.5 (WebRTC + WebSocket client)
- Opus v1.5.2 (audio codec)
- Boost ≥1.89 (ASIO + Beast, system-installed)
- FFmpeg (system-installed, required for video encode/decode)
- nlohmann/json v3.11.3, fmt v10.2.1

Qt client additionally requires system-installed Qt6 (Quick, Network, Widgets, QuickDialogs2).

Python API deps (installed via `./scripts/build.sh --api`):
- FastAPI, uvicorn, SQLAlchemy (asyncpg), python-jose (JWT), passlib (bcrypt), pydantic-settings
