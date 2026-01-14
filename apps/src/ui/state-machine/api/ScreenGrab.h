#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <zpp_bits.h>

namespace DirtSim {
namespace UiApi {

namespace ScreenGrab {

DEFINE_API_NAME(ScreenGrab);

struct Okay;

// Output format for screen capture.
enum class Format : uint8_t {
    Raw = 0,  // ARGB8888 raw pixel data.
    H264 = 1, // H.264 encoded video frame.
    Png = 2   // PNG compressed image.
};

// JSON serialization for Format enum (string-based for readability).
inline void to_json(nlohmann::json& j, const Format& f)
{
    switch (f) {
        case Format::Raw:
            j = "raw";
            break;
        case Format::H264:
            j = "h264";
            break;
        case Format::Png:
            j = "png";
            break;
        default:
            j = "raw";
            break;
    }
}

inline void from_json(const nlohmann::json& j, Format& f)
{
    if (j.is_string()) {
        std::string s = j.get<std::string>();
        if (s == "h264") {
            f = Format::H264;
        }
        else if (s == "png") {
            f = Format::Png;
        }
        else {
            f = Format::Raw;
        }
    }
    else if (j.is_number()) {
        f = static_cast<Format>(j.get<uint8_t>());
    }
    else {
        f = Format::Raw;
    }
}

struct Command {
    double scale = 1.0;          // Resolution scale factor (0.25 = 4x smaller, 1.0 = full res).
    Format format = Format::Png; // Output format: Raw (ARGB8888), H264, or Png.
    int quality = 23;            // H.264 CRF quality (0-51, lower = better). Ignored for Raw/Png.

    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<3>;
};

struct Okay {
    std::string data; // Base64-encoded image data (raw ARGB8888 or H.264 NAL units).
    uint32_t width;
    uint32_t height;
    Format format;          // Format of data: Raw or H264.
    uint64_t timestampMs;   // Frame capture timestamp (milliseconds since epoch).
    bool isKeyframe = true; // True if this is a complete frame (always true for Raw).

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<6>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace ScreenGrab
} // namespace UiApi
} // namespace DirtSim
