#include "AudioManager.h"
#include "audio/network/CommandDeserializerJson.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace DirtSim {
namespace AudioProcess {

AudioManager::AudioManager(uint16_t port, const AudioEngineConfig& config)
    : port_(port), engineConfig_(config)
{
    setupWebSocketService();
}

Result<std::monostate, ApiError> AudioManager::start()
{
    auto startResult = engine_.start(engineConfig_);
    if (startResult.isError()) {
        return startResult;
    }

    auto listenResult = wsService_.listen(port_);
    if (listenResult.isError()) {
        engine_.stop();
        return Result<std::monostate, ApiError>::error(ApiError(listenResult.errorValue()));
    }

    LOG_INFO(Network, "dirtsim-audio WebSocket listening on port {}", port_);
    return Result<std::monostate, ApiError>::okay(std::monostate{});
}

void AudioManager::stop()
{
    wsService_.stopListening();
    engine_.stop();
}

void AudioManager::mainLoopRun()
{
    LOG_INFO(State, "Audio main loop running");
    while (!shouldExit_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    LOG_INFO(State, "Audio main loop exiting");
}

void AudioManager::requestExit()
{
    shouldExit_.store(true);
}

void AudioManager::setupWebSocketService()
{
    wsService_.registerHandler<AudioApi::NoteOn::Cwc>(
        [this](AudioApi::NoteOn::Cwc cwc) { handleNoteOn(cwc); });
    wsService_.registerHandler<AudioApi::NoteOff::Cwc>(
        [this](AudioApi::NoteOff::Cwc cwc) { handleNoteOff(cwc); });
    wsService_.registerHandler<AudioApi::StatusGet::Cwc>(
        [this](AudioApi::StatusGet::Cwc cwc) { handleStatusGet(cwc); });

    wsService_.setJsonDeserializer([](const std::string& json) -> std::any {
        CommandDeserializerJson deserializer;
        auto result = deserializer.deserialize(json);
        if (result.isError()) {
            throw std::runtime_error(result.errorValue().message);
        }
        return result.value();
    });

    wsService_.setJsonCommandDispatcher(
        [this](
            std::any cmdAny,
            std::shared_ptr<rtc::WebSocket> ws,
            uint64_t correlationId,
            Network::WebSocketService::HandlerInvoker invokeHandler) {
            AudioApi::AudioApiCommand cmdVariant = std::any_cast<AudioApi::AudioApiCommand>(cmdAny);

#define DISPATCH_AUDIO_CMD_WITH_RESP(NamespaceType)                                         \
    if (auto* cmd = std::get_if<NamespaceType::Command>(&cmdVariant)) {                     \
        NamespaceType::Cwc cwc;                                                             \
        cwc.command = *cmd;                                                                 \
        cwc.callback = [ws, correlationId](NamespaceType::Response&& resp) {                \
            ws->send(Network::makeJsonResponse(correlationId, resp).dump());                \
        };                                                                                  \
        auto payload = Network::serialize_payload(cwc.command);                             \
        invokeHandler(std::string(NamespaceType::Command::name()), payload, correlationId); \
        return;                                                                             \
    }

            DISPATCH_AUDIO_CMD_WITH_RESP(AudioApi::NoteOn);
            DISPATCH_AUDIO_CMD_WITH_RESP(AudioApi::NoteOff);
            DISPATCH_AUDIO_CMD_WITH_RESP(AudioApi::StatusGet);

#undef DISPATCH_AUDIO_CMD_WITH_RESP

            LOG_WARN(Network, "Unknown audio JSON command");
        });

    LOG_INFO(Network, "dirtsim-audio WebSocket handlers registered");
}

void AudioManager::handleNoteOn(AudioApi::NoteOn::Cwc cwc)
{
    const auto& cmd = cwc.command;
    const double attackSeconds = cmd.attack_ms / 1000.0;
    const double durationSeconds = cmd.duration_ms / 1000.0;
    const double releaseSeconds = cmd.release_ms / 1000.0;

    if (cmd.duration_ms <= 0.0) {
        cwc.sendResponse(AudioApi::NoteOn::Response::error(ApiError("duration_ms must be > 0")));
        return;
    }

    const uint32_t noteId = engine_.enqueueNoteOn(
        cmd.frequency_hz,
        cmd.amplitude,
        attackSeconds,
        durationSeconds,
        releaseSeconds,
        cmd.waveform,
        cmd.note_id);

    AudioApi::NoteOn::Okay response{ .accepted = true, .note_id = noteId };
    cwc.sendResponse(AudioApi::NoteOn::Response::okay(response));
}

void AudioManager::handleNoteOff(AudioApi::NoteOff::Cwc cwc)
{
    engine_.enqueueNoteOff(cwc.command.note_id);
    AudioApi::NoteOff::Okay response{ .released = true };
    cwc.sendResponse(AudioApi::NoteOff::Response::okay(response));
}

void AudioManager::handleStatusGet(AudioApi::StatusGet::Cwc cwc)
{
    const AudioStatus status = engine_.getStatus();
    AudioApi::StatusGet::Okay response{
        .active = status.active,
        .note_id = status.noteId,
        .frequency_hz = status.frequencyHz,
        .amplitude = status.amplitude,
        .envelope_level = status.envelopeLevel,
        .envelope_state = status.envelopeState,
        .waveform = status.waveform,
        .sample_rate = status.sampleRate,
        .device_name = status.deviceName,
    };
    cwc.sendResponse(AudioApi::StatusGet::Response::okay(response));
}

} // namespace AudioProcess
} // namespace DirtSim
