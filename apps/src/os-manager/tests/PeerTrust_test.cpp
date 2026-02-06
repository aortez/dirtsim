#include "os-manager/OperatingSystemManager.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <unistd.h>

using namespace DirtSim;
using namespace DirtSim::OsManager;

namespace {

constexpr const char* kClientPublicKey = "ssh-ed25519 AAAATESTKEY test@unit";

std::filesystem::path makeTempDir(const std::string& suffix)
{
    const auto base = std::filesystem::temp_directory_path();
    const auto pid = static_cast<long>(::getpid());
    const auto path = base / ("dirtsim-peertrust-" + std::to_string(pid) + "-" + suffix);
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return path;
}

void writeFile(const std::filesystem::path& path, const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << contents;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string extractFlagValue(const std::string& command, const std::string& flag)
{
    const auto flagPos = command.find(flag);
    if (flagPos == std::string::npos) {
        return "";
    }
    const auto start = flagPos + flag.size();
    const auto end = command.find(' ', start);
    if (end == std::string::npos) {
        return command.substr(start);
    }
    return command.substr(start, end - start);
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
    mode.dependencies = std::move(dependencies);
    mode.backendConfig.workDir = workDir.string();
    mode.hasBackendConfig = true;
    return mode;
}

} // namespace

TEST(PeerTrustTest, PeerClientKeyEnsureCreatesKeyAndReturnsFingerprint)
{
    const auto rootDir = makeTempDir("ensure");
    const auto workDir = rootDir / "work";
    const auto homeDir = rootDir / "home";

    OperatingSystemManager::Dependencies dependencies;
    dependencies.commandRunner = [workDir](const std::string& command) {
        if (command.find("ssh-keygen -t ed25519") != std::string::npos) {
            const auto keyPath = extractFlagValue(command, "-f ");
            if (keyPath.empty()) {
                return Result<std::string, ApiError>::error(ApiError("Missing -f path"));
            }
            writeFile(keyPath, "PRIVATEKEY");
            writeFile(keyPath + ".pub", std::string(kClientPublicKey) + "\n");
            return Result<std::string, ApiError>::okay("");
        }
        if (command.find("ssh-keygen -l -E sha256 -f /etc/ssh/ssh_host_ecdsa_key.pub")
            != std::string::npos) {
            return Result<std::string, ApiError>::okay("256 SHA256:HOSTFP host (ECDSA)\n");
        }
        if (command.find("ssh-keygen -l -E sha256 -f ") != std::string::npos) {
            return Result<std::string, ApiError>::okay("256 SHA256:CLIENTFP client (ED25519)\n");
        }
        return Result<std::string, ApiError>::error(ApiError("Unexpected command"));
    };

    OperatingSystemManager manager(makeTestMode(workDir, homeDir, std::move(dependencies)));

    const auto result = manager.ensurePeerClientKey();
    ASSERT_TRUE(result.isValue());
    const auto& okay = result.value();
    EXPECT_TRUE(okay.created);
    EXPECT_EQ(okay.public_key, kClientPublicKey);
    EXPECT_EQ(okay.fingerprint_sha256, "SHA256:CLIENTFP");

    const auto keyPath = workDir / "ssh" / "peer_ed25519";
    EXPECT_TRUE(std::filesystem::exists(keyPath));
    EXPECT_TRUE(std::filesystem::exists(keyPath.string() + ".pub"));
}

TEST(PeerTrustTest, TrustBundleGetReturnsHostFingerprintAndClientKey)
{
    const auto rootDir = makeTempDir("bundle");
    const auto workDir = rootDir / "work";
    const auto homeDir = rootDir / "home";

    const auto keyPath = workDir / "ssh" / "peer_ed25519";
    writeFile(keyPath, "PRIVATEKEY");
    writeFile(keyPath.string() + ".pub", std::string(kClientPublicKey) + "\n");

    OperatingSystemManager::Dependencies dependencies;
    dependencies.commandRunner = [](const std::string& command) {
        if (command.find("ssh-keygen -l -E sha256 -f /etc/ssh/ssh_host_ecdsa_key.pub")
            != std::string::npos) {
            return Result<std::string, ApiError>::okay("256 SHA256:HOSTFP host (ECDSA)\n");
        }
        return Result<std::string, ApiError>::error(ApiError("Unexpected command"));
    };

    OperatingSystemManager manager(makeTestMode(workDir, homeDir, std::move(dependencies)));

    const auto result = manager.getTrustBundle();
    ASSERT_TRUE(result.isValue());
    const auto& okay = result.value();
    EXPECT_FALSE(okay.client_key_created);
    EXPECT_EQ(okay.bundle.client_pubkey, kClientPublicKey);
    EXPECT_EQ(okay.bundle.host_fingerprint_sha256, "SHA256:HOSTFP");
    EXPECT_EQ(okay.bundle.ssh_user, "dirtsim");
    EXPECT_EQ(okay.bundle.ssh_port, 22);
    EXPECT_FALSE(okay.bundle.host.empty());
}

TEST(PeerTrustTest, TrustPeerWritesAllowlistAndAuthorizedKeys)
{
    const auto rootDir = makeTempDir("trust");
    const auto workDir = rootDir / "work";
    const auto homeDir = rootDir / "home";

    OperatingSystemManager::Dependencies dependencies;
    OperatingSystemManager manager(makeTestMode(workDir, homeDir, std::move(dependencies)));

    OsApi::TrustPeer::Command command;
    command.bundle.host = "peer1";
    command.bundle.ssh_user = "dirtsim";
    command.bundle.ssh_port = 22;
    command.bundle.host_fingerprint_sha256 = "SHA256:HOSTFP";
    command.bundle.client_pubkey = kClientPublicKey;

    const auto result = manager.trustPeer(command);
    ASSERT_TRUE(result.isValue());
    const auto& okay = result.value();
    EXPECT_TRUE(okay.allowlist_updated);
    EXPECT_TRUE(okay.authorized_key_added);

    const auto allowlistPath = workDir / "peer-allowlist.json";
    const auto allowlistText = readFile(allowlistPath);
    const auto allowlistJson = nlohmann::json::parse(allowlistText);
    ASSERT_TRUE(allowlistJson.is_array());
    ASSERT_EQ(allowlistJson.size(), 1u);
    EXPECT_EQ(allowlistJson[0]["host"], "peer1");

    const auto authorizedKeys = homeDir / ".ssh" / "authorized_keys";
    const auto authorizedText = readFile(authorizedKeys);
    EXPECT_NE(authorizedText.find(kClientPublicKey), std::string::npos);
}

TEST(PeerTrustTest, TrustPeerIsIdempotent)
{
    const auto rootDir = makeTempDir("idempotent");
    const auto workDir = rootDir / "work";
    const auto homeDir = rootDir / "home";

    OperatingSystemManager::Dependencies dependencies;
    OperatingSystemManager manager(makeTestMode(workDir, homeDir, std::move(dependencies)));

    OsApi::TrustPeer::Command command;
    command.bundle.host = "peer1";
    command.bundle.ssh_user = "dirtsim";
    command.bundle.ssh_port = 22;
    command.bundle.host_fingerprint_sha256 = "SHA256:HOSTFP";
    command.bundle.client_pubkey = kClientPublicKey;

    const auto first = manager.trustPeer(command);
    ASSERT_TRUE(first.isValue());

    const auto second = manager.trustPeer(command);
    ASSERT_TRUE(second.isValue());
    const auto& okay = second.value();
    EXPECT_FALSE(okay.allowlist_updated);
    EXPECT_FALSE(okay.authorized_key_added);

    const auto authorizedKeys = homeDir / ".ssh" / "authorized_keys";
    const auto authorizedText = readFile(authorizedKeys);
    const auto firstPos = authorizedText.find(kClientPublicKey);
    const auto secondPos = authorizedText.find(kClientPublicKey, firstPos + 1);
    EXPECT_NE(firstPos, std::string::npos);
    EXPECT_EQ(secondPos, std::string::npos);
}

TEST(PeerTrustTest, TrustPeerIgnoresBundleSshUserForLocalAuthorizedKeys)
{
    const auto rootDir = makeTempDir("ignore-user");
    const auto workDir = rootDir / "work";
    const auto dirtsimHomeDir = rootDir / "home-dirtsim";
    const auto rootHomeDir = rootDir / "home-root";

    OperatingSystemManager::Dependencies dependencies;
    dependencies.homeDirResolver = [dirtsimHomeDir, rootHomeDir](const std::string& user) {
        if (user == "root") {
            return rootHomeDir;
        }
        return dirtsimHomeDir;
    };
    dependencies.sshPermissionsEnsurer =
        [](const std::filesystem::path&, const std::filesystem::path&, const std::string& user) {
            if (user != "dirtsim") {
                return Result<std::monostate, ApiError>::error(
                    ApiError("Unexpected local authorized_keys user: " + user));
            }
            return Result<std::monostate, ApiError>::okay(std::monostate{});
        };

    OperatingSystemManager manager(makeTestMode(workDir, dirtsimHomeDir, std::move(dependencies)));

    OsApi::TrustPeer::Command command;
    command.bundle.host = "peer1";
    command.bundle.ssh_user = "root";
    command.bundle.ssh_port = 22;
    command.bundle.host_fingerprint_sha256 = "SHA256:HOSTFP";
    command.bundle.client_pubkey = kClientPublicKey;

    const auto result = manager.trustPeer(command);
    ASSERT_TRUE(result.isValue());

    const auto dirtsimAuthorizedKeys = dirtsimHomeDir / ".ssh" / "authorized_keys";
    EXPECT_TRUE(std::filesystem::exists(dirtsimAuthorizedKeys));
    EXPECT_NE(readFile(dirtsimAuthorizedKeys).find(kClientPublicKey), std::string::npos);

    const auto rootAuthorizedKeys = rootHomeDir / ".ssh" / "authorized_keys";
    EXPECT_FALSE(std::filesystem::exists(rootAuthorizedKeys));
}

TEST(PeerTrustTest, UntrustPeerRemovesAllowlistAndAuthorizedKey)
{
    const auto rootDir = makeTempDir("untrust");
    const auto workDir = rootDir / "work";
    const auto homeDir = rootDir / "home";

    OperatingSystemManager::Dependencies dependencies;
    OperatingSystemManager manager(makeTestMode(workDir, homeDir, std::move(dependencies)));

    OsApi::TrustPeer::Command trust;
    trust.bundle.host = "peer1";
    trust.bundle.ssh_user = "dirtsim";
    trust.bundle.ssh_port = 22;
    trust.bundle.host_fingerprint_sha256 = "SHA256:HOSTFP";
    trust.bundle.client_pubkey = kClientPublicKey;
    ASSERT_TRUE(manager.trustPeer(trust).isValue());

    OsApi::UntrustPeer::Command untrust;
    untrust.host = "peer1";

    const auto result = manager.untrustPeer(untrust);
    ASSERT_TRUE(result.isValue());
    const auto& okay = result.value();
    EXPECT_TRUE(okay.allowlist_removed);
    EXPECT_TRUE(okay.authorized_key_removed);

    const auto allowlistPath = workDir / "peer-allowlist.json";
    const auto allowlistText = readFile(allowlistPath);
    const auto allowlistJson = nlohmann::json::parse(allowlistText);
    EXPECT_TRUE(allowlistJson.empty());

    const auto authorizedKeys = homeDir / ".ssh" / "authorized_keys";
    const auto authorizedText = readFile(authorizedKeys);
    EXPECT_EQ(authorizedText.find(kClientPublicKey), std::string::npos);
}

TEST(PeerTrustTest, TrustPeerRejectsMissingFields)
{
    const auto rootDir = makeTempDir("reject");
    const auto workDir = rootDir / "work";
    const auto homeDir = rootDir / "home";

    OperatingSystemManager::Dependencies dependencies;
    OperatingSystemManager manager(makeTestMode(workDir, homeDir, std::move(dependencies)));

    OsApi::TrustPeer::Command command;
    command.bundle.host = "peer1";
    command.bundle.ssh_user = "dirtsim";
    command.bundle.ssh_port = 22;
    command.bundle.host_fingerprint_sha256 = "SHA256:HOSTFP";
    command.bundle.client_pubkey = kClientPublicKey;

    auto missingHost = command;
    missingHost.bundle.host.clear();
    EXPECT_TRUE(manager.trustPeer(missingHost).isError());

    auto missingFingerprint = command;
    missingFingerprint.bundle.host_fingerprint_sha256.clear();
    EXPECT_TRUE(manager.trustPeer(missingFingerprint).isError());

    auto missingKey = command;
    missingKey.bundle.client_pubkey.clear();
    EXPECT_TRUE(manager.trustPeer(missingKey).isError());
}

TEST(PeerTrustTest, TrustPeerRejectsMultilineClientPublicKey)
{
    const auto rootDir = makeTempDir("multiline-key");
    const auto workDir = rootDir / "work";
    const auto homeDir = rootDir / "home";

    OperatingSystemManager::Dependencies dependencies;
    OperatingSystemManager manager(makeTestMode(workDir, homeDir, std::move(dependencies)));

    OsApi::TrustPeer::Command command;
    command.bundle.host = "peer1";
    command.bundle.ssh_user = "dirtsim";
    command.bundle.ssh_port = 22;
    command.bundle.host_fingerprint_sha256 = "SHA256:HOSTFP";
    command.bundle.client_pubkey =
        "ssh-ed25519 AAAATESTKEY test@unit\nssh-ed25519 AAAAATTACK attack@unit";

    const auto result = manager.trustPeer(command);
    ASSERT_TRUE(result.isError());
    EXPECT_NE(result.errorValue().message.find("invalid control characters"), std::string::npos);

    EXPECT_FALSE(std::filesystem::exists(workDir / "peer-allowlist.json"));
    EXPECT_FALSE(std::filesystem::exists(homeDir / ".ssh" / "authorized_keys"));
}
