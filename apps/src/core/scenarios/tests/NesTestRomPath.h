#pragma once

#include <cstdlib>
#include <filesystem>
#include <optional>

namespace DirtSim::Test {

// Resolve the Flappy Paratroopa ROM path for integration tests. Checks
// DIRTSIM_NES_TEST_ROM_PATH env var first, then falls back to the repo-relative
// testdata path.
inline std::optional<std::filesystem::path> resolveFlappyRomPath()
{
    if (const char* env = std::getenv("DIRTSIM_NES_TEST_ROM_PATH"); env != nullptr) {
        const std::filesystem::path romPath{ env };
        if (std::filesystem::exists(romPath)) {
            return romPath;
        }
    }

    const std::filesystem::path repoRelative =
        std::filesystem::path("testdata") / "roms" / "Flappy.Paratroopa.World.Unl.nes";
    if (std::filesystem::exists(repoRelative)) {
        return repoRelative;
    }

    return std::nullopt;
}

} // namespace DirtSim::Test
