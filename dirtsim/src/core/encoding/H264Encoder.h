#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Forward declare OpenH264 types to avoid exposing the header.
class ISVCEncoder;

namespace DirtSim {

/**
 * @brief Result of encoding a single frame.
 */
struct EncodedFrame {
    std::vector<uint8_t> data; // H.264 NAL units (including start codes).
    bool isKeyframe = false;   // True if this is an IDR frame.
    uint64_t timestampMs = 0;  // Capture timestamp.
};

/**
 * @brief H.264 encoder wrapper using OpenH264.
 *
 * Encodes ARGB8888 frames to H.264 NAL units suitable for streaming.
 * The encoder is stateful and maintains context between frames for
 * temporal compression (P-frames reference previous frames).
 *
 * Usage:
 *   H264Encoder encoder;
 *   if (encoder.initialize(800, 600)) {
 *       auto result = encoder.encode(argbPixels, width, height);
 *       if (result) {
 *           // Send result->data to client.
 *       }
 *   }
 */
class H264Encoder {
public:
    H264Encoder();
    ~H264Encoder();

    // Non-copyable, movable.
    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;
    H264Encoder(H264Encoder&&) noexcept;
    H264Encoder& operator=(H264Encoder&&) noexcept;

    /**
     * @brief Initialize the encoder for a specific resolution.
     *
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param targetBitrate Target bitrate in bits/second (default 500kbps).
     * @param frameRate Target frame rate (default 30fps).
     * @return true if initialization succeeded.
     */
    bool initialize(
        uint32_t width, uint32_t height, uint32_t targetBitrate = 500000, float frameRate = 30.0f);

    /**
     * @brief Check if encoder is initialized.
     */
    bool isInitialized() const;

    /**
     * @brief Encode an ARGB8888 frame to H.264.
     *
     * @param argbData Raw ARGB8888 pixel data (4 bytes per pixel).
     * @param width Frame width (must match initialized width).
     * @param height Frame height (must match initialized height).
     * @param forceKeyframe If true, force this frame to be an IDR keyframe.
     * @return Encoded frame data, or nullopt on error.
     */
    std::optional<EncodedFrame> encode(
        const uint8_t* argbData, uint32_t width, uint32_t height, bool forceKeyframe = false);

    /**
     * @brief Force the next frame to be a keyframe.
     */
    void requestKeyframe();

    /**
     * @brief Get the configured width.
     */
    uint32_t getWidth() const { return width_; }

    /**
     * @brief Get the configured height.
     */
    uint32_t getHeight() const { return height_; }

private:
    // Convert ARGB8888 to I420 (YUV420 planar).
    void convertArgbToI420(const uint8_t* argb, uint32_t width, uint32_t height);

    ISVCEncoder* encoder_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool forceNextKeyframe_ = false;

    // I420 buffer (Y, U, V planes).
    std::vector<uint8_t> yuvBuffer_;
};

} // namespace DirtSim
