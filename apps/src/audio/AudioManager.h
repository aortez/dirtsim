#pragma once

#include "AudioEngine.h"
#include "audio/api/NoteOff.h"
#include "audio/api/NoteOn.h"
#include "audio/api/StatusGet.h"
#include "core/Result.h"
#include "core/network/WebSocketService.h"
#include "server/api/ApiError.h"
#include <atomic>
#include <cstdint>
#include <string>

namespace DirtSim {
namespace AudioProcess {

class AudioManager {
public:
    AudioManager(uint16_t port, const AudioEngineConfig& config);

    Result<std::monostate, ApiError> start();
    void stop();
    void mainLoopRun();
    void requestExit();

private:
    void setupWebSocketService();
    void handleNoteOn(AudioApi::NoteOn::Cwc cwc);
    void handleNoteOff(AudioApi::NoteOff::Cwc cwc);
    void handleStatusGet(AudioApi::StatusGet::Cwc cwc);

    uint16_t port_ = 0;
    AudioEngineConfig engineConfig_{};
    AudioEngine engine_;
    Network::WebSocketService wsService_;
    std::atomic<bool> shouldExit_{ false };
};

} // namespace AudioProcess
} // namespace DirtSim
