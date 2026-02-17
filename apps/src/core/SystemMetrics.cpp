#include "SystemMetrics.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace DirtSim {

uint64_t SystemMetrics::CpuSnapshot::totalActive() const
{
    return user + nice + system + irq + softirq + steal;
}

uint64_t SystemMetrics::CpuSnapshot::total() const
{
    return totalActive() + idle + iowait;
}

SystemMetrics::SystemMetrics() = default;
SystemMetrics::~SystemMetrics() = default;

void SystemMetrics::readCpuSnapshots(
    CpuSnapshot& totalSnapshot, std::vector<CpuSnapshot>& coreSnapshots)
{
    totalSnapshot = CpuSnapshot{};
    coreSnapshots.clear();

    std::ifstream stat("/proc/stat");
    if (!stat.is_open()) {
        return;
    }

    std::string line;
    bool sawCpuLine = false;
    while (std::getline(stat, line)) {
        if (line.compare(0, 3, "cpu") != 0) {
            if (sawCpuLine) {
                break;
            }
            continue;
        }

        sawCpuLine = true;

        // Parse: cpu[N] user nice system idle iowait irq softirq steal guest guest_nice.
        std::istringstream iss(line);
        std::string cpuLabel;
        CpuSnapshot snap;
        iss >> cpuLabel >> snap.user >> snap.nice >> snap.system >> snap.idle >> snap.iowait
            >> snap.irq >> snap.softirq >> snap.steal;
        if (!iss) {
            continue;
        }

        if (cpuLabel == "cpu") {
            totalSnapshot = snap;
            continue;
        }

        if (cpuLabel.size() <= 3 || cpuLabel.rfind("cpu", 0) != 0) {
            continue;
        }

        const bool hasNumericSuffix = std::all_of(cpuLabel.begin() + 3, cpuLabel.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        });
        if (hasNumericSuffix) {
            coreSnapshots.push_back(snap);
        }
    }
}

SystemMetrics::Metrics SystemMetrics::get()
{
    Metrics m;

    // Read CPU snapshot and calculate delta.
    CpuSnapshot currTotal;
    std::vector<CpuSnapshot> currPerCore;
    readCpuSnapshots(currTotal, currPerCore);
    m.cpu_percent_per_core.assign(currPerCore.size(), 0.0);
    if (has_prev_snapshot_) {
        uint64_t total_delta = currTotal.total() - prev_cpu_.total();
        uint64_t active_delta = currTotal.totalActive() - prev_cpu_.totalActive();
        if (total_delta > 0) {
            m.cpu_percent =
                (static_cast<double>(active_delta) / static_cast<double>(total_delta)) * 100.0;
        }

        const size_t comparableCoreCount = std::min(currPerCore.size(), prev_cpu_per_core_.size());
        for (size_t i = 0; i < comparableCoreCount; ++i) {
            const uint64_t coreTotalDelta = currPerCore[i].total() - prev_cpu_per_core_[i].total();
            const uint64_t coreActiveDelta =
                currPerCore[i].totalActive() - prev_cpu_per_core_[i].totalActive();
            if (coreTotalDelta > 0) {
                m.cpu_percent_per_core[i] =
                    (static_cast<double>(coreActiveDelta) / static_cast<double>(coreTotalDelta))
                    * 100.0;
            }
        }
    }
    prev_cpu_ = currTotal;
    prev_cpu_per_core_ = currPerCore;
    has_prev_snapshot_ = true;

    // Read memory from /proc/meminfo.
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo.is_open()) {
        std::string line;
        uint64_t mem_total = 0;
        uint64_t mem_available = 0;
        bool found_total = false;
        bool found_available = false;

        while (std::getline(meminfo, line) && !(found_total && found_available)) {
            if (line.compare(0, 10, "MemTotal:") == 0 || line.compare(0, 9, "MemTotal:") == 0) {
                std::istringstream iss(line);
                std::string label;
                iss >> label >> mem_total;
                found_total = true;
            }
            else if (
                line.compare(0, 14, "MemAvailable:") == 0
                || line.compare(0, 13, "MemAvailable:") == 0) {
                std::istringstream iss(line);
                std::string label;
                iss >> label >> mem_available;
                found_available = true;
            }
        }

        if (found_total && found_available && mem_total > 0) {
            m.memory_total_kb = mem_total;
            m.memory_used_kb = mem_total - mem_available;
            m.memory_percent =
                (static_cast<double>(m.memory_used_kb) / static_cast<double>(mem_total)) * 100.0;
        }
    }

    return m;
}

} // namespace DirtSim
