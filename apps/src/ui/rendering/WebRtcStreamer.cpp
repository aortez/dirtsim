#include "WebRtcStreamer.h"
#include "core/LoggingChannels.h"

#include "core/encoding/H264Encoder.h"
#include "ui/DisplayCapture.h"

#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

WebRtcStreamer::WebRtcStreamer()
{
    // Enable libdatachannel logging at Warning level (errors/warnings only).
    static bool loggerInitialized = false;
    if (!loggerInitialized) {
        rtc::InitLogger(rtc::LogLevel::Warning);
        loggerInitialized = true;
    }

    streamStartTime_ = std::chrono::steady_clock::now();
    LOG_INFO(Network, "Created");
}

WebRtcStreamer::~WebRtcStreamer()
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.clear();
    LOG_INFO(Network, "Destroyed");
}

void WebRtcStreamer::setDisplay(lv_display_t* display)
{
    display_ = display;
    LOG_INFO(Network, "Display set");
}

std::string WebRtcStreamer::initiateStream(
    const std::string& clientId, IceCandidateCallback onIceCandidate)
{
    LOG_INFO(Network, "Initiating stream for client {}", clientId);

    std::lock_guard<std::mutex> lock(clientsMutex_);

    // Remove existing connection if any.
    clients_.erase(clientId);

    // Configuration - no STUN needed for same network.
    rtc::Configuration config;
    config.disableAutoNegotiation = true;
    config.portRangeBegin = 0; // Use any available port.
    config.portRangeEnd = 0;

    auto pc = std::make_shared<rtc::PeerConnection>(config);

    // Set up state change callback.
    pc->onStateChange([clientId](rtc::PeerConnection::State state) {
        LOG_INFO(Network, "Client {} state: {}", clientId, static_cast<int>(state));

        if (state == rtc::PeerConnection::State::Disconnected
            || state == rtc::PeerConnection::State::Failed
            || state == rtc::PeerConnection::State::Closed) {
            LOG_INFO(Network, "Client {} connection closed", clientId);
            // Note: Cleanup handled by track close callback or manual removeClient()
        }
    });

    // ICE candidate callback - trickle ICE candidates to browser as they're gathered.
    pc->onLocalCandidate([clientId, onIceCandidate](rtc::Candidate candidate) {
        if (!onIceCandidate) {
            LOG_WARN(Network, "ICE callback null for client {}", clientId);
            return;
        }

        // Format ICE candidate as JSON for browser.
        nlohmann::json message = { { "type", "candidate" },
                                   { "clientId", clientId },
                                   { "candidate", std::string(candidate) },
                                   { "mid", candidate.mid() } };

        spdlog::info(
            "WebRtcStreamer: Sending ICE candidate for client {} (mid={})",
            clientId,
            candidate.mid());
        onIceCandidate(message.dump());
    });

    // Log gathering state changes for debugging.
    pc->onGatheringStateChange([clientId](rtc::PeerConnection::GatheringState state) {
        spdlog::info(
            "WebRtcStreamer: Client {} gathering state: {}", clientId, static_cast<int>(state));
    });

    // Add video track with H.264.
    const std::string cname = "video-stream";
    const std::string msid = "stream1";

    rtc::Description::Video video(cname, rtc::Description::Direction::SendOnly);
    video.addH264Codec(97); // Use PT 97 (common for H.264).
    video.addSSRC(kVideoSsrc, cname, msid, cname);

    auto track = pc->addTrack(video);

    // Set up RTP packetization.
    auto rtpConfig =
        std::make_shared<rtc::RtpPacketizationConfig>(kVideoSsrc, cname, 97, kClockRate);

    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::NalUnit::Separator::LongStartSequence, rtpConfig);

    // Add RTCP sender reports.
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
    packetizer->addToChain(srReporter);

    // Add NACK responder for packet loss recovery.
    auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nackResponder);

    track->setMediaHandler(packetizer);

    // Track open callback.
    track->onOpen([this, clientId]() {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clients_.find(clientId);
        if (it != clients_.end()) {
            it->second.ready = true;
            it->second.startTime = std::chrono::steady_clock::now();
            LOG_INFO(Network, "Video track open for client {}", clientId);
        }
    });

    track->onClosed(
        [clientId]() { LOG_INFO(Network, "Video track closed for client {}", clientId); });

    // Store client connection.
    ClientConnection conn;
    conn.pc = pc;
    conn.videoTrack = track;
    conn.rtpConfig = rtpConfig;
    conn.srReporter = srReporter;
    conn.onIceCandidate = onIceCandidate;
    conn.ready = false;

    clients_[clientId] = std::move(conn);

    // Set local description to generate offer and trigger ICE gathering.
    try {
        pc->setLocalDescription();
    }
    catch (const std::exception& e) {
        LOG_ERROR(Network, "Failed to create offer: {}", e.what());
        clients_.erase(clientId);
        return ""; // Return empty string on error.
    }

    // Get the SDP offer immediately (trickle ICE - candidates come separately).
    auto description = pc->localDescription();
    if (!description) {
        LOG_ERROR(Network, "No local description for {}", clientId);
        clients_.erase(clientId);
        return "";
    }

    std::string sdpOffer = std::string(description.value());
    spdlog::info(
        "WebRtcStreamer: Created offer for client {} ({} bytes)", clientId, sdpOffer.size());

    return sdpOffer;
}

void WebRtcStreamer::handleAnswer(const std::string& clientId, const std::string& sdpAnswer)
{
    LOG_INFO(Network, "Received answer from client {}", clientId);

    std::lock_guard<std::mutex> lock(clientsMutex_);

    auto it = clients_.find(clientId);
    if (it == clients_.end()) {
        LOG_WARN(Network, "Received answer for unknown client {}", clientId);
        return;
    }

    // Set remote description (the answer).
    try {
        it->second.pc->setRemoteDescription(
            rtc::Description(sdpAnswer, rtc::Description::Type::Answer));
        LOG_INFO(Network, "Set remote description (answer) for client {}", clientId);
    }
    catch (const std::exception& e) {
        LOG_ERROR(Network, "Failed to set remote description: {}", e.what());
        clients_.erase(clientId);
        return;
    }
}

void WebRtcStreamer::handleCandidate(
    const std::string& clientId, const std::string& candidate, const std::string& mid)
{
    std::lock_guard<std::mutex> lock(clientsMutex_);

    auto it = clients_.find(clientId);
    if (it == clients_.end()) {
        LOG_WARN(Network, "Received candidate for unknown client {}", clientId);
        return;
    }

    try {
        it->second.pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
        LOG_DEBUG(Network, "Added ICE candidate for client {}", clientId);
    }
    catch (const std::exception& e) {
        LOG_WARN(Network, "Failed to add candidate: {}", e.what());
    }
}

void WebRtcStreamer::removeClient(const std::string& clientId)
{
    std::lock_guard<std::mutex> lock(clientsMutex_);

    auto it = clients_.find(clientId);
    if (it != clients_.end()) {
        clients_.erase(it);
        spdlog::info(
            "WebRtcStreamer: Removed client {} (remaining: {})", clientId, clients_.size());
    }
}

void WebRtcStreamer::sendFrame()
{
    if (!display_) {
        return;
    }

    std::lock_guard<std::mutex> lock(clientsMutex_);

    // If no clients at all, skip.
    if (clients_.empty()) {
        return;
    }

    // Frame rate limiting - only send at target FPS (30fps = ~33ms interval).
    static auto lastFrameTime = std::chrono::steady_clock::now();
    auto currentTime = std::chrono::steady_clock::now();
    auto timeSinceLastFrame =
        std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastFrameTime);
    constexpr auto frameInterval = std::chrono::milliseconds(33); // ~30fps.

    if (timeSinceLastFrame < frameInterval) {
        return; // Too soon, skip this frame.
    }
    lastFrameTime = currentTime;

    // NOTE: We'll try to send frames even if not "ready" to trigger track opening.
    // Some WebRTC implementations need to see media flowing before onOpen fires.

    // Capture display pixels.
    auto screenshotData = captureDisplayPixels(display_, 1.0);
    if (!screenshotData) {
        return;
    }

    // Initialize encoder if needed.
    uint32_t evenWidth = screenshotData->width & ~1u;
    uint32_t evenHeight = screenshotData->height & ~1u;

    if (!encoder_ || encoder_->getWidth() != evenWidth || encoder_->getHeight() != evenHeight) {
        encoder_ = std::make_unique<H264Encoder>();
        // Use 5Mbps bitrate for rapidly-changing fractal content.
        // 500kbps was causing massive oversized frames that overflow send buffers.
        if (!encoder_->initialize(
                screenshotData->width, screenshotData->height, 5000000, kTargetFps)) {
            LOG_ERROR(Network, "Failed to initialize encoder");
            return;
        }
    }

    // Encode frame.
    auto encoded = encoder_->encode(
        screenshotData->pixels.data(), screenshotData->width, screenshotData->height);
    if (!encoded) {
        return;
    }

    // Calculate RTP timestamp.
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - streamStartTime_);
    auto rtpTimestamp = static_cast<uint32_t>((elapsed.count() * kClockRate) / 1'000'000);

    // Send to all clients (even if not "ready" - might help trigger track opening).
    for (auto& [id, client] : clients_) {
        if (!client.videoTrack) {
            continue;
        }

        if (!client.ready) {
            LOG_DEBUG(Network, "Attempting to send frame to non-ready client {}", id);
        }

        try {
            // Check buffered amount to prevent queue overflow.
            auto buffered = client.videoTrack->bufferedAmount();
            const size_t maxBuffered = 1'000'000; // 1MB max buffer.

            if (buffered > maxBuffered) {
                spdlog::warn(
                    "WebRtcStreamer: Dropping frames for {} (buffered={} bytes)", id, buffered);
                continue; // Skip this frame to let buffer drain.
            }

            // Update RTCP sender report timing.
            client.rtpConfig->timestamp = rtpTimestamp;

            // Send frame with FrameInfo (includes timestamp in microseconds).
            // This is CRITICAL - RTP packetizer needs timing info.
            auto elapsed_us = std::chrono::duration<double, std::micro>(elapsed);

            client.videoTrack->sendFrame(
                reinterpret_cast<const std::byte*>(encoded->data.data()),
                encoded->data.size(),
                rtc::FrameInfo(elapsed_us));

            spdlog::debug(
                "WebRtcStreamer: Sent frame to {} ({} bytes, ts={}, keyframe={})",
                id,
                encoded->data.size(),
                rtpTimestamp,
                encoded->isKeyframe);
        }
        catch (const std::exception& e) {
            LOG_WARN(Network, "Failed to send frame to {}: {}", id, e.what());
        }
    }

    frameCount_++;
}

bool WebRtcStreamer::hasClients() const
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return !clients_.empty();
}

size_t WebRtcStreamer::clientCount() const
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return clients_.size();
}

} // namespace Ui
} // namespace DirtSim
