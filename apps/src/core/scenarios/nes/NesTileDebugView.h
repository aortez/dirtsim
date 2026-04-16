#pragma once

#include <cstdint>

namespace DirtSim {

enum class NesTileDebugView : uint8_t {
    NormalVideo = 0,
    PatternPixels = 1,
    ScreenTokens = 2,
    PlayerRelativeTokens = 3,
    Comparison = 4,
};

constexpr bool isNesTileDebugViewValid(NesTileDebugView view)
{
    switch (view) {
        case NesTileDebugView::NormalVideo:
        case NesTileDebugView::PatternPixels:
        case NesTileDebugView::ScreenTokens:
        case NesTileDebugView::PlayerRelativeTokens:
        case NesTileDebugView::Comparison:
            return true;
    }

    return false;
}

constexpr uint16_t nesTileDebugViewIndex(NesTileDebugView view)
{
    switch (view) {
        case NesTileDebugView::NormalVideo:
            return 0u;
        case NesTileDebugView::PatternPixels:
            return 1u;
        case NesTileDebugView::ScreenTokens:
            return 2u;
        case NesTileDebugView::PlayerRelativeTokens:
            return 3u;
        case NesTileDebugView::Comparison:
            return 4u;
    }

    return 0u;
}

constexpr const char* nesTileDebugViewOptions()
{
    return "Normal\nPattern\nScreen Tokens\nPlayer Relative\nComparison";
}

constexpr NesTileDebugView nesTileDebugViewFromIndex(uint16_t index)
{
    switch (index) {
        case 0u:
            return NesTileDebugView::NormalVideo;
        case 1u:
            return NesTileDebugView::PatternPixels;
        case 2u:
            return NesTileDebugView::ScreenTokens;
        case 3u:
            return NesTileDebugView::PlayerRelativeTokens;
        case 4u:
            return NesTileDebugView::Comparison;
    }

    return NesTileDebugView::NormalVideo;
}

} // namespace DirtSim
