# Vendored Google WebRTC test helpers

Source: the pinned Google WebRTC checkout, revision
`third_party/google_webrtc_revision.txt` (`956083e9a9f487b9c2d0cdb96c64ba23cfc1ac76`),
directory `src/test/`. License: BSD-3-Clause (`LICENSE` in this directory is
the upstream WebRTC license).

Vendored files (verbatim copies, no local modifications):

- `test/rtcp_packet_parser.{h,cc}` — typed parse/count access to every RTCP
  packet kind the SFU terminates or forwards;
- `test/rtp_file_reader.{h,cc}` — reader for rtpdump and pcap capture files;
- `test/rtp_file_writer.{h,cc}` — writer for rtpdump fixture files.

Why vendored: the pinned `libwebrtc.a` is produced with
`rtc_include_tests=false`, so nothing under upstream `src/test/` exists in the
archive, while everything these files call (rtcp_packet/*, `rtp_util`,
`rtc_base`) is production code that does. Copying ~1.2k lines into git keeps
the offline CI contract (`/homelab` doctrine: everything a build reads is in
git) without mutating the `google-webrtc-sdk` supply-store blob.

Build target: `driscord_webrtc_test_support` in `core/tests/CMakeLists.txt`
(only configured with `BUILD_CORE=ON` + tests). It compiles against the SDK
headers through the `driscord::google_webrtc` imported target, keeping the
`test/...` include layout, so a WebRTC pin bump only requires re-copying these
six files if their upstream API changed.

Adaptations policy: keep the copies byte-identical to upstream. If an
adaptation ever becomes unavoidable, list it here per file.
