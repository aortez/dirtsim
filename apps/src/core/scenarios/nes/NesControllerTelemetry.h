#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <zpp_bits.h>

namespace DirtSim {

enum class NesGameAdapterControllerSource : uint8_t {
    InferredPolicy = 0,
    ScriptedSetup = 1,
    LiveInput = 2,
};

struct NesControllerTelemetry {
    float aRaw = 0.0f;
    float bRaw = 0.0f;
    float xRaw = 0.0f;
    float yRaw = 0.0f;
    uint8_t inferredControllerMask = 0;
    uint8_t resolvedControllerMask = 0;
    NesGameAdapterControllerSource controllerSource =
        NesGameAdapterControllerSource::InferredPolicy;
    std::optional<uint64_t> controllerSourceFrameIndex = std::nullopt;
    std::optional<uint64_t> controllerAppliedFrameId = std::nullopt;
    std::optional<uint64_t> controllerObservedTimestampNs = std::nullopt;
    std::optional<uint64_t> controllerLatchTimestampNs = std::nullopt;
    std::optional<uint64_t> controllerRequestTimestampNs = std::nullopt;
    std::optional<uint64_t> controllerSequenceId = std::nullopt;

    using serialize = zpp::bits::members<13>;
};

void to_json(nlohmann::json& j, const NesControllerTelemetry& value);
void from_json(const nlohmann::json& j, NesControllerTelemetry& value);

} // namespace DirtSim
