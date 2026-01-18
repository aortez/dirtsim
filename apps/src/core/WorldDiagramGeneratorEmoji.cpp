#include "WorldDiagramGeneratorEmoji.h"
#include "Cell.h"
#include "ColorNames.h"
#include "MaterialType.h"
#include "PhysicsSettings.h"
#include "World.h"
#include "WorldData.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using namespace DirtSim;

namespace {

ColorNames::RgbF getMaterialBaseColor(Material::EnumType material)
{
    using ColorNames::toRgbF;
    switch (material) {
        case Material::EnumType::Air:
            return toRgbF(ColorNames::white());
        case Material::EnumType::Dirt:
            return toRgbF(ColorNames::dirt());
        case Material::EnumType::Leaf:
            return toRgbF(ColorNames::leaf());
        case Material::EnumType::Metal:
            return toRgbF(ColorNames::metal());
        case Material::EnumType::Root:
            return toRgbF(ColorNames::root());
        case Material::EnumType::Sand:
            return toRgbF(ColorNames::sand());
        case Material::EnumType::Seed:
            return toRgbF(ColorNames::seed());
        case Material::EnumType::Wall:
            return toRgbF(ColorNames::stone());
        case Material::EnumType::Water:
            return toRgbF(ColorNames::water());
        case Material::EnumType::Wood:
            return toRgbF(ColorNames::wood());
        default:
            return toRgbF(ColorNames::white());
    }
}

const char* kAnsiReset = "\x1b[0m";

std::vector<std::string> splitDiagramLines(const std::string& diagram)
{
    std::vector<std::string> lines;
    std::istringstream stream(diagram);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    if (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

} // namespace

std::string WorldDiagramGeneratorEmoji::generateEmojiDiagram(const World& world)
{
    std::ostringstream diagram;

    uint32_t width = world.getData().width;
    uint32_t height = world.getData().height;

    // Top border with sparkles!
    diagram << "✨";
    for (uint32_t x = 0; x < width; ++x) {
        diagram << "━━";
        if (x < width - 1) {
            diagram << "━"; // Extra for spacing between cells.
        }
    }
    diagram << "✨\n";

    // Each row.
    for (uint32_t y = 0; y < height; ++y) {
        diagram << "┃";

        for (uint32_t x = 0; x < width; ++x) {
            const auto& cell = world.getData().at(x, y);

            if (cell.isEmpty()) {
                diagram << "⬜";
            }
            else {
                // Get material type and fill ratio.
                auto cellB = dynamic_cast<const Cell*>(&cell);
                if (cellB) {
                    const Material::EnumType renderMaterial = cellB->getRenderMaterial();
                    switch (renderMaterial) {
                        case Material::EnumType::Air:
                            diagram << "⬜";
                            break;
                        case Material::EnumType::Dirt:
                            diagram << "🟫";
                            break;
                        case Material::EnumType::Water:
                            diagram << "💧";
                            break;
                        case Material::EnumType::Wood:
                            diagram << "🪵";
                            break;
                        case Material::EnumType::Sand:
                            diagram << "🟨";
                            break;
                        case Material::EnumType::Metal:
                            diagram << "🔩";
                            break;
                        case Material::EnumType::Root:
                            diagram << "🌿";
                            break;
                        case Material::EnumType::Leaf:
                            diagram << "🍃";
                            break;
                        case Material::EnumType::Seed:
                            diagram << "🌰";
                            break;
                        case Material::EnumType::Wall:
                            diagram << "🧱";
                            break;
                        default:
                            diagram << "❓";
                            break;
                    }
                }
            }

            if (x < width - 1) {
                diagram << " ";
            }
        }

        diagram << "┃\n";
    }

    // Bottom border.
    diagram << "✨";
    for (uint32_t x = 0; x < width; ++x) {
        diagram << "━━";
        if (x < width - 1) {
            diagram << "━"; // Extra for spacing between cells.
        }
    }
    diagram << "✨\n";

    return diagram.str();
}

std::string WorldDiagramGeneratorEmoji::generateMixedDiagram(const World& world)
{
    std::ostringstream diagram;

    uint32_t width = world.getData().width;
    uint32_t height = world.getData().height;

    // Top border.
    diagram << "🦆✨ Sparkle Duck World ✨🦆\n";
    diagram << "┌";
    for (uint32_t x = 0; x < width; ++x) {
        diagram << "───";
        if (x < width - 1) diagram << "┬";
    }
    diagram << "┐\n";

    // Each row.
    for (uint32_t y = 0; y < height; ++y) {
        diagram << "│";

        for (uint32_t x = 0; x < width; ++x) {
            const auto& cell = world.getData().at(x, y);

            if (cell.isEmpty()) {
                diagram << "   ";
            }
            else {
                // Get material type and fill ratio.
                auto cellB = dynamic_cast<const Cell*>(&cell);
                if (cellB) {
                    float fill = cellB->fill_ratio;

                    // Material emoji.
                    const Material::EnumType renderMaterial = cellB->getRenderMaterial();
                    switch (renderMaterial) {
                        case Material::EnumType::Air:
                            diagram << " ";
                            break;
                        case Material::EnumType::Dirt:
                            diagram << "🟫";
                            break;
                        case Material::EnumType::Water:
                            diagram << "💧";
                            break;
                        case Material::EnumType::Wood:
                            diagram << "🪵";
                            break;
                        case Material::EnumType::Sand:
                            diagram << "🟨";
                            break;
                        case Material::EnumType::Metal:
                            diagram << "🔩";
                            break;
                        case Material::EnumType::Root:
                            diagram << "🌿";
                            break;
                        case Material::EnumType::Leaf:
                            diagram << "🍃";
                            break;
                        case Material::EnumType::Seed:
                            diagram << "🌰";
                            break;
                        case Material::EnumType::Wall:
                            diagram << "🧱";
                            break;
                        default:
                            diagram << "❓";
                            break;
                    }

                    // Fill level indicator.
                    if (fill < 0.25) {
                        diagram << "░";
                    }
                    else if (fill < 0.5) {
                        diagram << "▒";
                    }
                    else if (fill < 0.75) {
                        diagram << "▓";
                    }
                    else {
                        diagram << "█";
                    }
                }
                else {
                    // Safety fallback for unexpected cell type.
                    diagram << "? ";
                }
            }

            if (x < width - 1) {
                diagram << "│";
            }
        }

        diagram << "│\n";

        // Horizontal divider (except last row).
        if (y < height - 1) {
            diagram << "├";
            for (uint32_t x = 0; x < width; ++x) {
                diagram << "───";
                if (x < width - 1) {
                    diagram << "┼";
                }
            }
            diagram << "┤\n";
        }
    }

    // Bottom border.
    diagram << "└";
    for (uint32_t x = 0; x < width; ++x) {
        diagram << "───";
        if (x < width - 1) diagram << "┴";
    }
    diagram << "┘\n";

    return diagram.str();
}

std::string WorldDiagramGeneratorEmoji::generateAnsiDiagram(
    const World& world, bool useLitColors, bool includeEmoji)
{
    std::ostringstream ansiDiagram;

    const auto& data = world.getData();
    const int width = data.width;
    const int height = data.height;
    const size_t expectedSize = static_cast<size_t>(width) * height;
    const bool hasLitColors = useLitColors && data.colors.width == width
        && data.colors.height == height && data.colors.size() == expectedSize;

    ansiDiagram << kAnsiReset << "+";
    for (int x = 0; x < width * 2; ++x) {
        ansiDiagram << "-";
    }
    ansiDiagram << "+\n";

    for (int y = 0; y < height; ++y) {
        ansiDiagram << "|";

        for (int x = 0; x < width; ++x) {
            const Cell& cell = data.at(x, y);
            Material::EnumType renderMaterial = cell.getRenderMaterial();
            if (cell.isEmpty()) {
                renderMaterial = Material::EnumType::Air;
            }

            const ColorNames::RgbF color =
                hasLitColors ? data.colors.at(x, y) : getMaterialBaseColor(renderMaterial);
            const uint32_t rgba = ColorNames::toRgba(color);
            ansiDiagram << "\x1b[48;2;" << static_cast<int>(ColorNames::getR(rgba)) << ";"
                        << static_cast<int>(ColorNames::getG(rgba)) << ";"
                        << static_cast<int>(ColorNames::getB(rgba)) << "m  ";
        }

        ansiDiagram << kAnsiReset << "|\n";
    }

    ansiDiagram << "+";
    for (int x = 0; x < width * 2; ++x) {
        ansiDiagram << "-";
    }
    ansiDiagram << "+\n";

    const std::string ansiOutput = ansiDiagram.str();
    if (!includeEmoji) {
        return ansiOutput;
    }

    const std::string emojiOutput = generateEmojiDiagram(world);
    const auto ansiLines = splitDiagramLines(ansiOutput);
    const auto emojiLines = splitDiagramLines(emojiOutput);
    const size_t lineCount = std::max(ansiLines.size(), emojiLines.size());
    std::ostringstream combined;

    for (size_t i = 0; i < lineCount; ++i) {
        if (i < ansiLines.size()) {
            combined << ansiLines[i];
        }
        if (i < emojiLines.size()) {
            if (i < ansiLines.size()) {
                combined << "  ";
            }
            combined << emojiLines[i];
        }
        combined << "\n";
    }

    return combined.str();
}
