#include "core/scenarios/nes/NesPaletteClusterer.h"

#include "core/scenarios/nes/NesPaletteTable.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace DirtSim {

namespace {

constexpr int kClusterCount = DuckSensoryData::NUM_MATERIALS;
constexpr int kPaletteIndexCount = 64;
constexpr int kObserveFramesForClustering = 60;
constexpr int kKMeansIterations = 8;

struct Rgb8 {
    int r = 0;
    int g = 0;
    int b = 0;
};

Rgb8 decodeBgr565(uint16_t value)
{
    const int blue5 = (value >> 11) & 0x1F;
    const int green6 = (value >> 5) & 0x3F;
    const int red5 = value & 0x1F;

    const int r = (red5 * 255 + 15) / 31;
    const int g = (green6 * 255 + 31) / 63;
    const int b = (blue5 * 255 + 15) / 31;
    return Rgb8{ .r = r, .g = g, .b = b };
}

int distanceSquared(const Rgb8& a, const Rgb8& b)
{
    const int dr = a.r - b.r;
    const int dg = a.g - b.g;
    const int db = a.b - b.b;
    return (dr * dr) + (dg * dg) + (db * db);
}

int luminance(const Rgb8& rgb)
{
    // Integer approximation of luma: (0.299 * R + 0.587 * G + 0.114 * B).
    return (77 * rgb.r + 150 * rgb.g + 29 * rgb.b + 128) / 256;
}

std::array<Rgb8, kPaletteIndexCount> makePaletteRgb()
{
    std::array<Rgb8, kPaletteIndexCount> rgb{};
    for (size_t i = 0; i < rgb.size(); ++i) {
        rgb[i] = decodeBgr565(kNesBgr565Palette[i]);
    }
    return rgb;
}

} // namespace

NesPaletteClusterer::NesPaletteClusterer()
{
    buildFallbackMapping();
    reset();
}

void NesPaletteClusterer::reset(const std::string& romId)
{
    romId_ = romId;
    ready_ = false;
    observedFrameCount_ = 0;
    indexCounts_.fill(0);
    indexToCluster_ = fallbackIndexToCluster_;
    lastFrameId_.reset();
}

void NesPaletteClusterer::observeFrame(const NesPaletteFrame& frame)
{
    if (ready_) {
        return;
    }

    if (frame.indices.empty()) {
        return;
    }

    if (lastFrameId_.has_value() && frame.frameId == lastFrameId_.value()) {
        return;
    }
    lastFrameId_ = frame.frameId;

    for (uint8_t rawIndex : frame.indices) {
        const uint8_t paletteIndex = rawIndex & 0x3F;
        indexCounts_[paletteIndex] += 1u;
    }

    observedFrameCount_++;
    if (observedFrameCount_ >= kObserveFramesForClustering) {
        buildClusters();
    }
}

bool NesPaletteClusterer::isReady() const
{
    return ready_;
}

uint8_t NesPaletteClusterer::mapIndex(uint8_t paletteIndex) const
{
    return indexToCluster_[paletteIndex & 0x3F];
}

void NesPaletteClusterer::buildFallbackMapping()
{
    static const std::array<Rgb8, kPaletteIndexCount> paletteRgb = makePaletteRgb();

    fallbackIndexToCluster_.fill(0);
    for (int index = 0; index < kPaletteIndexCount; ++index) {
        const int y = luminance(paletteRgb[static_cast<size_t>(index)]);
        const int bucket = std::clamp((y * kClusterCount) / 256, 0, kClusterCount - 1);
        fallbackIndexToCluster_[static_cast<size_t>(index)] = static_cast<uint8_t>(bucket);
    }
}

void NesPaletteClusterer::buildClusters()
{
    static const std::array<Rgb8, kPaletteIndexCount> paletteRgb = makePaletteRgb();

    std::array<uint64_t, kPaletteIndexCount> counts = indexCounts_;
    uint64_t totalCount = 0;
    for (uint64_t c : counts) {
        totalCount += c;
    }
    if (totalCount == 0) {
        indexToCluster_ = fallbackIndexToCluster_;
        ready_ = true;
        return;
    }

    std::vector<int> sortedIndices;
    sortedIndices.reserve(kPaletteIndexCount);
    for (int i = 0; i < kPaletteIndexCount; ++i) {
        sortedIndices.push_back(i);
    }

    std::sort(sortedIndices.begin(), sortedIndices.end(), [&counts](int a, int b) {
        const uint64_t ca = counts[static_cast<size_t>(a)];
        const uint64_t cb = counts[static_cast<size_t>(b)];
        if (ca != cb) {
            return ca > cb;
        }
        return a < b;
    });

    std::array<int, kClusterCount> seedIndices{};
    seedIndices.fill(0);
    int seedCount = 0;
    for (int idx : sortedIndices) {
        if (counts[static_cast<size_t>(idx)] == 0) {
            continue;
        }
        seedIndices[static_cast<size_t>(seedCount)] = idx;
        seedCount++;
        if (seedCount >= kClusterCount) {
            break;
        }
    }
    for (int i = seedCount; i < kClusterCount; ++i) {
        seedIndices[static_cast<size_t>(i)] = sortedIndices[static_cast<size_t>(i)];
    }

    std::array<Rgb8, kClusterCount> centers{};
    for (int c = 0; c < kClusterCount; ++c) {
        centers[static_cast<size_t>(c)] = paletteRgb[static_cast<size_t>(seedIndices[c])];
    }

    size_t reseedCursor = 0;
    for (int iter = 0; iter < kKMeansIterations; ++iter) {
        std::array<uint64_t, kClusterCount> sumW{};
        std::array<uint64_t, kClusterCount> sumR{};
        std::array<uint64_t, kClusterCount> sumG{};
        std::array<uint64_t, kClusterCount> sumB{};
        sumW.fill(0);
        sumR.fill(0);
        sumG.fill(0);
        sumB.fill(0);

        for (int i = 0; i < kPaletteIndexCount; ++i) {
            const uint64_t w = counts[static_cast<size_t>(i)];
            if (w == 0) {
                continue;
            }

            int bestCluster = 0;
            int bestDistance = std::numeric_limits<int>::max();
            for (int c = 0; c < kClusterCount; ++c) {
                const int dist = distanceSquared(
                    paletteRgb[static_cast<size_t>(i)], centers[static_cast<size_t>(c)]);
                if (dist < bestDistance) {
                    bestDistance = dist;
                    bestCluster = c;
                }
            }

            sumW[static_cast<size_t>(bestCluster)] += w;
            sumR[static_cast<size_t>(bestCluster)] +=
                w * static_cast<uint64_t>(paletteRgb[static_cast<size_t>(i)].r);
            sumG[static_cast<size_t>(bestCluster)] +=
                w * static_cast<uint64_t>(paletteRgb[static_cast<size_t>(i)].g);
            sumB[static_cast<size_t>(bestCluster)] +=
                w * static_cast<uint64_t>(paletteRgb[static_cast<size_t>(i)].b);
        }

        for (int c = 0; c < kClusterCount; ++c) {
            const uint64_t w = sumW[static_cast<size_t>(c)];
            if (w > 0) {
                const int r = static_cast<int>((sumR[static_cast<size_t>(c)] + (w / 2u)) / w);
                const int g = static_cast<int>((sumG[static_cast<size_t>(c)] + (w / 2u)) / w);
                const int b = static_cast<int>((sumB[static_cast<size_t>(c)] + (w / 2u)) / w);
                centers[static_cast<size_t>(c)] = Rgb8{ .r = r, .g = g, .b = b };
                continue;
            }

            while (reseedCursor < sortedIndices.size()
                   && counts[static_cast<size_t>(sortedIndices[reseedCursor])] == 0) {
                reseedCursor++;
            }
            if (reseedCursor >= sortedIndices.size()) {
                centers[static_cast<size_t>(c)] = centers[0];
                continue;
            }

            const int reseedIndex = sortedIndices[reseedCursor];
            reseedCursor++;
            centers[static_cast<size_t>(c)] = paletteRgb[static_cast<size_t>(reseedIndex)];
            seedIndices[static_cast<size_t>(c)] = reseedIndex;
        }
    }

    std::array<int, kPaletteIndexCount> finalAssignments{};
    finalAssignments.fill(0);
    for (int i = 0; i < kPaletteIndexCount; ++i) {
        int bestCluster = 0;
        int bestDistance = std::numeric_limits<int>::max();
        for (int c = 0; c < kClusterCount; ++c) {
            const int dist = distanceSquared(
                paletteRgb[static_cast<size_t>(i)], centers[static_cast<size_t>(c)]);
            if (dist < bestDistance) {
                bestDistance = dist;
                bestCluster = c;
            }
        }
        finalAssignments[static_cast<size_t>(i)] = bestCluster;
    }

    struct ClusterOrderEntry {
        int lum = 0;
        int seedIndex = 0;
        int cluster = 0;
    };

    std::array<ClusterOrderEntry, kClusterCount> order{};
    for (int c = 0; c < kClusterCount; ++c) {
        order[static_cast<size_t>(c)] = ClusterOrderEntry{
            .lum = luminance(centers[static_cast<size_t>(c)]),
            .seedIndex = seedIndices[static_cast<size_t>(c)],
            .cluster = c,
        };
    }

    std::sort(
        order.begin(), order.end(), [](const ClusterOrderEntry& a, const ClusterOrderEntry& b) {
            if (a.lum != b.lum) {
                return a.lum < b.lum;
            }
            if (a.seedIndex != b.seedIndex) {
                return a.seedIndex < b.seedIndex;
            }
            return a.cluster < b.cluster;
        });

    std::array<uint8_t, kClusterCount> oldToNew{};
    for (int newCluster = 0; newCluster < kClusterCount; ++newCluster) {
        const int oldCluster = order[static_cast<size_t>(newCluster)].cluster;
        oldToNew[static_cast<size_t>(oldCluster)] = static_cast<uint8_t>(newCluster);
    }

    for (int i = 0; i < kPaletteIndexCount; ++i) {
        const int oldCluster = finalAssignments[static_cast<size_t>(i)];
        indexToCluster_[static_cast<size_t>(i)] = oldToNew[static_cast<size_t>(oldCluster)];
    }

    ready_ = true;
}

} // namespace DirtSim
