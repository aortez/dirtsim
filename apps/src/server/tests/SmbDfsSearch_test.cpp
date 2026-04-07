#include "SmbSearchTestHelpers.h"
#include "core/RenderMessage.h"
#include "core/scenarios/nes/NesSuperMarioBrosRamExtractor.h"
#include "server/PlanRepository.h"
#include "server/search/SmbDfsSearch.h"
#include "server/search/SmbPlanExecution.h"
#include "server/search/SmbSearchHarness.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using namespace DirtSim::Server::SearchSupport;
using DirtSim::NesSuperMarioBrosRamExtractor;
using DirtSim::NesSuperMarioBrosState;
using DirtSim::SmbLifeState;
using DirtSim::SmbPhase;
using DirtSim::Test::expectFrameEq;
using DirtSim::Test::requireSmbRomOrSkip;

namespace {

using TraceNodeMap = std::unordered_map<size_t, SmbDfsSearchTraceEntry>;
using ChildNodeMap = std::unordered_map<size_t, std::vector<size_t>>;
using ReplayResultCache = std::unordered_map<size_t, Result<SmbSearchReplayResult, std::string>>;
using ReplayStateCache = std::unordered_map<size_t, Result<NesSuperMarioBrosState, std::string>>;

struct NodePathStats {
    std::optional<size_t> firstDivergenceFromRightRun = std::nullopt;
    size_t pathLength = 0u;
};

using NodePathStatsMap = std::unordered_map<size_t, NodePathStats>;

// PPM screenshot helpers (from NesSuperMarioBrosRamProbe_test.cpp).

std::optional<uint16_t> readRgb565Pixel(const DirtSim::ScenarioVideoFrame& frame, size_t pixelIndex)
{
    const size_t offset = pixelIndex * 2u;
    if (offset + 1u >= frame.pixels.size()) {
        return std::nullopt;
    }
    const uint8_t lo = std::to_integer<uint8_t>(frame.pixels[offset]);
    const uint8_t hi = std::to_integer<uint8_t>(frame.pixels[offset + 1u]);
    return static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
}

std::array<uint8_t, 3> rgb565ToRgb888(uint16_t value)
{
    const uint8_t red5 = static_cast<uint8_t>((value >> 11) & 0x1Fu);
    const uint8_t green6 = static_cast<uint8_t>((value >> 5) & 0x3Fu);
    const uint8_t blue5 = static_cast<uint8_t>(value & 0x1Fu);
    const uint8_t red8 = static_cast<uint8_t>((red5 << 3) | (red5 >> 2));
    const uint8_t green8 = static_cast<uint8_t>((green6 << 2) | (green6 >> 4));
    const uint8_t blue8 = static_cast<uint8_t>((blue5 << 3) | (blue5 >> 2));
    return { red8, green8, blue8 };
}

bool writeScenarioFramePpm(
    const DirtSim::ScenarioVideoFrame& frame, const std::filesystem::path& path)
{
    if (frame.width == 0 || frame.height == 0) {
        return false;
    }
    const size_t expectedBytes =
        static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 2u;
    if (frame.pixels.size() != expectedBytes) {
        return false;
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }
    stream << "P6\n" << frame.width << " " << frame.height << "\n255\n";
    const size_t pixelCount = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const std::optional<uint16_t> rgb565 = readRgb565Pixel(frame, pixelIndex);
        if (!rgb565.has_value()) {
            return false;
        }
        const std::array<uint8_t, 3> rgb = rgb565ToRgb888(rgb565.value());
        stream.write(
            reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    }
    return stream.good();
}

bool isCreationEvent(SmbDfsSearchTraceEventType eventType)
{
    switch (eventType) {
        case SmbDfsSearchTraceEventType::ExpandedAlive:
        case SmbDfsSearchTraceEventType::PrunedDead:
        case SmbDfsSearchTraceEventType::PrunedStalled:
        case SmbDfsSearchTraceEventType::PrunedVelocityStuck:
        case SmbDfsSearchTraceEventType::RootInitialized:
            return true;
        case SmbDfsSearchTraceEventType::Backtracked:
        case SmbDfsSearchTraceEventType::CompletedBudgetExceeded:
        case SmbDfsSearchTraceEventType::CompletedExhausted:
        case SmbDfsSearchTraceEventType::CompletedMilestoneReached:
        case SmbDfsSearchTraceEventType::Error:
        case SmbDfsSearchTraceEventType::Stopped:
            return false;
    }

    return false;
}

std::string toString(SmbDfsSearchTraceEventType eventType)
{
    switch (eventType) {
        case SmbDfsSearchTraceEventType::Backtracked:
            return "Backtracked";
        case SmbDfsSearchTraceEventType::CompletedBudgetExceeded:
            return "CompletedBudgetExceeded";
        case SmbDfsSearchTraceEventType::CompletedExhausted:
            return "CompletedExhausted";
        case SmbDfsSearchTraceEventType::CompletedMilestoneReached:
            return "CompletedMilestoneReached";
        case SmbDfsSearchTraceEventType::Error:
            return "Error";
        case SmbDfsSearchTraceEventType::ExpandedAlive:
            return "ExpandedAlive";
        case SmbDfsSearchTraceEventType::PrunedDead:
            return "PrunedDead";
        case SmbDfsSearchTraceEventType::PrunedStalled:
            return "PrunedStalled";
        case SmbDfsSearchTraceEventType::PrunedVelocityStuck:
            return "PrunedVelocityStuck";
        case SmbDfsSearchTraceEventType::RootInitialized:
            return "RootInitialized";
        case SmbDfsSearchTraceEventType::Stopped:
            return "Stopped";
    }

    return "Unknown";
}

TraceNodeMap buildCreatedNodeMap(const std::vector<SmbDfsSearchTraceEntry>& trace)
{
    TraceNodeMap nodeMap;
    for (const auto& entry : trace) {
        if (!isCreationEvent(entry.eventType)) {
            continue;
        }

        nodeMap.try_emplace(entry.nodeIndex, entry);
    }
    return nodeMap;
}

ChildNodeMap buildChildrenByParent(const std::vector<SmbDfsSearchTraceEntry>& trace)
{
    ChildNodeMap childrenByParent;
    for (const auto& entry : trace) {
        if (!isCreationEvent(entry.eventType) || !entry.parentIndex.has_value()) {
            continue;
        }

        childrenByParent[entry.parentIndex.value()].push_back(entry.nodeIndex);
    }
    return childrenByParent;
}

NodePathStatsMap buildNodePathStats(const std::vector<SmbDfsSearchTraceEntry>& trace)
{
    NodePathStatsMap pathStatsByNode;
    for (const auto& entry : trace) {
        if (!isCreationEvent(entry.eventType)) {
            continue;
        }

        NodePathStats nodeStats{};
        if (entry.parentIndex.has_value()) {
            const auto parentIt = pathStatsByNode.find(entry.parentIndex.value());
            if (parentIt == pathStatsByNode.end()) {
                continue;
            }

            nodeStats.pathLength = parentIt->second.pathLength + 1u;
            nodeStats.firstDivergenceFromRightRun = parentIt->second.firstDivergenceFromRightRun;
            if (!nodeStats.firstDivergenceFromRightRun.has_value()
                && entry.action != SmbSearchLegalAction::RightRun) {
                nodeStats.firstDivergenceFromRightRun = parentIt->second.pathLength;
            }
        }

        pathStatsByNode.try_emplace(entry.nodeIndex, nodeStats);
    }

    return pathStatsByNode;
}

std::string describeDivergenceFromRightRun(const NodePathStats& pathStats)
{
    if (!pathStats.firstDivergenceFromRightRun.has_value()) {
        return "all-RightRun";
    }

    return std::to_string(pathStats.firstDivergenceFromRightRun.value());
}

std::unordered_map<size_t, uint64_t> buildMaxDescendantFrontierMap(
    const TraceNodeMap& nodeMap, const ChildNodeMap& childrenByParent)
{
    std::unordered_map<size_t, uint64_t> maxFrontierByNode;
    const std::function<uint64_t(size_t)> computeMaxFrontier = [&](size_t nodeIndex) -> uint64_t {
        const auto memoIt = maxFrontierByNode.find(nodeIndex);
        if (memoIt != maxFrontierByNode.end()) {
            return memoIt->second;
        }

        const auto nodeIt = nodeMap.find(nodeIndex);
        if (nodeIt == nodeMap.end()) {
            return 0u;
        }

        uint64_t maxFrontier = nodeIt->second.frontier;
        const auto childrenIt = childrenByParent.find(nodeIndex);
        if (childrenIt != childrenByParent.end()) {
            for (const size_t childNodeIndex : childrenIt->second) {
                maxFrontier = std::max(maxFrontier, computeMaxFrontier(childNodeIndex));
            }
        }

        maxFrontierByNode[nodeIndex] = maxFrontier;
        return maxFrontier;
    };

    for (const auto& [nodeIndex, _] : nodeMap) {
        computeMaxFrontier(nodeIndex);
    }

    return maxFrontierByNode;
}

bool writeTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }
    stream << contents;
    return stream.good();
}

void printDiagnosticProgress(const std::string& message)
{
    std::cerr << "[pipe-report] " << message << std::endl;
}

Result<NesSuperMarioBrosState, std::string> extractReplayState(
    const SmbSearchReplayResult& replayResult)
{
    if (!replayResult.memorySnapshot.has_value()) {
        return Result<NesSuperMarioBrosState, std::string>::error(
            "Replay result did not provide an SMB memory snapshot");
    }

    const NesSuperMarioBrosRamExtractor extractor;
    return Result<NesSuperMarioBrosState, std::string>::okay(
        extractor.extract(replayResult.memorySnapshot.value(), true));
}

std::optional<size_t> findFirstTraceIndexByEvent(
    const std::vector<SmbDfsSearchTraceEntry>& trace, SmbDfsSearchTraceEventType eventType)
{
    for (size_t traceIndex = 0; traceIndex < trace.size(); ++traceIndex) {
        if (trace[traceIndex].eventType == eventType) {
            return traceIndex;
        }
    }

    return std::nullopt;
}

std::optional<size_t> findHighestFrontierNodeIndex(
    const std::vector<SmbDfsSearchTraceEntry>& trace, uint64_t minFrontier = 0u)
{
    std::optional<size_t> bestNodeIndex = std::nullopt;
    uint64_t bestFrontier = 0u;
    for (const auto& entry : trace) {
        if (!isCreationEvent(entry.eventType) || entry.frontier < minFrontier) {
            continue;
        }

        if (!bestNodeIndex.has_value() || entry.frontier > bestFrontier) {
            bestNodeIndex = entry.nodeIndex;
            bestFrontier = entry.frontier;
        }
    }

    return bestNodeIndex;
}

std::optional<size_t> findNextCreationTraceIndex(
    const std::vector<SmbDfsSearchTraceEntry>& trace, size_t startTraceIndexExclusive)
{
    for (size_t traceIndex = startTraceIndexExclusive + 1u; traceIndex < trace.size();
         ++traceIndex) {
        if (isCreationEvent(trace[traceIndex].eventType)) {
            return traceIndex;
        }
    }

    return std::nullopt;
}

Result<std::vector<SmbSearchLegalAction>, std::string> reconstructTraceNodeActions(
    const TraceNodeMap& nodeMap, size_t nodeIndex)
{
    std::vector<SmbSearchLegalAction> reversedActions;
    std::optional<size_t> cursor = nodeIndex;
    while (cursor.has_value()) {
        const auto it = nodeMap.find(cursor.value());
        if (it == nodeMap.end()) {
            return Result<std::vector<SmbSearchLegalAction>, std::string>::error(
                "Trace node missing from creation map");
        }

        if (it->second.action.has_value()) {
            reversedActions.push_back(it->second.action.value());
        }

        cursor = it->second.parentIndex;
    }

    std::reverse(reversedActions.begin(), reversedActions.end());
    return Result<std::vector<SmbSearchLegalAction>, std::string>::okay(reversedActions);
}

Result<SmbSearchReplayResult, std::string> replayTraceNode(
    const SmbSearchHarness& harness,
    const SmbSearchRootFixture& root,
    const TraceNodeMap& nodeMap,
    size_t nodeIndex)
{
    const auto actionsResult = reconstructTraceNodeActions(nodeMap, nodeIndex);
    if (actionsResult.isError()) {
        return Result<SmbSearchReplayResult, std::string>::error(actionsResult.errorValue());
    }

    return harness.replayFromRoot(root.savestate, root.evaluatorSummary, actionsResult.value());
}

const Result<SmbSearchReplayResult, std::string>& getCachedReplayResult(
    ReplayResultCache& replayCache,
    const SmbSearchHarness& harness,
    const SmbSearchRootFixture& root,
    const TraceNodeMap& nodeMap,
    size_t nodeIndex)
{
    const auto existingIt = replayCache.find(nodeIndex);
    if (existingIt != replayCache.end()) {
        return existingIt->second;
    }

    const auto replayResult = replayTraceNode(harness, root, nodeMap, nodeIndex);
    return replayCache.emplace(nodeIndex, replayResult).first->second;
}

const Result<NesSuperMarioBrosState, std::string>& getCachedReplayState(
    ReplayResultCache& replayCache,
    ReplayStateCache& stateCache,
    const SmbSearchHarness& harness,
    const SmbSearchRootFixture& root,
    const TraceNodeMap& nodeMap,
    size_t nodeIndex)
{
    const auto existingIt = stateCache.find(nodeIndex);
    if (existingIt != stateCache.end()) {
        return existingIt->second;
    }

    const auto& replayResult =
        getCachedReplayResult(replayCache, harness, root, nodeMap, nodeIndex);
    if (replayResult.isError()) {
        return stateCache
            .emplace(
                nodeIndex,
                Result<NesSuperMarioBrosState, std::string>::error(replayResult.errorValue()))
            .first->second;
    }

    return stateCache.emplace(nodeIndex, extractReplayState(replayResult.value())).first->second;
}

size_t firstDivergenceFromRightRun(const std::vector<SmbSearchLegalAction>& actions)
{
    for (size_t i = 0; i < actions.size(); ++i) {
        if (actions[i] != SmbSearchLegalAction::RightRun) {
            return i;
        }
    }

    return actions.size();
}

struct WindowStats {
    size_t backtrackedCount = 0;
    size_t expandedAliveCount = 0;
    size_t prunedDeadCount = 0;
    size_t prunedStalledCount = 0;
    size_t prunedVelocityStuckCount = 0;
};

WindowStats computeWindowStats(
    const std::vector<SmbDfsSearchTraceEntry>& trace, uint64_t minFrontier, uint64_t maxFrontier)
{
    WindowStats stats;
    for (const auto& entry : trace) {
        if (entry.frontier < minFrontier || entry.frontier > maxFrontier) {
            continue;
        }

        switch (entry.eventType) {
            case SmbDfsSearchTraceEventType::Backtracked:
                stats.backtrackedCount++;
                break;
            case SmbDfsSearchTraceEventType::ExpandedAlive:
                stats.expandedAliveCount++;
                break;
            case SmbDfsSearchTraceEventType::PrunedDead:
                stats.prunedDeadCount++;
                break;
            case SmbDfsSearchTraceEventType::PrunedStalled:
                stats.prunedStalledCount++;
                break;
            case SmbDfsSearchTraceEventType::PrunedVelocityStuck:
                stats.prunedVelocityStuckCount++;
                break;
            case SmbDfsSearchTraceEventType::CompletedBudgetExceeded:
            case SmbDfsSearchTraceEventType::CompletedExhausted:
            case SmbDfsSearchTraceEventType::CompletedMilestoneReached:
            case SmbDfsSearchTraceEventType::Error:
            case SmbDfsSearchTraceEventType::RootInitialized:
            case SmbDfsSearchTraceEventType::Stopped:
                break;
        }
    }

    return stats;
}

Result<SmbSearchReplayResult, std::string> replayHoldRight(
    const SmbSearchHarness& harness, const SmbSearchRootFixture& root, size_t frameCount)
{
    const std::vector<SmbSearchLegalAction> holdRightActions(
        frameCount, SmbSearchLegalAction::RightRun);
    return harness.replayFromRoot(root.savestate, root.evaluatorSummary, holdRightActions);
}

bool writeReplayScreenshot(
    const SmbSearchReplayResult& replayResult, const std::filesystem::path& path)
{
    if (!replayResult.scenarioVideoFrame.has_value()) {
        return false;
    }

    return writeScenarioFramePpm(replayResult.scenarioVideoFrame.value(), path);
}

std::optional<size_t> findFirstControllableNodePastFrontier(
    const SmbSearchHarness& harness,
    const SmbSearchRootFixture& root,
    ReplayResultCache& replayCache,
    ReplayStateCache& stateCache,
    const TraceNodeMap& nodeMap,
    const std::vector<SmbDfsSearchTraceEntry>& trace,
    uint64_t frontierThreshold)
{
    for (const auto& entry : trace) {
        if (!isCreationEvent(entry.eventType) || entry.frontier < frontierThreshold) {
            continue;
        }

        const auto& stateResult =
            getCachedReplayState(replayCache, stateCache, harness, root, nodeMap, entry.nodeIndex);
        if (stateResult.isError()) {
            continue;
        }

        if (stateResult.value().phase == SmbPhase::Gameplay
            && stateResult.value().lifeState == SmbLifeState::Alive) {
            return entry.nodeIndex;
        }
    }

    return std::nullopt;
}

std::vector<SmbDfsSearchTraceEntry> collectTopCreatedNodesByFrontier(
    const std::vector<SmbDfsSearchTraceEntry>& trace, uint64_t minFrontier, size_t maxCount)
{
    std::vector<SmbDfsSearchTraceEntry> createdNodes;
    createdNodes.reserve(trace.size());
    for (const auto& entry : trace) {
        if (isCreationEvent(entry.eventType) && entry.frontier >= minFrontier) {
            createdNodes.push_back(entry);
        }
    }

    std::sort(
        createdNodes.begin(),
        createdNodes.end(),
        [](const SmbDfsSearchTraceEntry& lhs, const SmbDfsSearchTraceEntry& rhs) {
            if (lhs.frontier != rhs.frontier) {
                return lhs.frontier > rhs.frontier;
            }
            return lhs.nodeIndex < rhs.nodeIndex;
        });
    if (createdNodes.size() > maxCount) {
        createdNodes.resize(maxCount);
    }

    return createdNodes;
}

bool writeTraceJsonl(
    const std::filesystem::path& path,
    const SmbSearchHarness& harness,
    const SmbSearchRootFixture& root,
    ReplayResultCache& replayCache,
    ReplayStateCache& stateCache,
    const std::vector<SmbDfsSearchTraceEntry>& trace,
    const TraceNodeMap& nodeMap,
    const NodePathStatsMap& pathStatsByNode,
    uint64_t minWindowFrontier,
    uint64_t maxWindowFrontier)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }

    size_t emittedLines = 0u;
    for (size_t traceIndex = 0; traceIndex < trace.size(); ++traceIndex) {
        const auto& entry = trace[traceIndex];
        const bool inPipeWindow =
            entry.frontier >= minWindowFrontier && entry.frontier <= maxWindowFrontier;
        nlohmann::json jsonLine{
            { "action",
              entry.action.has_value() ? nlohmann::json(toString(entry.action.value()))
                                       : nlohmann::json(nullptr) },
            { "creationEvent", isCreationEvent(entry.eventType) },
            { "evaluationScore", entry.evaluationScore },
            { "eventType", toString(entry.eventType) },
            { "framesSinceProgress", entry.framesSinceProgress },
            { "frontier", entry.frontier },
            { "gameplayFrame", entry.gameplayFrame },
            { "inPipeWindow", inPipeWindow },
            { "nodeIndex", entry.nodeIndex },
            { "parentIndex",
              entry.parentIndex.has_value() ? nlohmann::json(entry.parentIndex.value())
                                            : nlohmann::json(nullptr) },
            { "traceIndex", traceIndex },
        };

        const auto pathStatsIt = pathStatsByNode.find(entry.nodeIndex);
        if (pathStatsIt != pathStatsByNode.end()) {
            jsonLine["pathLength"] = pathStatsIt->second.pathLength;
            jsonLine["firstDivergenceFromRightRun"] =
                pathStatsIt->second.firstDivergenceFromRightRun.has_value()
                ? nlohmann::json(pathStatsIt->second.firstDivergenceFromRightRun.value())
                : nlohmann::json(nullptr);
        }

        if (entry.parentIndex.has_value()) {
            const auto parentIt = nodeMap.find(entry.parentIndex.value());
            if (parentIt != nodeMap.end()) {
                jsonLine["parentFrontier"] = parentIt->second.frontier;
                jsonLine["parentGameplayFrame"] = parentIt->second.gameplayFrame;
            }
        }

        if (isCreationEvent(entry.eventType) && inPipeWindow) {
            const auto& stateResult = getCachedReplayState(
                replayCache, stateCache, harness, root, nodeMap, entry.nodeIndex);
            if (!stateResult.isError()) {
                const auto& state = stateResult.value();
                jsonLine["absoluteX"] = state.absoluteX;
                jsonLine["airborne"] = state.airborne;
                jsonLine["horizontalSpeedNormalized"] = state.horizontalSpeedNormalized;
                jsonLine["lifeState"] = static_cast<uint8_t>(state.lifeState);
                jsonLine["phase"] = static_cast<uint8_t>(state.phase);
                jsonLine["playerYScreen"] = state.playerYScreen;
                jsonLine["verticalSpeedNormalized"] = state.verticalSpeedNormalized;
            }
            else {
                jsonLine["replayStateError"] = stateResult.errorValue();
            }
        }

        stream << jsonLine.dump() << "\n";
        emittedLines++;
        if (emittedLines % 500u == 0u) {
            printDiagnosticProgress("Wrote " + std::to_string(emittedLines) + " JSONL rows");
        }
    }

    return stream.good();
}

std::vector<size_t> buildAncestorChain(const TraceNodeMap& nodeMap, size_t nodeIndex)
{
    std::vector<size_t> reversedNodes;
    std::optional<size_t> cursor = nodeIndex;
    while (cursor.has_value()) {
        reversedNodes.push_back(cursor.value());
        const auto nodeIt = nodeMap.find(cursor.value());
        if (nodeIt == nodeMap.end()) {
            break;
        }
        cursor = nodeIt->second.parentIndex;
    }

    std::reverse(reversedNodes.begin(), reversedNodes.end());
    return reversedNodes;
}

std::string buildFailureConeReport(
    const TraceNodeMap& nodeMap,
    const ChildNodeMap& childrenByParent,
    const NodePathStatsMap& pathStatsByNode,
    const std::unordered_map<size_t, uint64_t>& maxFrontierByNode,
    size_t anchorNodeIndex,
    size_t ancestorLimit)
{
    std::ostringstream stream;
    const auto fullAnchorPath = buildAncestorChain(nodeMap, anchorNodeIndex);
    const size_t startIndex =
        fullAnchorPath.size() > ancestorLimit ? fullAnchorPath.size() - ancestorLimit : 0u;
    std::vector<size_t> anchorPath(
        fullAnchorPath.begin() + static_cast<std::ptrdiff_t>(startIndex), fullAnchorPath.end());
    const std::unordered_set<size_t> anchorPathSet(anchorPath.begin(), anchorPath.end());

    stream << "Failure cone for anchor node " << anchorNodeIndex << ".\n";
    for (const size_t nodeIndex : anchorPath) {
        const auto nodeIt = nodeMap.find(nodeIndex);
        if (nodeIt == nodeMap.end()) {
            continue;
        }

        const auto pathStatsIt = pathStatsByNode.find(nodeIndex);
        stream << "node=" << nodeIndex << " frame=" << nodeIt->second.gameplayFrame
               << " frontier=" << nodeIt->second.frontier
               << " event=" << toString(nodeIt->second.eventType) << " divergence="
               << (pathStatsIt != pathStatsByNode.end()
                       ? describeDivergenceFromRightRun(pathStatsIt->second)
                       : "unknown")
               << "\n";

        const auto childrenIt = childrenByParent.find(nodeIndex);
        if (childrenIt == childrenByParent.end()) {
            stream << "  <no children>\n";
            continue;
        }

        for (const size_t childNodeIndex : childrenIt->second) {
            const auto childIt = nodeMap.find(childNodeIndex);
            if (childIt == nodeMap.end()) {
                continue;
            }

            const auto childPathStatsIt = pathStatsByNode.find(childNodeIndex);
            const auto maxFrontierIt = maxFrontierByNode.find(childNodeIndex);
            stream << "  " << (anchorPathSet.count(childNodeIndex) > 0u ? "*" : "-") << " action="
                   << (childIt->second.action.has_value() ? toString(childIt->second.action.value())
                                                          : "Root")
                   << " child=" << childNodeIndex << " frame=" << childIt->second.gameplayFrame
                   << " frontier=" << childIt->second.frontier
                   << " event=" << toString(childIt->second.eventType) << " divergence="
                   << (childPathStatsIt != pathStatsByNode.end()
                           ? describeDivergenceFromRightRun(childPathStatsIt->second)
                           : "unknown")
                   << " maxDescendantFrontier="
                   << (maxFrontierIt != maxFrontierByNode.end() ? maxFrontierIt->second : 0u)
                   << "\n";
        }
    }

    return stream.str();
}

std::string buildDivergenceHistogramReport(
    const std::vector<SmbDfsSearchTraceEntry>& trace,
    const NodePathStatsMap& pathStatsByNode,
    uint64_t minWindowFrontier,
    uint64_t maxWindowFrontier)
{
    struct HistogramBucket {
        size_t branchCount = 0u;
        size_t prunedDeadCount = 0u;
        size_t prunedStalledCount = 0u;
        size_t prunedVelocityStuckCount = 0u;
        size_t survivingCount = 0u;
        uint64_t bestFrontier = 0u;
    };

    std::map<std::string, HistogramBucket> histogram;
    for (const auto& entry : trace) {
        if (!isCreationEvent(entry.eventType)
            || entry.eventType == SmbDfsSearchTraceEventType::RootInitialized
            || entry.frontier < minWindowFrontier || entry.frontier > maxWindowFrontier) {
            continue;
        }

        const auto statsIt = pathStatsByNode.find(entry.nodeIndex);
        if (statsIt == pathStatsByNode.end()) {
            continue;
        }

        const std::string bucketKey = describeDivergenceFromRightRun(statsIt->second);
        auto& bucket = histogram[bucketKey];
        bucket.branchCount++;
        bucket.bestFrontier = std::max(bucket.bestFrontier, entry.frontier);
        switch (entry.eventType) {
            case SmbDfsSearchTraceEventType::ExpandedAlive:
                bucket.survivingCount++;
                break;
            case SmbDfsSearchTraceEventType::PrunedDead:
                bucket.prunedDeadCount++;
                break;
            case SmbDfsSearchTraceEventType::PrunedStalled:
                bucket.prunedStalledCount++;
                break;
            case SmbDfsSearchTraceEventType::PrunedVelocityStuck:
                bucket.prunedVelocityStuckCount++;
                break;
            case SmbDfsSearchTraceEventType::Backtracked:
            case SmbDfsSearchTraceEventType::CompletedBudgetExceeded:
            case SmbDfsSearchTraceEventType::CompletedExhausted:
            case SmbDfsSearchTraceEventType::CompletedMilestoneReached:
            case SmbDfsSearchTraceEventType::Error:
            case SmbDfsSearchTraceEventType::RootInitialized:
            case SmbDfsSearchTraceEventType::Stopped:
                break;
        }
    }

    std::ostringstream stream;
    stream << "Divergence histogram for pipe window.\n";
    for (const auto& [bucketKey, bucket] : histogram) {
        stream << "divergence=" << bucketKey << " count=" << bucket.branchCount
               << " bestFrontier=" << bucket.bestFrontier << " alive=" << bucket.survivingCount
               << " prunedDead=" << bucket.prunedDeadCount
               << " prunedStalled=" << bucket.prunedStalledCount
               << " prunedVelocityStuck=" << bucket.prunedVelocityStuckCount << "\n";
    }

    return stream.str();
}

std::string buildCompressedTreeReport(
    const TraceNodeMap& nodeMap,
    const ChildNodeMap& childrenByParent,
    const NodePathStatsMap& pathStatsByNode,
    const std::unordered_map<size_t, uint64_t>& maxFrontierByNode,
    size_t windowRootNodeIndex,
    uint64_t minWindowFrontier,
    size_t maxPrintedNodes)
{
    size_t printedNodeCount = 0u;
    std::ostringstream stream;
    stream << "Compressed tree from pipe-window root node " << windowRootNodeIndex << ".\n";

    const std::function<void(size_t, size_t)> appendNode = [&](size_t nodeIndex, size_t indent) {
        if (printedNodeCount >= maxPrintedNodes) {
            return;
        }

        const auto nodeIt = nodeMap.find(nodeIndex);
        if (nodeIt == nodeMap.end()) {
            return;
        }

        std::string indentText(indent * 2u, ' ');
        size_t currentNodeIndex = nodeIndex;
        std::vector<std::string> compressedActions;

        while (true) {
            const auto childrenIt = childrenByParent.find(currentNodeIndex);
            if (childrenIt == childrenByParent.end()) {
                break;
            }

            std::vector<size_t> inWindowChildren;
            for (const size_t childNodeIndex : childrenIt->second) {
                const auto childIt = nodeMap.find(childNodeIndex);
                if (childIt != nodeMap.end() && childIt->second.frontier >= minWindowFrontier) {
                    inWindowChildren.push_back(childNodeIndex);
                }
            }

            if (inWindowChildren.size() != 1u) {
                break;
            }

            const auto childIt = nodeMap.find(inWindowChildren.front());
            if (childIt == nodeMap.end() || !childIt->second.action.has_value()) {
                break;
            }

            compressedActions.push_back(toString(childIt->second.action.value()));
            currentNodeIndex = childIt->second.nodeIndex;
            if (childIt->second.eventType != SmbDfsSearchTraceEventType::ExpandedAlive) {
                break;
            }
        }

        const auto currentNodeIt = nodeMap.find(currentNodeIndex);
        if (currentNodeIt == nodeMap.end()) {
            return;
        }

        const auto statsIt = pathStatsByNode.find(currentNodeIndex);
        stream << indentText << "node=" << currentNodeIndex
               << " frontier=" << currentNodeIt->second.frontier
               << " frame=" << currentNodeIt->second.gameplayFrame
               << " event=" << toString(currentNodeIt->second.eventType) << " divergence="
               << (statsIt != pathStatsByNode.end()
                       ? describeDivergenceFromRightRun(statsIt->second)
                       : "unknown");
        if (!compressedActions.empty()) {
            stream << " chain=";
            for (size_t i = 0; i < compressedActions.size(); ++i) {
                if (i > 0u) {
                    stream << ",";
                }
                stream << compressedActions[i];
            }
        }
        stream << "\n";
        printedNodeCount++;

        auto childrenIt = childrenByParent.find(currentNodeIndex);
        if (childrenIt == childrenByParent.end()) {
            return;
        }

        std::vector<size_t> sortedChildren;
        for (const size_t childNodeIndex : childrenIt->second) {
            const auto childIt = nodeMap.find(childNodeIndex);
            if (childIt != nodeMap.end() && childIt->second.frontier >= minWindowFrontier) {
                sortedChildren.push_back(childNodeIndex);
            }
        }
        std::sort(sortedChildren.begin(), sortedChildren.end(), [&](size_t lhs, size_t rhs) {
            const auto lhsIt = maxFrontierByNode.find(lhs);
            const auto rhsIt = maxFrontierByNode.find(rhs);
            const uint64_t lhsFrontier = lhsIt != maxFrontierByNode.end() ? lhsIt->second : 0u;
            const uint64_t rhsFrontier = rhsIt != maxFrontierByNode.end() ? rhsIt->second : 0u;
            if (lhsFrontier != rhsFrontier) {
                return lhsFrontier > rhsFrontier;
            }
            return lhs < rhs;
        });

        for (const size_t childNodeIndex : sortedChildren) {
            if (printedNodeCount >= maxPrintedNodes) {
                break;
            }

            appendNode(childNodeIndex, indent + 1u);
        }
    };

    appendNode(windowRootNodeIndex, 0u);
    if (printedNodeCount >= maxPrintedNodes) {
        stream << "<tree truncated>\n";
    }

    return stream.str();
}

struct ReplayStallInfo {
    uint64_t frontier = 0;
    size_t frameCount = 0;
    SmbSearchReplayResult replayResult;
};

Result<std::optional<ReplayStallInfo>, std::string> findFirstReplayStall(
    const SmbSearchHarness& harness,
    const SmbSearchRootFixture& root,
    const std::vector<DirtSim::PlayerControlFrame>& frames,
    size_t consecutiveNoProgressFrames)
{
    std::vector<DirtSim::PlayerControlFrame> prefixFrames;
    prefixFrames.reserve(frames.size());
    uint64_t lastFrontier = root.evaluatorSummary.bestFrontier;
    size_t noProgressCount = 0u;

    for (size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex) {
        prefixFrames.push_back(frames[frameIndex]);
        const auto replayResult =
            harness.replayFromRoot(root.savestate, root.evaluatorSummary, prefixFrames);
        if (replayResult.isError()) {
            return Result<std::optional<ReplayStallInfo>, std::string>::error(
                replayResult.errorValue());
        }

        const auto stateResult = extractReplayState(replayResult.value());
        if (stateResult.isError()) {
            return Result<std::optional<ReplayStallInfo>, std::string>::error(
                stateResult.errorValue());
        }

        const auto& state = stateResult.value();
        const bool controllable =
            state.phase == SmbPhase::Gameplay && state.lifeState == SmbLifeState::Alive;
        const uint64_t frontier = replayResult.value().evaluatorSummary.bestFrontier;
        if (controllable && frontier <= lastFrontier) {
            noProgressCount++;
        }
        else {
            noProgressCount = 0u;
        }
        lastFrontier = frontier;

        if ((frameIndex + 1u) % 50u == 0u) {
            printDiagnosticProgress(
                "Checked replay-stall prefixes through frame " + std::to_string(frameIndex + 1u));
        }

        if (noProgressCount >= consecutiveNoProgressFrames) {
            return Result<std::optional<ReplayStallInfo>, std::string>::okay(
                ReplayStallInfo{
                    .frontier = frontier,
                    .frameCount = frameIndex + 1u,
                    .replayResult = replayResult.value(),
                });
        }
    }

    return Result<std::optional<ReplayStallInfo>, std::string>::okay(std::nullopt);
}

void printTraceSummary(const std::vector<SmbDfsSearchTraceEntry>& trace)
{
    size_t expandedCount = 0u;
    size_t prunedDeadCount = 0u;
    size_t prunedStalledCount = 0u;
    size_t prunedVelocityStuckCount = 0u;
    size_t backtrackedCount = 0u;
    for (const auto& entry : trace) {
        switch (entry.eventType) {
            case SmbDfsSearchTraceEventType::Backtracked:
                backtrackedCount++;
                break;
            case SmbDfsSearchTraceEventType::ExpandedAlive:
                expandedCount++;
                break;
            case SmbDfsSearchTraceEventType::PrunedDead:
                prunedDeadCount++;
                break;
            case SmbDfsSearchTraceEventType::PrunedStalled:
                prunedStalledCount++;
                break;
            case SmbDfsSearchTraceEventType::PrunedVelocityStuck:
                prunedVelocityStuckCount++;
                break;
            case SmbDfsSearchTraceEventType::CompletedBudgetExceeded:
            case SmbDfsSearchTraceEventType::CompletedExhausted:
            case SmbDfsSearchTraceEventType::CompletedMilestoneReached:
            case SmbDfsSearchTraceEventType::Error:
            case SmbDfsSearchTraceEventType::RootInitialized:
            case SmbDfsSearchTraceEventType::Stopped:
                break;
        }
    }

    std::cout << "Trace summary: expanded=" << expandedCount << " prunedDead=" << prunedDeadCount
              << " prunedStalled=" << prunedStalledCount
              << " prunedVelocityStuck=" << prunedVelocityStuckCount
              << " backtracked=" << backtrackedCount << "\n";
}

Result<std::monostate, std::string> runSearchToCompletion(
    SmbDfsSearch& search, size_t maxTicks = 200000u)
{
    for (size_t tickIndex = 0; tickIndex < maxTicks; ++tickIndex) {
        const auto tickResult = search.tick();
        if (tickResult.error.has_value()) {
            return Result<std::monostate, std::string>::error(tickResult.error.value());
        }
        if (tickResult.completed) {
            return Result<std::monostate, std::string>::okay(std::monostate{});
        }
    }

    return Result<std::monostate, std::string>::error("DFS search did not complete in time");
}

Result<std::monostate, std::string> runPlaybackToCompletion(
    SmbPlanExecution& playback, size_t maxTicks = 200000u)
{
    for (size_t tickIndex = 0; tickIndex < maxTicks; ++tickIndex) {
        const auto tickResult = playback.tick();
        if (tickResult.error.has_value()) {
            return Result<std::monostate, std::string>::error(tickResult.error.value());
        }
        if (tickResult.completed) {
            return Result<std::monostate, std::string>::okay(std::monostate{});
        }
    }

    return Result<std::monostate, std::string>::error("SMB playback did not complete in time");
}

void expectPlanFramesEq(
    const std::vector<DirtSim::PlayerControlFrame>& actual,
    const std::vector<DirtSim::PlayerControlFrame>& expected);

void expectPersistedPlanPlaybackMatchesSearchPlan(const DirtSim::Api::Plan& searchPlan)
{
    DirtSim::Server::PlanRepository planRepository;
    const auto storeResult = planRepository.store(searchPlan);
    ASSERT_FALSE(storeResult.isError()) << storeResult.errorValue();

    const auto getResult = planRepository.get(searchPlan.summary.id);
    ASSERT_FALSE(getResult.isError()) << getResult.errorValue();
    ASSERT_TRUE(getResult.value().has_value());
    const DirtSim::Api::Plan persistedPlan = getResult.value().value();
    expectPlanFramesEq(persistedPlan.frames, searchPlan.frames);

    SmbPlanExecution playback;
    const auto playbackStartResult = playback.startPlayback(persistedPlan);
    ASSERT_FALSE(playbackStartResult.isError()) << playbackStartResult.errorValue();

    const auto playbackRunResult =
        runPlaybackToCompletion(playback, persistedPlan.frames.size() + 2000u);
    ASSERT_FALSE(playbackRunResult.isError()) << playbackRunResult.errorValue();
    EXPECT_EQ(
        playback.getCompletionReason(),
        std::optional<SmbPlanExecutionCompletionReason>{
            SmbPlanExecutionCompletionReason::Completed });
    EXPECT_EQ(playback.getProgress().bestFrontier, searchPlan.summary.bestFrontier);
    EXPECT_EQ(playback.getPlan().summary.bestFrontier, searchPlan.summary.bestFrontier);
    EXPECT_EQ(playback.getPlan().summary.elapsedFrames, searchPlan.summary.elapsedFrames);
}

void expectTraceEq(const SmbDfsSearchTraceEntry& actual, const SmbDfsSearchTraceEntry& expected)
{
    EXPECT_EQ(actual.eventType, expected.eventType);
    EXPECT_EQ(actual.nodeIndex, expected.nodeIndex);
    EXPECT_EQ(actual.parentIndex, expected.parentIndex);
    EXPECT_EQ(actual.action, expected.action);
    EXPECT_EQ(actual.gameplayFrame, expected.gameplayFrame);
    EXPECT_EQ(actual.frontier, expected.frontier);
    EXPECT_DOUBLE_EQ(actual.evaluationScore, expected.evaluationScore);
    EXPECT_EQ(actual.framesSinceProgress, expected.framesSinceProgress);
}

void expectPlanFramesEq(
    const std::vector<DirtSim::PlayerControlFrame>& actual,
    const std::vector<DirtSim::PlayerControlFrame>& expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        expectFrameEq(actual[i], expected[i]);
    }
}

} // namespace

TEST(SmbDfsSearchTest, StartCapturesRoot)
{
    requireSmbRomOrSkip();

    SmbDfsSearch search;
    const auto startResult = search.startDfs();
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    EXPECT_FALSE(search.isCompleted());
    EXPECT_TRUE(search.hasRenderableFrame());
    EXPECT_GT(search.getProgress().bestFrontier, 0u);
    ASSERT_FALSE(search.getTrace().empty());
    EXPECT_EQ(search.getTrace().front().eventType, SmbDfsSearchTraceEventType::RootInitialized);
}

TEST(SmbDfsSearchTest, TickAdvancesSearch)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    SmbDfsSearch search;
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    uint64_t lastBestFrontier = search.getProgress().bestFrontier;
    uint64_t lastSearchedNodeCount = search.getProgress().searchedNodeCount;
    for (size_t tickIndex = 0; tickIndex < 16u; ++tickIndex) {
        const auto tickResult = search.tick();
        ASSERT_FALSE(tickResult.error.has_value()) << tickResult.error.value();
        EXPECT_GE(search.getProgress().bestFrontier, lastBestFrontier);
        EXPECT_GT(search.getProgress().searchedNodeCount, lastSearchedNodeCount);
        lastBestFrontier = search.getProgress().bestFrontier;
        lastSearchedNodeCount = search.getProgress().searchedNodeCount;
        if (tickResult.completed) {
            break;
        }
    }
}

TEST(SmbDfsSearchTest, ExploresRightRunPrefixOnFlatGround)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    constexpr uint32_t kPrefixNodeCount = 32u;
    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = kPrefixNodeCount,
            .stallFrameLimit = 120u,
        });
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    const auto runResult = runSearchToCompletion(search, kPrefixNodeCount + 16u);
    ASSERT_FALSE(runResult.isError()) << runResult.errorValue();

    uint64_t lastFrontier = fixtureResult.value().evaluatorSummary.bestFrontier;
    size_t expandedPrefixCount = 0u;
    for (const auto& entry : search.getTrace()) {
        if (!isCreationEvent(entry.eventType)
            || entry.eventType == SmbDfsSearchTraceEventType::RootInitialized) {
            continue;
        }

        ASSERT_EQ(entry.eventType, SmbDfsSearchTraceEventType::ExpandedAlive);
        ASSERT_TRUE(entry.action.has_value());
        EXPECT_EQ(entry.action.value(), SmbSearchLegalAction::RightRun);
        EXPECT_GT(entry.frontier, lastFrontier);
        lastFrontier = entry.frontier;
        expandedPrefixCount++;
    }

    EXPECT_EQ(expandedPrefixCount, kPrefixNodeCount);
}

TEST(SmbDfsSearchTest, BacktracksChronologicallyAndTriesRightJumpRunNext)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = 5000u,
            .stallFrameLimit = 120u,
        });
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    std::optional<size_t> firstBacktrackTraceIndex = std::nullopt;
    std::optional<size_t> nextCreationTraceIndex = std::nullopt;
    for (size_t tickIndex = 0; tickIndex < 5000u && !search.isCompleted(); ++tickIndex) {
        const auto tickResult = search.tick();
        ASSERT_FALSE(tickResult.error.has_value()) << tickResult.error.value();

        if (!firstBacktrackTraceIndex.has_value()) {
            firstBacktrackTraceIndex = findFirstTraceIndexByEvent(
                search.getTrace(), SmbDfsSearchTraceEventType::Backtracked);
        }
        if (firstBacktrackTraceIndex.has_value()) {
            nextCreationTraceIndex =
                findNextCreationTraceIndex(search.getTrace(), firstBacktrackTraceIndex.value());
            if (nextCreationTraceIndex.has_value()) {
                break;
            }
        }
    }

    ASSERT_TRUE(firstBacktrackTraceIndex.has_value());
    ASSERT_TRUE(nextCreationTraceIndex.has_value());

    const auto& trace = search.getTrace();
    const auto nodeMap = buildCreatedNodeMap(trace);
    const auto& firstBacktrack = trace[firstBacktrackTraceIndex.value()];
    const auto& nextCreatedNode = trace[nextCreationTraceIndex.value()];
    ASSERT_TRUE(nextCreatedNode.parentIndex.has_value());

    const auto backtrackedActionsResult =
        reconstructTraceNodeActions(nodeMap, firstBacktrack.nodeIndex);
    const auto newParentActionsResult =
        reconstructTraceNodeActions(nodeMap, nextCreatedNode.parentIndex.value());
    const auto newActionsResult = reconstructTraceNodeActions(nodeMap, nextCreatedNode.nodeIndex);
    ASSERT_FALSE(backtrackedActionsResult.isError()) << backtrackedActionsResult.errorValue();
    ASSERT_FALSE(newParentActionsResult.isError()) << newParentActionsResult.errorValue();
    ASSERT_FALSE(newActionsResult.isError()) << newActionsResult.errorValue();

    const auto& backtrackedActions = backtrackedActionsResult.value();
    const auto& newParentActions = newParentActionsResult.value();
    const auto& newActions = newActionsResult.value();

    size_t backtrackCount = 0u;
    for (size_t traceIndex = firstBacktrackTraceIndex.value();
         traceIndex < nextCreationTraceIndex.value();
         ++traceIndex) {
        if (trace[traceIndex].eventType == SmbDfsSearchTraceEventType::Backtracked) {
            backtrackCount++;
        }
    }

    ASSERT_GE(backtrackedActions.size(), newParentActions.size());
    EXPECT_EQ(backtrackCount, backtrackedActions.size() - newParentActions.size());
    EXPECT_TRUE(
        std::equal(
            newParentActions.begin(),
            newParentActions.end(),
            backtrackedActions.begin(),
            backtrackedActions.begin() + static_cast<std::ptrdiff_t>(newParentActions.size())));

    ASSERT_EQ(newActions.size(), newParentActions.size() + 1u);
    EXPECT_TRUE(
        std::equal(
            newParentActions.begin(),
            newParentActions.end(),
            newActions.begin(),
            newActions.begin() + static_cast<std::ptrdiff_t>(newParentActions.size())));
    EXPECT_EQ(newActions.back(), SmbSearchLegalAction::RightJumpRun);
}

TEST(SmbDfsSearchTest, PrunesAndBacktracksHazardBranches)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = 5000u,
            .stallFrameLimit = 120u,
        });
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    bool sawPrune = false;
    bool sawBacktrack = false;
    for (size_t tickIndex = 0; tickIndex < 5000u && !search.isCompleted(); ++tickIndex) {
        const auto tickResult = search.tick();
        ASSERT_FALSE(tickResult.error.has_value()) << tickResult.error.value();

        for (const auto& entry : search.getTrace()) {
            sawPrune |= entry.eventType == SmbDfsSearchTraceEventType::PrunedDead
                || entry.eventType == SmbDfsSearchTraceEventType::PrunedStalled
                || entry.eventType == SmbDfsSearchTraceEventType::PrunedVelocityStuck;
            sawBacktrack |= entry.eventType == SmbDfsSearchTraceEventType::Backtracked;
        }

        if (sawPrune && sawBacktrack) {
            break;
        }
    }

    EXPECT_TRUE(sawPrune);
    EXPECT_TRUE(sawBacktrack);
}

TEST(SmbDfsSearchTest, NeverExpandsUncontrollableNodes)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FirstGoomba);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = 500u,
            .stallFrameLimit = 120u,
        });
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    const auto runResult = runSearchToCompletion(search, 1000u);
    ASSERT_FALSE(runResult.isError()) << runResult.errorValue();

    const auto nodeMap = buildCreatedNodeMap(search.getTrace());
    size_t expandedNodeCount = 0u;
    for (const auto& entry : search.getTrace()) {
        if (entry.eventType != SmbDfsSearchTraceEventType::ExpandedAlive) {
            continue;
        }

        const auto replayResult =
            replayTraceNode(harness, fixtureResult.value(), nodeMap, entry.nodeIndex);
        ASSERT_FALSE(replayResult.isError()) << replayResult.errorValue();
        const auto stateResult = extractReplayState(replayResult.value());
        ASSERT_FALSE(stateResult.isError()) << stateResult.errorValue();
        EXPECT_EQ(stateResult.value().phase, SmbPhase::Gameplay);
        EXPECT_EQ(stateResult.value().lifeState, SmbLifeState::Alive);
        expandedNodeCount++;
    }

    EXPECT_GT(expandedNodeCount, 0u);
}

TEST(SmbDfsSearchTest, FindsPlanToFirstGap)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto flatResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(flatResult.isError()) << flatResult.errorValue();

    const auto firstGapResult = harness.captureFixture(SmbSearchRootFixtureId::FirstGap);
    ASSERT_FALSE(firstGapResult.isError()) << firstGapResult.errorValue();
    const uint64_t targetFrontier = firstGapResult.value().evaluatorSummary.bestFrontier;
    std::cout << "First gap frontier: " << targetFrontier << "\n";

    constexpr uint32_t kSearchBudget = 2000u;
    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = kSearchBudget,
            .stallFrameLimit = 120u,
            .stopAfterBestFrontier = targetFrontier,
        });
    const auto startResult = search.startFromFixture(flatResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    const auto runResult = runSearchToCompletion(search, kSearchBudget * 2u);
    ASSERT_FALSE(runResult.isError()) << runResult.errorValue();

    std::cout << "Searched nodes: " << search.getProgress().searchedNodeCount << "\n";
    std::cout << "Best frontier: " << search.getProgress().bestFrontier << "\n";
    std::cout << "Plan frames: " << search.getPlan().frames.size() << "\n";
    const bool reachedTarget = search.getProgress().bestFrontier >= targetFrontier;
    std::cout << "Reached target: " << (reachedTarget ? "YES" : "NO") << "\n";

    const auto screenshotDir = std::filesystem::path(::testing::TempDir());
    if (search.getScenarioVideoFrame().has_value()) {
        const auto bestLeafScreenshot = screenshotDir / "dfs_first_gap_best_leaf.ppm";
        writeScenarioFramePpm(search.getScenarioVideoFrame().value(), bestLeafScreenshot);
        std::cout << "Best leaf screenshot: " << bestLeafScreenshot.string() << "\n";
    }

    for (const auto& entry : search.getTrace()) {
        if (entry.eventType != SmbDfsSearchTraceEventType::PrunedDead) {
            continue;
        }
        std::cout << "First death prune at node " << entry.nodeIndex << " (frame "
                  << entry.gameplayFrame << ", frontier " << entry.frontier << ")\n";
        break;
    }

    size_t expandedCount = 0;
    size_t prunedDeadCount = 0;
    size_t prunedStalledCount = 0;
    size_t backtrackedCount = 0;
    for (const auto& entry : search.getTrace()) {
        if (entry.eventType == SmbDfsSearchTraceEventType::ExpandedAlive) {
            expandedCount++;
        }
        else if (entry.eventType == SmbDfsSearchTraceEventType::PrunedDead) {
            prunedDeadCount++;
        }
        else if (entry.eventType == SmbDfsSearchTraceEventType::PrunedStalled) {
            prunedStalledCount++;
        }
        else if (entry.eventType == SmbDfsSearchTraceEventType::PrunedVelocityStuck) {
            prunedStalledCount++;
        }
        else if (entry.eventType == SmbDfsSearchTraceEventType::Backtracked) {
            backtrackedCount++;
        }
    }
    std::cout << "Trace: expanded=" << expandedCount << " prunedDead=" << prunedDeadCount
              << " prunedStalled=" << prunedStalledCount << " backtracked=" << backtrackedCount
              << "\n";

    ASSERT_TRUE(search.hasPersistablePlan());
    EXPECT_GE(search.getPlan().summary.bestFrontier, targetFrontier);
    EXPECT_GE(
        search.getPlan().summary.elapsedFrames, flatResult.value().evaluatorSummary.gameplayFrames);
}

TEST(SmbDfsSearchTest, DISABLED_ReportFirstGoombaSearch)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    constexpr uint32_t kSearchBudget = 500u;
    constexpr size_t kHoldRightFrameCount = 200u;
    constexpr uint64_t kGoombaClearMargin = 64u;
    const auto holdRightReplay =
        replayHoldRight(harness, fixtureResult.value(), kHoldRightFrameCount);
    ASSERT_FALSE(holdRightReplay.isError()) << holdRightReplay.errorValue();
    const uint64_t holdRightDeathFrontier = holdRightReplay.value().evaluatorSummary.bestFrontier;
    const uint64_t goombaClearFrontier = holdRightDeathFrontier + kGoombaClearMargin;

    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = kSearchBudget,
            .stallFrameLimit = 120u,
        });
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();
    const auto runResult = runSearchToCompletion(search, kSearchBudget * 2u);
    ASSERT_FALSE(runResult.isError()) << runResult.errorValue();

    const auto screenshotDir = std::filesystem::path(::testing::TempDir());
    const auto nodeMap = buildCreatedNodeMap(search.getTrace());
    ReplayResultCache replayCache;
    ReplayStateCache stateCache;
    const auto highestNodeIndex = findHighestFrontierNodeIndex(search.getTrace());
    const auto firstBacktrackTraceIndex =
        findFirstTraceIndexByEvent(search.getTrace(), SmbDfsSearchTraceEventType::Backtracked);
    const auto firstClearNodeIndex = findFirstControllableNodePastFrontier(
        harness,
        fixtureResult.value(),
        replayCache,
        stateCache,
        nodeMap,
        search.getTrace(),
        goombaClearFrontier);

    std::cout << "\n=== First Goomba Search Report ===\n";
    std::cout << "Budget: " << kSearchBudget << "\n";
    std::cout << "Searched nodes: " << search.getProgress().searchedNodeCount << "\n";
    std::cout << "Best frontier: " << search.getProgress().bestFrontier << "\n";
    std::cout << "Hold-right death frontier: " << holdRightDeathFrontier << "\n";
    std::cout << "Goomba clear frontier: " << goombaClearFrontier << "\n";
    printTraceSummary(search.getTrace());

    const auto holdRightScreenshot = screenshotDir / "dfs_goomba_hold_right_death.ppm";
    if (writeReplayScreenshot(holdRightReplay.value(), holdRightScreenshot)) {
        std::cout << "Hold-right death screenshot: " << holdRightScreenshot.string() << "\n";
    }

    if (firstBacktrackTraceIndex.has_value()) {
        const auto& entry = search.getTrace()[firstBacktrackTraceIndex.value()];
        std::cout << "First backtrack: node=" << entry.nodeIndex << " frame=" << entry.gameplayFrame
                  << " frontier=" << entry.frontier << "\n";
    }

    if (highestNodeIndex.has_value()) {
        const auto actionsResult = reconstructTraceNodeActions(nodeMap, highestNodeIndex.value());
        ASSERT_FALSE(actionsResult.isError()) << actionsResult.errorValue();
        const auto& replayResult = getCachedReplayResult(
            replayCache, harness, fixtureResult.value(), nodeMap, highestNodeIndex.value());
        ASSERT_FALSE(replayResult.isError()) << replayResult.errorValue();
        const auto highestScreenshot = screenshotDir / "dfs_goomba_best_frontier.ppm";
        if (writeReplayScreenshot(replayResult.value(), highestScreenshot)) {
            std::cout << "Best frontier screenshot: " << highestScreenshot.string() << "\n";
        }
        std::cout << "Best frontier divergence from RightRun: "
                  << firstDivergenceFromRightRun(actionsResult.value()) << "\n";
    }

    if (firstClearNodeIndex.has_value()) {
        const auto clearActionsResult =
            reconstructTraceNodeActions(nodeMap, firstClearNodeIndex.value());
        ASSERT_FALSE(clearActionsResult.isError()) << clearActionsResult.errorValue();
        const auto& clearReplayResult = getCachedReplayResult(
            replayCache, harness, fixtureResult.value(), nodeMap, firstClearNodeIndex.value());
        ASSERT_FALSE(clearReplayResult.isError()) << clearReplayResult.errorValue();
        const auto clearScreenshot = screenshotDir / "dfs_goomba_first_clear.ppm";
        if (writeReplayScreenshot(clearReplayResult.value(), clearScreenshot)) {
            std::cout << "First clear screenshot: " << clearScreenshot.string() << "\n";
        }
        std::cout << "First clear node: " << firstClearNodeIndex.value() << "\n";
        std::cout << "First clear divergence from RightRun: "
                  << firstDivergenceFromRightRun(clearActionsResult.value()) << "\n";
    }
    else {
        std::cout << "No controllable node cleared the goomba frontier within budget.\n";
    }

    EXPECT_TRUE(firstClearNodeIndex.has_value());
}

TEST(SmbDfsSearchTest, DISABLED_ReportFirstPipeSearch)
{
    requireSmbRomOrSkip();

    // TODO: Align this harness with the scenario/configuration that reportedly reaches frontier
    // 1242. The checked-in diagnostic path here still plateaus at frontier 435 and does not clear
    // the pipe within the 4096-node budget, so it does not currently reproduce the commit's main
    // success claim.
    // TODO: Promote a fast deterministic pipe-clear assertion into the enabled suite once we have
    // a fixture/budget combination that pins this behavior. Right now the pipe investigation is
    // still diagnostic-only, so CI will not catch regressions in the dynamic action ordering.
    printDiagnosticProgress("Capturing FlatGroundSanity fixture");
    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    constexpr uint32_t kSearchBudget = 4096u;
    constexpr size_t kHoldRightFrameCount = 200u;
    constexpr uint64_t kGoombaClearMargin = 64u;
    constexpr size_t kPlanStallFrames = 8u;
    constexpr uint64_t kPipeWindowBefore = 64u;
    constexpr uint64_t kPipeWindowAfter = 32u;

    printDiagnosticProgress("Replaying hold-right baseline");
    const auto holdRightReplay =
        replayHoldRight(harness, fixtureResult.value(), kHoldRightFrameCount);
    ASSERT_FALSE(holdRightReplay.isError()) << holdRightReplay.errorValue();
    const uint64_t postGoombaFrontier =
        holdRightReplay.value().evaluatorSummary.bestFrontier + kGoombaClearMargin;

    printDiagnosticProgress("Running DFS search to completion");
    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = kSearchBudget,
            .stallFrameLimit = 120u,
            .velocityPruningEnabled = true,
        });
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();
    const auto runResult = runSearchToCompletion(search, kSearchBudget * 2u);
    ASSERT_FALSE(runResult.isError()) << runResult.errorValue();
    ASSERT_TRUE(search.hasPersistablePlan());
    printDiagnosticProgress(
        "DFS search finished at " + std::to_string(search.getProgress().searchedNodeCount)
        + " searched nodes");

    printDiagnosticProgress("Finding first best-plan replay stall");
    const auto stallInfoResult = findFirstReplayStall(
        harness, fixtureResult.value(), search.getPlan().frames, kPlanStallFrames);
    ASSERT_FALSE(stallInfoResult.isError()) << stallInfoResult.errorValue();

    const uint64_t stallFrontier = stallInfoResult.value().has_value()
        ? stallInfoResult.value()->frontier
        : search.getPlan().summary.bestFrontier;
    const uint64_t minWindowFrontier =
        stallFrontier > kPipeWindowBefore ? stallFrontier - kPipeWindowBefore : 0u;
    const uint64_t maxWindowFrontier = stallFrontier + kPipeWindowAfter;
    const auto windowStats =
        computeWindowStats(search.getTrace(), minWindowFrontier, maxWindowFrontier);

    const auto screenshotDir = std::filesystem::path(::testing::TempDir());
    const auto nodeMap = buildCreatedNodeMap(search.getTrace());
    const auto childrenByParent = buildChildrenByParent(search.getTrace());
    const auto pathStatsByNode = buildNodePathStats(search.getTrace());
    const auto maxFrontierByNode = buildMaxDescendantFrontierMap(nodeMap, childrenByParent);
    ReplayResultCache replayCache;
    ReplayStateCache stateCache;
    const auto topNodes =
        collectTopCreatedNodesByFrontier(search.getTrace(), minWindowFrontier, 10u);
    const auto anchorNodeIndex = findHighestFrontierNodeIndex(search.getTrace(), minWindowFrontier);

    size_t pipeWindowCreationCount = 0u;
    for (const auto& entry : search.getTrace()) {
        if (isCreationEvent(entry.eventType) && entry.frontier >= minWindowFrontier
            && entry.frontier <= maxWindowFrontier) {
            pipeWindowCreationCount++;
        }
    }

    printDiagnosticProgress(
        "Scanning " + std::to_string(pipeWindowCreationCount)
        + " pipe-window creation nodes and filling replay cache");
    std::optional<size_t> firstAirborneNodeIndex = std::nullopt;
    std::optional<size_t> firstJumpNodeIndex = std::nullopt;
    std::optional<size_t> firstVelocityPruneNodeIndex = std::nullopt;
    size_t scannedPipeWindowNodes = 0u;
    for (const auto& entry : search.getTrace()) {
        if (!isCreationEvent(entry.eventType) || entry.frontier < minWindowFrontier
            || entry.frontier > maxWindowFrontier) {
            continue;
        }

        scannedPipeWindowNodes++;
        const auto& stateResult = getCachedReplayState(
            replayCache, stateCache, harness, fixtureResult.value(), nodeMap, entry.nodeIndex);
        ASSERT_FALSE(stateResult.isError()) << stateResult.errorValue();

        const bool isJumpAction = entry.action == SmbSearchLegalAction::RightJumpRun
            || entry.action == SmbSearchLegalAction::RightJump
            || entry.action == SmbSearchLegalAction::DuckJump
            || entry.action == SmbSearchLegalAction::DuckRightJumpRun
            || entry.action == SmbSearchLegalAction::DuckLeftJumpRun
            || entry.action == SmbSearchLegalAction::LeftJumpRun;
        if (!firstJumpNodeIndex.has_value() && isJumpAction) {
            firstJumpNodeIndex = entry.nodeIndex;
        }
        if (!firstAirborneNodeIndex.has_value() && stateResult.value().airborne) {
            firstAirborneNodeIndex = entry.nodeIndex;
        }
        if (!firstVelocityPruneNodeIndex.has_value()
            && entry.eventType == SmbDfsSearchTraceEventType::PrunedVelocityStuck) {
            firstVelocityPruneNodeIndex = entry.nodeIndex;
        }

        if (scannedPipeWindowNodes % 100u == 0u) {
            printDiagnosticProgress(
                "Scanned " + std::to_string(scannedPipeWindowNodes) + "/"
                + std::to_string(pipeWindowCreationCount)
                + " pipe-window nodes; replay cache size=" + std::to_string(replayCache.size()));
        }
    }
    printDiagnosticProgress("Finished pipe-window scan");

    std::cout << "\n=== First Pipe Search Report ===\n";
    std::cout << "Budget: " << kSearchBudget << "\n";
    std::cout << "Searched nodes: " << search.getProgress().searchedNodeCount << "\n";
    std::cout << "Best frontier: " << search.getProgress().bestFrontier << "\n";
    std::cout << "Post-goomba frontier: " << postGoombaFrontier << "\n";
    std::cout << "Best-plan stall frontier: " << stallFrontier << "\n";
    std::cout << "Pipe window: [" << minWindowFrontier << ", " << maxWindowFrontier << "]\n";
    std::cout << "Window stats: expanded=" << windowStats.expandedAliveCount
              << " prunedDead=" << windowStats.prunedDeadCount
              << " prunedStalled=" << windowStats.prunedStalledCount
              << " prunedVelocityStuck=" << windowStats.prunedVelocityStuckCount
              << " backtracked=" << windowStats.backtrackedCount << "\n";
    printTraceSummary(search.getTrace());

    const auto stepsJsonl = screenshotDir / "dfs_pipe_steps.jsonl";
    printDiagnosticProgress("Writing JSONL step log");
    if (writeTraceJsonl(
            stepsJsonl,
            harness,
            fixtureResult.value(),
            replayCache,
            stateCache,
            search.getTrace(),
            nodeMap,
            pathStatsByNode,
            minWindowFrontier,
            maxWindowFrontier)) {
        std::cout << "Pipe step log: " << stepsJsonl.string() << "\n";
    }

    if (anchorNodeIndex.has_value()) {
        printDiagnosticProgress("Building failure cone report");
        const auto failureConeText = buildFailureConeReport(
            nodeMap,
            childrenByParent,
            pathStatsByNode,
            maxFrontierByNode,
            anchorNodeIndex.value(),
            16u);
        const auto failureConePath = screenshotDir / "dfs_pipe_failure_cone.txt";
        if (writeTextFile(failureConePath, failureConeText)) {
            std::cout << "Failure cone: " << failureConePath.string() << "\n";
        }

        const auto fullAnchorPath = buildAncestorChain(nodeMap, anchorNodeIndex.value());
        size_t windowRootNodeIndex = anchorNodeIndex.value();
        for (const size_t nodeIndex : fullAnchorPath) {
            const auto nodeIt = nodeMap.find(nodeIndex);
            if (nodeIt != nodeMap.end() && nodeIt->second.frontier >= minWindowFrontier) {
                windowRootNodeIndex = nodeIndex;
                break;
            }
        }

        printDiagnosticProgress("Building compressed tree report");
        const auto compressedTreeText = buildCompressedTreeReport(
            nodeMap,
            childrenByParent,
            pathStatsByNode,
            maxFrontierByNode,
            windowRootNodeIndex,
            minWindowFrontier,
            80u);
        const auto compressedTreePath = screenshotDir / "dfs_pipe_compressed_tree.txt";
        if (writeTextFile(compressedTreePath, compressedTreeText)) {
            std::cout << "Compressed tree: " << compressedTreePath.string() << "\n";
        }
    }

    printDiagnosticProgress("Building divergence histogram");
    const auto divergenceHistogramText = buildDivergenceHistogramReport(
        search.getTrace(), pathStatsByNode, minWindowFrontier, maxWindowFrontier);
    const auto divergenceHistogramPath = screenshotDir / "dfs_pipe_divergence_histogram.txt";
    if (writeTextFile(divergenceHistogramPath, divergenceHistogramText)) {
        std::cout << "Divergence histogram: " << divergenceHistogramPath.string() << "\n";
    }

    if (stallInfoResult.value().has_value()) {
        const auto stallScreenshot = screenshotDir / "dfs_pipe_best_plan_stall.ppm";
        if (writeReplayScreenshot(stallInfoResult.value()->replayResult, stallScreenshot)) {
            std::cout << "Best plan stall screenshot: " << stallScreenshot.string() << "\n";
        }
        std::cout << "Best plan stall frame count: " << stallInfoResult.value()->frameCount << "\n";
    }
    else {
        std::cout << "Best plan did not show a sustained stall in replay.\n";
    }

    if (firstVelocityPruneNodeIndex.has_value()) {
        const auto& replayResult = getCachedReplayResult(
            replayCache,
            harness,
            fixtureResult.value(),
            nodeMap,
            firstVelocityPruneNodeIndex.value());
        ASSERT_FALSE(replayResult.isError()) << replayResult.errorValue();
        const auto screenshot = screenshotDir / "dfs_pipe_first_velocity_prune.ppm";
        if (writeReplayScreenshot(replayResult.value(), screenshot)) {
            std::cout << "First velocity prune screenshot: " << screenshot.string() << "\n";
        }
        std::cout << "First velocity prune node: " << firstVelocityPruneNodeIndex.value() << "\n";
    }

    if (firstAirborneNodeIndex.has_value()) {
        const auto& replayResult = getCachedReplayResult(
            replayCache, harness, fixtureResult.value(), nodeMap, firstAirborneNodeIndex.value());
        ASSERT_FALSE(replayResult.isError()) << replayResult.errorValue();
        const auto screenshot = screenshotDir / "dfs_pipe_first_airborne.ppm";
        if (writeReplayScreenshot(replayResult.value(), screenshot)) {
            std::cout << "First airborne screenshot: " << screenshot.string() << "\n";
        }
        std::cout << "First airborne node: " << firstAirborneNodeIndex.value() << "\n";
    }

    if (firstJumpNodeIndex.has_value()) {
        const auto pathStatsIt = pathStatsByNode.find(firstJumpNodeIndex.value());
        ASSERT_TRUE(pathStatsIt != pathStatsByNode.end());
        std::cout << "First jump node: " << firstJumpNodeIndex.value()
                  << " divergence=" << describeDivergenceFromRightRun(pathStatsIt->second) << "\n";
    }
    else {
        std::cout << "No jump-action branch reached the pipe window within budget.\n";
    }

    printDiagnosticProgress("Searching for replay-validated pipe clear");
    std::optional<size_t> firstPipeClearNodeIndex = findFirstControllableNodePastFrontier(
        harness,
        fixtureResult.value(),
        replayCache,
        stateCache,
        nodeMap,
        search.getTrace(),
        stallFrontier + 16u);
    if (firstPipeClearNodeIndex.has_value()) {
        const auto& clearReplayResult = getCachedReplayResult(
            replayCache, harness, fixtureResult.value(), nodeMap, firstPipeClearNodeIndex.value());
        ASSERT_FALSE(clearReplayResult.isError()) << clearReplayResult.errorValue();
        const auto clearScreenshot = screenshotDir / "dfs_pipe_first_clear.ppm";
        if (writeReplayScreenshot(clearReplayResult.value(), clearScreenshot)) {
            std::cout << "First pipe clear screenshot: " << clearScreenshot.string() << "\n";
        }
        std::cout << "First replay-validated pipe clear node: " << firstPipeClearNodeIndex.value()
                  << "\n";
    }
    else {
        std::cout << "No replay-validated pipe clear within " << kSearchBudget << " budget.\n";
    }

    std::cout << "Top frontier nodes in pipe window:\n";
    for (const auto& entry : topNodes) {
        const auto pathStatsIt = pathStatsByNode.find(entry.nodeIndex);
        ASSERT_TRUE(pathStatsIt != pathStatsByNode.end());
        std::cout << "  node=" << entry.nodeIndex << " frontier=" << entry.frontier
                  << " frame=" << entry.gameplayFrame << " event=" << toString(entry.eventType)
                  << " lastAction="
                  << (entry.action.has_value() ? toString(entry.action.value()) : "Root")
                  << " divergence=" << describeDivergenceFromRightRun(pathStatsIt->second) << "\n";
    }

    printDiagnosticProgress("Pipe report complete");
    EXPECT_GE(search.getProgress().bestFrontier, postGoombaFrontier);
}

TEST(SmbDfsSearchTest, DeterministicTrace)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FirstGoomba);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    const SmbDfsSearchOptions options{
        .maxSearchedNodeCount = 2000u,
        .stallFrameLimit = 120u,
    };
    SmbDfsSearch firstSearch(options);
    SmbDfsSearch secondSearch(options);
    const auto firstStartResult = firstSearch.startFromFixture(fixtureResult.value());
    const auto secondStartResult = secondSearch.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(firstStartResult.isError()) << firstStartResult.errorValue();
    ASSERT_FALSE(secondStartResult.isError()) << secondStartResult.errorValue();

    const auto firstRunResult = runSearchToCompletion(firstSearch, 3000u);
    const auto secondRunResult = runSearchToCompletion(secondSearch, 3000u);
    ASSERT_FALSE(firstRunResult.isError()) << firstRunResult.errorValue();
    ASSERT_FALSE(secondRunResult.isError()) << secondRunResult.errorValue();

    ASSERT_EQ(firstSearch.getTrace().size(), secondSearch.getTrace().size());
    for (size_t i = 0; i < firstSearch.getTrace().size(); ++i) {
        expectTraceEq(firstSearch.getTrace()[i], secondSearch.getTrace()[i]);
    }

    EXPECT_EQ(
        firstSearch.getPlan().summary.bestFrontier, secondSearch.getPlan().summary.bestFrontier);
    EXPECT_EQ(
        firstSearch.getPlan().summary.elapsedFrames, secondSearch.getPlan().summary.elapsedFrames);
    expectPlanFramesEq(firstSearch.getPlan().frames, secondSearch.getPlan().frames);
}

TEST(SmbDfsSearchTest, PersistedPlanPlaybackMatchesFixtureSearchToFirstGap)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto flatResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(flatResult.isError()) << flatResult.errorValue();
    const auto firstGapResult = harness.captureFixture(SmbSearchRootFixtureId::FirstGap);
    ASSERT_FALSE(firstGapResult.isError()) << firstGapResult.errorValue();
    const uint64_t targetFrontier = firstGapResult.value().evaluatorSummary.bestFrontier;

    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = 2000u,
            .stallFrameLimit = 120u,
            .stopAfterBestFrontier = targetFrontier,
        });
    const auto startResult = search.startFromFixture(flatResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();
    const auto runResult = runSearchToCompletion(search, 4000u);
    ASSERT_FALSE(runResult.isError()) << runResult.errorValue();
    ASSERT_TRUE(search.hasPersistablePlan());
    EXPECT_GE(search.getPlan().summary.bestFrontier, targetFrontier);
    expectPersistedPlanPlaybackMatchesSearchPlan(search.getPlan());
}

TEST(SmbDfsSearchTest, PersistedPlanPlaybackMatchesStartDfsSearchToFirstGap)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto firstGapResult = harness.captureFixture(SmbSearchRootFixtureId::FirstGap);
    ASSERT_FALSE(firstGapResult.isError()) << firstGapResult.errorValue();
    const uint64_t targetFrontier = firstGapResult.value().evaluatorSummary.bestFrontier;

    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = 2000u,
            .stallFrameLimit = 120u,
            .stopAfterBestFrontier = targetFrontier,
        });
    const auto startResult = search.startDfs();
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();
    const auto runResult = runSearchToCompletion(search, 4000u);
    ASSERT_FALSE(runResult.isError()) << runResult.errorValue();
    ASSERT_TRUE(search.hasPersistablePlan());
    EXPECT_GE(search.getPlan().summary.bestFrontier, targetFrontier);
    expectPersistedPlanPlaybackMatchesSearchPlan(search.getPlan());
}

TEST(SmbDfsSearchTest, StopCompletes)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    SmbDfsSearch search;
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    const auto firstTickResult = search.tick();
    ASSERT_FALSE(firstTickResult.error.has_value()) << firstTickResult.error.value();
    search.stop();

    EXPECT_TRUE(search.isCompleted());
    EXPECT_EQ(search.getCompletionReason(), SmbDfsSearchCompletionReason::Stopped);
}

TEST(SmbDfsSearchTest, PauseHaltsTicks)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    SmbDfsSearch search;
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    search.pauseSet(true);
    const uint64_t pausedSearchedNodeCount = search.getProgress().searchedNodeCount;
    const auto pausedTickResult = search.tick();
    ASSERT_FALSE(pausedTickResult.error.has_value()) << pausedTickResult.error.value();
    EXPECT_FALSE(pausedTickResult.frameAdvanced);
    EXPECT_FALSE(pausedTickResult.completed);
    EXPECT_EQ(search.getProgress().searchedNodeCount, pausedSearchedNodeCount);

    search.pauseSet(false);
    const auto resumedTickResult = search.tick();
    ASSERT_FALSE(resumedTickResult.error.has_value()) << resumedTickResult.error.value();
    EXPECT_TRUE(resumedTickResult.frameAdvanced);
    EXPECT_GT(search.getProgress().searchedNodeCount, pausedSearchedNodeCount);
}

TEST(SmbDfsSearchTest, BacktrackSignalsRenderChange)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = 5000u,
            .stallFrameLimit = 120u,
        });
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    bool sawBacktrackRenderChange = false;
    for (size_t tickIndex = 0; tickIndex < 5000u && !search.isCompleted(); ++tickIndex) {
        const auto tickResult = search.tick();
        ASSERT_FALSE(tickResult.error.has_value()) << tickResult.error.value();
        if (tickResult.renderChanged && !tickResult.frameAdvanced
            && search.getProgress().lastSearchEvent
                == DirtSim::Api::SearchProgressEvent::Backtracked) {
            sawBacktrackRenderChange = true;
            break;
        }
    }

    EXPECT_TRUE(sawBacktrackRenderChange);
}

TEST(SmbDfsSearchTest, VelocityPruningProducesDedicatedTraceEvent)
{
    requireSmbRomOrSkip();

    SmbSearchHarness harness;
    const auto fixtureResult = harness.captureFixture(SmbSearchRootFixtureId::FlatGroundSanity);
    ASSERT_FALSE(fixtureResult.isError()) << fixtureResult.errorValue();

    SmbDfsSearch search(
        SmbDfsSearchOptions{
            .maxSearchedNodeCount = 5000u,
            .stallFrameLimit = 120u,
            .velocityPruningEnabled = true,
        });
    const auto startResult = search.startFromFixture(fixtureResult.value());
    ASSERT_FALSE(startResult.isError()) << startResult.errorValue();

    bool sawVelocityPrune = false;
    for (size_t tickIndex = 0; tickIndex < 5000u && !search.isCompleted(); ++tickIndex) {
        const auto tickResult = search.tick();
        ASSERT_FALSE(tickResult.error.has_value()) << tickResult.error.value();

        for (const auto& entry : search.getTrace()) {
            if (entry.eventType == SmbDfsSearchTraceEventType::PrunedVelocityStuck) {
                sawVelocityPrune = true;
                break;
            }
        }

        if (sawVelocityPrune) {
            break;
        }
    }

    EXPECT_TRUE(sawVelocityPrune);
}

TEST(SmbDfsSearchTest, LegalActionOrderRightRunFirst)
{
    const auto& legalActions = getSmbSearchLegalActions();
    ASSERT_FALSE(legalActions.empty());
    EXPECT_EQ(legalActions[0], SmbSearchLegalAction::RightRun);
}
