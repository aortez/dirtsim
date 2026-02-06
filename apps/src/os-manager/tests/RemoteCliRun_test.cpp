#include "os-manager/OperatingSystemManager.h"
#include "os-manager/ssh/RemoteSshExecutor.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <unistd.h>

using namespace DirtSim;
using namespace DirtSim::OsManager;

namespace {

std::filesystem::path makeTempDir(const std::string& suffix)
{
    const auto base = std::filesystem::temp_directory_path();
    const auto pid = static_cast<long>(::getpid());
    const auto path = base / ("dirtsim-remotecli-" + std::to_string(pid) + "-" + suffix);
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return path;
}

void writeAllowlist(const std::filesystem::path& workDir, const PeerTrustBundle& bundle)
{
    std::filesystem::create_directories(workDir);
    nlohmann::json json = std::vector<PeerTrustBundle>{ bundle };
    std::ofstream file(workDir / "peer-allowlist.json");
    file << json.dump(2);
}

OperatingSystemManager::TestMode makeTestMode(
    const std::filesystem::path& workDir,
    const std::filesystem::path& homeDir,
    OperatingSystemManager::Dependencies dependencies)
{
    dependencies.homeDirResolver = [homeDir](const std::string&) { return homeDir; };
    if (!dependencies.sshPermissionsEnsurer) {
        dependencies.sshPermissionsEnsurer =
            [](const std::filesystem::path&, const std::filesystem::path&, const std::string&) {
                return Result<std::monostate, ApiError>::okay(std::monostate{});
            };
    }

    OperatingSystemManager::TestMode mode;
    mode.dependencies = dependencies;
    mode.backendConfig.workDir = workDir.string();
    mode.hasBackendConfig = true;
    return mode;
}

PeerTrustBundle makeBundle()
{
    PeerTrustBundle bundle;
    bundle.host = "peer1";
    bundle.ssh_user = "dirtsim";
    bundle.ssh_port = 22;
    bundle.host_fingerprint_sha256 = "SHA256:HOSTFP";
    bundle.client_pubkey = "ssh-ed25519 AAAATESTKEY test@unit";
    return bundle;
}

OsApi::RemoteCliRun::Command makeCommand()
{
    OsApi::RemoteCliRun::Command command;
    command.host = "peer1";
    command.args = { "server", "StatusGet" };
    return command;
}

} // namespace

TEST(RemoteCliRunTest, AllowlistMissingReturnsError)
{
    const auto rootDir = makeTempDir("missing");
    const auto workDir = rootDir / "work";
    const auto homeDir = rootDir / "home";

    OperatingSystemManager::Dependencies dependencies;
    dependencies.remoteCliRunner =
        [](const PeerTrustBundle&, const std::vector<std::string>&, int) {
            return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(ApiError("Unexpected call"));
        };

    OperatingSystemManager manager(makeTestMode(workDir, homeDir, dependencies));

    const auto result = manager.remoteCliRun(makeCommand());
    ASSERT_TRUE(result.isError());
    EXPECT_EQ(result.errorValue().message, "Peer allowlist not found");
}

TEST(RemoteCliRunTest, HostKeyMismatchReturnsError)
{
    const auto rootDir = makeTempDir("fingerprint");
    const auto workDir = rootDir / "work";
    const auto homeDir = rootDir / "home";

    writeAllowlist(workDir, makeBundle());

    OperatingSystemManager::Dependencies dependencies;
    dependencies.remoteCliRunner =
        [](const PeerTrustBundle&, const std::vector<std::string>&, int) {
            return Result<OsApi::RemoteCliRun::Okay, ApiError>::error(
                ApiError("Host fingerprint mismatch"));
        };

    OperatingSystemManager manager(makeTestMode(workDir, homeDir, dependencies));

    const auto result = manager.remoteCliRun(makeCommand());
    ASSERT_TRUE(result.isError());
    EXPECT_EQ(result.errorValue().message, "Host fingerprint mismatch");
}

TEST(RemoteCliRunTest, MissingCliReturnsError)
{
    const auto rootDir = makeTempDir("missing-cli");
    const auto workDir = rootDir / "work";
    const auto homeDir = rootDir / "home";

    writeAllowlist(workDir, makeBundle());

    OperatingSystemManager::Dependencies dependencies;
    dependencies.remoteCliRunner =
        [](const PeerTrustBundle&, const std::vector<std::string>&, int) {
            OsApi::RemoteCliRun::Okay okay;
            okay.exit_code = 127;
            okay.stderr = "dirtsim-cli: not found";
            return Result<OsApi::RemoteCliRun::Okay, ApiError>::okay(okay);
        };

    OperatingSystemManager manager(makeTestMode(workDir, homeDir, dependencies));

    const auto result = manager.remoteCliRun(makeCommand());
    ASSERT_TRUE(result.isError());
    EXPECT_EQ(result.errorValue().message, "dirtsim-cli not found on remote host");
}

TEST(RemoteCliRunTest, NonZeroExitCodeReturnsOkay)
{
    const auto rootDir = makeTempDir("nonzero");
    const auto workDir = rootDir / "work";
    const auto homeDir = rootDir / "home";

    writeAllowlist(workDir, makeBundle());

    OperatingSystemManager::Dependencies dependencies;
    dependencies.remoteCliRunner =
        [](const PeerTrustBundle&, const std::vector<std::string>&, int) {
            OsApi::RemoteCliRun::Okay okay;
            okay.exit_code = 2;
            okay.stdout = "failed";
            okay.elapsed_ms = 12;
            return Result<OsApi::RemoteCliRun::Okay, ApiError>::okay(okay);
        };

    OperatingSystemManager manager(makeTestMode(workDir, homeDir, dependencies));

    const auto result = manager.remoteCliRun(makeCommand());
    ASSERT_TRUE(result.isValue());
    EXPECT_EQ(result.value().exit_code, 2);
    EXPECT_EQ(result.value().stdout, "failed");
}

TEST(RemoteCliRunTest, CommandSerializationRoundTrip)
{
    OsApi::RemoteCliRun::Command command;
    command.host = "dirtsim2";
    command.args = { "server", "GenomeList" };
    command.timeout_ms = 1234;

    const nlohmann::json json = command.toJson();
    const auto decoded = OsApi::RemoteCliRun::Command::fromJson(json);

    EXPECT_EQ(decoded.host, command.host);
    EXPECT_EQ(decoded.args, command.args);
    ASSERT_TRUE(decoded.timeout_ms.has_value());
    EXPECT_EQ(decoded.timeout_ms.value(), 1234);
}

TEST(RemoteCliRunTest, OutputTooLargeReturnsError)
{
    const auto rootDir = makeTempDir("too-large");
    const auto workDir = rootDir / "work";
    const auto homeDir = rootDir / "home";

    writeAllowlist(workDir, makeBundle());

    OperatingSystemManager::Dependencies dependencies;
    dependencies.remoteCliRunner =
        [](const PeerTrustBundle&, const std::vector<std::string>&, int) {
            OsApi::RemoteCliRun::Okay okay;
            okay.exit_code = 0;
            okay.stdout = std::string(RemoteSshExecutor::kMaxStdoutBytes + 1, 'x');
            return Result<OsApi::RemoteCliRun::Okay, ApiError>::okay(okay);
        };

    OperatingSystemManager manager(makeTestMode(workDir, homeDir, dependencies));

    const auto result = manager.remoteCliRun(makeCommand());
    ASSERT_TRUE(result.isError());
    EXPECT_EQ(result.errorValue().message, "Remote CLI output exceeded limit");
}
