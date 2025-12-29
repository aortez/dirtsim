#include "core/ConfigLoader.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

using namespace DirtSim;

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

TEST_F(ConfigLoaderTest, LoadReturnsNulloptWhenFileNotFound)
{
    ConfigLoader::setConfigDir(testDir_.string());
    auto result = ConfigLoader::load("nonexistent.json");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ConfigLoaderTest, LoadReturnsJsonWhenFileExists)
{
    writeConfigFile("test.json", R"({"key": "value"})");
    ConfigLoader::setConfigDir(testDir_.string());

    auto result = ConfigLoader::load("test.json");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()["key"], "value");
}

TEST_F(ConfigLoaderTest, LocalFileTakesPrecedenceOverBase)
{
    writeConfigFile("test.json", R"({"source": "base"})");
    writeConfigFile("test.json.local", R"({"source": "local"})");
    ConfigLoader::setConfigDir(testDir_.string());

    auto result = ConfigLoader::load("test.json");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()["source"], "local");
}

TEST_F(ConfigLoaderTest, LoadWithDefaultReturnsDefaultWhenNotFound)
{
    ConfigLoader::setConfigDir(testDir_.string());
    nlohmann::json defaultConfig = {{"default", true}};

    auto result = ConfigLoader::loadWithDefault("missing.json", defaultConfig);
    EXPECT_EQ(result["default"], true);
}

TEST_F(ConfigLoaderTest, LoadWithDefaultReturnsFileWhenExists)
{
    writeConfigFile("test.json", R"({"from_file": true})");
    ConfigLoader::setConfigDir(testDir_.string());
    nlohmann::json defaultConfig = {{"default", true}};

    auto result = ConfigLoader::loadWithDefault("test.json", defaultConfig);
    EXPECT_EQ(result["from_file"], true);
    EXPECT_FALSE(result.contains("default"));
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

TEST_F(ConfigLoaderTest, InvalidJsonReturnsNullopt)
{
    writeConfigFile("bad.json", "not valid json {{{");
    ConfigLoader::setConfigDir(testDir_.string());

    auto result = ConfigLoader::load("bad.json");
    EXPECT_FALSE(result.has_value());
}
