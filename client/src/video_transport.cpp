#include "video_transport.hpp"

namespace {
constexpr uint8_t kKeyframeRequestTag = 0x01;
}

VideoTransport::VideoTransport(Transport& transport) : transport_(transport) {
    transport.register_channel({
        .label          = "video",
        .unordered       = true,
        .max_retransmits = -1,  // reliable + unordered: SCTP retransmits until delivered
        .on_data = [this](const std::string& peer_id, const uint8_t* data, size_t len) {
            if (len == 1 && data[0] == kKeyframeRequestTag) {
                if (on_kf_req_) {
                    on_kf_req_();
                }
                return;
            }
            if (on_video_) {
                on_video_(peer_id, data, len);
            }
        },
        .on_open = [this](const std::string& /*peer_id*/) {
            if (on_opened_) {
                on_opened_();
            }
        },
    });
}

void VideoTransport::send_video(const uint8_t* data, size_t len) {
    transport_.send_on_channel("video", data, len);
}

void VideoTransport::send_keyframe_request() {
    transport_.send_on_channel("video", &kKeyframeRequestTag, 1);
}
