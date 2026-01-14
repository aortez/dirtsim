#include "CharacterMetrics.h"

#include "core/scenarios/ClockFontPatterns.h"

namespace DirtSim {

CharacterMetrics::CharacterMetrics(Config::ClockFont font) : font_(font)
{
    initDimensions();
}

void CharacterMetrics::initDimensions()
{
    switch (font_) {
        case Config::ClockFont::DotMatrix:
            digitWidth_ = ClockFonts::DOT_MATRIX_WIDTH;
            digitHeight_ = ClockFonts::DOT_MATRIX_HEIGHT;
            colonWidth_ = ClockFonts::DOT_MATRIX_COLON_WIDTH;
            gap_ = ClockFonts::DOT_MATRIX_GAP;
            colonPadding_ = ClockFonts::DOT_MATRIX_COLON_PADDING;
            break;

        case Config::ClockFont::Montserrat24:
            digitWidth_ = ClockFonts::MONTSERRAT24_WIDTH;
            digitHeight_ = ClockFonts::MONTSERRAT24_HEIGHT;
            colonWidth_ = ClockFonts::MONTSERRAT24_COLON_WIDTH;
            gap_ = ClockFonts::MONTSERRAT24_GAP;
            colonPadding_ = ClockFonts::MONTSERRAT24_COLON_PADDING;
            break;

        case Config::ClockFont::NotoColorEmoji:
            digitWidth_ = ClockFonts::NOTO_EMOJI_WIDTH;
            digitHeight_ = ClockFonts::NOTO_EMOJI_HEIGHT;
            colonWidth_ = ClockFonts::NOTO_EMOJI_COLON_WIDTH;
            gap_ = ClockFonts::NOTO_EMOJI_GAP;
            colonPadding_ = ClockFonts::NOTO_EMOJI_COLON_PADDING;
            break;

        case Config::ClockFont::Segment7:
            digitWidth_ = ClockFonts::SEGMENT7_WIDTH;
            digitHeight_ = ClockFonts::SEGMENT7_HEIGHT;
            colonWidth_ = ClockFonts::SEGMENT7_COLON_WIDTH;
            gap_ = ClockFonts::SEGMENT7_GAP;
            colonPadding_ = ClockFonts::SEGMENT7_COLON_PADDING;
            break;

        case Config::ClockFont::Segment7ExtraTall:
            digitWidth_ = ClockFonts::SEGMENT7_EXTRA_TALL_WIDTH;
            digitHeight_ = ClockFonts::SEGMENT7_EXTRA_TALL_HEIGHT;
            colonWidth_ = ClockFonts::SEGMENT7_EXTRA_TALL_COLON_WIDTH;
            gap_ = ClockFonts::SEGMENT7_EXTRA_TALL_GAP;
            colonPadding_ = ClockFonts::SEGMENT7_EXTRA_TALL_COLON_PADDING;
            break;

        case Config::ClockFont::Segment7Jumbo:
            digitWidth_ = ClockFonts::SEGMENT7_JUMBO_WIDTH;
            digitHeight_ = ClockFonts::SEGMENT7_JUMBO_HEIGHT;
            colonWidth_ = ClockFonts::SEGMENT7_JUMBO_COLON_WIDTH;
            gap_ = ClockFonts::SEGMENT7_JUMBO_GAP;
            colonPadding_ = ClockFonts::SEGMENT7_JUMBO_COLON_PADDING;
            break;

        case Config::ClockFont::Segment7Large:
            digitWidth_ = ClockFonts::SEGMENT7_LARGE_WIDTH;
            digitHeight_ = ClockFonts::SEGMENT7_LARGE_HEIGHT;
            colonWidth_ = ClockFonts::SEGMENT7_LARGE_COLON_WIDTH;
            gap_ = ClockFonts::SEGMENT7_LARGE_GAP;
            colonPadding_ = ClockFonts::SEGMENT7_LARGE_COLON_PADDING;
            break;

        case Config::ClockFont::Segment7Tall:
            digitWidth_ = ClockFonts::SEGMENT7_TALL_WIDTH;
            digitHeight_ = ClockFonts::SEGMENT7_TALL_HEIGHT;
            colonWidth_ = ClockFonts::SEGMENT7_TALL_COLON_WIDTH;
            gap_ = ClockFonts::SEGMENT7_TALL_GAP;
            colonPadding_ = ClockFonts::SEGMENT7_TALL_COLON_PADDING;
            break;
    }
}

int CharacterMetrics::getWidth(const std::string& utf8Char) const
{
    if (utf8Char.empty()) {
        return 0;
    }

    // Colon has special width.
    if (utf8Char == ":") {
        return colonWidth_;
    }

    // Space uses gap width.
    if (utf8Char == " ") {
        return gap_;
    }

    // All other characters use digit width.
    return digitWidth_;
}

int CharacterMetrics::getHeight() const
{
    return digitHeight_;
}

int CharacterMetrics::getGap() const
{
    return gap_;
}

int CharacterMetrics::getColonPadding() const
{
    return colonPadding_;
}

bool CharacterMetrics::isColorFont() const
{
    return font_ == Config::ClockFont::NotoColorEmoji;
}

std::function<int(const std::string&)> CharacterMetrics::widthFunction() const
{
    // Capture by value since CharacterMetrics might be temporary.
    return [metrics = *this](const std::string& utf8Char) { return metrics.getWidth(utf8Char); };
}

} // namespace DirtSim
