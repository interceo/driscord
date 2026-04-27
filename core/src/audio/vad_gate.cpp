#include "vad_gate.hpp"

#include <algorithm>

void VadGate::set_thresholds(float open, float close)
{
    open_threshold_ = std::clamp(open, 0.0f, 1.0f);
    close_threshold_ = std::clamp(close, 0.0f, open_threshold_);
}

void VadGate::set_open_frames(int n)
{
    open_frames_required_ = std::max(1, n);
}

void VadGate::set_hangover_ms(int ms)
{
    hangover_ms_ = std::max(0, ms);
}

void VadGate::reset()
{
    state_ = State::Closed;
    opening_count_ = 0;
    hangover_remaining_ms_ = 0;
}

bool VadGate::update(float prob, int frame_duration_ms)
{
    switch (state_) {
    case State::Closed:
        if (prob >= open_threshold_) {
            state_ = State::Opening;
            opening_count_ = 1;
            if (opening_count_ >= open_frames_required_) {
                state_ = State::Open;
                return true;
            }
        }
        return false;

    case State::Opening:
        if (prob >= open_threshold_) {
            ++opening_count_;
            if (opening_count_ >= open_frames_required_) {
                state_ = State::Open;
                return true;
            }
            return false;
        }
        state_ = State::Closed;
        opening_count_ = 0;
        return false;

    case State::Open:
        if (prob <= close_threshold_) {
            state_ = State::Hangover;
            hangover_remaining_ms_ = hangover_ms_;
        }
        return true;

    case State::Hangover:
        if (prob >= open_threshold_) {
            state_ = State::Open;
            hangover_remaining_ms_ = 0;
            return true;
        }
        hangover_remaining_ms_ -= frame_duration_ms;
        if (hangover_remaining_ms_ <= 0) {
            state_ = State::Closed;
            opening_count_ = 0;
            return false;
        }
        return true;
    }
    return false;
}
