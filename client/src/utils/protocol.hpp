#pragma once

#include <cstddef>
#include <cstdint>

namespace protocol {

// Audio packet: [seq:2][sender_ts:4][opus_data:N]
constexpr size_t kAudioHeaderSize = 6;

// Video frame envelope: [width:4][height:4][sender_ts:4][bitrate_kbps:4]
constexpr size_t kVideoHeaderSize = 16;

// Video chunk: [frame_id:2][chunk_idx:2][total_chunks:2][payload:N]
constexpr size_t kChunkHeaderSize = 6;
constexpr size_t kMaxChunkPayload = 60000;

}  // namespace protocol
