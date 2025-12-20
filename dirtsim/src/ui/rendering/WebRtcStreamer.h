#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// Forward declarations for libdatachannel types.
namespace rtc {
class PeerConnection;
class Track;
class RtpPacketizationConfig;
class RtcpSrReporter;
} // namespace rtc

typedef struct _lv_display_t lv_display_t;

namespace DirtSim {

class H264Encoder;

namespace Ui {

/**
 * @brief Manages WebRTC video streaming to browser clients.
 *
 * Handles peer connections, video tracks, and H.264 frame transmission.
 * Uses libdatachannel for WebRTC implementation.
 *
 * Signaling flow:
 * 1. Browser sends StreamStart request via WebSocket
 * 2. Server creates PeerConnection, adds video track, returns SDP offer synchronously
 * 3. ICE candidates are sent to browser as they're gathered (via provided callback)
 * 4. Browser sends SDP answer via WebRtcAnswer command
 * 5. Connection established, frames flow via RTP
 */
class WebRtcStreamer {
public:
    // Callback to send ICE candidates to the client.
    using IceCandidateCallback = std::function<void(const std::string& candidateJson)>;

    WebRtcStreamer();
    ~WebRtcStreamer();

    // Non-copyable.
    WebRtcStreamer(const WebRtcStreamer&) = delete;
    WebRtcStreamer& operator=(const WebRtcStreamer&) = delete;

    /**
     * @brief Set the display to capture frames from.
     */
    void setDisplay(lv_display_t* display);

    /**
     * @brief Initiate streaming to a browser client.
     *
     * Creates peer connection, adds video track, and returns SDP offer synchronously.
     * ICE candidates will be sent via the provided callback as they're gathered.
     *
     * @param clientId Unique identifier for this client (from browser).
     * @param onIceCandidate Callback invoked for each ICE candidate to send to browser.
     * @return The SDP offer string for the browser to answer.
     */
    std::string initiateStream(const std::string& clientId, IceCandidateCallback onIceCandidate);

    /**
     * @brief Handle incoming SDP answer from browser client.
     *
     * @param clientId Client identifier.
     * @param sdpAnswer The SDP answer from the browser.
     */
    void handleAnswer(const std::string& clientId, const std::string& sdpAnswer);

    /**
     * @brief Handle incoming ICE candidate from browser.
     *
     * @param clientId Client identifier.
     * @param candidate ICE candidate string.
     * @param mid Media stream ID.
     */
    void handleCandidate(
        const std::string& clientId, const std::string& candidate, const std::string& mid);

    /**
     * @brief Remove a client connection.
     */
    void removeClient(const std::string& clientId);

    /**
     * @brief Capture and send a frame to all connected clients.
     *
     * Should be called regularly (e.g., 30fps) from the main loop.
     */
    void sendFrame();

    /**
     * @brief Check if any clients are connected.
     */
    bool hasClients() const;

    /**
     * @brief Get number of connected clients.
     */
    size_t clientCount() const;

private:
    struct ClientConnection {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::Track> videoTrack;
        std::shared_ptr<rtc::RtpPacketizationConfig> rtpConfig;
        std::shared_ptr<rtc::RtcpSrReporter> srReporter;
        IceCandidateCallback onIceCandidate; // Callback for sending ICE candidates.
        bool ready = false;
        std::chrono::steady_clock::time_point startTime;
    };

    lv_display_t* display_ = nullptr;
    std::unique_ptr<H264Encoder> encoder_;

    mutable std::mutex clientsMutex_;
    std::unordered_map<std::string, ClientConnection> clients_;

    // RTP timing.
    std::chrono::steady_clock::time_point streamStartTime_;
    uint32_t frameCount_ = 0;

    // Video parameters.
    static constexpr uint32_t kVideoSsrc = 42;
    static constexpr uint8_t kPayloadType = 96;   // Dynamic payload type for H.264.
    static constexpr uint32_t kClockRate = 90000; // Standard for video.
    static constexpr float kTargetFps = 30.0f;
};

} // namespace Ui
} // namespace DirtSim
