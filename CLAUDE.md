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

Runtime config is loaded from `config.json` (server host/port, API host/port, video bitrate). Example:
```json
{ "server": "host:9001", "api": "host:9002", "video_bitrate_kbps": 8000 }
```
The signaling server reads `DRISCORD_PORT` plus `DRISCORD_ICE_PORT_MIN`/`DRISCORD_ICE_PORT_MAX` (default 49160-49200) for the UDP range it accepts media on.
API config is loaded from `backend/api/.env` (see `.env.example` for template).

## Architecture

Driscord is a WebRTC-based voice and screen-sharing app (Discord-like) with three backend/library layers plus a Qt client. Media flows **client → server → clients**: the signaling server terminates each client's PeerConnection and fans media out (SFU). There is no peer mesh, and therefore no STUN/TURN/coturn anywhere.

### 1. Signaling Server (`backend/signaling_server/`)
Boost.Beast WebSocket (rooms, SDP/ICE) plus libdatachannel. Each session owns one `rtc::PeerConnection` to its client; incoming DataChannel messages are re-sent to the other sessions in the room on the channel of the same label, prefixed with the sender id. The server never decodes codecs — it routes on the channel label alone. `audio`/`control` go to everyone in the room; `video`/`screen_audio` only to peers that sent `watch_start` (tracked per room in `Room::video_watchers`).

### 2. API Server (`backend/api/`)
Python/FastAPI backend with PostgreSQL (asyncpg + SQLAlchemy). Provides user auth (JWT), channel management, and update distribution. All endpoints except `/auth/*` and `/health` require a Bearer token.

### 3. C++ Core Library (`core/src/`, built as `driscord_core` static lib)
The core has two parallel transport systems:

**Audio pipeline**: `audio_sender` → mic capture (miniaudio) → Opus encode (48kHz/mono) → DataChannel → `audio_receiver` → reorder buffer → decode (PLC/FEC on gaps) → WSOLA → PCM ring → `audio_mixer` → playback

**Video pipeline**: `video_sender` → screen capture (platform-specific) → H.264/H.265 encode (FFmpeg) → one DataChannel message per frame (`FrameHeader` + payload, fragmented by SCTP) → server → `video_receiver` → decode → OpenGL texture

Frames are **not** chunked at the application level — SCTP fragments them. Before sending, `VideoTransport::send_video` checks `bufferedAmount()` and drops the frame if the send queue is over `stream_defaults::kVideoSendBufferLimitBytes`, since queueing behind a saturated uplink buys latency and nothing else.

**A/V synchronisation** (`core/src/sync/`): both pipelines stamp packets from one monotonic `utils::MonoClock` per sending process. On the receiving side a `MediaClock` per peer turns those timestamps into playout deadlines — `sender_ts + offset + target_delay` — shared by that peer's audio and video, so the two cannot drift apart. `ScreenReceiver` owns the clock for a screen share (video + its system audio); voice gets its own, which stays at low latency because no video is waiting on it.

**Transport layer** (`transport.cpp`): owns the WebSocket signaling connection and the single `rtc::PeerConnection` to the server. The client is always the offerer and creates the channels (audio, video, control, screen_audio); `audio`/`video`/`screen_audio` are unordered with no retransmits (a lost packet stays lost, playout keeps moving), `control` is ordered and reliable. Peer identity (usernames) arrives via signaling — the `?u=` connect param, echoed back in `welcome`/`peer_joined` — not over any channel.

### 4. UI Client (`client-qt/`)
Qt6 / QML application. Links `driscord_core` directly as a C++ library. Enabled via `-DBUILD_QT_CLIENT=ON`; requires `Qt6::{Quick,Network,Widgets,QuickDialogs2}`. C++↔QML bridging lives in `client-qt/src/app/DriscordBridge.*`.

### Wire Protocol (`core/src/utils/protocol.hpp`)
Custom binary headers prepended to all media packets:
- `AudioHeader`: 16 bytes (u32 seq, u32 flags, i64 sender_ts_us) + Opus payload
- `VideoHeader`: 32 bytes (width, height, sender_ts_us, bitrate, frame duration, flags, codec)
- `FrameHeader`: 8 bytes (frame_id) + payload, one per video frame

Server → client, every media message is prefixed with `u8` sender-id length followed by the sender id.

`sender_ts_us` is microseconds on the sender's `utils::MonoClock`, shared by both
media headers — that shared timeline is what A/V sync is built on. `flags` carries
`kTalkspurtStart` for audio (silence the sender chose, as opposed to loss) and
`kKeyframe` for video.

### Platform Abstraction
- Audio I/O: miniaudio (single header, all platforms)
- Screen capture: `core/src/video/capture/` — separate `.cpp` per platform (Linux/X11+Xrandr, Windows/D3D11, macOS/ScreenCaptureKit)
- System audio capture: `core/src/audio/capture/` — same pattern (Linux/PulseAudio, Windows/Media Foundation, macOS/AudioToolbox)

### Logging
`core/src/utils/log.hpp` — thread-safe, millisecond timestamps. Use macros: `LOG_INFO()`, `LOG_WARNING()`, `LOG_ERROR()`.

### Key Config
`core/src/config.hpp` holds `stream_defaults` (bitrates, buffer sizes) and `sync_defaults` (playout delay bounds, time-stretch limits). `config.json` provides runtime values; the Qt client parses it in `client-qt/src/app/AppConfig.*`.

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
