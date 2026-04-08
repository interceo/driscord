# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```bash
# Build client (release, default)
./scripts/build.sh

# Build client (debug)
./scripts/build.sh --debug

# Build server
./scripts/build.sh --server
./scripts/build.sh --server --debug

# Cross-compile Windows client
./scripts/build.sh --windows

# Tests & benchmarks
./scripts/build.sh --test
./scripts/build.sh --bench

# Run client
./scripts/run.sh
./scripts/run.sh --debug

# Run server
./scripts/run.sh --server
./scripts/run.sh --server --debug

# Debug with GDB
./scripts/run.sh --gdb
```

Build outputs:
- `.builds/client/linux/{release,debug}/` — driscord.jar + libcore.so
- `.builds/client/windows/release/` — driscord.jar + core.dll
- `.builds/server/{release,debug}/` — driscord_server

Runtime config is loaded from `driscord.json` (server host/port, TURN servers, bitrates, jitter settings).

## Architecture

Driscord is a WebRTC-based P2P voice and screen-sharing app (Discord-like) with three layers:

### 1. Signaling Server (`server/`)
Boost.Beast WebSocket relay — purely a message router for SDP/ICE negotiation. It never touches audio/video data. All real-time media flows P2P directly between clients.

### 2. C++ Core Library (`core/src/`, built as `driscord_core` static lib)
The core has two parallel transport systems:

**Audio pipeline**: `audio_sender` → mic capture (miniaudio) → Opus encode (48kHz/mono) → DataChannel → `audio_receiver` → jitter buffer → decode → `audio_mixer` → playback

**Video pipeline**: `video_sender` → screen capture (platform-specific) → H.264/H.265 encode (FFmpeg) → chunk (max 60KB each, see `ChunkHeader`) → DataChannel → `video_receiver` → reassemble → decode → OpenGL texture

**Transport layer** (`transport.cpp`): manages the WebSocket signaling connection and all WebRTC peer connections. Each peer gets multiple DataChannels (audio, video, control, optionally system audio).

### 3. UI Clients
- **Modern**: `client-compose/` — Kotlin/Compose Desktop, calls into `driscord_core` via JNI (`core/jni/`)

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
`core/src/config.hpp` defines `Config` struct. `driscord.json` provides runtime values. `core/src/stream_defs.hpp` defines FPS and quality preset enums.

## Dependencies
All C++ deps except FFmpeg and system libs are fetched at configure time via CMake FetchContent:
- libdatachannel v0.22.5 (WebRTC + WebSocket client)
- Opus v1.5.2 (audio codec)
- Boost ≥1.89 (ASIO + Beast, system-installed)
- FFmpeg (system-installed, required for video encode/decode)
- nlohmann/json v3.11.3, fmt v10.2.1
