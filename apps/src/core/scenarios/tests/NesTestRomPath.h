#pragma once

#include <cstdlib>
#include <filesystem>
#include <optional>

namespace DirtSim::Test {

inline std::optional<std::filesystem::path> resolveNesTestRomPath(
    const char* envVarName, const std::filesystem::path& repoRelativePath)
{
    if (const char* env = std::getenv(envVarName); env != nullptr) {
        const std::filesystem::path romPath{ env };
        if (std::filesystem::exists(romPath)) {
            return romPath;
        }
    }

    if (std::filesystem::exists(repoRelativePath)) {
        return repoRelativePath;
    }

    return std::nullopt;
}

// Resolve the Flappy Paratroopa ROM path for integration tests. Checks
// DIRTSIM_NES_TEST_ROM_PATH env var first, then falls back to the repo-relative
// testdata path.
inline std::optional<std::filesystem::path> resolveFlappyRomPath()
{
    return resolveNesTestRomPath(
        "DIRTSIM_NES_TEST_ROM_PATH",
        std::filesystem::path("testdata") / "roms" / "Flappy.Paratroopa.World.Unl.nes");
}

// Resolve the SMB ROM path for integration tests. Checks
// DIRTSIM_NES_SMB_TEST_ROM_PATH env var first, then falls back to the repo-relative
// testdata path.
inline std::optional<std::filesystem::path> resolveSmbRomPath()
{
    return resolveNesTestRomPath(
        "DIRTSIM_NES_SMB_TEST_ROM_PATH", std::filesystem::path("testdata") / "roms" / "smb.nes");
}

} // namespace DirtSim::Test
