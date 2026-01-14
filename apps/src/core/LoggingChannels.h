#pragma once

// Enable all log levels for SPDLOG_LOGGER_* macros (must be before spdlog includes).
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#include <memory>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace DirtSim {

/**
 * @brief Available logging channels for categorizing log messages.
 */
enum class LogChannel {
    Brain,
    Collision,
    Cohesion,
    Controls,
    Friction,
    Network,
    Physics,
    Pressure,
    Render,
    Scenario,
    State,
    Support,
    Swap,
    Tree,
    Ui,
    Viscosity
};

inline const char* toString(LogChannel channel)
{
    switch (channel) {
        case LogChannel::Brain:
            return "brain";
        case LogChannel::Collision:
            return "collision";
        case LogChannel::Cohesion:
            return "cohesion";
        case LogChannel::Controls:
            return "controls";
        case LogChannel::Friction:
            return "friction";
        case LogChannel::Network:
            return "network";
        case LogChannel::Physics:
            return "physics";
        case LogChannel::Pressure:
            return "pressure";
        case LogChannel::Render:
            return "render";
        case LogChannel::Scenario:
            return "scenario";
        case LogChannel::State:
            return "state";
        case LogChannel::Support:
            return "support";
        case LogChannel::Swap:
            return "swap";
        case LogChannel::Tree:
            return "tree";
        case LogChannel::Ui:
            return "ui";
        case LogChannel::Viscosity:
            return "viscosity";
    }
    assert(false && "Unhandled LogChannel in switch");
    return "";
}

/**
 * @brief Centralized logging channel management for fine-grained log filtering.
 *
 * Provides named loggers for different subsystems to enable focused debugging
 * without log flooding from unrelated components.
 */
class LoggingChannels {
public:
    /**
     * @brief Initialize the logging system with shared sinks.
     * @param consoleLevel Default log level for console output
     * @param fileLevel Default log level for file output
     * @param componentName Component name for log file and pattern (e.g., "server", "ui")
     */
    static void initialize(
        spdlog::level::level_enum consoleLevel = spdlog::level::info,
        spdlog::level::level_enum fileLevel = spdlog::level::debug,
        const std::string& componentName = "default");

    /**
     * @brief Initialize the logging system from a JSON config file.
     * Looks for <configPath>.local first, falls back to <configPath> if not found.
     * @param configPath Path to the JSON config file (default: "logging-config.json")
     * @param componentName Component name for log file and pattern (e.g., "server", "ui")
     * @return true if config was loaded successfully, false if using defaults
     */
    static bool initializeFromConfig(
        const std::string& configPath = "logging-config.json",
        const std::string& componentName = "default");

    /**
     * @brief Get a specific channel logger.
     */
    static std::shared_ptr<spdlog::logger> get(LogChannel channel);

    /**
     * @brief Configure channels from a specification string.
     * @param spec Format: "channel:level,channel2:level2" or "*:level" for all
     * Examples:
     *   "swap:trace,physics:debug" - Set swap to trace, physics to debug
     *   "*:error" - Set all channels to error
     *   "*:off,swap:trace" - Disable all except swap at trace level
     */
    static void configureFromString(const std::string& spec);

    /**
     * @brief Set the log level for a specific channel.
     */
    static void setChannelLevel(LogChannel channel, spdlog::level::level_enum level);

    /**
     * @brief Set the log level for a channel by name.
     */
    static void setChannelLevel(const std::string& channel, spdlog::level::level_enum level);

private:
    /**
     * @brief Create a logger with the given name and sinks.
     */
    static void createLogger(
        const std::string& name,
        const std::vector<spdlog::sink_ptr>& sinks,
        spdlog::level::level_enum level);

    /**
     * @brief Parse a log level string to enum.
     */
    static spdlog::level::level_enum parseLevelString(const std::string& levelStr);

    /**
     * @brief Load JSON config from file, with .local override support.
     * Creates default config if file doesn't exist.
     * Exits on error if file exists but cannot be read.
     */
    static nlohmann::json loadConfigFile(const std::string& configPath);

    /**
     * @brief Create default config file at the given path.
     */
    static bool createDefaultConfigFile(const std::string& path);

    /**
     * @brief Apply configuration from JSON object.
     * @param config JSON configuration object
     * @param componentName Component name for log file and pattern
     */
    static void applyConfig(
        const nlohmann::json& config, const std::string& componentName = "default");

    /**
     * @brief Create specialized sinks from config.
     */
    static void createSpecializedSinks(const nlohmann::json& specializedConfig);

    static bool initialized_;
    static std::vector<spdlog::sink_ptr> sharedSinks_;
};

// Undefine any existing LOG_* macros (e.g., from libdatachannel).
#ifdef LOG_TRACE
#undef LOG_TRACE
#endif
#ifdef LOG_DEBUG
#undef LOG_DEBUG
#endif
#ifdef LOG_INFO
#undef LOG_INFO
#endif
#ifdef LOG_WARN
#undef LOG_WARN
#endif
#ifdef LOG_ERROR
#undef LOG_ERROR
#endif

#define LOG_TRACE(channel, ...) \
    SPDLOG_LOGGER_TRACE(LoggingChannels::get(::DirtSim::LogChannel::channel), __VA_ARGS__)
#define LOG_DEBUG(channel, ...) \
    SPDLOG_LOGGER_DEBUG(LoggingChannels::get(::DirtSim::LogChannel::channel), __VA_ARGS__)
#define LOG_INFO(channel, ...) \
    SPDLOG_LOGGER_INFO(LoggingChannels::get(::DirtSim::LogChannel::channel), __VA_ARGS__)
#define LOG_WARN(channel, ...) \
    SPDLOG_LOGGER_WARN(LoggingChannels::get(::DirtSim::LogChannel::channel), __VA_ARGS__)
#define LOG_ERROR(channel, ...) \
    SPDLOG_LOGGER_ERROR(LoggingChannels::get(::DirtSim::LogChannel::channel), __VA_ARGS__)

// Simple logging macros using default logger (no channel parameter, omits channel in output).
#define SLOG_TRACE(...) SPDLOG_LOGGER_TRACE(spdlog::default_logger(), __VA_ARGS__)
#define SLOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(spdlog::default_logger(), __VA_ARGS__)
#define SLOG_INFO(...) SPDLOG_LOGGER_INFO(spdlog::default_logger(), __VA_ARGS__)
#define SLOG_WARN(...) SPDLOG_LOGGER_WARN(spdlog::default_logger(), __VA_ARGS__)
#define SLOG_ERROR(...) SPDLOG_LOGGER_ERROR(spdlog::default_logger(), __VA_ARGS__)
// clang-format on

} // namespace DirtSim
