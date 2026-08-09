#include "webrtc/google_webrtc_peer_adapters.hpp"

#include "api/peer_connection_interface.h"

#include <utility>

namespace driscord::media::detail {

SetLocalDescriptionObserver::SetLocalDescriptionObserver(Callback callback)
    : callback_(std::move(callback))
{
}

void SetLocalDescriptionObserver::OnSetLocalDescriptionComplete(
    webrtc::RTCError error)
{
    if (callback_) {
        callback_(std::move(error));
    }
}

SetRemoteDescriptionObserver::SetRemoteDescriptionObserver(Callback callback)
    : callback_(std::move(callback))
{
}

void SetRemoteDescriptionObserver::OnSetRemoteDescriptionComplete(
    webrtc::RTCError error)
{
    if (callback_) {
        callback_(std::move(error));
    }
}

std::string rtc_error_message(std::string_view context,
    const webrtc::RTCError& error)
{
    std::string message(context);
    message += ": ";
    message += error.message();
    return message;
}

MediaConnectionState to_public_connection_state(int raw_state)
{
    using State = webrtc::PeerConnectionInterface::PeerConnectionState;
    switch (static_cast<State>(raw_state)) {
    case State::kNew:
        return MediaConnectionState::New;
    case State::kConnecting:
        return MediaConnectionState::Connecting;
    case State::kConnected:
        return MediaConnectionState::Connected;
    case State::kDisconnected:
        return MediaConnectionState::Disconnected;
    case State::kFailed:
        return MediaConnectionState::Failed;
    case State::kClosed:
        return MediaConnectionState::Closed;
    }
    return MediaConnectionState::Failed;
}

} // namespace driscord::media::detail
