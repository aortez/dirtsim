#include "GenomeDbBenchmark.h"

#include "core/UUID.h"
#include "core/organisms/brains/Genome.h"
#include "server/api/GenomeDelete.h"
#include "server/api/GenomeGet.h"
#include "server/api/GenomeList.h"
#include "server/api/GenomeSet.h"

#include <chrono>
#include <optional>
#include <spdlog/spdlog.h>
#include <vector>

namespace DirtSim {
namespace Client {

namespace {

constexpr const char* kServerAddress = "ws://localhost:8080";
constexpr int kTimeoutMs = 5000;

std::vector<WeightType> createSentinelWeights(float seedValue)
{
    std::vector<WeightType> weights(Genome::EXPECTED_WEIGHT_COUNT);
    for (size_t i = 0; i < weights.size(); ++i) {
        weights[i] = seedValue + static_cast<float>(i) * 0.0001f;
    }
    return weights;
}

} // namespace

GenomeDbBenchmarkResults GenomeDbBenchmark::run(int count)
{
    GenomeDbBenchmarkResults results;
    results.count = count;
    results.genomeSizeBytes = Genome::EXPECTED_SIZE_BYTES;

    // Connect to server.
    spdlog::info("Connecting to server at {}", kServerAddress);
    const auto connectResult = client_.connect(kServerAddress);
    if (connectResult.isError()) {
        results.correctnessError = "Failed to connect: " + connectResult.errorValue();
        return results;
    }

    // Run correctness tests first.
    spdlog::info("Running correctness tests...");
    results.correctnessError = runCorrectnessTests();
    results.correctnessPassed = results.correctnessError.empty();

    if (!results.correctnessPassed) {
        spdlog::error("Correctness tests failed: {}", results.correctnessError);
        client_.disconnect();
        return results;
    }
    spdlog::info("Correctness tests passed");

    // Run performance tests.
    spdlog::info("Running performance tests with {} genomes...", count);
    runPerformanceTests(count, results);

    client_.disconnect();
    return results;
}

std::string GenomeDbBenchmark::runCorrectnessTests()
{
    const auto testId = UUID::generate();
    const auto sentinelWeights = createSentinelWeights(1.0f);
    const auto updatedWeights = createSentinelWeights(2.0f);

    // 1. Create genome.
    {
        Api::GenomeSet::Command cmd;
        cmd.id = testId;
        cmd.weights = sentinelWeights;
        cmd.metadata = GenomeMetadata{
            .name = "test-genome",
            .fitness = 42.0,
            .generation = 1,
            .createdTimestamp = 0,
            .scenarioId = Scenario::EnumType::TreeGermination,
            .notes = "",
            .organismType = std::nullopt,
            .brainKind = std::nullopt,
            .brainVariant = std::nullopt,
            .trainingSessionId = std::nullopt,
        };

        const auto result =
            client_.sendCommandAndGetResponse<Api::GenomeSet::Okay>(cmd, kTimeoutMs);
        if (result.isError()) {
            return "GenomeSet failed: " + result.errorValue();
        }
        if (result.value().isError()) {
            return "GenomeSet error: " + result.value().errorValue().message;
        }
        if (result.value().value().overwritten) {
            return "GenomeSet: unexpected overwrite on new genome";
        }
    }

    // 2. Get genome and verify.
    {
        Api::GenomeGet::Command cmd;
        cmd.id = testId;

        const auto result =
            client_.sendCommandAndGetResponse<Api::GenomeGet::Okay>(cmd, kTimeoutMs);
        if (result.isError()) {
            return "GenomeGet failed: " + result.errorValue();
        }
        if (result.value().isError()) {
            return "GenomeGet error: " + result.value().errorValue().message;
        }

        const auto& okay = result.value().value();
        if (!okay.found) {
            return "GenomeGet: genome not found after create";
        }
        if (okay.weights.size() != sentinelWeights.size()) {
            return "GenomeGet: weight count mismatch";
        }
        if (okay.weights[0] != sentinelWeights[0]) {
            return "GenomeGet: first weight mismatch";
        }
        if (okay.metadata.name != "test-genome") {
            return "GenomeGet: metadata name mismatch";
        }
    }

    // 3. Update genome.
    {
        Api::GenomeSet::Command cmd;
        cmd.id = testId;
        cmd.weights = updatedWeights;
        cmd.metadata = GenomeMetadata{
            .name = "test-genome-updated",
            .fitness = 99.0,
            .generation = 2,
            .createdTimestamp = 0,
            .scenarioId = Scenario::EnumType::TreeGermination,
            .notes = "",
            .organismType = std::nullopt,
            .brainKind = std::nullopt,
            .brainVariant = std::nullopt,
            .trainingSessionId = std::nullopt,
        };

        const auto result =
            client_.sendCommandAndGetResponse<Api::GenomeSet::Okay>(cmd, kTimeoutMs);
        if (result.isError()) {
            return "GenomeSet (update) failed: " + result.errorValue();
        }
        if (result.value().isError()) {
            return "GenomeSet (update) error: " + result.value().errorValue().message;
        }
        if (!result.value().value().overwritten) {
            return "GenomeSet: expected overwrite=true on update";
        }
    }

    // 4. Verify update.
    {
        Api::GenomeGet::Command cmd;
        cmd.id = testId;

        const auto result =
            client_.sendCommandAndGetResponse<Api::GenomeGet::Okay>(cmd, kTimeoutMs);
        if (result.isError()) {
            return "GenomeGet (after update) failed: " + result.errorValue();
        }
        if (result.value().isError()) {
            return "GenomeGet (after update) error: " + result.value().errorValue().message;
        }

        const auto& okay = result.value().value();
        if (okay.weights[0] != updatedWeights[0]) {
            return "GenomeGet: weights not updated";
        }
        if (okay.metadata.name != "test-genome-updated") {
            return "GenomeGet: metadata not updated";
        }
    }

    // 5. Delete genome.
    {
        Api::GenomeDelete::Command cmd;
        cmd.id = testId;

        const auto result =
            client_.sendCommandAndGetResponse<Api::GenomeDelete::Okay>(cmd, kTimeoutMs);
        if (result.isError()) {
            return "GenomeDelete failed: " + result.errorValue();
        }
        if (result.value().isError()) {
            return "GenomeDelete error: " + result.value().errorValue().message;
        }
        if (!result.value().value().success) {
            return "GenomeDelete: expected success=true";
        }
    }

    // 6. Verify deletion.
    {
        Api::GenomeGet::Command cmd;
        cmd.id = testId;

        const auto result =
            client_.sendCommandAndGetResponse<Api::GenomeGet::Okay>(cmd, kTimeoutMs);
        if (result.isError()) {
            return "GenomeGet (after delete) failed: " + result.errorValue();
        }
        if (result.value().isError()) {
            return "GenomeGet (after delete) error: " + result.value().errorValue().message;
        }
        if (result.value().value().found) {
            return "GenomeGet: genome still exists after delete";
        }
    }

    return ""; // All tests passed.
}

void GenomeDbBenchmark::runPerformanceTests(int count, GenomeDbBenchmarkResults& results)
{
    std::vector<UUID> ids;
    ids.reserve(count);

    const auto sentinelWeights = createSentinelWeights(3.0f);
    const auto updatedWeights = createSentinelWeights(4.0f);

    // Create N genomes.
    {
        const auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < count; ++i) {
            const auto id = UUID::generate();
            ids.push_back(id);

            Api::GenomeSet::Command cmd;
            cmd.id = id;
            cmd.weights = sentinelWeights;
            cmd.metadata = GenomeMetadata{
                .name = "perf-genome-" + std::to_string(i),
                .fitness = static_cast<double>(i),
                .generation = i,
                .createdTimestamp = 0,
                .scenarioId = Scenario::EnumType::TreeGermination,
                .notes = "",
                .organismType = std::nullopt,
                .brainKind = std::nullopt,
                .brainVariant = std::nullopt,
                .trainingSessionId = std::nullopt,
            };

            const auto result =
                client_.sendCommandAndGetResponse<Api::GenomeSet::Okay>(cmd, kTimeoutMs);
            if (result.isError() || result.value().isError()) {
                spdlog::warn("Create {} failed", i);
            }
        }

        const auto end = std::chrono::steady_clock::now();
        results.createTotalMs = std::chrono::duration<double, std::milli>(end - start).count();
        results.createOpsPerSec = count / (results.createTotalMs / 1000.0);
    }

    spdlog::info(
        "Create: {:.1f}ms ({:.1f} ops/sec)", results.createTotalMs, results.createOpsPerSec);

    // List all genomes.
    {
        const auto start = std::chrono::steady_clock::now();

        Api::GenomeList::Command cmd;
        const auto result =
            client_.sendCommandAndGetResponse<Api::GenomeList::Okay>(cmd, kTimeoutMs);

        const auto end = std::chrono::steady_clock::now();
        results.listMs = std::chrono::duration<double, std::milli>(end - start).count();

        if (!result.isError() && !result.value().isError()) {
            spdlog::info(
                "List: {:.1f}ms ({} genomes)",
                results.listMs,
                result.value().value().genomes.size());
        }
    }

    // Update all genomes.
    {
        const auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < count; ++i) {
            Api::GenomeSet::Command cmd;
            cmd.id = ids[i];
            cmd.weights = updatedWeights;
            cmd.metadata = GenomeMetadata{
                .name = "perf-genome-updated-" + std::to_string(i),
                .fitness = static_cast<double>(i * 2),
                .generation = i + 100,
                .createdTimestamp = 0,
                .scenarioId = Scenario::EnumType::TreeGermination,
                .notes = "",
                .organismType = std::nullopt,
                .brainKind = std::nullopt,
                .brainVariant = std::nullopt,
                .trainingSessionId = std::nullopt,
            };

            const auto result =
                client_.sendCommandAndGetResponse<Api::GenomeSet::Okay>(cmd, kTimeoutMs);
            if (result.isError() || result.value().isError()) {
                spdlog::warn("Update {} failed", i);
            }
        }

        const auto end = std::chrono::steady_clock::now();
        results.updateTotalMs = std::chrono::duration<double, std::milli>(end - start).count();
        results.updateOpsPerSec = count / (results.updateTotalMs / 1000.0);
    }

    spdlog::info(
        "Update: {:.1f}ms ({:.1f} ops/sec)", results.updateTotalMs, results.updateOpsPerSec);

    // Delete all genomes.
    {
        const auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < count; ++i) {
            Api::GenomeDelete::Command cmd;
            cmd.id = ids[i];

            const auto result =
                client_.sendCommandAndGetResponse<Api::GenomeDelete::Okay>(cmd, kTimeoutMs);
            if (result.isError() || result.value().isError()) {
                spdlog::warn("Delete {} failed", i);
            }
        }

        const auto end = std::chrono::steady_clock::now();
        results.deleteTotalMs = std::chrono::duration<double, std::milli>(end - start).count();
        results.deleteOpsPerSec = count / (results.deleteTotalMs / 1000.0);
    }

    spdlog::info(
        "Delete: {:.1f}ms ({:.1f} ops/sec)", results.deleteTotalMs, results.deleteOpsPerSec);
}

} // namespace Client
} // namespace DirtSim
