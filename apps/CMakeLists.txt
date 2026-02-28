cmake_minimum_required(VERSION 3.14)
project(lvgl)

# Export compile commands for clangd/LSP support.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Suppress FindBoost deprecation warning (we use Boost's config mode when available).
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# Use ccache for faster rebuilds when available.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
else()
    message(STATUS "ccache not found - builds will not be cached")
endif()

# Allow linking to targets from subdirectories.
cmake_policy(SET CMP0079 NEW)

# Add Google Test (1.15.2 fixes CMake deprecation warnings).
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)

# Add libdatachannel for WebSocket/WebRTC support.
FetchContent_Declare(
    libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG v0.24.0
)
# Disable tests and examples for libdatachannel.
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libdatachannel)

# Add nlohmann/json for JSON serialization.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Find packages.
find_package(Boost REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVAHI REQUIRED avahi-client)
pkg_check_modules(LIBNM REQUIRED libnm)

pkg_check_modules(OPENH264 REQUIRED openh264)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(FREETYPE REQUIRED freetype2)

# Headers at /usr/include/libyuv.h, library at /usr/lib/x86_64-linux-gnu/libyuv.so

# Find OpenMP for parallel physics calculations.
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found: ${OpenMP_CXX_VERSION}")
else()
    message(STATUS "OpenMP not found - physics will run sequentially")
endif()

# Add zpp_bits for high-performance binary serialization.
FetchContent_Declare(
    zpp_bits
    GIT_REPOSITORY https://github.com/eyalz800/zpp_bits.git
    GIT_TAG v4.4.25
)
FetchContent_MakeAvailable(zpp_bits)

# Add sqlite_modern_cpp for type-safe SQLite access (header-only wrapper).
FetchContent_Declare(
    sqlite_modern_cpp
    GIT_REPOSITORY https://github.com/SqliteModernCpp/sqlite_modern_cpp.git
    GIT_TAG v3.2
)
FetchContent_MakeAvailable(sqlite_modern_cpp)

# Add libssh2 for SSH execution.
option(DIRTSIM_USE_SYSTEM_LIBSSH2 "Use system libssh2 instead of FetchContent" OFF)
set(DIRTSIM_LIBSSH2_INCLUDE_DIRS "")

if (DIRTSIM_USE_SYSTEM_LIBSSH2)
    set(PKG_CONFIG_USE_STATIC_LIBS ON)
    pkg_check_modules(DIRTSIM_LIBSSH2 REQUIRED libssh2)
    set(PKG_CONFIG_USE_STATIC_LIBS OFF)
    set(LIBSSH2_TARGET ${DIRTSIM_LIBSSH2_LIBRARIES})
else()
    FetchContent_Declare(
        libssh2
        GIT_REPOSITORY https://github.com/libssh2/libssh2.git
        GIT_TAG libssh2-1.11.1
    )
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(LIBSSH2_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LIBSSH2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(libssh2)

    if (TARGET libssh2)
        set(LIBSSH2_TARGET libssh2)
    elseif (TARGET libssh2_static)
        set(LIBSSH2_TARGET libssh2_static)
    elseif (TARGET libssh2::libssh2)
        set(LIBSSH2_TARGET libssh2::libssh2)
    else()
        message(FATAL_ERROR "libssh2 target not found")
    endif()
endif()

# qlibs/reflect is header-only, downloaded to src/core/reflect

# Embed web resources (HTML, CSS, JS) at build time.
include(cmake/EmbedWebResources.cmake)
embed_web_resources(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/WebResources.h
    HTML src/server/web/garden.html
    CSS src/server/web/garden.css
    JS src/server/web/garden.js
)

set(CONF_PATH "${PROJECT_SOURCE_DIR}/lv_conf.h")

foreach(BACKEND_NAME "SDL" "LINUX_DRM" "LINUX_FBDEV" "X11" "WAYLAND" "OPENGLES")

    execute_process(WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                    COMMAND "scripts/backend_conf.sh" ${BACKEND_NAME} ${CONF_PATH} OUTPUT_VARIABLE IS_BACKEND_ENABLED)
    set("LV_USE_${BACKEND_NAME}" ${IS_BACKEND_ENABLED})

endforeach()

# Uncomment if the program needs debugging
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -ggdb")

set(CMAKE_C_STANDARD 99) # LVGL officially supports C99 and above
set(CMAKE_CXX_STANDARD 23) #C++23
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)

# Common warning flags for all dirtsim targets.
set(DIRTSIM_WARNINGS -Wall -Wextra -Werror -Wswitch-enum)

# Find SDL2 for gamepad support and display backend.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
message(STATUS "SDL2 found - gamepad support enabled")

# Configuration
if (LV_USE_SDL)

    message("Including SDL2 display backend support")
    pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)

    list(APPEND PKG_CONFIG_LIB ${SDL2_LIBRARIES} ${SDL2_IMAGE_LIBRARIES})
    list(APPEND PKG_CONFIG_INC ${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS})

    list(APPEND LV_LINUX_BACKEND_SRC src/ui/lib/display_backends/sdl.cpp)
endif()


if (LV_USE_WAYLAND)

    message("Including Wayland support")

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
    pkg_check_modules(WAYLAND_CURSOR REQUIRED wayland-cursor)
    pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)

    list(APPEND PKG_CONFIG_LIB ${WAYLAND_CLIENT_LIBRARIES})
    list(APPEND PKG_CONFIG_LIB ${WAYLAND_CURSOR_LIBRARIES})
    list(APPEND PKG_CONFIG_LIB ${XKBCOMMON_LIBRARIES})

    # Wayland protocols
    pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.25)
    pkg_get_variable(WAYLAND_PROTOCOLS_BASE wayland-protocols pkgdatadir)

    execute_process(COMMAND "scripts/gen_wl_protocols.sh" OUTPUT_VARIABLE WAYLAND_PROTOCOLS_SRC)

    list(APPEND PKG_CONFIG_INC "${PROJECT_SOURCE_DIR}/wl_protocols")
    list(APPEND LV_LINUX_BACKEND_SRC src/ui/lib/display_backends/wayland.cpp wl_protocols/wayland_xdg_shell.c)

endif()

if (LV_USE_X11)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(X11 REQUIRED x11)

    message("Including X11 support")

    list(APPEND PKG_CONFIG_INC ${X11_INCLUDE_DIRS})
    list(APPEND PKG_CONFIG_LIB ${X11_LIBRARIES})
    list(APPEND LV_LINUX_BACKEND_SRC src/ui/lib/display_backends/x11.cpp)

endif()

if (LV_USE_LINUX_FBDEV)

    # FBDEV has no dependencies
    message("Including FBDEV support")
    list(APPEND LV_LINUX_BACKEND_SRC src/ui/lib/display_backends/fbdev.cpp)

endif()

foreach(arg ${PKG_CONFIG_LIB})
    string(APPEND LVGL_PKG_CONFIG_EXT_LIB " -l${arg}")
endforeach()

string(APPEND LVGL_PKG_CONFIG_LIB "-llvgl_linux")

file(GLOB LV_LINUX_SRC src/ui/lib/*.c src/ui/lib/*.cpp)
set(LV_LINUX_INC src/ui/lib)

set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)

add_subdirectory(./lvgl ${CMAKE_BINARY_DIR}/lvgl)
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/lib ${PKG_CONFIG_INC} ${FREETYPE_INCLUDE_DIRS})
target_link_libraries(lvgl PUBLIC ${PKG_CONFIG_LIB} ${FREETYPE_LIBRARIES})

add_subdirectory(./spdlog ${CMAKE_BINARY_DIR}/spdlog)

# Mark spdlog includes as SYSTEM to suppress third-party warnings.
get_target_property(SPDLOG_INC spdlog INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(spdlog PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${SPDLOG_INC}")

# Add args header-only library
set(ARGS_BUILD_EXAMPLE OFF CACHE BOOL "Build examples" FORCE)
set(ARGS_BUILD_UNITTESTS OFF CACHE BOOL "Build unit tests" FORCE)
add_subdirectory(./external/args ${CMAKE_BINARY_DIR}/args)

add_library(lvgl_linux STATIC ${LV_LINUX_SRC} ${LV_LINUX_BACKEND_SRC})
target_include_directories(lvgl_linux PRIVATE ${LV_LINUX_INC} ${PROJECT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/ui/lib ${zpp_bits_SOURCE_DIR} ${PKG_CONFIG_INC})
target_link_libraries(lvgl_linux PUBLIC nlohmann_json::nlohmann_json spdlog::spdlog ${PKG_CONFIG_LIB})

# ============================================================================
# Component Libraries
# ============================================================================

# Core library - minimal shared types for serialization.
set(DIRTSIM_CORE_SOURCES
    src/core/Cell.cpp
    src/core/ColorMaterialMapper.cpp
    src/core/ColorNames.cpp
    src/core/ConfigLoader.cpp
    src/core/Entity.cpp
    src/core/EntityType.cpp
    src/core/FontSampler.cpp
    src/core/IconFont.cpp
    src/core/LightConfig.cpp
    src/core/LoggingChannels.cpp
    src/core/MaterialFragmentationParams.cpp
    src/core/MaterialType.cpp
    src/core/PhysicsSettings.cpp
    src/core/RenderMessage.cpp
    src/core/ScenarioConfig.cpp
    src/core/ScenarioId.cpp
    src/core/StateMachineBase.cpp
    src/core/SystemMetrics.cpp
    src/core/Timers.cpp
    src/core/UUID.cpp
    # Vector2d.cpp and Vector2i.cpp removed - now fully inline template in Vector2.h

    # Audio synthesis.
    src/core/audio/Envelope.cpp
    src/core/audio/Oscillator.cpp
    src/core/audio/SynthVoice.cpp

    # Bitmap infrastructure.
    src/core/bitmaps/CellBitmap.cpp
    src/core/bitmaps/EmptyNeighborhood.cpp
    src/core/bitmaps/MaterialNeighborhood.cpp

    # Grid cache layer.
    src/core/GridOfCells.cpp

    # Network layer.
    src/core/network/WebSocketService.cpp
    src/core/network/WifiManager.cpp

    # Video encoding.
    src/core/encoding/H264Encoder.cpp

    # Gamepad input.
    src/core/input/GamepadManager.cpp
)

add_library(dirtsim-core STATIC ${DIRTSIM_CORE_SOURCES})
target_include_directories(dirtsim-core PUBLIC ${CMAKE_SOURCE_DIR}/src ${zpp_bits_SOURCE_DIR} ${OPENH264_INCLUDE_DIRS} ${LIBNM_INCLUDE_DIRS})
target_link_libraries(dirtsim-core PUBLIC nlohmann_json::nlohmann_json spdlog::spdlog datachannel-static ${OPENH264_LIBRARIES} ${LIBNM_LIBRARIES} yuv lvgl)

# Link SDL2 for gamepad support.
target_include_directories(dirtsim-core PUBLIC ${SDL2_INCLUDE_DIRS})
target_link_libraries(dirtsim-core PUBLIC ${SDL2_LIBRARIES})

# Define DIRTSIM_PRODUCTION_BUILD for Release builds to use production logging defaults.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(dirtsim-core PUBLIC DIRTSIM_PRODUCTION_BUILD)
endif()
target_compile_options(dirtsim-core PRIVATE ${DIRTSIM_WARNINGS})

# Server library - physics engine and server logic.
add_library(dirtsim-server-lib STATIC
    # Server state machine.
    src/server/ServerConfig.cpp
    src/server/StateMachine.cpp
    src/server/TrainingResultRepository.cpp
    src/server/UserSettings.cpp
    src/server/EventProcessor.cpp
    src/server/states/Error.cpp
    src/server/states/Evolution.cpp
    src/server/states/Idle.cpp
    src/server/states/Shutdown.cpp
    src/server/states/SimPaused.cpp
    src/server/states/SimRunning.cpp
    src/server/states/Startup.cpp
    src/server/states/UnsavedTrainingResult.cpp

    # Server API commands.
    src/server/api/CellGet.cpp
    src/server/api/CellSet.cpp
    src/server/api/ClockEventTrigger.cpp
    src/server/api/DiagramGet.cpp
    src/server/api/EventSubscribe.cpp
    src/server/api/EvolutionProgress.cpp
    src/server/api/EvolutionStart.cpp
    src/server/api/EvolutionStop.cpp
    src/server/api/Exit.cpp
    src/server/api/FitnessBreakdownReport.cpp
    src/server/api/FingerDown.cpp
    src/server/api/FingerMove.cpp
    src/server/api/FingerUp.cpp
    src/server/api/GenomeDelete.cpp
    src/server/api/GenomeGet.cpp
    src/server/api/GenomeList.cpp
    src/server/api/GenomeSet.cpp
    src/server/api/GravitySet.cpp
    src/server/api/PerfStatsGet.cpp
    src/server/api/PhysicsSettingsGet.cpp
    src/server/api/PhysicsSettingsSet.cpp
    src/server/api/RenderFormatGet.cpp
    src/server/api/RenderFormatSet.cpp
    src/server/api/RenderStreamConfigSet.cpp
    src/server/api/Reset.cpp
    src/server/api/ScenarioListGet.cpp
    src/server/api/SeedAdd.cpp
    src/server/api/SimRun.cpp
    src/server/api/SimStop.cpp
    src/server/api/SpawnDirtBall.cpp
    src/server/api/StateGet.cpp
    src/server/api/StatusGet.cpp
    src/server/api/TimerStatsGet.cpp
    src/server/api/TrainingBestPlaybackFrame.cpp
    src/server/api/UserSettingsSet.cpp
    src/server/api/TrainingBestSnapshot.cpp
    src/server/api/TrainingResult.cpp
    src/server/api/TrainingResultDiscard.cpp
    src/server/api/TrainingResultList.cpp
    src/server/api/TrainingResultSave.cpp
    src/server/api/UserSettingsUpdated.cpp
    src/server/api/WorldResize.cpp

    # Server network.
    src/server/network/CommandDeserializerJson.cpp
    src/server/network/HttpServer.cpp

    # Scenarios.
    src/core/scenarios/ScenarioRegistry.cpp
    src/core/scenarios/BenchmarkScenario.cpp
    src/core/scenarios/ClockConfig.cpp
    src/core/scenarios/ClockScenario.cpp
    src/core/scenarios/RainingConfig.cpp
    src/core/scenarios/SandboxConfig.cpp
    src/core/scenarios/TreeGerminationConfig.cpp
    src/core/scenarios/clock_scenario/CharacterMetrics.cpp
    src/core/scenarios/clock_scenario/ColorCycleEvent.cpp
    src/core/scenarios/clock_scenario/ColorShowcaseEvent.cpp
    src/core/scenarios/clock_scenario/DoorEntrySpawn.cpp
    src/core/scenarios/clock_scenario/DoorManager.cpp
    src/core/scenarios/clock_scenario/DrainManager.cpp
    src/core/scenarios/clock_scenario/EventManager.cpp
    src/core/scenarios/clock_scenario/GlowConfig.cpp
    src/core/scenarios/clock_scenario/GlowManager.cpp
    src/core/scenarios/clock_scenario/MarqueeTypes.cpp
    src/core/scenarios/clock_scenario/MeltdownEvent.cpp
    src/core/scenarios/clock_scenario/ObstacleManager.cpp
    src/core/scenarios/clock_scenario/RainEvent.cpp
    src/core/scenarios/clock_scenario/StormManager.cpp
    src/core/scenarios/DamBreakScenario.cpp
    src/core/scenarios/EmptyScenario.cpp
    src/core/scenarios/GooseTestScenario.cpp
    src/core/scenarios/LightsScenario.cpp
    src/core/scenarios/NesFlappyParatroopaScenario.cpp
    src/core/scenarios/NesSuperTiltBroScenario.cpp
    src/core/scenarios/nes/NesDuckSensoryBuilder.cpp
    src/core/scenarios/nes/NesGameAdapter.cpp
    src/core/scenarios/nes/NesGameAdapterRegistry.cpp
    src/core/scenarios/nes/NesFlappyBirdEvaluator.cpp
    src/core/scenarios/nes/NesPaletteClusterer.cpp
    src/core/scenarios/nes/NesRamProbe.cpp
    src/core/scenarios/nes/NesRomValidation.cpp
    src/core/scenarios/nes/NesRomProfileExtractor.cpp
    src/core/scenarios/nes/SmolnesRuntime.cpp
    src/core/scenarios/nes/SmolnesRuntimeBackend.c
    src/core/scenarios/RainingScenario.cpp
    src/core/scenarios/SandboxScenario.cpp
    src/core/scenarios/TreeGerminationScenario.cpp
    src/core/scenarios/WaterEqualizationScenario.cpp

    # Physics engine.
    src/core/LightManager.cpp
    src/core/LightTypes.cpp
    src/core/World.cpp
    src/core/WorldAdhesionCalculator.cpp
    src/core/WorldAirResistanceCalculator.cpp
    src/core/WorldCalculatorBase.cpp
    src/core/WorldCohesionCalculator.cpp
    src/core/WorldCollisionCalculator.cpp
    src/core/WorldDiagramGeneratorEmoji.cpp
    src/core/WorldFrictionCalculator.cpp
    src/core/WorldInterpolationTool.cpp
    src/core/WorldLightCalculator.cpp
    src/core/WorldPressureCalculator.cpp
    src/core/WorldRigidBodyCalculator.cpp
    src/core/WorldVelocityLimitCalculator.cpp
    src/core/WorldViscosityCalculator.cpp

    # Organism system.
    src/core/organisms/Duck.cpp
    src/core/organisms/DuckBrain.cpp
    src/core/organisms/DuckSensoryData.cpp
    src/core/organisms/Goose.cpp
    src/core/organisms/GooseBrain.cpp
    src/core/organisms/Body.cpp
    src/core/organisms/Bone.cpp
    src/core/organisms/OrganismManager.cpp
    src/core/organisms/OrganismSensoryData.cpp
    src/core/organisms/PlayerDuckBrain.cpp
    src/core/organisms/Tree.cpp
    src/core/organisms/TreeCommandProcessor.cpp
    src/core/organisms/TreeSensoryData.cpp
    src/core/organisms/brains/Genome.cpp
    src/core/organisms/brains/DuckNeuralNetBrain.cpp
    src/core/organisms/brains/DuckNeuralNetRecurrentBrain.cpp
    src/core/organisms/brains/NeuralNetBrain.cpp
    src/core/organisms/brains/RuleBasedBrain.cpp
    src/core/organisms/brains/RuleBased2Brain.cpp
    src/core/organisms/evolution/DuckEvaluator.cpp
    src/core/organisms/evolution/FitnessCalculator.cpp
    src/core/organisms/evolution/GenomeMetadata.cpp
    src/core/organisms/evolution/GenomeRepository.cpp
    src/core/organisms/evolution/GooseEvaluator.cpp
    src/core/organisms/evolution/MovementScoring.cpp
    src/core/organisms/evolution/Mutation.cpp
    src/core/organisms/evolution/NesEvaluator.cpp
    src/core/organisms/evolution/OrganismTracker.cpp
    src/core/organisms/evolution/Selection.cpp
    src/core/organisms/evolution/TrainingBrainRegistry.cpp
    src/core/organisms/evolution/TrainingRunner.cpp
    src/core/organisms/evolution/TreeEvaluator.cpp
    src/core/organisms/components/LightHandHeld.cpp
    src/core/organisms/components/LocalShapeProjection.cpp
    src/core/organisms/tests/MultiCellTestOrganism.cpp
    src/core/organisms/components/RigidBodyCollisionComponent.cpp
    src/core/organisms/components/RigidBodyComponent.cpp
    src/core/organisms/components/RigidBodyPhysicsComponent.cpp
)
target_include_directories(dirtsim-server-lib PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)
target_include_directories(dirtsim-server-lib SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/external ${zpp_bits_SOURCE_DIR} ${sqlite_modern_cpp_SOURCE_DIR}/hdr ${SQLITE3_INCLUDE_DIRS})
target_link_libraries(dirtsim-server-lib PUBLIC dirtsim-core datachannel-static lvgl lvgl_linux ${SQLITE3_LIBRARIES})
add_dependencies(dirtsim-server-lib web_resources_generate)
target_compile_options(dirtsim-server-lib PRIVATE ${DIRTSIM_WARNINGS})
set_source_files_properties(src/core/scenarios/nes/SmolnesRuntimeBackend.c PROPERTIES COMPILE_OPTIONS "-w")

# Link OpenMP for parallel physics calculations (cohesion, adhesion loops).
if(OpenMP_CXX_FOUND)
    target_link_libraries(dirtsim-server-lib PUBLIC OpenMP::OpenMP_CXX)
endif()

# UI library - UI components and client logic.
add_library(dirtsim-ui-lib STATIC
    # UI state machine.
    src/ui/state-machine/StateMachine.cpp
    src/ui/state-machine/EventProcessor.cpp
    src/ui/state-machine/states/Disconnected.cpp
    src/ui/state-machine/states/Network.cpp
    src/ui/state-machine/states/Paused.cpp
    src/ui/state-machine/states/Shutdown.cpp
    src/ui/state-machine/states/SimRunning.cpp
    src/ui/state-machine/states/StartMenu.cpp
    src/ui/state-machine/states/Startup.cpp
    src/ui/state-machine/states/Synth.cpp
    src/ui/state-machine/states/SynthConfig.cpp
    src/ui/state-machine/states/SynthKeyboard.cpp
    src/ui/state-machine/states/TrainingActive.cpp
    src/ui/state-machine/states/TrainingFitnessHistory.cpp
    src/ui/state-machine/states/TrainingIdle.cpp
    src/ui/state-machine/states/TrainingUnsavedResult.cpp

    # UI API commands.
    src/ui/state-machine/api/DrawDebugToggle.cpp
    src/ui/state-machine/api/Exit.cpp
    src/ui/state-machine/api/GenomeBrowserOpen.cpp
    src/ui/state-machine/api/GenomeDetailLoad.cpp
    src/ui/state-machine/api/GenomeDetailOpen.cpp
    src/ui/state-machine/api/IconRailExpand.cpp
    src/ui/state-machine/api/IconRailShowIcons.cpp
    src/ui/state-machine/api/IconSelect.cpp
    src/ui/state-machine/api/MouseDown.cpp
    src/ui/state-machine/api/MouseMove.cpp
    src/ui/state-machine/api/MouseUp.cpp
    src/ui/state-machine/api/PlantSeed.cpp
    src/ui/state-machine/api/PixelRendererToggle.cpp
    src/ui/state-machine/api/RenderModeSelect.cpp
    src/ui/state-machine/api/ScreenGrab.cpp
    src/ui/state-machine/api/SimPause.cpp
    src/ui/state-machine/api/SimRun.cpp
    src/ui/state-machine/api/SimStop.cpp
    src/ui/state-machine/api/StateGet.cpp
    src/ui/state-machine/api/StatusGet.cpp
    src/ui/state-machine/api/StopButtonPress.cpp
    src/ui/state-machine/api/StreamStart.cpp
    src/ui/state-machine/api/SynthKeyEvent.cpp
    src/ui/state-machine/api/TrainingActiveScenarioControlsShow.cpp
    src/ui/state-machine/api/TrainingConfigShowEvolution.cpp
    src/ui/state-machine/api/TrainingQuit.cpp
    src/ui/state-machine/api/TrainingResultDiscard.cpp
    src/ui/state-machine/api/TrainingResultSave.cpp
    src/ui/state-machine/api/TrainingStart.cpp
    src/ui/state-machine/api/WebSocketAccessSet.cpp
    src/ui/state-machine/api/WebRtcAnswer.cpp
    src/ui/state-machine/api/WebRtcCandidate.cpp
    src/ui/state-machine/api/WebUiAccessSet.cpp

    # UI network.
    src/ui/state-machine/network/CommandDeserializerJson.cpp
    src/ui/state-machine/network/MessageParser.cpp

    # UI components.
    src/ui/DisplayCapture.cpp
    src/ui/PanelViewController.cpp
    src/ui/RemoteInputDevice.cpp
    src/ui/UiComponentManager.cpp
    src/ui/UserSettingsManager.cpp
    src/ui/SimPlayground.cpp
    src/ui/TrainingActiveView.cpp
    src/ui/TrainingIdleView.cpp
    src/ui/TrainingUnsavedResultView.cpp
    src/ui/rendering/CellRenderer.cpp
    src/ui/rendering/EntityRenderer.cpp
    src/ui/rendering/FramebufferCapture.cpp
    src/ui/rendering/FractalAnimator.cpp
    src/ui/rendering/JuliaFractal.cpp
    src/ui/rendering/NeuralGridRenderer.cpp
    src/ui/rendering/RenderMode.cpp
    src/ui/rendering/Starfield.cpp
    src/ui/rendering/WebRtcStreamer.cpp
    src/ui/controls/ClockControls.cpp
    src/ui/controls/BrowserPanel.cpp
    src/ui/controls/ControlPanel.cpp
    src/ui/controls/CoreControls.cpp
    src/ui/controls/DuckStopButton.cpp
    src/ui/controls/EvolutionConfigPanel.cpp
    src/ui/controls/EvolutionControls.cpp
    src/ui/controls/ExpandablePanel.cpp
    src/ui/controls/GenomeBrowserPanel.cpp
    src/ui/controls/IconRail.cpp
    src/ui/controls/LogPanel.cpp
    src/ui/controls/NetworkDiagnosticsPanel.cpp
    src/ui/controls/PhysicsControlHelpers.cpp
    src/ui/controls/PhysicsPanel.cpp
    src/ui/controls/RainingControls.cpp
    src/ui/controls/SandboxControls.cpp
    src/ui/controls/ScenarioControlsBase.cpp
    src/ui/controls/ScenarioControlsFactory.cpp
    src/ui/controls/ScenarioPanel.cpp
    src/ui/controls/StartMenuCorePanel.cpp
    src/ui/controls/StartMenuSettingsPanel.cpp
    src/ui/controls/StopPanel.cpp
    src/ui/controls/SparklingDuckButton.cpp
    src/ui/controls/ToggleSlider.cpp
    src/ui/controls/TrainingConfigPanel.cpp
    src/ui/controls/TrainingPopulationPanel.cpp
    src/ui/controls/TrainingResultBrowserPanel.cpp
    src/ui/controls/TreeGerminationControls.cpp
    src/ui/ScenarioMetadataCache.cpp
    src/ui/ui_builders/LVGLBuilder.cpp
    src/ui/widgets/TimeSeriesPlotWidget.cpp
)
target_include_directories(dirtsim-ui-lib PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external ${zpp_bits_SOURCE_DIR})
target_link_libraries(dirtsim-ui-lib PUBLIC dirtsim-core dirtsim-server-lib lvgl_linux lvgl)
target_compile_options(dirtsim-ui-lib PRIVATE ${DIRTSIM_WARNINGS})

add_executable(dirtsim-ui
    src/ui/main.cpp
)
target_link_libraries(dirtsim-ui PRIVATE
    dirtsim-ui-lib
    -Wl,--start-group
    lvgl_linux
    lvgl
    -Wl,--end-group
    lvgl::thorvg
    datachannel-static
    m
    pthread
    ${PKG_CONFIG_LIB}
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
)
target_include_directories(dirtsim-ui PRIVATE ${CMAKE_SOURCE_DIR}/src ${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/external/args ${zpp_bits_SOURCE_DIR})

target_compile_options(dirtsim-ui PRIVATE ${DIRTSIM_WARNINGS})

# Install the lvgl_linux library and its headers
install(DIRECTORY src/lib/
    DESTINATION include/lvgl
    FILES_MATCHING
    PATTERN "backends*" EXCLUDE
    PATTERN "*.h")

install(TARGETS lvgl_linux
    ARCHIVE DESTINATION lib
)

add_custom_target (run COMMAND ${EXECUTABLE_OUTPUT_PATH}/dirtsim-ui DEPENDS dirtsim-ui)

# Headless WebSocket server executable.
add_executable(dirtsim-server
    src/server/main.cpp
)
target_link_libraries(dirtsim-server PRIVATE dirtsim-server-lib m pthread)
target_include_directories(dirtsim-server PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/args ${zpp_bits_SOURCE_DIR})

target_compile_options(dirtsim-server PRIVATE ${DIRTSIM_WARNINGS})

add_custom_target (run-server COMMAND ${EXECUTABLE_OUTPUT_PATH}/dirtsim-server DEPENDS dirtsim-server)

# OS manager executable (privileged system control).
add_executable(dirtsim-os-manager
    src/cli/SubprocessManager.cpp
    src/os-manager/main.cpp
    src/os-manager/OperatingSystemManager.cpp
    src/os-manager/EventProcessor.cpp
    src/os-manager/PeerTrust.cpp
    src/os-manager/api/PeerClientKeyEnsure.cpp
    src/os-manager/api/PeersGet.cpp
    src/os-manager/api/RemoteCliRun.cpp
    src/os-manager/api/Reboot.cpp
    src/os-manager/api/RestartAudio.cpp
    src/os-manager/api/RestartServer.cpp
    src/os-manager/api/RestartUi.cpp
    src/os-manager/api/StartAudio.cpp
    src/os-manager/api/StartServer.cpp
    src/os-manager/api/StartUi.cpp
    src/os-manager/api/StopAudio.cpp
    src/os-manager/api/StopServer.cpp
    src/os-manager/api/StopUi.cpp
    src/os-manager/api/SystemStatus.cpp
    src/os-manager/api/TrustBundleGet.cpp
    src/os-manager/api/TrustPeer.cpp
    src/os-manager/api/UntrustPeer.cpp
    src/os-manager/api/WebSocketAccessSet.cpp
    src/os-manager/api/WebUiAccessSet.cpp
    src/os-manager/network/CommandDeserializerJson.cpp
    src/os-manager/network/PeerAdvertisement.cpp
    src/os-manager/network/PeerDiscovery.cpp
    src/os-manager/network/PeerInfoJson.cpp
    src/os-manager/ssh/RemoteSshExecutor.cpp
    src/os-manager/states/Error.cpp
    src/os-manager/states/Idle.cpp
    src/os-manager/states/Rebooting.cpp
    src/os-manager/states/Startup.cpp
)
target_link_libraries(dirtsim-os-manager PRIVATE dirtsim-core pthread ${AVAHI_LIBRARIES} ${LIBSSH2_TARGET})
target_include_directories(dirtsim-os-manager PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/args ${zpp_bits_SOURCE_DIR} ${AVAHI_INCLUDE_DIRS} ${DIRTSIM_LIBSSH2_INCLUDE_DIRS})
target_compile_options(dirtsim-os-manager PRIVATE ${DIRTSIM_WARNINGS})

# Audio synthesis executable.
add_executable(dirtsim-audio
    src/audio/main.cpp
    src/audio/AudioEngine.cpp
    src/audio/AudioManager.cpp
    src/audio/network/CommandDeserializerJson.cpp
)
target_link_libraries(dirtsim-audio PRIVATE dirtsim-core pthread)
target_include_directories(dirtsim-audio PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/args ${zpp_bits_SOURCE_DIR})
target_compile_options(dirtsim-audio PRIVATE ${DIRTSIM_WARNINGS})

# CLI client executable.
add_executable(cli
    src/cli/main.cpp
    src/cli/BenchmarkRunner.cpp
    src/cli/CleanupRunner.cpp
    src/cli/CommandDispatcher.cpp
    src/cli/FunctionalTestRunner.cpp
    src/cli/GenomeDbBenchmark.cpp
    src/cli/RunAllRunner.cpp
    src/cli/SubprocessManager.cpp
    src/cli/TrainRunner.cpp
    src/os-manager/PeerTrust.cpp
    src/os-manager/network/PeerInfoJson.cpp
    src/os-manager/api/PeerClientKeyEnsure.cpp
    src/os-manager/api/PeersGet.cpp
    src/os-manager/api/RemoteCliRun.cpp
    src/os-manager/api/Reboot.cpp
    src/os-manager/api/RestartAudio.cpp
    src/os-manager/api/RestartServer.cpp
    src/os-manager/api/RestartUi.cpp
    src/os-manager/api/StartAudio.cpp
    src/os-manager/api/StartServer.cpp
    src/os-manager/api/StartUi.cpp
    src/os-manager/api/StopAudio.cpp
    src/os-manager/api/StopServer.cpp
    src/os-manager/api/StopUi.cpp
    src/os-manager/api/SystemStatus.cpp
    src/os-manager/api/TrustBundleGet.cpp
    src/os-manager/api/TrustPeer.cpp
    src/os-manager/api/UntrustPeer.cpp
    src/os-manager/api/WebSocketAccessSet.cpp
    src/os-manager/api/WebUiAccessSet.cpp
)
target_link_libraries(cli PRIVATE dirtsim-server-lib dirtsim-ui-lib datachannel-static pthread)
target_include_directories(cli PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/external/args ${zpp_bits_SOURCE_DIR})

target_compile_options(cli PRIVATE ${DIRTSIM_WARNINGS})

# Test executable (fast unit tests).
add_executable(dirtsim-tests
    src/audio/AudioEngine.cpp
    src/server/tests/StateEvolution_test.cpp
    src/server/tests/StateIdle_test.cpp
    src/server/tests/StateSimRunning_test.cpp
    src/server/tests/StateUnsavedTrainingResult_test.cpp
    src/server/tests/TestStateMachineFixture.cpp
    src/server/tests/UserSettings_test.cpp
    src/server/tests/TrainingResultRepository_test.cpp
    src/tests/MockWebSocketService.cpp
    src/ui/state-machine/tests/StateTraining_test.cpp
    src/ui/state-machine/tests/TestStateMachineFixture.cpp
    src/os-manager/tests/OperatingSystemManager_test.cpp
    src/os-manager/tests/PeerTrust_test.cpp
    src/os-manager/tests/RemoteCliRun_test.cpp
    src/os-manager/tests/StateError_test.cpp
    src/os-manager/tests/StateIdle_test.cpp
    src/os-manager/tests/StateRebooting_test.cpp

    # Os manager sources.
    src/os-manager/OperatingSystemManager.cpp
    src/os-manager/EventProcessor.cpp
    src/os-manager/PeerTrust.cpp
    src/os-manager/api/PeerClientKeyEnsure.cpp
    src/os-manager/api/PeersGet.cpp
    src/os-manager/api/RemoteCliRun.cpp
    src/os-manager/api/Reboot.cpp
    src/os-manager/api/RestartAudio.cpp
    src/os-manager/api/RestartServer.cpp
    src/os-manager/api/RestartUi.cpp
    src/os-manager/api/StartAudio.cpp
    src/os-manager/api/StartServer.cpp
    src/os-manager/api/StartUi.cpp
    src/os-manager/api/StopAudio.cpp
    src/os-manager/api/StopServer.cpp
    src/os-manager/api/StopUi.cpp
    src/os-manager/api/SystemStatus.cpp
    src/os-manager/api/TrustBundleGet.cpp
    src/os-manager/api/TrustPeer.cpp
    src/os-manager/api/UntrustPeer.cpp
    src/os-manager/api/WebSocketAccessSet.cpp
    src/os-manager/api/WebUiAccessSet.cpp
    src/os-manager/network/CommandDeserializerJson.cpp
    src/os-manager/network/PeerAdvertisement.cpp
    src/os-manager/network/PeerDiscovery.cpp
    src/os-manager/network/PeerInfoJson.cpp
    src/os-manager/ssh/RemoteSshExecutor.cpp
    src/os-manager/states/Error.cpp
    src/os-manager/states/Idle.cpp
    src/os-manager/states/Rebooting.cpp
    src/os-manager/states/Startup.cpp
    src/cli/SubprocessManager.cpp
    src/tests/BinaryProtocol_test.cpp
    src/tests/AudioEngine_test.cpp
    src/tests/BresenhamLine_test.cpp
    src/tests/Buoyancy_test.cpp
    src/tests/CacheCorrectness_test.cpp
    src/tests/Cell_serialization_test.cpp
    src/tests/ConfigLoader_test.cpp
    src/tests/Pimpl_test.cpp
    src/tests/ResultTest.cpp
    src/tests/RigidBodyIntegration_test.cpp
    src/tests/StrongType_test.cpp
    src/tests/TimersTest.cpp
    src/tests/Vector2d_test.cpp
    src/tests/Vector2i_test.cpp
    src/tests/WebSocketService_integration_test.cpp
    src/tests/HorizontalMomentum_test.cpp
    src/tests/WaterEqualization_test.cpp
    src/tests/WorldRigidBodyCalculator_test.cpp

    # Bitmap tests.
    src/core/bitmaps/tests/CellBitmap_test.cpp
    src/core/bitmaps/tests/CellBitmap_neighborhood_test.cpp
    src/core/bitmaps/tests/GridOfCells_integration_test.cpp
    src/core/bitmaps/tests/MaterialNeighborhood_test.cpp
    src/core/bitmaps/tests/Neighborhood3x3_test.cpp

    # World light and resize tests.
    src/core/tests/ColorTuning_test.cpp
    src/core/tests/LightManager_test.cpp
    src/core/tests/UUID_test.cpp
    src/core/tests/WorldLightCalculator_test.cpp
    src/core/tests/WorldResize_test.cpp

    # Scenario tests.
    src/core/scenarios/clock_scenario/tests/MarqueeTypes_test.cpp
    src/core/scenarios/tests/ClockScenario_test.cpp
    src/core/scenarios/tests/NesGameAdapterRegistry_test.cpp
    src/core/scenarios/tests/NesPaletteClusterer_test.cpp
    src/core/scenarios/tests/NesRamProbe_test.cpp
    src/core/scenarios/tests/NesSuperTiltBroRamProbe_test.cpp
    src/core/scenarios/tests/NesRomProfileExtractor_test.cpp
    src/core/scenarios/tests/NesFlappyParatroopaScenario_test.cpp

    # Organism tests.
    src/core/organisms/components/LightHandHeld_test.cpp
    src/core/organisms/components/LocalShapeProjection_test.cpp
    src/core/organisms/components/RigidBodyCollisionComponent_test.cpp
    src/core/organisms/components/RigidBodyPhysicsComponent_test.cpp
    src/core/organisms/tests/CantileverSupport_test.cpp
    src/core/organisms/tests/CellTrackerUtil.cpp
    src/core/organisms/tests/Duck_test.cpp
    src/core/organisms/tests/DuckBrain_test.cpp
    src/core/organisms/tests/DuckBuoyancy_test.cpp
    src/core/organisms/tests/DuckJump_test.cpp
    src/core/organisms/tests/DuckLedgeStability_test.cpp
    src/core/organisms/tests/DuckNeuralNetRecurrentBrain_test.cpp
    src/core/organisms/tests/Genome_test.cpp
    src/core/organisms/tests/Goose_test.cpp
    src/core/organisms/tests/MultiCellOrganism_test.cpp
    src/core/organisms/tests/OrganismCollision_test.cpp
    src/core/organisms/tests/OrganismPhysics_test.cpp
    src/core/organisms/tests/OrganismSensoryData_test.cpp
    src/core/organisms/tests/NeuralNetBrain_test.cpp
    src/core/organisms/tests/TreeGermination_test.cpp
    src/core/organisms/tests/TreeManager_test.cpp
    src/core/organisms/tests/TreeNeuralNetwork_test.cpp
    src/core/organisms/tests/TreeSensory_test.cpp
    src/core/organisms/evolution/tests/FitnessCalculator_test.cpp
    src/core/organisms/evolution/tests/FitnessResult_test.cpp
    src/core/organisms/evolution/tests/GenomeRepository_test.cpp
    src/core/organisms/evolution/tests/Mutation_test.cpp
    src/core/organisms/evolution/tests/Selection_test.cpp
    src/core/organisms/evolution/tests/TrainingRunner_test.cpp

    # Core tests.
    src/core/tests/FontSampler_test.cpp
)
target_link_libraries(dirtsim-tests
    PRIVATE
    dirtsim-server-lib
    dirtsim-ui-lib
    GTest::gtest_main
    GTest::gmock_main
    ${AVAHI_LIBRARIES}
    ${LIBSSH2_TARGET}
    m
    pthread
)
target_include_directories(dirtsim-tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${PKG_CONFIG_INC} ${zpp_bits_SOURCE_DIR} ${AVAHI_INCLUDE_DIRS} ${DIRTSIM_LIBSSH2_INCLUDE_DIRS})
target_compile_options(dirtsim-tests PRIVATE ${DIRTSIM_WARNINGS})

# Slow test executable (integration/stress tests that take a long time).
add_executable(dirtsim-tests-slow
    src/tests/DiagonalWaterLeveling_test.cpp
)
target_link_libraries(dirtsim-tests-slow
    PRIVATE
    dirtsim-server-lib
    dirtsim-ui-lib
    GTest::gtest_main
    GTest::gmock_main
    m
    pthread
)
target_include_directories(dirtsim-tests-slow PRIVATE ${CMAKE_SOURCE_DIR}/src ${PKG_CONFIG_INC} ${zpp_bits_SOURCE_DIR})
target_compile_options(dirtsim-tests-slow PRIVATE ${DIRTSIM_WARNINGS})

# Enable testing.
enable_testing()
add_test(NAME dirtsim-tests COMMAND dirtsim-tests)
add_test(NAME dirtsim-tests-slow COMMAND dirtsim-tests-slow)
