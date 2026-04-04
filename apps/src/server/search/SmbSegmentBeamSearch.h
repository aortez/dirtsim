#pragma once

#include "core/Result.h"
#include "server/search/SmbSearchCore.h"
#include "server/search/SmbSearchHarness.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace DirtSim::Server::SearchSupport {

struct SmbSegmentBeamSearchParams {
    uint32_t beamWidth = 0;
    uint32_t segmentFrameBudget = 0;
};

struct SmbSegmentBeamSearchState {
    std::optional<size_t> bestNodeIndex = std::nullopt;
    std::vector<size_t> frontierNodeIndices;
    std::vector<SmbSearchNode> nodes;
    uint64_t expandedNodeCount = 0;
    uint64_t nextCandidateIndex = 0;
    uint64_t rootFrontier = 0;
    uint64_t totalCandidateCount = 0;
    double rootEvaluationScore = 0.0;
    uint32_t completedSteps = 0;
    uint32_t searchDepth = 0;
    bool frontierImproved = false;
    bool completed = false;
};

struct SmbSegmentBeamSearchResult {
    std::optional<size_t> bestNodeIndex = std::nullopt;
    std::vector<size_t> frontierNodeIndices;
    std::vector<SmbSearchNode> nodes;
    uint64_t expandedNodeCount = 0;
    uint64_t nextCandidateIndex = 0;
    uint64_t rootFrontier = 0;
    uint64_t totalCandidateCount = 0;
    double rootEvaluationScore = 0.0;
    uint32_t searchDepth = 0;
    bool frontierImproved = false;
};

struct SmbSegmentBeamSearchTickResult {
    bool completed = false;
    bool stepAdvanced = false;
};

class SmbSegmentBeamSearch {
public:
    SmbSegmentBeamSearchResult buildResult(const SmbSegmentBeamSearchState& state) const;
    Result<SmbSegmentBeamSearchState, std::string> start(
        const SmbSearchRootFixture& rootFixture, uint64_t startCandidateIndex = 0u) const;
    Result<SmbSegmentBeamSearchTickResult, std::string> tick(
        SmbSegmentBeamSearchState& state, const SmbSegmentBeamSearchParams& params) const;
    Result<SmbSegmentBeamSearchResult, std::string> run(
        const SmbSearchRootFixture& rootFixture, const SmbSegmentBeamSearchParams& params) const;
};

} // namespace DirtSim::Server::SearchSupport
