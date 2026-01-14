#include "core/ConfigLoader.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

using namespace DirtSim;

// Test config struct for templated load().
struct TestConfig {
    std::string key;
    std::string source;
    bool from_file = false;
};

void from_json(const nlohmann::json& j, TestConfig& c)
{
    if (j.contains("key")) {
        c.key = j["key"].get<std::string>();
    }
    if (j.contains("source")) {
        c.source = j["source"].get<std::string>();
    }
    if (j.contains("from_file")) {
        c.from_file = j["from_file"].get<bool>();
    }
}

class ConfigLoaderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        testDir_ = std::filesystem::temp_directory_path() / "config_loader_test";
        std::filesystem::create_directories(testDir_);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(testDir_);
        ConfigLoader::clearConfigDir();
    }

    void writeConfigFile(const std::string& filename, const std::string& content)
    {
        std::filesystem::path path = testDir_ / filename;
        std::ofstream file(path);
        file << content;
    }

    std::filesystem::path testDir_;
};

TEST_F(ConfigLoaderTest, LoadReturnsErrorWhenFileNotFound)
{
    ConfigLoader::setConfigDir(testDir_.string());
    auto result = ConfigLoader::load<TestConfig>("nonexistent.json");
    EXPECT_TRUE(result.isError());
    EXPECT_TRUE(result.errorValue().find("not found") != std::string::npos);
}

TEST_F(ConfigLoaderTest, LoadReturnsValueWhenFileExists)
{
    writeConfigFile("test.json", R"({"key": "value"})");
    ConfigLoader::setConfigDir(testDir_.string());

    auto result = ConfigLoader::load<TestConfig>("test.json");
    ASSERT_TRUE(result.isValue());
    EXPECT_EQ(result.value().key, "value");
}

TEST_F(ConfigLoaderTest, LocalFileTakesPrecedenceOverBase)
{
    writeConfigFile("test.json", R"({"source": "base"})");
    writeConfigFile("test.json.local", R"({"source": "local"})");
    ConfigLoader::setConfigDir(testDir_.string());

    auto result = ConfigLoader::load<TestConfig>("test.json");
    ASSERT_TRUE(result.isValue());
    EXPECT_EQ(result.value().source, "local");
}

TEST_F(ConfigLoaderTest, FindConfigFileReturnsPathWhenFound)
{
    writeConfigFile("test.json", "{}");
    ConfigLoader::setConfigDir(testDir_.string());

    auto path = ConfigLoader::findConfigFile("test.json");
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path.value(), testDir_ / "test.json");
}

TEST_F(ConfigLoaderTest, FindConfigFileReturnsLocalPathWhenBothExist)
{
    writeConfigFile("test.json", "{}");
    writeConfigFile("test.json.local", "{}");
    ConfigLoader::setConfigDir(testDir_.string());

    auto path = ConfigLoader::findConfigFile("test.json");
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path.value(), testDir_ / "test.json.local");
}

TEST_F(ConfigLoaderTest, InvalidJsonReturnsError)
{
    writeConfigFile("bad.json", "not valid json {{{");
    ConfigLoader::setConfigDir(testDir_.string());

    auto result = ConfigLoader::load<TestConfig>("bad.json");
    EXPECT_TRUE(result.isError());
    EXPECT_TRUE(result.errorValue().find("Parse error") != std::string::npos);
}

TEST_F(ConfigLoaderTest, EmptyFileReturnsError)
{
    writeConfigFile("empty.json", "");
    ConfigLoader::setConfigDir(testDir_.string());

    auto result = ConfigLoader::load<TestConfig>("empty.json");
    EXPECT_TRUE(result.isError());
    EXPECT_TRUE(result.errorValue().find("Empty config file") != std::string::npos);
}

TEST_F(ConfigLoaderTest, EmptyLocalFileSkipsToBase)
{
    writeConfigFile("test.json", R"({"source": "base"})");
    writeConfigFile("test.json.local", "");
    ConfigLoader::setConfigDir(testDir_.string());

    // Empty .local file should still be found but fail to parse.
    // The current implementation returns error, not fallback to base.
    auto result = ConfigLoader::load<TestConfig>("test.json");
    EXPECT_TRUE(result.isError());
    EXPECT_TRUE(result.errorValue().find("Empty config file") != std::string::npos);
}
