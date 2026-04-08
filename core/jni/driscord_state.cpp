#include "driscord_state.hpp"

DriscordState& DriscordState::get()
{
    static DriscordState s;
    return s;
}
