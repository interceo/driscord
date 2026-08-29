#include "voice_router.hpp"

#include "log.hpp"
#include "rtp_slot_rewriter.hpp"
#include "sfu_media_utils.hpp"

#include <boost/asio/steady_timer.hpp>
#include <rtc/rtcpnackresponder.hpp>
#include <rtc/rtcpreceivingsession.hpp>
#include <rtc/rtcpsrreporter.hpp>
#include <rtc/rtppacketizationconfig.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace driscord {

struct VoiceRouter::Impl final : std::enable_shared_from_this<Impl> {
    struct OutputSlot {
        std::string mid;
        uint32_t ssrc = 0;
        std::weak_ptr<rtc::Track> track;
        std::optional<PeerId> publisher;
        std::shared_ptr<sfu::RtpSlotRewriter> rewriter;
        uint64_t source_generation = 0;
    };

    struct PeerState {
        uint64_t order = 0;
        std::weak_ptr<rtc::Track> input;
        std::optional<uint8_t> input_mid_extension_id;
        sfu::RtpFaultState input_fault_state;
        sfu::LinkModelState input_link_state;
        std::vector<OutputSlot> outputs;
        BindingSender send_binding;
    };

    struct DelayedPacket {
        PeerId publisher;
        std::weak_ptr<rtc::Track> expected_input;
        rtc::binary packet;
    };

    struct BindingNotice {
        BindingSender send;
        std::string mid;
        std::optional<PeerId> publisher;
        std::weak_ptr<rtc::Track> output;
        uint32_t ssrc = 0;
    };

    std::mutex publish_mutex;
    mutable std::mutex mutex;
    std::unordered_map<PeerId, PeerState> peers;
    sfu::RtpFaultConfig fault_config;
    uint64_t next_peer_order = 0;
    bool closed = false;
    std::optional<boost::asio::any_io_executor> executor;
    std::optional<boost::asio::steady_timer> delay_timer;
    std::multimap<int64_t, DelayedPacket> delay_queue;
    std::atomic<uint64_t> packets_in { 0 };
    std::atomic<uint64_t> packets_out { 0 };
    std::atomic<uint64_t> bytes_out { 0 };

    explicit Impl(sfu::RtpFaultConfig config,
        std::optional<boost::asio::any_io_executor> timer_executor)
        : fault_config(std::move(config))
        , executor(std::move(timer_executor))
    {
    }

    static int64_t now_us()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
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

    void register_track(const PeerId& owner,
        const std::shared_ptr<rtc::Track>& track,
        BindingSender send_binding)
    {
        if (!track) {
            return;
        }
        const auto description = track->description();
        if (description.type() != "audio") {
            return;
        }

        if (track->direction() == rtc::Description::Direction::RecvOnly) {
            register_input(owner, track, description, std::move(send_binding));
        } else if (track->direction()
            == rtc::Description::Direction::SendOnly) {
            register_output(owner, track, description, std::move(send_binding));
        }
    }

    void register_input(const PeerId& owner,
        const std::shared_ptr<rtc::Track>& track,
        rtc::Description::Media description,
        BindingSender send_binding)
    {
        sfu::apply_forwarding_feedback_policy(description);
        track->setDescription(description);
        track->setMediaHandler(std::make_shared<rtc::RtcpReceivingSession>());
        const std::weak_ptr<Impl> weak = weak_from_this();
        const std::weak_ptr<rtc::Track> weak_track = track;
        track->onMessage(
            [weak, weak_track, owner](rtc::binary packet) mutable {
                if (auto self = weak.lock()) {
                    self->route(owner, weak_track, std::move(packet));
                }
            },
            nullptr);

        std::scoped_lock publish_lock(publish_mutex);
        std::vector<BindingNotice> notices;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            auto& peer = peer_for_locked(owner);
            const bool source_changed = peer.input.lock() != track;
            if (source_changed) {
                peer.input_fault_state = { };
                peer.input_link_state = { };
            }
            peer.input = track;
            peer.input_mid_extension_id = sfu::mid_extension_id(description);
            peer.send_binding = std::move(send_binding);
            if (source_changed) {
                for (auto& [_, subscriber] : peers) {
                    for (auto& output : subscriber.outputs) {
                        if (output.publisher
                            && *output.publisher == owner) {
                            output.source_generation
                                = output.rewriter->begin_source();
                            notices.push_back({ subscriber.send_binding,
                                output.mid, owner, output.track, output.ssrc });
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

    void register_output(const PeerId& owner,
        const std::shared_ptr<rtc::Track>& track,
        rtc::Description::Media description,
        BindingSender send_binding)
    {
        sfu::apply_forwarding_feedback_policy(description);
        const uint32_t ssrc = sfu::allocate_slot_ssrc();
        const std::string mid = track->mid();
        const std::string cname = "driscord-voice-slot-"
            + std::to_string(ssrc);
        description.clearSSRCs();
        description.addSSRC(ssrc, cname, "driscord-voice", cname);
        track->setDescription(description);
        track->onMessage([](rtc::binary) { }, nullptr);

        std::scoped_lock publish_lock(publish_mutex);
        std::vector<BindingNotice> notices;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            auto& peer = peer_for_locked(owner);
            peer.send_binding = std::move(send_binding);
            auto existing = std::find_if(peer.outputs.begin(), peer.outputs.end(),
                [&mid](const OutputSlot& slot) { return slot.mid == mid; });
            if (existing == peer.outputs.end()) {
                peer.outputs.push_back(OutputSlot { mid, ssrc, track,
                    std::nullopt,
                    std::make_shared<sfu::RtpSlotRewriter>(48'000, 960), 0 });
            } else {
                if (existing->publisher) {
                    existing->rewriter->end_source();
                    notices.push_back({ peer.send_binding, existing->mid,
                        std::nullopt, existing->track, existing->ssrc });
                }
                existing->ssrc = ssrc;
                existing->track = track;
                existing->publisher.reset();
                existing->rewriter
                    = std::make_shared<sfu::RtpSlotRewriter>(48'000, 960);
                existing->source_generation = 0;
            }
            auto assigned = assign_available_locked();
            notices.insert(notices.end(),
                std::make_move_iterator(assigned.begin()),
                std::make_move_iterator(assigned.end()));
        }
        publish(std::move(notices));
    }

    std::vector<BindingNotice> assign_available_locked()
    {
        std::vector<BindingNotice> notices;
        const auto ordered_peers = ordered_peers_locked();
        for (const auto& subscriber_id : ordered_peers) {
            auto& subscriber = peers.at(subscriber_id);
            std::unordered_set<PeerId> assigned;
            for (const auto& output : subscriber.outputs) {
                if (output.publisher) {
                    assigned.insert(*output.publisher);
                }
            }

            for (const auto& publisher_id : ordered_peers) {
                const auto& publisher = peers.at(publisher_id);
                if (publisher_id == subscriber_id || publisher.input.expired()
                    || assigned.contains(publisher_id)) {
                    continue;
                }
                auto slot = std::find_if(subscriber.outputs.begin(),
                    subscriber.outputs.end(), [](const OutputSlot& output) {
                        return !output.publisher && !output.track.expired();
                    });
                if (slot == subscriber.outputs.end()) {
                    break;
                }
                slot->publisher = publisher_id;
                slot->source_generation = slot->rewriter->begin_source();
                assigned.insert(publisher_id);
                notices.push_back({ subscriber.send_binding, slot->mid,
                    publisher_id, slot->track, slot->ssrc });
            }
        }
        return notices;
    }

    void publish(std::vector<BindingNotice> notices)
    {
        for (auto& notice : notices) {
            if (auto output = notice.output.lock()) {
                if (notice.publisher) {
                    const auto description = output->description();
                    const auto format = sfu::primary_rtp_format(
                        description, "audio");
                    const std::string cname = "driscord-voice-slot-"
                        + std::to_string(notice.ssrc);
                    auto config = std::make_shared<rtc::RtpPacketizationConfig>(
                        notice.ssrc, cname, format.payload_type,
                        format.clock_rate);
                    auto reporter = std::make_shared<rtc::RtcpSrReporter>(config);
                    reporter->addToChain(
                        std::make_shared<rtc::RtcpNackResponder>());
                    output->setMediaHandler(std::move(reporter));
                } else {
                    output->setMediaHandler(nullptr);
                }
            }
            if (notice.send) {
                notice.send(std::move(notice.mid), std::move(notice.publisher));
            }
        }
    }

    struct Target {
        std::weak_ptr<rtc::Track> track;
        uint32_t ssrc = 0;
        std::shared_ptr<sfu::RtpSlotRewriter> rewriter;
        uint64_t source_generation = 0;
    };

    std::vector<Target> collect_targets_locked(const PeerId& publisher) const
    {
        std::vector<Target> targets;
        for (const auto& [_, subscriber] : peers) {
            for (const auto& output : subscriber.outputs) {
                if (output.publisher && *output.publisher == publisher) {
                    targets.push_back({ output.track, output.ssrc,
                        output.rewriter, output.source_generation });
                }
            }
        }
        return targets;
    }

    void send_packets(const std::vector<Target>& targets,
        const std::optional<uint8_t>& mid_id,
        const std::vector<rtc::binary>& outgoing)
    {
        for (const auto& routed_packet : outgoing) {
            for (const auto& target : targets) {
                auto output = target.track.lock();
                if (!output || !output->isOpen()) {
                    continue;
                }
                rtc::binary rewritten = routed_packet;
                if (target.rewriter
                    && target.rewriter->rewrite(rewritten,
                        target.source_generation, target.ssrc, mid_id)) {
                    const size_t size = rewritten.size();
                    try {
                        output->send(std::move(rewritten));
                        packets_out.fetch_add(1, std::memory_order_relaxed);
                        bytes_out.fetch_add(size, std::memory_order_relaxed);
                    } catch (const std::exception& error) {
                        LOG_WARNING()
                            << "voice SFU send failed: " << error.what();
                    }
                }
            }
        }
    }

    void arm_delay_timer_locked()
    {
        if (!executor) {
            return;
        }
        if (delay_queue.empty()) {
            if (delay_timer) {
                delay_timer->cancel();
            }
            return;
        }
        if (!delay_timer) {
            delay_timer.emplace(*executor);
        }
        delay_timer->expires_at(std::chrono::steady_clock::time_point(
            std::chrono::microseconds(delay_queue.begin()->first)));
        delay_timer->async_wait(
            [weak = weak_from_this()](const boost::system::error_code& error) {
                if (error) {
                    return;
                }
                if (auto self = weak.lock()) {
                    self->flush_delayed();
                }
            });
    }

    void flush_delayed()
    {
        std::vector<DelayedPacket> due;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            const int64_t now = now_us();
            while (!delay_queue.empty()
                && delay_queue.begin()->first <= now) {
                due.push_back(std::move(delay_queue.begin()->second));
                delay_queue.erase(delay_queue.begin());
            }
            arm_delay_timer_locked();
        }
        for (auto& item : due) {
            forward_delayed(item);
        }
    }

    void forward_delayed(DelayedPacket& item)
    {
        std::vector<Target> targets;
        std::optional<uint8_t> mid_id;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            const auto source = peers.find(item.publisher);
            if (source == peers.end()
                || source->second.input.lock() != item.expected_input.lock()) {
                return;
            }
            auto& queued = source->second.input_link_state.queued_packets;
            if (queued > 0) {
                --queued;
            }
            mid_id = source->second.input_mid_extension_id;
            targets = collect_targets_locked(item.publisher);
        }
        std::vector<rtc::binary> outgoing;
        outgoing.push_back(std::move(item.packet));
        send_packets(targets, mid_id, outgoing);
    }

    void route(const PeerId& publisher,
        const std::weak_ptr<rtc::Track>& expected_input,
        rtc::binary packet)
    {
        std::vector<Target> targets;
        std::optional<uint8_t> mid_id;
        std::vector<rtc::binary> immediate;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            const auto source = peers.find(publisher);
            if (source == peers.end()
                || source->second.input.lock() != expected_input.lock()) {
                return;
            }
            mid_id = source->second.input_mid_extension_id;
            const bool rtcp = rtc::IsRtcp(packet);
            if (!rtcp) {
                packets_in.fetch_add(1, std::memory_order_relaxed);
                if (fault_config.link_down) {
                    return;
                }
            }
            auto packets = sfu::apply_rtp_faults(fault_config,
                source->second.input_fault_state, std::move(packet));
            for (auto* faulted : { &packets.first, &packets.second }) {
                if (!*faulted) {
                    continue;
                }
                if (rtcp || !fault_config.link.enabled()) {
                    immediate.push_back(std::move(**faulted));
                    continue;
                }
                const int64_t now = now_us();
                const auto scheduled = sfu::schedule_packet_departure(
                    fault_config.link, source->second.input_link_state, now,
                    (*faulted)->size());
                if (!scheduled) {
                    continue;
                }
                const size_t copies = scheduled->duplicate ? 2u : 1u;
                if (scheduled->departure_us <= now || !executor) {
                    for (size_t i = 0; i < copies; ++i) {
                        immediate.push_back(**faulted);
                    }
                } else {
                    for (size_t i = 0; i < copies; ++i) {
                        delay_queue.emplace(scheduled->departure_us,
                            DelayedPacket { publisher, expected_input,
                                **faulted });
                    }
                    arm_delay_timer_locked();
                }
            }
            if (immediate.empty()) {
                return;
            }
            targets = collect_targets_locked(publisher);
        }
        send_packets(targets, mid_id, immediate);
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
                for (auto& output : subscriber.outputs) {
                    if (output.publisher && *output.publisher == peer_id) {
                        output.rewriter->end_source();
                        output.source_generation = 0;
                        output.publisher.reset();
                        notices.push_back({ subscriber.send_binding, output.mid,
                            std::nullopt, output.track, output.ssrc });
                    }
                }
            }
            auto reassigned = assign_available_locked();
            notices.insert(notices.end(),
                std::make_move_iterator(reassigned.begin()),
                std::make_move_iterator(reassigned.end()));
        }
        publish(std::move(notices));
    }

    VoiceRouter::Stats stats() const
    {
        VoiceRouter::Stats result;
        {
            std::scoped_lock lock(mutex);
            for (const auto& [_, peer] : peers) {
                if (!peer.input.expired()) {
                    ++result.publishers;
                }
                for (const auto& output : peer.outputs) {
                    if (output.publisher) {
                        ++result.bound_slots;
                    }
                }
            }
        }
        result.packets_in = packets_in.load(std::memory_order_relaxed);
        result.packets_out = packets_out.load(std::memory_order_relaxed);
        result.bytes_out = bytes_out.load(std::memory_order_relaxed);
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
            delay_queue.clear();
            if (delay_timer) {
                delay_timer->cancel();
            }
            for (auto& [_, peer] : peers) {
                if (auto input = peer.input.lock()) {
                    tracks.push_back(std::move(input));
                }
                for (auto& output : peer.outputs) {
                    if (auto track = output.track.lock()) {
                        tracks.push_back(std::move(track));
                    }
                }
            }
            peers.clear();
        }

        for (const auto& track : tracks) {
            track->onMessage(nullptr);
            track->setMediaHandler(nullptr);
        }
    }
};

VoiceRouter::VoiceRouter(sfu::RtpFaultConfig fault_config,
    std::optional<boost::asio::any_io_executor> executor)
    : impl_(std::make_shared<Impl>(std::move(fault_config), std::move(executor)))
{
}

VoiceRouter::~VoiceRouter()
{
    close();
}

void VoiceRouter::update_fault_config(sfu::RtpFaultConfig fault_config)
{
    std::scoped_lock lock(impl_->mutex);
    impl_->fault_config = std::move(fault_config);
}

void VoiceRouter::register_track(const PeerId& owner,
    std::shared_ptr<rtc::Track> track,
    BindingSender send_binding)
{
    impl_->register_track(owner, track, std::move(send_binding));
}

void VoiceRouter::remove_peer(const PeerId& peer_id)
{
    impl_->remove_peer(peer_id);
}

VoiceRouter::Stats VoiceRouter::stats() const
{
    return impl_ ? impl_->stats() : Stats { };
}

void VoiceRouter::close()
{
    if (impl_) {
        impl_->close();
    }
}

}
