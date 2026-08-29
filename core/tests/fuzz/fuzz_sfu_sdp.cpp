
#include "sfu_media_utils.hpp"

#include <rtc/rtc.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const std::string sdp(reinterpret_cast<const char*>(data), size);
    try {
        rtc::Description description(sdp, rtc::Description::Type::Offer);
        const int count = description.mediaCount();
        for (int i = 0; i < count; ++i) {
            auto entry = description.media(i);
            auto* media = std::get_if<rtc::Description::Media*>(&entry);
            if (media == nullptr || *media == nullptr) {
                continue;
            }
            (void)driscord::sfu::mid_extension_id(**media);
            (void)driscord::sfu::primary_rtp_format(**media, "audio");
            (void)driscord::sfu::primary_rtp_format(**media, "video");
            driscord::sfu::apply_forwarding_feedback_policy(**media);
            driscord::sfu::remove_auxiliary_video_codecs(**media);
        }
        (void)description.generateSdp();
    } catch (...) {
    }
    return 0;
}
