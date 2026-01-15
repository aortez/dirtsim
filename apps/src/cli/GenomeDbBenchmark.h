#pragma once

#include "core/network/WebSocketService.h"

#include <string>

namespace DirtSim {
namespace Client {

/**
 * Results from genome database benchmark.
 */
struct GenomeDbBenchmarkResults {
    bool correctnessPassed = false;
    std::string correctnessError;

    int count = 0;

    double createTotalMs = 0.0;
    double createOpsPerSec = 0.0;
    double deleteTotalMs = 0.0;
    double deleteOpsPerSec = 0.0;
    double listMs = 0.0;
    double updateTotalMs = 0.0;
    double updateOpsPerSec = 0.0;

    size_t genomeSizeBytes = 0;
};

/**
 * Runs genome database correctness and performance tests.
 * Connects to an already-running server at localhost:8080.
 */
class GenomeDbBenchmark {
public:
    GenomeDbBenchmarkResults run(int count = 100);

private:
    Network::WebSocketService client_;

    std::string runCorrectnessTests();
    void runPerformanceTests(int count, GenomeDbBenchmarkResults& results);
};

} // namespace Client
} // namespace DirtSim
