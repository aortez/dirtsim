#include "H264Encoder.h"

#include <chrono>
#include <cstring>
#include <libyuv.h>
#include <spdlog/spdlog.h>
#include <wels/codec_api.h>
#include <wels/codec_app_def.h>

namespace DirtSim {

H264Encoder::H264Encoder() = default;

H264Encoder::~H264Encoder()
{
    if (encoder_) {
        encoder_->Uninitialize();
        WelsDestroySVCEncoder(encoder_);
        encoder_ = nullptr;
    }
}

H264Encoder::H264Encoder(H264Encoder&& other) noexcept
    : encoder_(other.encoder_),
      width_(other.width_),
      height_(other.height_),
      forceNextKeyframe_(other.forceNextKeyframe_),
      yuvBuffer_(std::move(other.yuvBuffer_))
{
    other.encoder_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
}

H264Encoder& H264Encoder::operator=(H264Encoder&& other) noexcept
{
    if (this != &other) {
        if (encoder_) {
            encoder_->Uninitialize();
            WelsDestroySVCEncoder(encoder_);
        }
        encoder_ = other.encoder_;
        width_ = other.width_;
        height_ = other.height_;
        forceNextKeyframe_ = other.forceNextKeyframe_;
        yuvBuffer_ = std::move(other.yuvBuffer_);

        other.encoder_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

bool H264Encoder::initialize(
    uint32_t width, uint32_t height, uint32_t targetBitrate, float frameRate)
{
    // Clean up any existing encoder.
    if (encoder_) {
        encoder_->Uninitialize();
        WelsDestroySVCEncoder(encoder_);
        encoder_ = nullptr;
    }

    // Create encoder.
    int rv = WelsCreateSVCEncoder(&encoder_);
    if (rv != 0 || !encoder_) {
        spdlog::error("H264Encoder: Failed to create OpenH264 encoder");
        return false;
    }

    // Round dimensions to even (H.264 requires even dimensions for 4:2:0 chroma subsampling).
    uint32_t evenWidth = width & ~1u;
    uint32_t evenHeight = height & ~1u;

    // Get default parameters and customize.
    SEncParamExt param;
    encoder_->GetDefaultParams(&param);

    param.iUsageType = SCREEN_CONTENT_REAL_TIME; // Optimized for screen capture.
    param.iPicWidth = static_cast<int>(evenWidth);
    param.iPicHeight = static_cast<int>(evenHeight);
    param.iTargetBitrate = static_cast<int>(targetBitrate);
    param.fMaxFrameRate = frameRate;
    param.iTemporalLayerNum = 1;
    param.iSpatialLayerNum = 1;
    param.bEnableDenoise = false;
    param.bEnableFrameSkip = false;
    param.iComplexityMode = LOW_COMPLEXITY;
    param.uiIntraPeriod = 60; // Keyframe every 60 frames (~2 sec at 30fps).
    param.eSpsPpsIdStrategy = CONSTANT_ID;
    param.bPrefixNalAddingCtrl = false;
    param.iEntropyCodingModeFlag = 0; // CAVLC (faster than CABAC).

    // Single spatial layer configuration.
    param.sSpatialLayers[0].iVideoWidth = static_cast<int>(evenWidth);
    param.sSpatialLayers[0].iVideoHeight = static_cast<int>(evenHeight);
    param.sSpatialLayers[0].fFrameRate = frameRate;
    param.sSpatialLayers[0].iSpatialBitrate = static_cast<int>(targetBitrate);
    param.sSpatialLayers[0].iMaxSpatialBitrate = static_cast<int>(targetBitrate * 2);
    param.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;

    rv = encoder_->InitializeExt(&param);
    if (rv != 0) {
        spdlog::error("H264Encoder: Failed to initialize encoder (rv={})", rv);
        WelsDestroySVCEncoder(encoder_);
        encoder_ = nullptr;
        return false;
    }

    // Set input format to I420.
    int videoFormat = videoFormatI420;
    encoder_->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat);

    width_ = evenWidth;
    height_ = evenHeight;

    // Allocate I420 buffer: Y = width*height, U = V = width*height/4.
    size_t ySize = evenWidth * evenHeight;
    size_t uvSize = (evenWidth / 2) * (evenHeight / 2);
    yuvBuffer_.resize(ySize + uvSize * 2);

    spdlog::info(
        "H264Encoder: Initialized {}x{} @ {}kbps, {}fps (rounded from {}x{})",
        evenWidth,
        evenHeight,
        targetBitrate / 1000,
        frameRate,
        width,
        height);

    return true;
}

bool H264Encoder::isInitialized() const
{
    return encoder_ != nullptr;
}

void H264Encoder::convertArgbToI420(const uint8_t* argb, uint32_t width, uint32_t height)
{
    // Use libyuv for fast, correct ARGB → I420 conversion.
    // Handles cropping/scaling automatically if input != encoder dimensions.

    size_t ySize = width_ * height_;
    size_t uvSize = (width_ / 2) * (height_ / 2);

    uint8_t* yPlane = yuvBuffer_.data();
    uint8_t* uPlane = yPlane + ySize;
    uint8_t* vPlane = uPlane + uvSize;

    // LVGL uses ARGB8888 which is B,G,R,A in little-endian memory.
    // libyuv calls this "ARGB little endian (bgra in memory)".
    int result = libyuv::ARGBToI420(
        argb,
        static_cast<int>(width * 4), // src + stride
        yPlane,
        static_cast<int>(width_), // Y dst + stride
        uPlane,
        static_cast<int>(width_ / 2), // U dst + stride
        vPlane,
        static_cast<int>(width_ / 2),               // V dst + stride
        static_cast<int>(std::min(width, width_)),  // width (crop if needed)
        static_cast<int>(std::min(height, height_)) // height (crop if needed)
    );

    if (result != 0) {
        spdlog::error("H264Encoder: libyuv ARGBToI420 failed (result={})", result);
    }
}

std::optional<EncodedFrame> H264Encoder::encode(
    const uint8_t* argbData, uint32_t width, uint32_t height, bool forceKeyframe)
{
    if (!encoder_) {
        spdlog::error("H264Encoder: Encoder not initialized");
        return std::nullopt;
    }

    // Accept input dimensions that are >= encoder dimensions (will crop to even).
    if (width < width_ || height < height_) {
        spdlog::error(
            "H264Encoder: Input too small (expected at least {}x{}, got {}x{})",
            width_,
            height_,
            width,
            height);
        return std::nullopt;
    }

    // Convert ARGB to I420.
    convertArgbToI420(argbData, width, height);

    // Force keyframe if requested.
    if (forceKeyframe || forceNextKeyframe_) {
        encoder_->ForceIntraFrame(true);
        forceNextKeyframe_ = false;
    }

    // Prepare source picture using encoder's even dimensions.
    SSourcePicture pic;
    std::memset(&pic, 0, sizeof(SSourcePicture));
    pic.iPicWidth = static_cast<int>(width_);
    pic.iPicHeight = static_cast<int>(height_);
    pic.iColorFormat = videoFormatI420;
    pic.iStride[0] = static_cast<int>(width_);
    pic.iStride[1] = static_cast<int>(width_ / 2);
    pic.iStride[2] = static_cast<int>(width_ / 2);
    pic.pData[0] = yuvBuffer_.data();
    pic.pData[1] = pic.pData[0] + (width_ * height_);
    pic.pData[2] = pic.pData[1] + (width_ * height_ / 4);

    // Encode.
    SFrameBSInfo info;
    std::memset(&info, 0, sizeof(SFrameBSInfo));

    int rv = encoder_->EncodeFrame(&pic, &info);
    if (rv != cmResultSuccess) {
        spdlog::error("H264Encoder: EncodeFrame failed (rv={})", rv);
        return std::nullopt;
    }

    // Check if frame was skipped.
    if (info.eFrameType == videoFrameTypeSkip) {
        spdlog::debug("H264Encoder: Frame skipped");
        return std::nullopt;
    }

    // Collect all NAL units into output buffer.
    EncodedFrame result;
    result.isKeyframe = (info.eFrameType == videoFrameTypeIDR);
    result.timestampMs =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count());

    // Calculate total size.
    size_t totalSize = 0;
    for (int layer = 0; layer < info.iLayerNum; ++layer) {
        const SLayerBSInfo& layerInfo = info.sLayerInfo[layer];
        for (int nal = 0; nal < layerInfo.iNalCount; ++nal) {
            totalSize += layerInfo.pNalLengthInByte[nal];
        }
    }

    result.data.reserve(totalSize);

    // Copy NAL units.
    for (int layer = 0; layer < info.iLayerNum; ++layer) {
        const SLayerBSInfo& layerInfo = info.sLayerInfo[layer];
        uint8_t* nalData = layerInfo.pBsBuf;
        for (int nal = 0; nal < layerInfo.iNalCount; ++nal) {
            int nalLen = layerInfo.pNalLengthInByte[nal];
            result.data.insert(result.data.end(), nalData, nalData + nalLen);
            nalData += nalLen;
        }
    }

    spdlog::debug(
        "H264Encoder: Encoded frame {} bytes, keyframe={}", result.data.size(), result.isKeyframe);

    return result;
}

void H264Encoder::requestKeyframe()
{
    forceNextKeyframe_ = true;
}

} // namespace DirtSim
