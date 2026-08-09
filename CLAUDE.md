# CLAUDE.md

Repository guidance for coding agents.

## Build and run

```bash
./scripts/build.sh                 # Qt client, Release
./scripts/build.sh --debug         # Qt client, Debug
./scripts/build.sh --test          # core + signaling + real WebRTC integration
./scripts/build.sh --server        # standalone signaling/SFU server
./scripts/build.sh --server --test # room/signaling tests
./scripts/build.sh --api           # Python API environment

./scripts/run.sh
./scripts/run.sh --server
./scripts/run.sh --api
```

Client/core builds currently require Linux x86_64, Clang/lld and the pinned
Google WebRTC checkout/archive produced by `scripts/build_google_webrtc.sh`.
Do not restore the deleted MinGW or legacy benchmark paths as fake
compatibility.

Outputs:

- `.builds/cmake/qt-webrtc-{release,debug}/client-qt/driscord_client`
- `.builds/server/{release,debug}/driscord_server`

## Architecture

Driscord is client → SFU → clients; there is no peer mesh.

### Signaling/SFU (`backend/signaling_server/`)

Boost.Beast owns WebSocket rooms and signaling. Each session can own two
libdatachannel PeerConnections through `MediaConnections`: `voice` and
`screen`. `VoiceRouter` and `ScreenRouter` forward encoded RTP between stable
subscriber transceiver slots and rewrite the fields required for a coherent
downstream RTP stream. They terminate RTCP per hop, cache packets for local
NACK and forward PLI for screen video. The server never decodes media.

`connection` is mandatory in offer/answer/candidate/track-binding signaling and
is only `voice` or `screen`. There is no legacy DataChannel media protocol.

### C++ core (`core/src/`)

Google WebRTC is the only client media engine:

- `GoogleWebRtcRuntime` owns threads, PeerConnectionFactory and audio devices.
- `GoogleWebRtcVoiceSession` owns one microphone track and a bounded set of
  recvonly voice transceivers.
- `GoogleWebRtcScreenSession` owns a screen video/system-audio pair and bounded
  recvonly video/audio pairs. Desktop capture uses WebRTC DesktopCapturer.
- `GoogleWebRtcClient` coordinates both sessions, `mid -> peer` bindings and UI
  preferences. It is a lifecycle coordinator, not a codec/packet pipeline.
- `Transport` is WebSocket signaling only. It uses libdatachannel's mature
  WebSocket implementation but owns no libdatachannel PeerConnection.

Do not add AudioSender/AudioReceiver or VideoSender/VideoReceiver wrappers.
Capture enters official source/track APIs; decoded media exits sink/render APIs.
Use RAII classes only where lifetime and shutdown ordering matter. Stateless
SDP/stats/RTP transforms should remain free functions or small value types.

System loopback capture is the sole remaining platform capture adapter under
`core/src/audio/capture/`; Google WebRTC owns microphone capture/playout and
audio processing. `GoogleWebRtcPcmPlayout` is a small hardware-output boundary
for mixed screen system audio.

### Qt client (`client-qt/`)

Qt6/QML links `driscord_core`. C++ ↔ QML bridging lives in
`client-qt/src/app/DriscordBridge.*`. Watched streams are a set/list, not a
single peer. Decoded video is push-driven by WebRTC sinks.

### API (`backend/api/`)

FastAPI/PostgreSQL service for auth, channels, invites and updates. Runtime
configuration is in `backend/api/.env`.

## Testing

The integration gate starts the real signaling/SFU server in-process and tests
Google WebRTC encode → SRTP → libdatachannel Track routing → decode. Keep voice
and screen tests on that production path; avoid mocks for codec, jitter buffer,
RTP packetization or decode. The test server's deterministic `RtpFaultConfig`
injects post-SRTP loss/reorder while production defaults remain disabled.
Unit-test deterministic signaling parsers and pure RTP transforms separately.

`rtc::Cleanup()` must run only after all libdatachannel users have stopped.
Google WebRTC sessions must close before their owning runtimes/factories.

## Dependencies and ABI constraints

- pinned Google WebRTC revision: `third_party/google_webrtc_revision.txt`
- libdatachannel v0.24.5
- Boost ≥ 1.89, Qt6, miniaudio, nlohmann/json v3.11.3, fmt v12.2.0

The combined client/test process has Google BoringSSL and therefore builds
libdatachannel as a GnuTLS DSO with NSS-backed libSRTP. Keep the DSO symbol
isolation and `--exclude-libs,ALL`; linking both media stacks statically can
silently interpose OpenSSL/libSRTP global symbols. A server-only `BUILD_CORE=OFF`
configuration stays independent of Google WebRTC and uses OpenSSL.

Google WebRTC is compiled with `use_custom_libcxx=false` to share the process
libstdc++ ABI. Backend code compiles without RTTI; do not leak WebRTC types into
public Qt/DriscordCore headers.

## Known follow-ups

- expose native ADM device enumeration/selection and input/output levels;
- continue splitting the private `GoogleWebRtcClient::Impl` into voice and
  screen lifecycle components without recreating sender/receiver abstraction
  layers; stateful screen stats are already isolated in `ScreenStatsTracker`;
- add a long-running multi-publisher soak/capacity gate;
- add screen simulcast/SVC and SFU layer selection if multi-tile bandwidth
  becomes part of the MVP target;
- produce pinned Windows/macOS WebRTC artifacts before enabling those clients.
