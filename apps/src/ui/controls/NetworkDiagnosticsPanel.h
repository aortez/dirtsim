#pragma once

#include "lvgl/lvgl.h"
#include <string>
#include <vector>

namespace DirtSim {
namespace Ui {

/**
 * @brief Network interface information for display.
 */
struct NetworkInterfaceInfo {
    std::string name;    // Interface name (e.g., "eth0", "wlan0").
    std::string address; // IPv4 address.
};

/**
 * @brief Panel displaying network diagnostics information.
 *
 * Shows the device's IP address(es) and other network status information.
 * Useful for remotely connecting to the device.
 */
class NetworkDiagnosticsPanel {
public:
    /**
     * @brief Construct the network diagnostics panel.
     * @param container Parent LVGL container to build UI in.
     */
    explicit NetworkDiagnosticsPanel(lv_obj_t* container);
    ~NetworkDiagnosticsPanel();

    /**
     * @brief Refresh the network information display.
     *
     * Call this to update the displayed IP addresses (e.g., if network
     * configuration changes).
     */
    void refresh();

    /**
     * @brief Get all non-loopback IPv4 addresses on the system.
     * @return Vector of interface info structs.
     */
    static std::vector<NetworkInterfaceInfo> getLocalAddresses();

private:
    lv_obj_t* container_;
    lv_obj_t* addressLabel_ = nullptr;
    lv_obj_t* refreshButton_ = nullptr;

    void createUI();
    void updateAddressDisplay();

    static void onRefreshClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
