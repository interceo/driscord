#include <rtc/rtc.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <variant>

namespace {

constexpr size_t kPayloadSize = 32 * 1024;

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: test_libdatachannel_wss_client <wss-url>\n";
        return 2;
    }

    std::mutex mutex;
    std::condition_variable changed;
    std::atomic_bool finished = false;
    bool success = false;
    std::string error;

    rtc::InitLogger(rtc::LogLevel::Verbose,
        [](rtc::LogLevel level, std::string message) {
            std::cerr << level << ": " << message << '\n';
        });
    rtc::WebSocket::Configuration config;
    config.disableTlsVerification = true;
    config.pingInterval = std::chrono::milliseconds::zero();
    // Keep callback state alive until after the socket and its asynchronous
    // callbacks have been destroyed.
    rtc::WebSocket socket(config);

    auto finish = [&](bool ok, std::string message = { }) {
        if (finished.exchange(true)) {
            return;
        }
        {
            std::scoped_lock lock(mutex);
            success = ok;
            error = std::move(message);
        }
        changed.notify_one();
    };

    socket.onOpen([&] {
        const std::string prefix = R"({"type":"probe","padding":")";
        const std::string suffix = R"("})";
        std::string payload = prefix;
        payload.append(kPayloadSize - prefix.size() - suffix.size(), 'a');
        payload += suffix;
        // A false return only means the lower transport buffered the frame;
        // delivery still proceeds asynchronously.
        (void)socket.send(payload);
    });
    socket.onMessage([&](rtc::message_variant message) {
        const auto* text = std::get_if<std::string>(&message);
        finish(text && *text == "ok", "unexpected WebSocket acknowledgement");
    });
    socket.onError(
        [&](std::string message) { finish(false, std::move(message)); });
    socket.onClosed([&] { finish(false, "WebSocket closed before acknowledgement"); });

    socket.open(argv[1]);
    {
        std::unique_lock lock(mutex);
        changed.wait_for(lock, std::chrono::seconds(10), [&] {
            return finished.load();
        });
        if (!finished) {
            error = "timed out waiting for the WSS acknowledgement";
        }
    }
    socket.close();
    if (!success) {
        std::cerr << error << '\n';
        return 1;
    }
    return 0;
}
