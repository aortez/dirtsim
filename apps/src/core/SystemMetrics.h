#pragma once

#include <cstdint>

namespace DirtSim {

// Provides system health metrics (CPU usage, memory usage) by reading from /proc.
// Linux-specific implementation.
class SystemMetrics {
  public:
    struct Metrics {
        double cpu_percent = 0.0;     // CPU usage as percentage (0-100).
        double memory_percent = 0.0;  // Memory usage as percentage (0-100).
        uint64_t memory_used_kb = 0;  // Memory used in KB.
        uint64_t memory_total_kb = 0; // Total memory in KB.
    };

    SystemMetrics();
    ~SystemMetrics();

    // Get current system metrics.
    // Call this periodically - CPU calculation requires delta from previous call.
    Metrics get();

  private:
    struct CpuSnapshot {
        uint64_t user = 0;
        uint64_t nice = 0;
        uint64_t system = 0;
        uint64_t idle = 0;
        uint64_t iowait = 0;
        uint64_t irq = 0;
        uint64_t softirq = 0;
        uint64_t steal = 0;

        uint64_t totalActive() const;
        uint64_t total() const;
    };

    CpuSnapshot readCpuSnapshot();

    CpuSnapshot prev_cpu_;
    bool has_prev_snapshot_ = false;
};

} // namespace DirtSim
