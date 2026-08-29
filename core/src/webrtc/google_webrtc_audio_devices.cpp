#include "webrtc/google_webrtc_audio_devices.hpp"

#include "api/audio/audio_device.h"
#include "api/audio/audio_device_defines.h"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <utility>

namespace driscord::media::detail {
namespace {

    constexpr std::string_view kDefaultAudioDeviceId = "default";

    struct EnumeratedAudioDevice {
        AudioDeviceInfo info;
        uint16_t index = 0;
    };

    std::string make_audio_device_id(
        std::string_view guid, std::string_view name, size_t name_ordinal)
    {
        if (!guid.empty()) {
            return "guid:" + std::string(guid);
        }
        return "name:" + std::string(name) + "#" + std::to_string(name_ordinal);
    }

    bool ensure_initialized(webrtc::AudioDeviceModule& adm)
    {
        return adm.Initialized() || adm.Init() == 0;
    }

    std::vector<EnumeratedAudioDevice> enumerate_audio_devices(
        webrtc::AudioDeviceModule& adm, bool recording)
    {
        std::vector<EnumeratedAudioDevice> result;
        const int16_t count = recording ? adm.RecordingDevices()
                                        : adm.PlayoutDevices();
        if (count <= 0) {
            return result;
        }

        result.reserve(static_cast<size_t>(count));
        std::unordered_map<std::string, size_t> name_ordinals;
        for (int index = 0; index < count; ++index) {
            std::array<char, webrtc::kAdmMaxDeviceNameSize> name { };
            std::array<char, webrtc::kAdmMaxGuidSize> guid { };
            const int error = recording
                ? adm.RecordingDeviceName(
                      static_cast<uint16_t>(index), name.data(), guid.data())
                : adm.PlayoutDeviceName(
                      static_cast<uint16_t>(index), name.data(), guid.data());
            if (error != 0) {
                continue;
            }

            std::string display_name(name.data());
            if (display_name.empty()) {
                display_name = index == 0 ? "System default"
                                          : "Unnamed audio device";
            }
            if (index == 0) {
                result.push_back({
                    .info = {
                        .id = std::string(kDefaultAudioDeviceId),
                        .name = display_name == "System default"
                            ? display_name
                            : "System default (" + display_name + ")",
                    },
                    .index = 0,
                });
                continue;
            }

            const size_t ordinal = name_ordinals[display_name]++;
            result.push_back({
                .info = {
                    .id = make_audio_device_id(
                        guid.data(), display_name, ordinal),
                    .name = std::move(display_name),
                },
                .index = static_cast<uint16_t>(index),
            });
        }
        return result;
    }

    bool restart_on_device(webrtc::AudioDeviceModule& adm,
        bool recording,
        uint16_t index)
    {
        const bool was_active = recording ? adm.Recording() : adm.Playing();
        const auto initialize_endpoint = [&] {
            return (recording ? adm.InitMicrophone() : adm.InitSpeaker()) == 0;
        };
        const auto start_stream = [&] {
            const int init_error = recording ? adm.InitRecording()
                                             : adm.InitPlayout();
            return init_error == 0
                && (recording ? adm.StartRecording() : adm.StartPlayout()) == 0;
        };
        const auto restore_default = [&] {
            (void)(recording ? adm.StopRecording() : adm.StopPlayout());
            const int select_default = recording
                ? adm.SetRecordingDevice(0)
                : adm.SetPlayoutDevice(0);
            return select_default == 0 && initialize_endpoint()
                && (!was_active || start_stream());
        };
        if (was_active) {
            const int stop_error = recording ? adm.StopRecording()
                                             : adm.StopPlayout();
            if (stop_error != 0) {
                return false;
            }
        }

        const int select_error = recording ? adm.SetRecordingDevice(index)
                                           : adm.SetPlayoutDevice(index);
        if (select_error != 0) {
            if (was_active) {
                (void)start_stream();
            }
            return false;
        }

        if (!initialize_endpoint()) {
            (void)restore_default();
            return false;
        }
        if (!was_active) {
            return true;
        }
        if (start_stream()) {
            return true;
        }
        (void)restore_default();
        return false;
    }

}

std::vector<AudioDeviceInfo> list_audio_devices(
    webrtc::AudioDeviceModule& adm, bool recording)
{
    if (!ensure_initialized(adm)) {
        return { };
    }
    auto enumerated = enumerate_audio_devices(adm, recording);
    std::vector<AudioDeviceInfo> result;
    result.reserve(enumerated.size());
    for (auto& device : enumerated) {
        result.push_back(std::move(device.info));
    }
    return result;
}

bool select_audio_device(webrtc::AudioDeviceModule& adm,
    bool recording,
    std::string_view id)
{
    if (id.empty() || !ensure_initialized(adm)) {
        return false;
    }
    const auto devices = enumerate_audio_devices(adm, recording);
    const auto selected = std::find_if(devices.begin(), devices.end(),
        [id](const EnumeratedAudioDevice& device) {
            return device.info.id == id;
        });
    return selected != devices.end()
        && restart_on_device(adm, recording, selected->index);
}

}
