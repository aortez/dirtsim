#include "server/search/SmbSegmentSearchSession.h"

#include <string>

namespace DirtSim::Server::SearchSupport {

Result<SmbSegmentSearchSessionResult, std::string> SmbSegmentSearchSession::run(
    const SmbSearchRootFixture& initialRoot, const SmbSegmentSearchSessionParams& params) const
{
    SmbSegmentSearchSessionResult result{};
    result.committedRoots.push_back(initialRoot);
    if (params.maxSegments == 0u) {
        return Result<SmbSegmentSearchSessionResult, std::string>::okay(std::move(result));
    }

    const SmbSegmentBeamSearch segmentSearch;
    const std::string baseName = initialRoot.name;
    SmbSearchRootFixture currentRoot = initialRoot;

    for (uint32_t segmentIndex = 0u; segmentIndex < params.maxSegments; ++segmentIndex) {
        const auto segmentResult = segmentSearch.run(currentRoot, params.segmentParams);
        if (segmentResult.isError()) {
            return Result<SmbSegmentSearchSessionResult, std::string>::error(
                "Failed to run SMB segment search attempt " + std::to_string(segmentIndex) + ": "
                + segmentResult.errorValue());
        }

        const auto& segment = segmentResult.value();
        result.expandedNodeCount += segment.expandedNodeCount;
        result.segmentAttempts += 1u;

        if (!segment.bestNodeIndex.has_value()) {
            result.outcome = SmbSegmentSearchSessionOutcome::SegmentFailure;
            return Result<SmbSegmentSearchSessionResult, std::string>::okay(std::move(result));
        }

        const size_t bestNodeIndex = segment.bestNodeIndex.value();
        if (bestNodeIndex >= segment.nodes.size()) {
            return Result<SmbSegmentSearchSessionResult, std::string>::error(
                "SMB segment search returned an out-of-range best node index");
        }

        const SmbSearchNode& bestNode = segment.nodes[bestNodeIndex];
        if (!bestNode.checkpointEligible
            || bestNode.evaluatorSummary.evaluationScore <= segment.rootEvaluationScore) {
            return Result<SmbSegmentSearchSessionResult, std::string>::error(
                "SMB segment search returned a non-promotable best node");
        }

        const auto segmentFramesResult = reconstructPlanFrames(segment.nodes, bestNodeIndex);
        if (segmentFramesResult.isError()) {
            return Result<SmbSegmentSearchSessionResult, std::string>::error(
                "Failed to reconstruct SMB segment plan: " + segmentFramesResult.errorValue());
        }

        const std::vector<PlayerControlFrame>& segmentFrames = segmentFramesResult.value();
        SmbSearchRootFixture promotedRoot{};
        promotedRoot.id = initialRoot.id;
        promotedRoot.evaluatorSummary = bestNode.evaluatorSummary;
        promotedRoot.memorySnapshot = bestNode.memorySnapshot;
        promotedRoot.scenarioVideoFrame = bestNode.scenarioVideoFrame;
        promotedRoot.savestate = bestNode.savestate;
        promotedRoot.name = baseName + "_segment_" + std::to_string(segmentIndex + 1u);

        result.planFrames.insert(
            result.planFrames.end(), segmentFrames.begin(), segmentFrames.end());
        result.promotions.push_back(
            SmbSegmentSearchPromotion{
                .committedRoot = promotedRoot,
                .segmentFrames = segmentFrames,
            });
        result.committedRoots.push_back(promotedRoot);
        result.successfulSegments += 1u;
        currentRoot = std::move(promotedRoot);
    }

    result.outcome = SmbSegmentSearchSessionOutcome::ReachedSegmentLimit;
    return Result<SmbSegmentSearchSessionResult, std::string>::okay(std::move(result));
}

} // namespace DirtSim::Server::SearchSupport
