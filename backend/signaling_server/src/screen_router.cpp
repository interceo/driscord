#include "screen_router.hpp"

#include "log.hpp"
#include "rtp_slot_rewriter.hpp"
#include "sfu_media_utils.hpp"

#include <rtc/plihandler.hpp>
#include <rtc/rtcpnackresponder.hpp>
#include <rtc/rtcpreceivingsession.hpp>
#include <rtc/rtcpsrreporter.hpp>
#include <rtc/rtppacketizationconfig.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace driscord {

struct ScreenRouter::Impl final : std::enable_shared_from_this<Impl> {
    struct OutputTrack {
        std::string mid;
        uint32_t ssrc = 0;
        std::weak_ptr<rtc::Track> track;
        bool ready = false;
        std::shared_ptr<sfu::RtpSlotRewriter> rewriter;
        uint64_t source_generation = 0;
    };

    struct OutputPair {
        std::string cname;
        std::optional<OutputTrack> video;
        std::optional<OutputTrack> audio;
        std::optional<PeerId> publisher;
    };

    struct PeerState {
        uint64_t order = 0;
        std::weak_ptr<rtc::Track> input_video;
        std::weak_ptr<rtc::Track> input_audio;
        std::optional<uint8_t> video_mid_extension_id;
        std::optional<uint8_t> audio_mid_extension_id;
        sfu::RtpFaultState video_fault_state;
        sfu::RtpFaultState audio_fault_state;
        std::vector<OutputPair> outputs;
        BindingSender send_binding;
        bool streaming = false;
        std::unordered_map<PeerId, uint64_t> watched_publishers;
        uint64_t next_watch_order = 0;
    };

    struct BindingNotice {
        BindingSender send;
        std::string mid;
        std::optional<PeerId> publisher;
        std::weak_ptr<rtc::Track> output;
        uint32_t ssrc = 0;
        std::string cname;
        std::string media_type;
        std::weak_ptr<rtc::Track> publisher_video;
    };

    // Ordering of outbound notifications. A subscriber must see slot changes
    // in the order the router decided them: two concurrent mutations touching
    // the same slot could otherwise deliver "bound to A" before "unbound", and
    // the tile would then stay black while A's packets keep arriving. Held
    // around the whole mutate-then-publish sequence; always taken before
    // `mutex`, and never by the RTP path.
    std::mutex publish_mutex;
    mutable std::mutex mutex;
    std::unordered_map<PeerId, PeerState> peers;
    sfu::RtpFaultConfig fault_config;
    uint64_t next_peer_order = 0;
    bool closed = false;
    // Kept out of `mutex` so the RTP path does not pay for observability.
    std::atomic<uint64_t> video_packets_in { 0 };
    std::atomic<uint64_t> video_packets_out { 0 };
    std::atomic<uint64_t> video_bytes_out { 0 };
    std::atomic<uint64_t> audio_packets_in { 0 };
    std::atomic<uint64_t> audio_packets_out { 0 };
    std::atomic<uint64_t> audio_bytes_out { 0 };
    std::atomic<uint64_t> keyframe_requests { 0 };

    explicit Impl(sfu::RtpFaultConfig config)
        : fault_config(config)
    {
    }

    PeerState& peer_for_locked(const PeerId& peer_id)
    {
        auto [peer, inserted] = peers.try_emplace(peer_id);
        if (inserted) {
            peer->second.order = next_peer_order++;
        }
        return peer->second;
    }

    std::vector<PeerId> ordered_peers_locked() const
    {
        std::vector<PeerId> result;
        result.reserve(peers.size());
        for (const auto& [peer_id, _] : peers) {
            result.push_back(peer_id);
        }
        std::sort(result.begin(), result.end(), [this](const PeerId& lhs, const PeerId& rhs) {
            const auto& left = peers.at(lhs);
            const auto& right = peers.at(rhs);
            return left.order != right.order ? left.order < right.order
                                             : lhs.value < rhs.value;
        });
        return result;
    }

    std::vector<PeerId> ordered_watches_locked(
        const PeerState& subscriber) const
    {
        std::vector<PeerId> result;
        result.reserve(subscriber.watched_publishers.size());
        for (const auto& [publisher_id, _] : subscriber.watched_publishers) {
            result.push_back(publisher_id);
        }
        std::sort(result.begin(), result.end(), [this, &subscriber](const PeerId& lhs, const PeerId& rhs) {
            const auto left_watch = subscriber.watched_publishers.at(lhs);
            const auto right_watch = subscriber.watched_publishers.at(rhs);
            if (left_watch != right_watch) {
                return left_watch < right_watch;
            }
            const auto left = peers.find(lhs);
            const auto right = peers.find(rhs);
            if (left != peers.end() && right != peers.end()
                && left->second.order != right->second.order) {
                return left->second.order < right->second.order;
            }
            return lhs.value < rhs.value;
        });
        return result;
    }

    void request_keyframe(
        const std::weak_ptr<rtc::Track>& publisher_video) noexcept
    {
        auto input = publisher_video.lock();
        if (!input || !input->isOpen()) {
            return;
        }
        try {
            input->requestKeyframe();
            keyframe_requests.fetch_add(1, std::memory_order_relaxed);
        } catch (const std::exception& error) {
            LOG_WARNING() << "screen SFU keyframe request failed: "
                          << error.what();
        }
    }

    void register_track(const PeerId& owner,
        const std::shared_ptr<rtc::Track>& track,
        BindingSender send_binding)
    {
        if (!track) {
            return;
        }
        const auto description = track->description();
        const std::string media_type = description.type();
        if (media_type != "audio" && media_type != "video") {
            return;
        }
        if (track->direction() == rtc::Description::Direction::RecvOnly) {
            register_input(owner, track, description, media_type,
                std::move(send_binding));
        } else if (track->direction()
            == rtc::Description::Direction::SendOnly) {
            register_output(owner, track, description, media_type,
                std::move(send_binding));
        }
    }

    void register_input(const PeerId& owner,
        const std::shared_ptr<rtc::Track>& track,
        rtc::Description::Media description,
        const std::string& media_type,
        BindingSender send_binding)
    {
        sfu::apply_forwarding_feedback_policy(description);
        if (media_type == "video") {
            sfu::remove_auxiliary_video_codecs(description);
        }
        track->setDescription(description);
        track->setMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
        const std::weak_ptr<Impl> weak = weak_from_this();
        const std::weak_ptr<rtc::Track> weak_track = track;
        track->onMessage(
            [weak, weak_track, owner, media_type](rtc::binary packet) mutable {
                if (auto self = weak.lock()) {
                    self->route(owner, media_type, weak_track,
                        std::move(packet));
                }
            },
            nullptr);
        if (media_type == "video") {
            track->onOpen([weak, weak_track] {
                if (auto self = weak.lock()) {
                    self->request_keyframe(weak_track);
                }
            });
        }

        std::scoped_lock publish_lock(publish_mutex);
        std::vector<BindingNotice> notices;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            auto& peer = peer_for_locked(owner);
            peer.send_binding = std::move(send_binding);
            bool source_changed = false;
            if (media_type == "video") {
                source_changed = peer.input_video.lock() != track;
                if (source_changed) {
                    peer.video_fault_state = { };
                }
                peer.input_video = track;
                peer.video_mid_extension_id = sfu::mid_extension_id(description);
            } else {
                source_changed = peer.input_audio.lock() != track;
                if (source_changed) {
                    peer.audio_fault_state = { };
                }
                peer.input_audio = track;
                peer.audio_mid_extension_id = sfu::mid_extension_id(description);
            }
            if (source_changed) {
                for (auto& [_, subscriber] : peers) {
                    for (auto& pair : subscriber.outputs) {
                        if (!pair.publisher || *pair.publisher != owner) {
                            continue;
                        }
                        auto& output = media_type == "video"
                            ? pair.video
                            : pair.audio;
                        if (output && output->rewriter) {
                            output->source_generation
                                = output->rewriter->begin_source();
                        }
                        append_pair_notices(
                            subscriber, pair, owner, notices);
                    }
                }
            }
            auto assigned = assign_available_locked();
            notices.insert(notices.end(),
                std::make_move_iterator(assigned.begin()),
                std::make_move_iterator(assigned.end()));
        }
        publish(std::move(notices));
    }

    static OutputTrack* output_by_mid(PeerState& peer,
        std::string_view mid,
        std::string_view media_type)
    {
        for (auto& pair : peer.outputs) {
            auto& output = media_type == "video" ? pair.video : pair.audio;
            if (output && output->mid == mid) {
                return &*output;
            }
        }
        return nullptr;
    }

    OutputPair& output_pair_for(PeerState& peer,
        std::string_view mid,
        std::string_view media_type)
    {
        for (auto& pair : peer.outputs) {
            const auto& output = media_type == "video" ? pair.video : pair.audio;
            if (output && output->mid == mid) {
                return pair;
            }
        }
        for (auto& pair : peer.outputs) {
            const auto& output = media_type == "video" ? pair.video : pair.audio;
            if (!output) {
                return pair;
            }
        }
        const uint32_t token = sfu::allocate_slot_ssrc();
        peer.outputs.push_back(OutputPair {
            .cname = "driscord-screen-slot-" + std::to_string(token),
            .video = std::nullopt,
            .audio = std::nullopt,
            .publisher = std::nullopt,
        });
        return peer.outputs.back();
    }

    void append_pair_notices(PeerState& subscriber,
        OutputPair& pair,
        std::optional<PeerId> publisher,
        std::vector<BindingNotice>& notices)
    {
        std::weak_ptr<rtc::Track> publisher_video;
        if (publisher) {
            const auto source = peers.find(*publisher);
            if (source != peers.end()) {
                publisher_video = source->second.input_video;
            }
        }
        auto append = [&](const std::optional<OutputTrack>& output,
                          const char* media_type) {
            if (output && output->ready) {
                notices.push_back({ subscriber.send_binding, output->mid,
                    publisher, output->track, output->ssrc, pair.cname,
                    media_type,
                    publisher_video });
            }
        };
        append(pair.video, "video");
        append(pair.audio, "audio");
    }

    void clear_pair_locked(PeerState& subscriber,
        OutputPair& pair,
        std::vector<BindingNotice>& notices)
    {
        if (!pair.publisher) {
            return;
        }
        for (auto* output : { &pair.video, &pair.audio }) {
            if (*output && (*output)->rewriter) {
                (*output)->rewriter->end_source();
                (*output)->source_generation = 0;
            }
        }
        pair.publisher.reset();
        append_pair_notices(subscriber, pair, std::nullopt, notices);
    }

    void register_output(const PeerId& owner,
        const std::shared_ptr<rtc::Track>& track,
        rtc::Description::Media description,
        const std::string& media_type,
        BindingSender send_binding)
    {
        const std::string mid = track->mid();
        const uint32_t ssrc = sfu::allocate_slot_ssrc();
        std::string cname;
        std::scoped_lock publish_lock(publish_mutex);
        std::vector<BindingNotice> notices;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            auto& peer = peer_for_locked(owner);
            peer.send_binding = std::move(send_binding);
            auto& pair = output_pair_for(peer, mid, media_type);
            clear_pair_locked(peer, pair, notices);
            cname = pair.cname;
            const bool video = media_type == "video";
            OutputTrack output { mid, ssrc, track, false,
                std::make_shared<sfu::RtpSlotRewriter>(
                    video ? 90'000 : 48'000, video ? 3'000 : 960),
                0 };
            if (media_type == "video") {
                pair.video = std::move(output);
            } else {
                pair.audio = std::move(output);
            }
        }

        sfu::apply_forwarding_feedback_policy(description);
        if (media_type == "video") {
            sfu::remove_auxiliary_video_codecs(description);
        }
        description.clearSSRCs();
        // Audio and video share one MediaStream/CNAME for RTCP sync, but the
        // WebRTC track IDs must remain distinct within that stream.
        description.addSSRC(ssrc, cname, "driscord-screen",
            cname + "-" + media_type);
        track->setDescription(description);
        track->onMessage([](rtc::binary) { }, nullptr);

        {
            std::scoped_lock lock(mutex);
            if (!closed) {
                auto peer = peers.find(owner);
                if (peer != peers.end()) {
                    auto* output = output_by_mid(peer->second, mid, media_type);
                    if (output && output->track.lock() == track) {
                        output->ready = true;
                    }
                    auto assigned = assign_available_locked();
                    notices.insert(notices.end(),
                        std::make_move_iterator(assigned.begin()),
                        std::make_move_iterator(assigned.end()));
                }
            }
        }
        publish(std::move(notices));
    }

    std::vector<BindingNotice> assign_available_locked()
    {
        std::vector<BindingNotice> notices;
        const auto ordered_peers = ordered_peers_locked();
        for (const auto& subscriber_id : ordered_peers) {
            auto& subscriber = peers.at(subscriber_id);
            if (subscriber.watched_publishers.empty()) {
                continue;
            }
            std::unordered_set<PeerId> assigned;
            for (const auto& pair : subscriber.outputs) {
                if (pair.publisher) {
                    assigned.insert(*pair.publisher);
                }
            }
            for (const auto& publisher_id :
                ordered_watches_locked(subscriber)) {
                const auto publisher = peers.find(publisher_id);
                if (publisher == peers.end() || publisher_id == subscriber_id
                    || !publisher->second.streaming
                    || publisher->second.input_video.expired()
                    || assigned.contains(publisher_id)) {
                    continue;
                }
                auto slot = std::find_if(subscriber.outputs.begin(),
                    subscriber.outputs.end(), [](const OutputPair& pair) {
                        return !pair.publisher && pair.video && pair.audio
                            && pair.video->ready && pair.audio->ready
                            && !pair.video->track.expired()
                            && !pair.audio->track.expired();
                    });
                if (slot == subscriber.outputs.end()) {
                    break;
                }
                slot->publisher = publisher_id;
                slot->video->source_generation
                    = slot->video->rewriter->begin_source();
                slot->audio->source_generation
                    = slot->audio->rewriter->begin_source();
                assigned.insert(publisher_id);
                append_pair_notices(
                    subscriber, *slot, publisher_id, notices);
            }
        }
        return notices;
    }

    void publish(std::vector<BindingNotice> notices)
    {
        for (auto& notice : notices) {
            if (auto output = notice.output.lock()) {
                if (notice.publisher) {
                    const auto format = sfu::primary_rtp_format(
                        output->description(), notice.media_type);
                    auto config = std::make_shared<rtc::RtpPacketizationConfig>(
                        notice.ssrc, notice.cname, format.payload_type,
                        format.clock_rate);
                    auto reporter = std::make_shared<rtc::RtcpSrReporter>(config);
                    reporter->addToChain(
                        std::make_shared<rtc::RtcpNackResponder>());
                    if (notice.media_type == "video") {
                        const auto publisher_video = notice.publisher_video;
                        const std::weak_ptr<Impl> weak = weak_from_this();
                        reporter->addToChain(std::make_shared<rtc::PliHandler>(
                            [weak, publisher_video] {
                                if (auto self = weak.lock()) {
                                    self->request_keyframe(publisher_video);
                                }
                            }));
                    }
                    output->setMediaHandler(std::move(reporter));
                    if (notice.media_type == "video") {
                        request_keyframe(notice.publisher_video);
                    }
                } else {
                    output->setMediaHandler(nullptr);
                }
            }
            if (notice.send) {
                notice.send(std::move(notice.mid),
                    std::move(notice.publisher));
            }
        }
    }

    void route(const PeerId& publisher,
        const std::string& media_type,
        const std::weak_ptr<rtc::Track>& expected_input,
        rtc::binary packet)
    {
        struct Target {
            std::weak_ptr<rtc::Track> track;
            uint32_t ssrc = 0;
            std::shared_ptr<sfu::RtpSlotRewriter> rewriter;
            uint64_t source_generation = 0;
        };
        std::vector<Target> targets;
        sfu::RtpFaultResult packets;
        std::optional<uint8_t> mid_id;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            const auto source = peers.find(publisher);
            if (source == peers.end()) {
                return;
            }
            const auto actual_input = media_type == "video"
                ? source->second.input_video.lock()
                : source->second.input_audio.lock();
            if (actual_input != expected_input.lock()
                || !source->second.streaming) {
                return;
            }
            mid_id = media_type == "video"
                ? source->second.video_mid_extension_id
                : source->second.audio_mid_extension_id;
            if (!rtc::IsRtcp(packet)) {
                (media_type == "video" ? video_packets_in : audio_packets_in)
                    .fetch_add(1, std::memory_order_relaxed);
            }
            auto& fault_state = media_type == "video"
                ? source->second.video_fault_state
                : source->second.audio_fault_state;
            packets = sfu::apply_rtp_faults(
                fault_config, fault_state, std::move(packet));
            if (!packets.first) {
                return;
            }
            for (const auto& [_, subscriber] : peers) {
                for (const auto& pair : subscriber.outputs) {
                    if (!pair.publisher || *pair.publisher != publisher) {
                        continue;
                    }
                    const auto& output = media_type == "video"
                        ? pair.video
                        : pair.audio;
                    if (output && output->ready) {
                        targets.push_back({ output->track, output->ssrc,
                            output->rewriter, output->source_generation });
                    }
                }
            }
        }

        for (const auto* routed_packet : { &packets.first, &packets.second }) {
            if (!*routed_packet) {
                continue;
            }
            for (const auto& target : targets) {
                auto output = target.track.lock();
                if (!output || !output->isOpen()) {
                    continue;
                }
                rtc::binary rewritten = **routed_packet;
                if (!target.rewriter
                    || !target.rewriter->rewrite(rewritten,
                        target.source_generation, target.ssrc, mid_id)) {
                    continue;
                }
                const size_t size = rewritten.size();
                try {
                    output->send(std::move(rewritten));
                    const bool video = media_type == "video";
                    (video ? video_packets_out : audio_packets_out)
                        .fetch_add(1, std::memory_order_relaxed);
                    (video ? video_bytes_out : audio_bytes_out)
                        .fetch_add(size, std::memory_order_relaxed);
                } catch (const std::exception& error) {
                    LOG_WARNING()
                        << "screen SFU send failed: " << error.what();
                }
            }
        }
    }

    void set_streaming(const PeerId& peer_id, bool streaming)
    {
        std::scoped_lock publish_lock(publish_mutex);
        std::vector<BindingNotice> notices;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            auto& peer = peer_for_locked(peer_id);
            if (peer.streaming == streaming) {
                return;
            }
            peer.streaming = streaming;
            if (!streaming) {
                for (auto& [_, subscriber] : peers) {
                    for (auto& pair : subscriber.outputs) {
                        if (pair.publisher
                            && *pair.publisher == peer_id) {
                            clear_pair_locked(subscriber, pair, notices);
                        }
                    }
                }
            }
            auto assigned = assign_available_locked();
            notices.insert(notices.end(),
                std::make_move_iterator(assigned.begin()),
                std::make_move_iterator(assigned.end()));
        }
        publish(std::move(notices));
    }

    void set_watching(const PeerId& peer_id,
        const PeerId& publisher_id,
        bool watching)
    {
        if (peer_id == publisher_id) {
            return;
        }
        std::scoped_lock publish_lock(publish_mutex);
        std::vector<BindingNotice> notices;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            auto& peer = peer_for_locked(peer_id);
            if (watching) {
                if (peer.watched_publishers.contains(publisher_id)) {
                    return;
                }
                peer.watched_publishers.emplace(
                    publisher_id, peer.next_watch_order++);
            } else {
                if (peer.watched_publishers.erase(publisher_id) == 0) {
                    return;
                }
                for (auto& pair : peer.outputs) {
                    if (pair.publisher && *pair.publisher == publisher_id) {
                        clear_pair_locked(peer, pair, notices);
                    }
                }
            }
            auto assigned = assign_available_locked();
            notices.insert(notices.end(),
                std::make_move_iterator(assigned.begin()),
                std::make_move_iterator(assigned.end()));
        }
        publish(std::move(notices));
    }

    void remove_peer(const PeerId& peer_id)
    {
        std::scoped_lock publish_lock(publish_mutex);
        std::vector<BindingNotice> notices;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            peers.erase(peer_id);
            for (auto& [_, subscriber] : peers) {
                subscriber.watched_publishers.erase(peer_id);
                for (auto& pair : subscriber.outputs) {
                    if (pair.publisher && *pair.publisher == peer_id) {
                        clear_pair_locked(subscriber, pair, notices);
                    }
                }
            }
            auto assigned = assign_available_locked();
            notices.insert(notices.end(),
                std::make_move_iterator(assigned.begin()),
                std::make_move_iterator(assigned.end()));
        }
        publish(std::move(notices));
    }

    ScreenRouter::Stats stats() const
    {
        ScreenRouter::Stats result;
        {
            std::scoped_lock lock(mutex);
            for (const auto& [_, peer] : peers) {
                if (peer.streaming && !peer.input_video.expired()) {
                    ++result.streaming_publishers;
                }
                for (const auto& pair : peer.outputs) {
                    if (pair.publisher) {
                        ++result.bound_slots;
                    } else {
                        ++result.free_slots;
                    }
                }
            }
        }
        result.video_packets_in
            = video_packets_in.load(std::memory_order_relaxed);
        result.video_packets_out
            = video_packets_out.load(std::memory_order_relaxed);
        result.video_bytes_out
            = video_bytes_out.load(std::memory_order_relaxed);
        result.audio_packets_in
            = audio_packets_in.load(std::memory_order_relaxed);
        result.audio_packets_out
            = audio_packets_out.load(std::memory_order_relaxed);
        result.audio_bytes_out
            = audio_bytes_out.load(std::memory_order_relaxed);
        result.keyframe_requests
            = keyframe_requests.load(std::memory_order_relaxed);
        return result;
    }

    void close()
    {
        std::vector<std::shared_ptr<rtc::Track>> tracks;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            closed = true;
            for (auto& [_, peer] : peers) {
                if (auto track = peer.input_video.lock()) {
                    tracks.push_back(std::move(track));
                }
                if (auto track = peer.input_audio.lock()) {
                    tracks.push_back(std::move(track));
                }
                for (auto& pair : peer.outputs) {
                    for (auto* output : { &pair.video, &pair.audio }) {
                        if (*output) {
                            if (auto track = (*output)->track.lock()) {
                                tracks.push_back(std::move(track));
                            }
                        }
                    }
                }
            }
            peers.clear();
        }

        // Detaching may wait for an in-flight route callback, which itself
        // takes mutex. Keep all external libdatachannel calls outside it.
        for (const auto& track : tracks) {
            track->onMessage(nullptr);
            track->setMediaHandler(nullptr);
        }
    }
};

ScreenRouter::ScreenRouter(sfu::RtpFaultConfig fault_config)
    : impl_(std::make_shared<Impl>(fault_config))
{
}

ScreenRouter::~ScreenRouter()
{
    close();
}

void ScreenRouter::register_track(const PeerId& owner,
    std::shared_ptr<rtc::Track> track,
    BindingSender send_binding)
{
    impl_->register_track(owner, std::move(track), std::move(send_binding));
}

void ScreenRouter::set_streaming(const PeerId& peer_id, bool streaming)
{
    impl_->set_streaming(peer_id, streaming);
}

void ScreenRouter::set_watching(const PeerId& peer_id,
    const PeerId& publisher_id,
    bool watching)
{
    impl_->set_watching(peer_id, publisher_id, watching);
}

void ScreenRouter::remove_peer(const PeerId& peer_id)
{
    impl_->remove_peer(peer_id);
}

ScreenRouter::Stats ScreenRouter::stats() const
{
    return impl_ ? impl_->stats() : Stats { };
}

void ScreenRouter::close()
{
    if (impl_) {
        impl_->close();
    }
}

} // namespace driscord
