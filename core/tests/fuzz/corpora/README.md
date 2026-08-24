# Fuzzing seed corpora

- `rtp/` — WebRTC's public `rtp-corpus` + `rtcp-corpus`, copied from the
  pinned Google WebRTC checkout (`third_party/google_webrtc_revision.txt`),
  `src/test/fuzzers/corpora/`. The RTCP packets are deliberate negative seeds
  for the RTP rewriter. Note from the upstream README: some entries carry a
  leading `0xff` byte that upstream fuzzers use to encode header-extension
  settings; our harness treats it as packet data, which is fine for seeds.
- `sdp/` — WebRTC's public `sdp-corpus` (assembled upstream from RFC 4317,
  parser unit tests and browser samples). The matching libFuzzer dictionary
  lives in `../dictionaries/sdp.tokens`.
- `signaling/` — one hand-written valid message per driscord signaling type
  (see `core/src/utils/signaling_protocol.cpp`), so the roundtrip fuzzer
  starts from the accepted grammar instead of discovering JSON by chance.

Crashing inputs found by fuzzing are minimized and committed into the same
directories: the `-runs=0` corpus replay in the PR gate then keeps them as
permanent regressions.

Upstream corpora license: BSD-3-Clause (Google WebRTC).
