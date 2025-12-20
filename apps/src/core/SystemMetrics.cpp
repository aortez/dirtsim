#include "SystemMetrics.h"

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

SystemMetrics::CpuSnapshot SystemMetrics::readCpuSnapshot()
{
    CpuSnapshot snap;
    std::ifstream stat("/proc/stat");
    if (!stat.is_open()) {
        return snap;
    }

    std::string line;
    while (std::getline(stat, line)) {
        if (line.compare(0, 4, "cpu ") == 0) {
            // Parse: cpu user nice system idle iowait irq softirq steal guest guest_nice
            std::istringstream iss(line);
            std::string cpu_label;
            iss >> cpu_label >> snap.user >> snap.nice >> snap.system >> snap.idle >> snap.iowait
                >> snap.irq >> snap.softirq >> snap.steal;
            break;
        }
    }

    return snap;
}

SystemMetrics::Metrics SystemMetrics::get()
{
    Metrics m;

    // Read CPU snapshot and calculate delta.
    CpuSnapshot curr = readCpuSnapshot();
    if (has_prev_snapshot_) {
        uint64_t total_delta = curr.total() - prev_cpu_.total();
        uint64_t active_delta = curr.totalActive() - prev_cpu_.totalActive();
        if (total_delta > 0) {
            m.cpu_percent = (static_cast<double>(active_delta) / static_cast<double>(total_delta)) * 100.0;
        }
    }
    prev_cpu_ = curr;
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
            } else if (line.compare(0, 14, "MemAvailable:") == 0 || line.compare(0, 13, "MemAvailable:") == 0) {
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
