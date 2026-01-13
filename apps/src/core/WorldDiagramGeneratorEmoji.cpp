#include "WorldDiagramGeneratorEmoji.h"
#include "Cell.h"
#include "MaterialType.h"
#include "PhysicsSettings.h"
#include "World.h"
#include "WorldData.h"

#include <cmath>
#include <sstream>

using namespace DirtSim;

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
                    switch (cellB->material_type) {
                        case Material::EnumType::AIR:
                            diagram << "⬜";
                            break;
                        case Material::EnumType::DIRT:
                            diagram << "🟫";
                            break;
                        case Material::EnumType::WATER:
                            diagram << "💧";
                            break;
                        case Material::EnumType::WOOD:
                            diagram << "🪵";
                            break;
                        case Material::EnumType::SAND:
                            diagram << "🟨";
                            break;
                        case Material::EnumType::METAL:
                            diagram << "🔩";
                            break;
                        case Material::EnumType::ROOT:
                            diagram << "🌿";
                            break;
                        case Material::EnumType::LEAF:
                            diagram << "🍃";
                            break;
                        case Material::EnumType::SEED:
                            diagram << "🌰";
                            break;
                        case Material::EnumType::WALL:
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
                    switch (cellB->material_type) {
                        case Material::EnumType::AIR:
                            diagram << " ";
                            break;
                        case Material::EnumType::DIRT:
                            diagram << "🟫";
                            break;
                        case Material::EnumType::WATER:
                            diagram << "💧";
                            break;
                        case Material::EnumType::WOOD:
                            diagram << "🪵";
                            break;
                        case Material::EnumType::SAND:
                            diagram << "🟨";
                            break;
                        case Material::EnumType::METAL:
                            diagram << "🔩";
                            break;
                        case Material::EnumType::ROOT:
                            diagram << "🌿";
                            break;
                        case Material::EnumType::LEAF:
                            diagram << "🍃";
                            break;
                        case Material::EnumType::SEED:
                            diagram << "🌰";
                            break;
                        case Material::EnumType::WALL:
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