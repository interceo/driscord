#include "voice_router.hpp"

#include "log.hpp"
#include "rtp_slot_rewriter.hpp"
#include "sfu_media_utils.hpp"

#include <rtc/rtcpnackresponder.hpp>
#include <rtc/rtcpreceivingsession.hpp>
#include <rtc/rtcpsrreporter.hpp>
#include <rtc/rtppacketizationconfig.hpp>

#include <algorithm>
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
        std::weak_ptr<rtc::Track> input;
        std::optional<uint8_t> input_mid_extension_id;
        sfu::RtpFaultState input_fault_state;
        std::vector<OutputSlot> outputs;
        BindingSender send_binding;
    };

    struct BindingNotice {
        BindingSender send;
        std::string mid;
        std::optional<PeerId> publisher;
        std::weak_ptr<rtc::Track> output;
        uint32_t ssrc = 0;
    };

    std::mutex mutex;
    std::unordered_map<PeerId, PeerState> peers;
    sfu::RtpFaultConfig fault_config;
    bool closed = false;

    explicit Impl(sfu::RtpFaultConfig config)
        : fault_config(config)
    {
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

        std::vector<BindingNotice> notices;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            auto& peer = peers[owner];
            const bool source_changed = peer.input.lock() != track;
            if (source_changed) {
                peer.input_fault_state = { };
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

        std::vector<BindingNotice> notices;
        {
            std::scoped_lock lock(mutex);
            if (closed) {
                return;
            }
            auto& peer = peers[owner];
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
            notices = assign_available_locked();
        }
        publish(std::move(notices));
    }

    std::vector<BindingNotice> assign_available_locked()
    {
        std::vector<BindingNotice> notices;
        for (auto& [subscriber_id, subscriber] : peers) {
            std::unordered_set<PeerId> assigned;
            for (const auto& output : subscriber.outputs) {
                if (output.publisher) {
                    assigned.insert(*output.publisher);
                }
            }

            for (const auto& [publisher_id, publisher] : peers) {
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

    void route(const PeerId& publisher,
        const std::weak_ptr<rtc::Track>& expected_input,
        rtc::binary packet)
    {
        struct Target {
            std::weak_ptr<rtc::Track> track;
            uint32_t ssrc;
            std::shared_ptr<sfu::RtpSlotRewriter> rewriter;
            uint64_t source_generation;
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
            if (source == peers.end()
                || source->second.input.lock() != expected_input.lock()) {
                return;
            }
            mid_id = source->second.input_mid_extension_id;
            packets = sfu::apply_rtp_faults(fault_config,
                source->second.input_fault_state, std::move(packet));
            if (!packets.first) {
                return;
            }
            for (const auto& [_, subscriber] : peers) {
                for (const auto& output : subscriber.outputs) {
                    if (output.publisher && *output.publisher == publisher) {
                        targets.push_back({ output.track, output.ssrc,
                            output.rewriter, output.source_generation });
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
                if (target.rewriter
                    && target.rewriter->rewrite(rewritten,
                        target.source_generation, target.ssrc, mid_id)) {
                    try {
                        output->send(std::move(rewritten));
                    } catch (const std::exception& error) {
                        LOG_WARNING()
                            << "voice SFU send failed: " << error.what();
                    }
                }
            }
        }
    }

    void remove_peer(const PeerId& peer_id)
    {
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

    void close()
    {
        std::scoped_lock lock(mutex);
        if (closed) {
            return;
        }
        closed = true;
        for (auto& [_, peer] : peers) {
            if (auto input = peer.input.lock()) {
                input->onMessage(nullptr);
            }
            for (auto& output : peer.outputs) {
                if (auto track = output.track.lock()) {
                    track->onMessage(nullptr);
                    track->setMediaHandler(nullptr);
                }
            }
        }
        peers.clear();
    }
};

VoiceRouter::VoiceRouter(sfu::RtpFaultConfig fault_config)
    : impl_(std::make_shared<Impl>(fault_config))
{
}

VoiceRouter::~VoiceRouter()
{
    close();
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

void VoiceRouter::close()
{
    if (impl_) {
        impl_->close();
    }
}

} // namespace driscord
