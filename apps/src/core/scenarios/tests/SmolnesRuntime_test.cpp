#include "core/scenarios/nes/SmolnesRuntime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using namespace DirtSim;

namespace {

constexpr uint16_t kProgramStart = 0x8000u;
constexpr uint16_t kSpriteHitResultAddr = 0x0000u;
constexpr uint8_t kSpritePaletteIndex = 0x02u;
constexpr uint8_t kSpriteScreenX = 32u;
constexpr uint8_t kSpriteScreenY = 32u;
constexpr uint8_t kSpriteOamY = kSpriteScreenY - 1u;

class TestRomAssembler {
public:
    void bytes(std::initializer_list<uint8_t> bytesToAppend)
    {
        bytes_.insert(bytes_.end(), bytesToAppend.begin(), bytesToAppend.end());
    }

    void label(std::string_view name) { labels_[std::string(name)] = pc(); }

    void ldaAbsXLabel(std::string_view labelName)
    {
        bytes({ 0xBDu, 0x00u, 0x00u });
        unresolvedAbsolute_.push_back(
            AbsolutePatch{ .operandOffset = bytes_.size() - 2u, .label = std::string(labelName) });
    }

    void ldaImmStaAbs(uint8_t value, uint16_t addr)
    {
        bytes({ 0xA9u, value, 0x8Du, lowByte(addr), highByte(addr) });
    }

    void branchToLabel(uint8_t opcode, std::string_view labelName)
    {
        bytes({ opcode, 0x00u });
        unresolvedRelative_.push_back(
            RelativePatch{ .operandOffset = bytes_.size() - 1u, .label = std::string(labelName) });
    }

    void jmpLabel(std::string_view labelName)
    {
        bytes({ 0x4Cu, 0x00u, 0x00u });
        unresolvedAbsolute_.push_back(
            AbsolutePatch{ .operandOffset = bytes_.size() - 2u, .label = std::string(labelName) });
    }

    std::vector<uint8_t> build() const
    {
        std::vector<uint8_t> output = bytes_;

        for (const AbsolutePatch& patch : unresolvedAbsolute_) {
            const auto labelIt = labels_.find(patch.label);
            EXPECT_NE(labelIt, labels_.end());
            if (labelIt == labels_.end()) {
                return output;
            }
            const uint16_t target = labelIt->second;
            output[patch.operandOffset] = lowByte(target);
            output[patch.operandOffset + 1u] = highByte(target);
        }

        for (const RelativePatch& patch : unresolvedRelative_) {
            const auto labelIt = labels_.find(patch.label);
            EXPECT_NE(labelIt, labels_.end());
            if (labelIt == labels_.end()) {
                return output;
            }
            const int32_t target = static_cast<int32_t>(labelIt->second);
            const int32_t nextPc = static_cast<int32_t>(
                kProgramStart + static_cast<uint16_t>(patch.operandOffset + 1u));
            const int32_t offset = target - nextPc;
            EXPECT_GE(offset, -128);
            EXPECT_LE(offset, 127);
            if (offset < -128 || offset > 127) {
                return output;
            }
            output[patch.operandOffset] = static_cast<uint8_t>(static_cast<int8_t>(offset));
        }

        return output;
    }

private:
    struct AbsolutePatch {
        size_t operandOffset = 0;
        std::string label;
    };

    struct RelativePatch {
        size_t operandOffset = 0;
        std::string label;
    };

    static uint8_t highByte(uint16_t value) { return static_cast<uint8_t>(value >> 8); }
    static uint8_t lowByte(uint16_t value) { return static_cast<uint8_t>(value & 0xFFu); }

    uint16_t pc() const { return kProgramStart + static_cast<uint16_t>(bytes_.size()); }

    std::vector<uint8_t> bytes_;
    std::map<std::string, uint16_t, std::less<>> labels_;
    std::vector<AbsolutePatch> unresolvedAbsolute_;
    std::vector<RelativePatch> unresolvedRelative_;
};

std::array<uint8_t, 32> buildPaletteData()
{
    std::array<uint8_t, 32> palette{};
    palette.fill(0x0Fu);
    palette[0x01] = 0x01u;
    palette[0x11] = kSpritePaletteIndex;
    return palette;
}

std::filesystem::path writeSpriteMaskTestRom(const char* stem, bool spritesEnabled)
{
    TestRomAssembler assembler;
    assembler.bytes({ 0x78u, 0xD8u, 0xA2u, 0xFFu, 0x9Au });
    assembler.ldaImmStaAbs(0x00u, 0x2000u);
    assembler.ldaImmStaAbs(0x00u, 0x2001u);
    assembler.ldaImmStaAbs(0x00u, kSpriteHitResultAddr);
    assembler.ldaImmStaAbs(0x00u, 0x2003u);
    assembler.ldaImmStaAbs(kSpriteOamY, 0x2004u);
    assembler.ldaImmStaAbs(0x01u, 0x2004u);
    assembler.ldaImmStaAbs(0x00u, 0x2004u);
    assembler.ldaImmStaAbs(kSpriteScreenX, 0x2004u);
    assembler.ldaImmStaAbs(0x3Fu, 0x2006u);
    assembler.ldaImmStaAbs(0x00u, 0x2006u);
    assembler.bytes({ 0xA2u, 0x00u });
    assembler.label("paletteLoop");
    assembler.ldaAbsXLabel("paletteData");
    assembler.bytes({ 0x8Du, 0x07u, 0x20u, 0xE8u, 0xE0u, 0x20u });
    assembler.branchToLabel(0xD0u, "paletteLoop");
    assembler.ldaImmStaAbs(spritesEnabled ? 0x18u : 0x08u, 0x2001u);
    assembler.label("waitNextVblank");
    assembler.bytes({ 0x2Cu, 0x02u, 0x20u });
    assembler.branchToLabel(0x10u, "waitNextVblank");
    assembler.label("monitorFrame");
    assembler.bytes({ 0x2Cu, 0x02u, 0x20u });
    assembler.branchToLabel(0x70u, "spriteHit");
    assembler.branchToLabel(0x30u, "noSpriteHit");
    assembler.jmpLabel("monitorFrame");
    assembler.label("spriteHit");
    assembler.ldaImmStaAbs(0x01u, kSpriteHitResultAddr);
    assembler.jmpLabel("done");
    assembler.label("noSpriteHit");
    assembler.ldaImmStaAbs(0x00u, kSpriteHitResultAddr);
    assembler.jmpLabel("done");
    assembler.label("done");
    assembler.jmpLabel("done");
    assembler.label("paletteData");
    const auto palette = buildPaletteData();
    for (uint8_t value : palette) {
        assembler.bytes({ value });
    }

    std::vector<uint8_t> prg = assembler.build();
    prg.resize(16u * 1024u, 0xEAu);
    prg[0x3FFAu] = static_cast<uint8_t>(kProgramStart & 0xFFu);
    prg[0x3FFBu] = static_cast<uint8_t>(kProgramStart >> 8);
    prg[0x3FFCu] = static_cast<uint8_t>(kProgramStart & 0xFFu);
    prg[0x3FFDu] = static_cast<uint8_t>(kProgramStart >> 8);
    prg[0x3FFEu] = static_cast<uint8_t>(kProgramStart & 0xFFu);
    prg[0x3FFFu] = static_cast<uint8_t>(kProgramStart >> 8);

    std::vector<uint8_t> chr(8u * 1024u, 0x00u);
    for (int row = 0; row < 8; ++row) {
        chr[row] = 0xFFu;
        chr[16u + static_cast<size_t>(row)] = 0xFFu;
    }

    const std::filesystem::path romPath =
        std::filesystem::path(::testing::TempDir()) / (std::string(stem) + ".nes");
    std::ofstream stream(romPath, std::ios::binary | std::ios::trunc);
    EXPECT_TRUE(stream.is_open());

    const std::array<uint8_t, 16> header = {
        'N',   'E',   'S',   0x1A,  0x01u, 0x01u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    };
    stream.write(
        reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    stream.write(
        reinterpret_cast<const char*>(prg.data()), static_cast<std::streamsize>(prg.size()));
    stream.write(
        reinterpret_cast<const char*>(chr.data()), static_cast<std::streamsize>(chr.size()));
    EXPECT_TRUE(stream.good());
    return romPath;
}

struct SpriteMaskObservation {
    bool sawSpritePalette = false;
    uint8_t spriteHitResult = 0u;
};

SpriteMaskObservation runSpriteMaskRom(const std::filesystem::path& romPath)
{
    SmolnesRuntime runtime;
    if (!runtime.start(romPath.string())) {
        ADD_FAILURE() << "Failed to start runtime for " << romPath;
        return {};
    }
    runtime.setApuEnabled(false);
    if (!runtime.runFrames(3u, 2000u)) {
        ADD_FAILURE() << "Failed to run frames for " << romPath << ": " << runtime.getLastError();
        runtime.stop();
        return {};
    }

    const auto paletteFrame = runtime.copyLatestPaletteFrame();
    if (!paletteFrame.has_value()) {
        ADD_FAILURE() << "Missing palette frame for " << romPath;
        runtime.stop();
        return {};
    }
    const auto memorySnapshot = runtime.copyMemorySnapshot();
    if (!memorySnapshot.has_value()) {
        ADD_FAILURE() << "Missing memory snapshot for " << romPath;
        runtime.stop();
        return {};
    }
    runtime.stop();

    EXPECT_LT(static_cast<size_t>(kSpriteHitResultAddr), memorySnapshot->cpuRam.size());

    return SpriteMaskObservation{
        .sawSpritePalette =
            std::find(
                paletteFrame->indices.begin(), paletteFrame->indices.end(), kSpritePaletteIndex)
            != paletteFrame->indices.end(),
        .spriteHitResult = memorySnapshot->cpuRam[kSpriteHitResultAddr],
    };
}

} // namespace

TEST(SmolnesRuntimeTest, SpriteMaskDisablesSpritePixelsAndSpriteZeroHit)
{
    const std::filesystem::path spritesDisabledRom =
        writeSpriteMaskTestRom("smolnes_sprite_mask_disabled", false);
    const std::filesystem::path spritesEnabledRom =
        writeSpriteMaskTestRom("smolnes_sprite_mask_enabled", true);

    const SpriteMaskObservation spritesDisabled = runSpriteMaskRom(spritesDisabledRom);
    const SpriteMaskObservation spritesEnabled = runSpriteMaskRom(spritesEnabledRom);

    EXPECT_FALSE(spritesDisabled.sawSpritePalette);
    EXPECT_EQ(spritesDisabled.spriteHitResult, 0u);
    EXPECT_TRUE(spritesEnabled.sawSpritePalette);
    EXPECT_EQ(spritesEnabled.spriteHitResult, 1u);
}

TEST(SmolnesRuntimeTest, CopyPpuSnapshotProvidesChrAndNametableState)
{
    const std::filesystem::path romPath = writeSpriteMaskTestRom("smolnes_ppu_snapshot", true);

    SmolnesRuntime runtime;
    ASSERT_TRUE(runtime.start(romPath.string())) << runtime.getLastError();
    runtime.setApuEnabled(false);
    ASSERT_TRUE(runtime.runFrames(3u, 2000u)) << runtime.getLastError();

    const auto snapshot = runtime.copyPpuSnapshot();
    runtime.stop();

    ASSERT_TRUE(snapshot.has_value());
    EXPECT_GT(snapshot->frameId, 0u);
    EXPECT_EQ(snapshot->chr.size(), NesPpuSnapshot::ChrBytes);
    EXPECT_EQ(snapshot->oam.size(), NesPpuSnapshot::OamBytes);
    EXPECT_EQ(snapshot->vram.size(), NesPpuSnapshot::VramBytes);
    EXPECT_TRUE(std::any_of(snapshot->chr.begin(), snapshot->chr.end(), [](uint8_t value) {
        return value != 0u;
    }));
}
