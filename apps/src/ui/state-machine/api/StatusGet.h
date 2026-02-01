#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/VariantSerializer.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include "ui/controls/IconRail.h"
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <zpp_bits.h>

namespace DirtSim {
namespace UiApi {

namespace StatusGet {

DEFINE_API_NAME(StatusGet);

struct Command {
    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);
};

struct NoStateDetails {
    using serialize = zpp::bits::members<0>;
};

struct TrainingStateDetails {
    bool trainingModalVisible = false;

    using serialize = zpp::bits::members<1>;
};

struct SynthStateDetails {
    int last_key_index = -1;
    bool last_key_is_black = false;

    using serialize = zpp::bits::members<2>;
};

using StateDetails = std::variant<NoStateDetails, TrainingStateDetails, SynthStateDetails>;

struct Okay {
    std::string state; // UI state machine current state.
    bool connected_to_server = false;
    std::string server_url;
    uint32_t display_width = 0;
    uint32_t display_height = 0;
    double fps = 0.0;

    // System health metrics.
    double cpu_percent = 0.0;
    double memory_percent = 0.0;
    Ui::IconId selected_icon = Ui::IconId::NONE;
    bool panel_visible = false;
    StateDetails state_details = NoStateDetails{};

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace StatusGet
} // namespace UiApi
} // namespace DirtSim
