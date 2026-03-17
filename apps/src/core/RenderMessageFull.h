#pragma once

#include "RenderMessage.h"
#include "ScenarioConfig.h"
#include "scenarios/nes/NesControllerTelemetry.h"
#include "scenarios/nes/NesSuperMarioBrosResponseTelemetry.h"

#include <optional>
#include <zpp_bits.h>

namespace DirtSim {

/**
 * @brief Full render message with scenario metadata for transport.
 *
 * This wrapper bundles RenderMessage with scenario state for network transmission.
 * By keeping scenario fields separate, we avoid RenderMessage.h depending on ScenarioConfig.h,
 * which reduces rebuild cascades when config headers change.
 */
struct RenderMessageFull {
    // Core render data.
    RenderMessage render_data;

    // Scenario metadata (sent alongside render data).
    Scenario::EnumType scenario_id = Scenario::EnumType::Empty;
    ScenarioConfig scenario_config = Config::Empty{};
    std::optional<NesControllerTelemetry> nes_controller_telemetry = std::nullopt;
    std::optional<NesSuperMarioBrosResponseTelemetry> nes_smb_response_telemetry = std::nullopt;
    uint64_t server_send_timestamp_ns = 0;

    using serialize = zpp::bits::members<6>;
};

} // namespace DirtSim
