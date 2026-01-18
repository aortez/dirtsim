#ifndef WORLD_DIAGRAM_GENERATOR_EMOJI_H
#define WORLD_DIAGRAM_GENERATOR_EMOJI_H

#include <string>

namespace DirtSim {

class World;

class WorldDiagramGeneratorEmoji {
public:
    static std::string generateEmojiDiagram(const World& world);
    static std::string generateMixedDiagram(const World& world);
    static std::string generateAnsiDiagram(
        const World& world, bool useLitColors, bool includeEmoji = false);
};

} // namespace DirtSim

#endif // WORLD_DIAGRAM_GENERATOR_EMOJI_H
