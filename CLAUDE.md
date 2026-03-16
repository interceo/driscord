# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Full build (C++ + Kotlin/Compose)
./scripts/build.sh

# C++ only (server + client core)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Kotlin/Compose client (requires libdriscord_jni.so built first)
cd client-compose
export DRISCORD_NATIVE_LIB_DIR=../build/client
./gradlew fatJar
```

Build outputs:
- `build/server/driscord_server` — signaling server
- `build/client/driscord_client` — legacy ImGui client (if OpenGL found)
- `build/client/libdriscord_jni.so` — JNI bridge for Kotlin client (if JNI found)

## Running

```bash
# Server (port from DRISCORD_PORT env var, or first arg, default 9001)
./build/server/driscord_server 8080

# Legacy ImGui client
./build/client/driscord_client
```

Runtime config is loaded from `driscord.json` (server host/port, TURN servers, bitrates, jitter settings).

## Architecture

Driscord is a WebRTC-based P2P voice and screen-sharing app (Discord-like) with three layers:

### 1. Signaling Server (`server/`)
Boost.Beast WebSocket relay — purely a message router for SDP/ICE negotiation. It never touches audio/video data. All real-time media flows P2P directly between clients.

### 2. C++ Core Library (`client/src/`, built as `driscord_core` static lib)
The core has two parallel transport systems:

**Audio pipeline**: `audio_sender` → mic capture (miniaudio) → Opus encode (48kHz/mono) → DataChannel → `audio_receiver` → jitter buffer → decode → `audio_mixer` → playback

**Video pipeline**: `video_sender` → screen capture (platform-specific) → H.264/H.265 encode (FFmpeg) → chunk (max 60KB each, see `ChunkHeader`) → DataChannel → `video_receiver` → reassemble → decode → OpenGL texture

**Transport layer** (`transport.cpp`): manages the WebSocket signaling connection and all WebRTC peer connections. Each peer gets multiple DataChannels (audio, video, control, optionally system audio).

### 3. UI Clients
- **Modern**: `client-compose/` — Kotlin/Compose Desktop, calls into `driscord_core` via JNI (`client/jni/driscord_jni.cpp`)

### Wire Protocol (`client/src/utils/protocol.hpp`)
Custom binary headers prepended to all media packets:
- `AudioHeader`: 16 bytes (seq + sender timestamp) + Opus payload
- `VideoHeader`: 24 bytes (width, height, timestamp, bitrate, frame duration)
- `ChunkHeader`: 6 bytes (frame_id, chunk_idx, total_chunks) + payload

### Platform Abstraction
- Audio I/O: miniaudio (single header, all platforms)
- Screen capture: `client/src/video/capture/` — separate `.cpp` per platform (Linux/X11+Xrandr, Windows/D3D11, macOS/ScreenCaptureKit)
- System audio capture: `client/src/audio/capture/` — same pattern (Linux/PulseAudio, Windows/Media Foundation, macOS/AudioToolbox)

### Logging
`common/log.hpp` — thread-safe, millisecond timestamps. Use macros: `LOG_INFO()`, `LOG_WARNING()`, `LOG_ERROR()`.

### Key Config
`client/src/config.hpp` defines `Config` struct. `driscord.json` provides runtime values. `client/src/stream_defs.hpp` defines FPS and quality preset enums.

## Dependencies
All C++ deps except FFmpeg and system libs are fetched at configure time via CMake FetchContent:
- libdatachannel v0.22.5 (WebRTC + WebSocket client)
- Opus v1.5.2 (audio codec)
- Dear ImGui v1.91.8 + GLFW 3.4 (legacy UI)
- Boost ≥1.89 (ASIO + Beast, system-installed)
- FFmpeg (system-installed, required for video encode/decode)
- nlohmann/json v3.11.3, fmt v10.2.1
