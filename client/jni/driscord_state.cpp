#include "driscord_state.hpp"

DriscordState::DriscordState()
    : transport_cbs(core.transport)
    , video_cbs(core.video_transport)
{}

DriscordState& DriscordState::get() {
    static DriscordState s;
    return s;
}
