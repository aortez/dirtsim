#include "AudioManager.h"
#include "core/LoggingChannels.h"
#include <SDL2/SDL.h>
#include <args.hxx>
#include <csignal>
#include <iostream>

using namespace DirtSim;

static AudioProcess::AudioManager* g_manager = nullptr;

void signalHandler(int signum)
{
    SLOG_INFO("Interrupt signal ({}) received, shutting down...", signum);
    if (g_manager) {
        g_manager->requestExit();
    }
}

int main(int argc, char** argv)
{
    args::ArgumentParser parser(
        "DirtSim Audio", "Audio synthesis process for beeps, tones, and music cues via WebSocket.");
    args::HelpFlag help(parser, "help", "Display this help menu", { 'h', "help" });
    args::ValueFlag<uint16_t> portArg(
        parser, "port", "WebSocket port (default: 6060)", { 'p', "port" });
    args::ValueFlag<std::string> deviceArg(
        parser, "device", "SDL audio device name (default: system default)", { "device" });
    args::ValueFlag<int> sampleRateArg(parser, "rate", "Sample rate (default: 48000)", { "rate" });
    args::ValueFlag<int> bufferFramesArg(
        parser, "buffer", "Buffer size in frames (default: 512)", { "buffer" });
    args::ValueFlag<int> channelsArg(
        parser, "audio-channels", "Channel count (default: 2)", { "audio-channels" });
    args::ValueFlag<std::string> logConfig(
        parser,
        "log-config",
        "Path to logging config JSON file (default: logging-config.json)",
        { "log-config" },
        "logging-config.json");
    args::ValueFlag<std::string> logChannels(
        parser,
        "channels",
        "Override log channels (e.g., network:debug,*:off)",
        { 'C', "channels" });
    args::Flag listDevices(
        parser,
        "list-devices",
        "List available SDL audio output devices and exit",
        { "list-devices" });

    try {
        parser.ParseCLI(argc, argv);
    }
    catch (const args::Help&) {
        std::cout << parser;
        return 0;
    }
    catch (const args::ParseError& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    const uint16_t port = portArg ? args::get(portArg) : 6060;

    LoggingChannels::initializeFromConfig(args::get(logConfig), "audio");
    if (logChannels) {
        LoggingChannels::configureFromString(args::get(logChannels));
        SLOG_INFO("Applied channel overrides: {}", args::get(logChannels));
    }

    if (listDevices) {
        const bool needsInit = !SDL_WasInit(SDL_INIT_AUDIO);
        if (needsInit && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            std::cerr << "SDL audio init failed: " << SDL_GetError() << std::endl;
            return 1;
        }

        const int driverCount = SDL_GetNumAudioDrivers();
        std::cout << "Audio drivers:" << std::endl;
        if (driverCount == 0) {
            std::cout << "  (none)" << std::endl;
        }
        else {
            for (int i = 0; i < driverCount; ++i) {
                const char* driver = SDL_GetAudioDriver(i);
                if (driver) {
                    std::cout << "  " << driver << std::endl;
                }
            }
        }
        const char* currentDriver = SDL_GetCurrentAudioDriver();
        std::cout << "Current driver: " << (currentDriver ? currentDriver : "(none)") << std::endl;

        const int deviceCount = SDL_GetNumAudioDevices(0);
        if (deviceCount < 0) {
            std::cerr << "SDL device enumeration failed: " << SDL_GetError() << std::endl;
        }
        else if (deviceCount == 0) {
            std::cout << "(no audio devices found)" << std::endl;
        }
        else {
            for (int i = 0; i < deviceCount; ++i) {
                const char* name = SDL_GetAudioDeviceName(i, 0);
                if (name) {
                    std::cout << name << std::endl;
                }
            }
        }

        if (needsInit) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        return deviceCount < 0 ? 1 : 0;
    }

    AudioProcess::AudioEngineConfig config;
    if (deviceArg) {
        config.deviceName = args::get(deviceArg);
    }
    if (sampleRateArg) {
        config.sampleRate = args::get(sampleRateArg);
    }
    if (bufferFramesArg) {
        config.bufferFrames = args::get(bufferFramesArg);
    }
    if (channelsArg) {
        config.channels = args::get(channelsArg);
    }

    AudioProcess::AudioManager manager(port, config);
    g_manager = &manager;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    auto startResult = manager.start();
    if (startResult.isError()) {
        SLOG_ERROR("Failed to start audio: {}", startResult.errorValue().message);
        return 1;
    }

    manager.mainLoopRun();
    manager.stop();
    SLOG_INFO("dirtsim-audio shut down cleanly");
    return 0;
}
