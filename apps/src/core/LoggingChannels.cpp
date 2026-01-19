#include "LoggingChannels.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace DirtSim {

// Static member initialization.
bool LoggingChannels::initialized_ = false;
std::vector<spdlog::sink_ptr> LoggingChannels::sharedSinks_;

void LoggingChannels::initialize(
    spdlog::level::level_enum consoleLevel,
    spdlog::level::level_enum fileLevel,
    const std::string& componentName,
    bool consoleToStderr)
{
    if (initialized_) {
        spdlog::warn("LoggingChannels already initialized, skipping re-initialization");
        return;
    }

    // Create shared sinks.
    spdlog::sink_ptr console_sink;
    if (consoleToStderr) {
        console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    }
    else {
        console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    }
    console_sink->set_level(consoleLevel);

    // All components log to the same file (component prefix distinguishes them).
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("dirtsim.log", true);
    file_sink->set_level(fileLevel);

    sharedSinks_ = { console_sink, file_sink };

    // Set pattern to include component, channel name, and source location.
    std::string pattern = componentName == "default"
        ? "[%H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v"
        : "[%H:%M:%S.%e] [" + componentName + "] [%n] [%^%l%$] [%s:%#] %v";

    // Set pattern on all sinks (so all loggers sharing these sinks get the pattern).
    for (auto& sink : sharedSinks_) {
        sink->set_pattern(pattern);
    }

    // Create channel-specific loggers with TRACE level (can be filtered later).
    // Organism channels.
    createLogger("brain", sharedSinks_, spdlog::level::info);
    createLogger("tree", sharedSinks_, spdlog::level::info);

    // Physics channels.
    createLogger("physics", sharedSinks_, spdlog::level::trace);
    createLogger("swap", sharedSinks_, spdlog::level::warn);
    createLogger("cohesion", sharedSinks_, spdlog::level::trace);
    createLogger("pressure", sharedSinks_, spdlog::level::trace);
    createLogger("collision", sharedSinks_, spdlog::level::trace);
    createLogger("friction", sharedSinks_, spdlog::level::trace);
    createLogger("support", sharedSinks_, spdlog::level::trace);
    createLogger("viscosity", sharedSinks_, spdlog::level::trace);

    // System channels.
    createLogger("controls", sharedSinks_, spdlog::level::info);
    createLogger("network", sharedSinks_, spdlog::level::info);
    createLogger("render", sharedSinks_, spdlog::level::info);
    createLogger("scenario", sharedSinks_, spdlog::level::info);
    createLogger("state", sharedSinks_, spdlog::level::debug);
    createLogger("ui", sharedSinks_, spdlog::level::info);

    // Create separate sinks for default logger (so its pattern doesn't affect channel loggers).
    spdlog::sink_ptr default_console_sink;
    if (consoleToStderr) {
        default_console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    }
    else {
        default_console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    }
    default_console_sink->set_level(consoleLevel);
    auto default_file_sink =
        std::make_shared<spdlog::sinks::basic_file_sink_mt>("dirtsim.log", false);
    default_file_sink->set_level(fileLevel);

    // Set pattern on default logger sinks (omits channel name to avoid redundancy).
    std::string defaultPattern = componentName == "default"
        ? "[%H:%M:%S.%e] [%^%l%$] [%s:%#] %v"
        : "[%H:%M:%S.%e] [" + componentName + "] [%^%l%$] [%s:%#] %v";
    default_console_sink->set_pattern(defaultPattern);
    default_file_sink->set_pattern(defaultPattern);

    // Create default logger with its own sinks.
    std::string loggerName = componentName.empty() ? "default" : componentName;
    std::vector<spdlog::sink_ptr> defaultSinks = { default_console_sink, default_file_sink };
    auto default_logger =
        std::make_shared<spdlog::logger>(loggerName, defaultSinks.begin(), defaultSinks.end());
    default_logger->set_level(spdlog::level::info);

    spdlog::set_default_logger(default_logger);

    // Flush periodically.
    spdlog::flush_every(std::chrono::seconds(1));

    initialized_ = true;
    SLOG_INFO("LoggingChannels initialized successfully");
}

std::shared_ptr<spdlog::logger> LoggingChannels::get(LogChannel channel)
{
    // Auto-initialize with defaults if get() is called before initialize().
    // This commonly happens in unit tests that use gtest_main.
    if (!initialized_) {
        initialize();
    }

    auto logger = spdlog::get(toString(channel));
    assert(logger && "LogChannel not found after initialization");
    return logger;
}

void LoggingChannels::configureFromString(const std::string& spec)
{
    if (spec.empty()) return;

    // Parse format: "channel:level,channel2:level2"
    std::stringstream ss(spec);
    std::string item;

    while (std::getline(ss, item, ',')) {
        // Trim whitespace.
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);

        // Split by colon.
        size_t colonPos = item.find(':');
        if (colonPos == std::string::npos) {
            spdlog::warn("Invalid channel spec (missing colon): {}", item);
            continue;
        }

        std::string channel = item.substr(0, colonPos);
        std::string levelStr = item.substr(colonPos + 1);

        // Trim channel and level strings.
        channel.erase(0, channel.find_first_not_of(" \t"));
        channel.erase(channel.find_last_not_of(" \t") + 1);
        levelStr.erase(0, levelStr.find_first_not_of(" \t"));
        levelStr.erase(levelStr.find_last_not_of(" \t") + 1);

        // Parse level.
        auto level = parseLevelString(levelStr);

        // Apply to channel(s).
        if (channel == "*") {
            // Apply to all registered loggers.
            auto& registry = spdlog::details::registry::instance();
            registry.apply_all(
                [level](std::shared_ptr<spdlog::logger> logger) { logger->set_level(level); });
            spdlog::debug("Set all channels to level: {}", spdlog::level::to_string_view(level));
        }
        else {
            setChannelLevel(channel, level);
        }
    }
}

void LoggingChannels::setChannelLevel(LogChannel channel, spdlog::level::level_enum level)
{
    auto logger = spdlog::get(toString(channel));
    assert(logger && "LogChannel not found");
    logger->set_level(level);
    spdlog::info(
        "Set channel '{}' to level: {}", toString(channel), spdlog::level::to_string_view(level));
}

void LoggingChannels::setChannelLevel(const std::string& channel, spdlog::level::level_enum level)
{
    auto logger = spdlog::get(channel);
    assert(logger && "Channel not found in config parsing");
    logger->set_level(level);
    spdlog::info("Set channel '{}' to level: {}", channel, spdlog::level::to_string_view(level));
}

void LoggingChannels::createLogger(
    const std::string& name,
    const std::vector<spdlog::sink_ptr>& sinks,
    spdlog::level::level_enum level)
{
    auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
    logger->set_level(level);
    spdlog::register_logger(logger);
}

spdlog::level::level_enum LoggingChannels::parseLevelString(const std::string& levelStr)
{
    std::string lower = levelStr;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "trace") {
        return spdlog::level::trace;
    }
    else if (lower == "debug") {
        return spdlog::level::debug;
    }
    else if (lower == "info") {
        return spdlog::level::info;
    }
    else if (lower == "warn" || lower == "warning") {
        return spdlog::level::warn;
    }
    else if (lower == "error" || lower == "err") {
        return spdlog::level::err;
    }
    else if (lower == "critical") {
        return spdlog::level::critical;
    }
    else if (lower == "off") {
        return spdlog::level::off;
    }
    else {
        spdlog::warn("Unknown log level '{}', defaulting to info", levelStr);
        return spdlog::level::info;
    }
}

bool LoggingChannels::initializeFromConfig(
    const std::string& configPath, const std::string& componentName)
{
    if (initialized_) {
        spdlog::warn("LoggingChannels already initialized, skipping re-initialization");
        return false;
    }

    // Load config with .local override support.
    auto config = loadConfigFile(configPath);

    // Apply configuration with component name.
    applyConfig(config, componentName);

    initialized_ = true;
    return true;
}

bool LoggingChannels::createDefaultConfigFile(const std::string& path)
{
    // Default configuration (differs for production vs development builds).
#ifdef DIRTSIM_PRODUCTION_BUILD
    // Production defaults: less verbose, rotating logs.
    nlohmann::json defaultConfig = {
        { "defaults",
          { { "console_level", "info" },
            { "file_level", "info" },
            { "pattern", "[%H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v" },
            { "flush_interval_ms", 1000 } } },
        { "sinks",
          { { "console", { { "enabled", true }, { "level", "info" }, { "colored", true } } },
            { "file",
              { { "enabled", true },
                { "level", "info" },
                { "path", "dirtsim.log" },
                { "truncate", false },
                { "max_size_mb", 10 },
                { "max_files", 3 } } },
#else
    // Development defaults: verbose logging, truncate for fresh logs each run.
    nlohmann::json defaultConfig = {
        { "defaults",
          { { "console_level", "info" },
            { "file_level", "debug" },
            { "pattern", "[%H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v" },
            { "flush_interval_ms", 1000 } } },
        { "sinks",
          { { "console", { { "enabled", true }, { "level", "info" }, { "colored", true } } },
            { "file",
              { { "enabled", true },
                { "level", "debug" },
                { "path", "dirtsim.log" },
                { "truncate", true },
                { "max_size_mb", 100 },
                { "max_files", 3 } } },
#endif
            { "specialized",
              { { "swap_trace",
                  { { "enabled", false },
                    { "channel_filter", nlohmann::json::array({ "swap" }) },
                    { "path", "swap-trace.log" },
                    { "level", "trace" } } },
                { "physics_deep",
                  { { "enabled", false },
                    { "channel_filter",
                      nlohmann::json::array({ "physics", "collision", "cohesion" }) },
                    { "path", "physics-deep.log" },
                    { "level", "trace" } } } } } } },
        { "channels",
          { { "brain", "info" },
            { "collision", "info" },
            { "cohesion", "info" },
            { "friction", "info" },
            { "network", "info" },
            { "physics", "info" },
            { "pressure", "info" },
            { "scenario", "info" },
#ifdef DIRTSIM_PRODUCTION_BUILD
            { "state", "info" },
#else
            { "state", "debug" },
#endif
            { "support", "info" },
            { "swap", "warn" },
            { "ui", "info" },
            { "viscosity", "info" } } },
        { "runtime",
          { { "allow_reload", true }, { "watch_config", false }, { "reload_signal", "SIGUSR1" } } }
    };

    try {
        std::ofstream configFile(path);
        if (!configFile.is_open()) {
            spdlog::error("Failed to create config file: {}", path);
            return false;
        }
        configFile << defaultConfig.dump(2) << std::endl;
        spdlog::info("Created default logging config file: {}", path);
        return true;
    }
    catch (const std::exception& e) {
        spdlog::error("Failed to write default config file {}: {}", path, e.what());
        return false;
    }
}

nlohmann::json LoggingChannels::loadConfigFile(const std::string& configPath)
{
    namespace fs = std::filesystem;

    // Default configuration (in-memory fallback, differs for production vs development).
#ifdef DIRTSIM_PRODUCTION_BUILD
    // Production defaults.
    nlohmann::json defaultConfig = {
        { "defaults",
          { { "console_level", "info" },
            { "file_level", "info" },
            { "pattern", "[%H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v" },
            { "flush_interval_ms", 1000 } } },
        { "sinks",
          { { "console", { { "enabled", true }, { "level", "info" }, { "colored", true } } },
            { "file",
              { { "enabled", true },
                { "level", "info" },
                { "path", "dirtsim.log" },
                { "truncate", false },
                { "max_size_mb", 10 },
                { "max_files", 3 } } } } },
#else
    // Development defaults.
    nlohmann::json defaultConfig = {
        { "defaults",
          { { "console_level", "info" },
            { "file_level", "debug" },
            { "pattern", "[%H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v" },
            { "flush_interval_ms", 1000 } } },
        { "sinks",
          { { "console", { { "enabled", true }, { "level", "info" }, { "colored", true } } },
            { "file",
              { { "enabled", true },
                { "level", "debug" },
                { "path", "dirtsim.log" },
                { "truncate", true } } } } },
#endif
        { "channels",
          { { "brain", "info" },
            { "collision", "info" },
            { "cohesion", "info" },
            { "friction", "info" },
            { "network", "info" },
            { "physics", "info" },
            { "pressure", "info" },
            { "scenario", "info" },
#ifdef DIRTSIM_PRODUCTION_BUILD
            { "state", "info" },
#else
            { "state", "debug" },
#endif
            { "support", "info" },
            { "swap", "info" },
            { "ui", "info" },
            { "viscosity", "info" } } }
    };

    // Try .local version first.
    std::string localPath = configPath + ".local";
    std::string pathToUse;

    if (fs::exists(localPath)) {
        pathToUse = localPath;
        spdlog::info("Using local config override: {}", localPath);
    }
    else if (fs::exists(configPath)) {
        pathToUse = configPath;
        spdlog::info("Using default config: {}", configPath);
    }
    else {
        // Neither file exists - create default config file.
        spdlog::info("Config file not found, creating default: {}", configPath);
        if (createDefaultConfigFile(configPath)) {
            pathToUse = configPath;
        }
        else {
            spdlog::warn("Could not create config file, using built-in defaults");
            return defaultConfig;
        }
    }

    // Try to load JSON.
    try {
        std::ifstream configFile(pathToUse);
        if (!configFile.is_open()) {
            spdlog::error("FATAL: Cannot open config file: {}", pathToUse);
            spdlog::error("Check file permissions or delete the file to regenerate defaults.");
            std::exit(1);
        }

        nlohmann::json config = nlohmann::json::parse(configFile);
        spdlog::info("Loaded logging config from {}", pathToUse);
        return config;
    }
    catch (const nlohmann::json::parse_error& e) {
        spdlog::error("FATAL: Failed to parse config file {}: {}", pathToUse, e.what());
        spdlog::error("Fix the JSON syntax or delete the file to regenerate defaults.");
        std::exit(1);
    }
    catch (const std::exception& e) {
        spdlog::error("FATAL: Error reading config file {}: {}", pathToUse, e.what());
        spdlog::error("Check file permissions or delete the file to regenerate defaults.");
        std::exit(1);
    }
}

void LoggingChannels::applyConfig(const nlohmann::json& config, const std::string& componentName)
{
    // Extract defaults with fallbacks.
    auto consoleLevel = spdlog::level::info;
    auto fileLevel = spdlog::level::debug;
    std::string basePattern = "[%H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v";
    std::string pattern = componentName == "default"
        ? basePattern
        : "[%H:%M:%S.%e] [" + componentName + "] [%n] [%^%l%$] [%s:%#] %v";
    int flushIntervalMs = 1000;

    try {
        if (config.contains("defaults")) {
            auto& defaults = config["defaults"];
            if (defaults.contains("console_level")) {
                consoleLevel = parseLevelString(defaults["console_level"].get<std::string>());
            }
            if (defaults.contains("file_level")) {
                fileLevel = parseLevelString(defaults["file_level"].get<std::string>());
            }
            if (defaults.contains("pattern")) {
                std::string configPattern = defaults["pattern"].get<std::string>();
                // Inject component name into pattern after timestamp if not default.
                if (componentName != "default") {
                    // Find timestamp end: "] " and inject component name after it.
                    size_t pos = configPattern.find("] ");
                    if (pos != std::string::npos) {
                        pattern = configPattern.substr(0, pos + 2) + "[" + componentName + "] "
                            + configPattern.substr(pos + 2);
                    }
                    else {
                        // Fallback: prepend component name.
                        pattern = "[" + componentName + "] " + configPattern;
                    }
                }
                else {
                    pattern = configPattern;
                }
            }
            if (defaults.contains("flush_interval_ms")) {
                flushIntervalMs = defaults["flush_interval_ms"].get<int>();
            }
        }
    }
    catch (const std::exception& e) {
        spdlog::warn("Error reading defaults from config: {}, using built-in defaults", e.what());
    }

    // Create sinks.
    std::vector<spdlog::sink_ptr> sinks;

    try {
        if (config.contains("sinks")) {
            auto& sinksConfig = config["sinks"];

            // Console sink.
            if (sinksConfig.contains("console")) {
                auto& consoleCfg = sinksConfig["console"];
                bool enabled = consoleCfg.value("enabled", true);
                if (enabled) {
                    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                    auto level = parseLevelString(consoleCfg.value("level", "info"));
                    console_sink->set_level(level);
                    sinks.push_back(console_sink);
                }
            }

            // File sink.
            if (sinksConfig.contains("file")) {
                auto& fileCfg = sinksConfig["file"];
                bool enabled = fileCfg.value("enabled", true);
                if (enabled) {
                    // All components use the same log file (component prefix distinguishes them).
                    std::string path = fileCfg.value("path", "dirtsim.log");
                    auto level = parseLevelString(fileCfg.value("level", "debug"));

                    // Use rotating sink if max_size_mb is specified, otherwise basic sink.
                    std::shared_ptr<spdlog::sinks::sink> file_sink;
                    if (fileCfg.contains("max_size_mb")) {
                        size_t maxSizeMB = fileCfg.value("max_size_mb", 100);
                        size_t maxFiles = fileCfg.value("max_files", 3);
                        size_t maxSizeBytes = maxSizeMB * 1024 * 1024;
                        file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                            path, maxSizeBytes, maxFiles);
                        spdlog::info(
                            "Using rotating file sink: {} (max {} MB, {} files)",
                            path,
                            maxSizeMB,
                            maxFiles);
                    }
                    else {
                        bool truncate = fileCfg.value("truncate", true);
                        file_sink =
                            std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, truncate);
                    }

                    file_sink->set_level(level);
                    sinks.push_back(file_sink);
                }
            }

            // Specialized sinks.
            if (sinksConfig.contains("specialized")) {
                createSpecializedSinks(sinksConfig["specialized"]);
            }
        }
    }
    catch (const std::exception& e) {
        spdlog::error("Error creating sinks from config: {}, using defaults", e.what());
        // Create default sinks if config failed.
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(consoleLevel);
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("dirtsim.log", true);
        file_sink->set_level(fileLevel);
        sinks = { console_sink, file_sink };
    }

    sharedSinks_ = sinks;

    // Set pattern on all sinks (so all loggers sharing these sinks get the pattern).
    for (auto& sink : sharedSinks_) {
        sink->set_pattern(pattern);
    }

    // Create channel loggers.
    createLogger("brain", sharedSinks_, spdlog::level::trace);
    createLogger("collision", sharedSinks_, spdlog::level::trace);
    createLogger("cohesion", sharedSinks_, spdlog::level::trace);
    createLogger("controls", sharedSinks_, spdlog::level::trace);
    createLogger("friction", sharedSinks_, spdlog::level::trace);
    createLogger("network", sharedSinks_, spdlog::level::trace);
    createLogger("physics", sharedSinks_, spdlog::level::trace);
    createLogger("pressure", sharedSinks_, spdlog::level::trace);
    createLogger("render", sharedSinks_, spdlog::level::trace);
    createLogger("scenario", sharedSinks_, spdlog::level::trace);
    createLogger("state", sharedSinks_, spdlog::level::trace);
    createLogger("support", sharedSinks_, spdlog::level::trace);
    createLogger("swap", sharedSinks_, spdlog::level::trace);
    createLogger("tree", sharedSinks_, spdlog::level::trace);
    createLogger("ui", sharedSinks_, spdlog::level::trace);
    createLogger("viscosity", sharedSinks_, spdlog::level::trace);

    // Apply channel levels from config.
    try {
        if (config.contains("channels")) {
            auto& channels = config["channels"];
            for (auto& [channel, levelStr] : channels.items()) {
                auto level = parseLevelString(levelStr.get<std::string>());
                setChannelLevel(channel, level);
            }
        }
    }
    catch (const std::exception& e) {
        spdlog::warn("Error applying channel levels from config: {}", e.what());
    }

    // Create separate sinks for default logger (so its pattern doesn't affect channel loggers).
    std::vector<spdlog::sink_ptr> defaultSinks;
    for (const auto& sharedSink : sharedSinks_) {
        // Clone sink configuration but create new instances for default logger.
        if (auto* consoleSink =
                dynamic_cast<spdlog::sinks::stdout_color_sink_mt*>(sharedSink.get())) {
            auto default_console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            default_console_sink->set_level(consoleSink->level());
            defaultSinks.push_back(default_console_sink);
        }
        else if (
            auto* fileSink = dynamic_cast<spdlog::sinks::basic_file_sink_mt*>(sharedSink.get())) {
            auto default_file_sink =
                std::make_shared<spdlog::sinks::basic_file_sink_mt>("dirtsim.log", false);
            default_file_sink->set_level(fileSink->level());
            defaultSinks.push_back(default_file_sink);
        }
        else if (
            auto* rotatingSink =
                dynamic_cast<spdlog::sinks::rotating_file_sink_mt*>(sharedSink.get())) {
            auto default_file_sink =
                std::make_shared<spdlog::sinks::basic_file_sink_mt>("dirtsim.log", false);
            default_file_sink->set_level(rotatingSink->level());
            defaultSinks.push_back(default_file_sink);
        }
    }

    // Set pattern on default logger sinks (omits channel name to avoid redundancy).
    std::string defaultPattern = componentName == "default"
        ? "[%H:%M:%S.%e] [%^%l%$] [%s:%#] %v"
        : "[%H:%M:%S.%e] [" + componentName + "] [%^%l%$] [%s:%#] %v";
    for (auto& sink : defaultSinks) {
        sink->set_pattern(defaultPattern);
    }

    // Create default logger with its own sinks.
    std::string loggerName = componentName.empty() ? "default" : componentName;
    auto default_logger =
        std::make_shared<spdlog::logger>(loggerName, defaultSinks.begin(), defaultSinks.end());
    default_logger->set_level(spdlog::level::info);

    spdlog::set_default_logger(default_logger);

    // Set flush interval.
    spdlog::flush_every(std::chrono::milliseconds(flushIntervalMs));

    SLOG_INFO("LoggingChannels initialized from config successfully");
}

void LoggingChannels::createSpecializedSinks(const nlohmann::json& specializedConfig)
{
    try {
        for (auto& [name, cfg] : specializedConfig.items()) {
            bool enabled = cfg.value("enabled", false);
            if (!enabled) {
                spdlog::debug("Specialized sink '{}' is disabled", name);
                continue;
            }

            std::string path = cfg.value("path", name + ".log");
            auto level = parseLevelString(cfg.value("level", "trace"));

            // Create file sink for this specialized logger.
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, true);
            sink->set_level(level);

            // Get channel filters.
            std::vector<std::string> channelFilters;
            if (cfg.contains("channel_filter")) {
                channelFilters = cfg["channel_filter"].get<std::vector<std::string>>();
            }

            // Create a logger for each filtered channel.
            for (const auto& channel : channelFilters) {
                std::string loggerName = channel + "_" + name;
                auto logger = std::make_shared<spdlog::logger>(loggerName, sink);
                logger->set_level(level);
                spdlog::register_logger(logger);
                spdlog::info(
                    "Created specialized sink '{}' for channel '{}' -> {}", name, channel, path);
            }
        }
    }
    catch (const std::exception& e) {
        spdlog::error("Error creating specialized sinks: {}", e.what());
    }
}

} // namespace DirtSim
