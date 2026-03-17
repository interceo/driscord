#include "utils/opus_codec.hpp"

#include "log.hpp"

#include <opus.h>

// --- OpusEncode --------------------------------------------------------------

OpusEncode::~OpusEncode() {
    shutdown();
}

bool OpusEncode::init(int sample_rate, int channels, int bitrate, int application) {
    shutdown();
    int err;
    encoder_ = opus_encoder_create(sample_rate, channels, application, &err);
    if (err != OPUS_OK) {
        LOG_ERROR() << "opus_encoder_create failed: " << opus_strerror(err);
        encoder_ = nullptr;
        return false;
    }
    opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrate));
    sample_rate_ = sample_rate;
    channels_    = channels;
    return true;
}

void OpusEncode::shutdown() {
    if (encoder_) {
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
    }
    sample_rate_ = 0;
    channels_    = 0;
}

int OpusEncode::encode(const float* pcm, int frame_size, uint8_t* output, int max_output) {
    if (!encoder_) {
        return -1;
    }
    return opus_encode_float(encoder_, pcm, frame_size, output, max_output);
}

// --- OpusDecode --------------------------------------------------------------

OpusDecode::~OpusDecode() {
    shutdown();
}

bool OpusDecode::init(int sample_rate, int channels) {
    shutdown();
    int err;
    decoder_ = opus_decoder_create(sample_rate, channels, &err);
    if (err != OPUS_OK) {
        LOG_ERROR() << "opus_decoder_create failed: " << opus_strerror(err);
        decoder_ = nullptr;
        return false;
    }
    sample_rate_ = sample_rate;
    channels_    = channels;
    return true;
}

void OpusDecode::shutdown() {
    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }
    sample_rate_ = 0;
    channels_    = 0;
}

int OpusDecode::decode(const uint8_t* data, int len, float* output, int max_samples) {
    if (!decoder_) {
        return -1;
    }
    return opus_decode_float(decoder_, data, len, output, max_samples, 0);
}
