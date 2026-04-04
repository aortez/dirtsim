#pragma once

#include "core/Result.h"
#include "core/input/PlayerControlFrame.h"
#include "server/search/SmbSegmentBeamSearch.h"

#include <cstdint>
#include <vector>

namespace DirtSim::Server::SearchSupport {

enum class SmbSegmentSearchSessionOutcome : uint8_t {
    ReachedSegmentLimit = 0,
    SegmentFailure = 1,
};

struct SmbSegmentSearchPromotion {
    SmbSearchRootFixture committedRoot;
    std::vector<PlayerControlFrame> segmentFrames;
};

struct SmbSegmentSearchSessionParams {
    uint32_t maxSegments = 0;
    SmbSegmentBeamSearchParams segmentParams;
};

struct SmbSegmentSearchSessionResult {
    std::vector<SmbSearchRootFixture> committedRoots;
    std::vector<SmbSegmentSearchPromotion> promotions;
    std::vector<PlayerControlFrame> planFrames;
    uint64_t expandedNodeCount = 0;
    uint64_t segmentAttempts = 0;
    uint64_t successfulSegments = 0;
    SmbSegmentSearchSessionOutcome outcome = SmbSegmentSearchSessionOutcome::ReachedSegmentLimit;
};

class SmbSegmentSearchSession {
public:
    Result<SmbSegmentSearchSessionResult, std::string> run(
        const SmbSearchRootFixture& initialRoot, const SmbSegmentSearchSessionParams& params) const;
};

} // namespace DirtSim::Server::SearchSupport
