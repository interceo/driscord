#pragma once

#include "webrtc/google_webrtc_media_types.hpp"
#include "webrtc/google_webrtc_runtime.hpp"

#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/set_local_description_observer_interface.h"
#include "api/set_remote_description_observer_interface.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace driscord::media::detail {

class SetLocalDescriptionObserver
    : public webrtc::SetLocalDescriptionObserverInterface {
public:
    using Callback = std::function<void(webrtc::RTCError)>;
    explicit SetLocalDescriptionObserver(Callback callback);
    void OnSetLocalDescriptionComplete(webrtc::RTCError error) override;

private:
    const Callback callback_;
};

class SetRemoteDescriptionObserver
    : public webrtc::SetRemoteDescriptionObserverInterface {
public:
    using Callback = std::function<void(webrtc::RTCError)>;
    explicit SetRemoteDescriptionObserver(Callback callback);
    void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override;

private:
    const Callback callback_;
};

[[nodiscard]] std::string rtc_error_message(std::string_view context,
    const webrtc::RTCError& error);

[[nodiscard]] MediaConnectionState to_public_connection_state(int state);

[[nodiscard]] std::vector<webrtc::PeerConnectionInterface::IceServer>
to_ice_servers(const std::vector<IceServerConfig>& servers);

}
