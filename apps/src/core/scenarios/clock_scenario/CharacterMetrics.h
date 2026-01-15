#pragma once

#include "core/scenarios/ClockConfig.h"
#include <functional>
#include <string>

namespace DirtSim {

/**
 * Font dimension and layout properties for clock rendering.
 *
 * Use getFont() for convenient access: getFont(Config::ClockFont::Segment7).digitHeight
 */
class CharacterMetrics {
public:
    explicit CharacterMetrics(Config::ClockFont font);

    Config::ClockFont font;
    int colonPadding = 0;
    int colonWidth = 0;
    int digitHeight = 0;
    int digitWidth = 0;
    int gap = 0;

    bool isColorFont() const;
    bool usesFontSampler() const;
    int charWidth(const std::string& utf8Char) const;
    std::function<int(const std::string&)> widthFunction() const;
};

const CharacterMetrics& getFont(Config::ClockFont font);

} // namespace DirtSim
