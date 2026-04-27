#include "opus_codec.hpp"

#include "log.hpp"

#include <algorithm>
#include <opus.h>

// --- OpusEncode --------------------------------------------------------------

OpusEncode::~OpusEncode()
{
    shutdown();
}

bool OpusEncode::init(const size_t sample_rate,
    const size_t channels,
    const size_t bitrate,
    const size_t application)
{
    shutdown();

    int err;
    encoder_ = opus_encoder_create(sample_rate, channels, application, &err);
    if (err != OPUS_OK) {
        LOG_ERROR() << "opus_encoder_create failed: " << opus_strerror(err);
        encoder_ = nullptr;
        return false;
    }

    if (opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrate)) != OPUS_OK) {
        LOG_ERROR() << "opus_encoder_ctl (OPUS_SET_BITRATE) failed: "
                    << opus_strerror(err);
        shutdown();
        return false;
    }

    opus_encoder_ctl(encoder_, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(encoder_, OPUS_SET_DTX(1));
    set_packet_loss_pct(10);

    sample_rate_ = sample_rate;
    channels_ = channels;
    return true;
}

void OpusEncode::set_packet_loss_pct(int pct)
{
    if (!encoder_) {
        return;
    }
    pct = std::clamp(pct, 0, 30);
    opus_encoder_ctl(encoder_, OPUS_SET_PACKET_LOSS_PERC(pct));
}

void OpusEncode::shutdown()
{
    if (encoder_) {
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
    }
    sample_rate_ = 0;
    channels_ = 0;
}

int OpusEncode::encode(const float* pcm,
    const size_t frame_size,
    uint8_t* output,
    const size_t max_output)
{
    if (!encoder_) {
        return -1;
    }
    return opus_encode_float(encoder_, pcm, frame_size, output, max_output);
}

// --- OpusDecode --------------------------------------------------------------

OpusDecode::~OpusDecode()
{
    shutdown();
}

bool OpusDecode::init(int sample_rate, int channels)
{
    shutdown();

    int err;
    decoder_ = opus_decoder_create(sample_rate, channels, &err);
    if (err != OPUS_OK) {
        LOG_ERROR() << "opus_decoder_create failed: " << opus_strerror(err);
        decoder_ = nullptr;
        return false;
    }
    sample_rate_ = sample_rate;
    channels_ = channels;
    return true;
}

void OpusDecode::shutdown()
{
    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }
    sample_rate_ = 0;
    channels_ = 0;
}

int OpusDecode::decode(const uint8_t* data,
    const size_t len,
    float* output,
    const size_t max_samples)
{
    if (!decoder_) {
        return -1;
    }
    return opus_decode_float(decoder_, data, len, output, max_samples, 0);
}

int OpusDecode::decode_plc(float* output, const size_t max_samples)
{
    if (!decoder_) {
        return -1;
    }
    return opus_decode_float(decoder_, nullptr, 0, output, max_samples, 0);
}

int OpusDecode::decode_fec(const uint8_t* data, const size_t len,
    float* output, const size_t max_samples)
{
    if (!decoder_) {
        return -1;
    }
    return opus_decode_float(decoder_, data, len, output, max_samples, 1);
}

void OpusDecode::reset_state()
{
    if (decoder_) {
        opus_decoder_ctl(decoder_, OPUS_RESET_STATE);
    }
}
