#pragma once

#include "core/scenarios/ClockConfig.h"
#include <functional>
#include <string>

namespace DirtSim {

/**
 * Provides character dimension queries for clock fonts.
 *
 * Encapsulates font-specific width/height logic so layout code
 * doesn't need to know about individual font implementations.
 */
class CharacterMetrics {
public:
    explicit CharacterMetrics(Config::ClockFont font);

    // Character dimensions.
    int getWidth(const std::string& utf8Char) const;
    int getHeight() const;

    // Layout spacing.
    int getGap() const;
    int getColonPadding() const;

    // Font type queries.
    bool isColorFont() const;
    Config::ClockFont getFont() const { return font_; }

    // Convenience: returns a width function suitable for layoutString().
    std::function<int(const std::string&)> widthFunction() const;

private:
    Config::ClockFont font_;

    // Cached dimensions for current font.
    int digitWidth_ = 0;
    int digitHeight_ = 0;
    int colonWidth_ = 0;
    int gap_ = 0;
    int colonPadding_ = 0;

    void initDimensions();
};

} // namespace DirtSim
