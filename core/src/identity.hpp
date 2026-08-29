#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace driscord {

struct PeerId {
    std::string value;

    friend bool operator==(const PeerId&, const PeerId&) = default;
};

struct RoomId {
    std::string value;

    friend bool operator==(const RoomId&, const RoomId&) = default;
};

struct Username {
    std::string value;

    friend bool operator==(const Username&, const Username&) = default;
};

}

template <>
struct std::hash<driscord::PeerId> {
    std::size_t operator()(const driscord::PeerId& id) const noexcept
    {
        return std::hash<std::string> { }(id.value);
    }
};

template <>
struct std::hash<driscord::RoomId> {
    std::size_t operator()(const driscord::RoomId& id) const noexcept
    {
        return std::hash<std::string> { }(id.value);
    }
};
