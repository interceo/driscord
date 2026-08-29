# CLAUDE.md

Repository guidance for coding agents.

## Build and run

Every configuration lives in `CMakePresets.json`; there is no build script to
wrap it. `cmake --list-presets` is authoritative, `cmake --workflow` runs
configure, build and tests in one step.

```bash
cmake --workflow --preset client        # Qt client, Release
cmake --workflow --preset client-debug  # Qt client, Debug
cmake --workflow --preset client-tests  # client + its tests (offscreen QPA)
cmake --workflow --preset core-tests    # core + signaling + real WebRTC integration
cmake --workflow --preset server        # standalone signaling/SFU server
cmake --workflow --preset server-tests  # protocol/SFU/auth tests, no Google WebRTC

cd backend/api && tox                   # Python API tests

./scripts/run.sh
./scripts/run.sh --server
./scripts/run.sh --api
```

Client/core builds require Clang and the pinned Google WebRTC SDK produced by
`scripts/build_google_webrtc.sh`: natively on Linux x86_64, or cross-compiled
to Windows x64 with `DRISCORD_WEBRTC_TARGET=windows` plus a packaged MSVC
sysroot (`DRISCORD_MSVC_SYSROOT`, xwin layout — see
`cmake/toolchain/windows-clang-cl.cmake`). The `client-windows` and
`windows-release` presets additionally read `DRISCORD_QT_WIN_ROOT` (Qt
msvc2019_64), `QT_HOST_PATH` (Linux Qt of the same version) and, for
packaging, `DRISCORD_MSVC_REDIST_DIR`. Do not restore the deleted MinGW or
legacy benchmark paths as fake compatibility.

Outputs:

- `.builds/{client,client-debug}/client-qt/driscord_client`
- `.builds/{server,server-debug}/backend/signaling_server/driscord_server`

The nix dev shell sets `DRISCORD_BUILD_TAG=nixos-`, which prefixes those
directories so two toolchains never share one CMake cache.

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

Sessions are authorized: `ApiAuthenticator` asks the REST API whether a token
may join a channel before the WebSocket upgrade is accepted. The server refuses
to start without `DRISCORD_API_URL` unless `DRISCORD_ALLOW_ANONYMOUS=1`. Do not
verify JWTs inside the SFU — a second token implementation (and the signing
secret) in a process holding two TLS stacks is what this design avoids.

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

Self-update lives in `client-qt/src/update/`: `UpdateManager` fetches the
static channel's `latest.json` + `.minisig`, verifies the minisign signature
(Ed25519 via the WebRTC archive's BoringSSL, headers via
`driscord::boringssl_headers` only in .cpp files) with the compiled-in trusted
key list before parsing, checks sha256, and applies on explicit user action:
on Linux the artifact is a single AppImage swapped in place
(`install_swap::applyImageFile`), on Windows the zip is extracted with the
system tar and swapped per file. The API is not involved; release presets bake
the production endpoints and dev builds may override the channel via
`config.json`.

The Linux release package is an AppImage: the CPack staging doubles as the
AppDir (launcher installed again as `AppRun`), `cmake/VerifyPackage.cmake`
gates the staged tree (required + forbidden lists mirror the deploy prune in
`client-qt/CMakeLists.txt`), then `cmake/PackageAppImage.cmake` (CPack
External) runs mksquashfs and prepends the pinned type-2 runtime
(`DRISCORD_APPIMAGE_RUNTIME`). Do not re-add the pruned Qt bloat (extra
QuickControls2 styles, QtTest, EglFS, qmltooling, Qt5Compat) — the verify
scripts fail the package if it reappears.

### API (`backend/api/`)

FastAPI/PostgreSQL service for auth, channels and invites. Runtime
configuration is in `backend/api/.env`.

## Testing

The integration gate starts the real signaling/SFU server in-process and tests
Google WebRTC encode → SRTP → libdatachannel Track routing → decode. Keep voice
and screen tests on that production path; avoid mocks for codec, jitter buffer,
RTP packetization or decode. The test server's deterministic `RtpFaultConfig`
injects post-SRTP faults — Gilbert–Elliott burst loss plus a SimulatedNetwork
style link model (delay/jitter/capacity/blackout, runtime-mutable via
`update_fault_config`) — while production defaults remain disabled and add no
timers. RTCP is never impaired. Media-quality gates (PSNR/SSIM via frame
markers, degradation ladders, A/V sync via chirps) live on the same path; see
`docs/testing-media-quality.md` for the program, metrics and thresholds.
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
- media-quality testing program (`docs/testing-media-quality.md`, tracked as
  DRISCORD-16): phases 1–3 are in the per-PR gate; remaining phases — nightly
  scenario matrix with baselines and per-subscriber egress faults, headless
  probe client + unprivileged-netns netem tier, the soak/capacity gate for the
  pre-wired `soak` ctest label, and the offline ViSQOL/OCR/VMAF analyzer
  container with Prometheus trend export;
- move the decoded-video path off `QQuickImageProvider` onto per-peer
  `QVideoSink`/`VideoOutput` (three copies per frame today); blocked on a local
  client build, see `PLAN3.md`;
- add screen simulcast/SVC and SFU layer selection if multi-tile bandwidth
  becomes part of the MVP target;
- the Windows cross build is wired end to end: supply-store blobs
  (msvc-sysroot, Qt msvc kit, WebRTC SDK win, VC redist), the
  `driscord-win-builder` profile/pool (its image carries WineHQ), the
  `windows.yml` push gate — cross-build, the `windows-tests` unit tier under
  Wine, packaging — and the `client-windows` bundle in the release-builder
  policy. The signaling server cross-builds too (MbedTLS libdatachannel), so
  the Wine gate covers signaling unit tests; the integration tier passes 7/11
  under Wine and joins CI once the four Wine-sensitive tests stabilise
  (DRISCORD-15). Remaining Windows debt: a real Windows-VM runner for
  WASAPI/capture/D3D coverage and Authenticode signing. MinGW stays off the
  table. macOS remains unplanned.
