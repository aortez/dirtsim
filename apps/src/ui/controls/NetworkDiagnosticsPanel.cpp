#include "NetworkDiagnosticsPanel.h"
#include "core/LoggingChannels.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

NetworkDiagnosticsPanel::NetworkDiagnosticsPanel(lv_obj_t* container) : container_(container)
{
    createUI();
    LOG_INFO(Controls, "NetworkDiagnosticsPanel created");
}

NetworkDiagnosticsPanel::~NetworkDiagnosticsPanel()
{
    LOG_INFO(Controls, "NetworkDiagnosticsPanel destroyed");
}

void NetworkDiagnosticsPanel::createUI()
{
    // Title.
    lv_obj_t* title = lv_label_create(container_);
    lv_label_set_text(title, "Network");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(title, LV_PCT(100));

    // IP Address section header.
    lv_obj_t* ipHeader = lv_label_create(container_);
    lv_label_set_text(ipHeader, "IP Address:");
    lv_obj_set_style_text_font(ipHeader, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ipHeader, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_pad_top(ipHeader, 16, 0);

    // Address display label (will be updated with actual addresses).
    addressLabel_ = lv_label_create(container_);
    lv_obj_set_style_text_font(addressLabel_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(addressLabel_, lv_color_hex(0x00CED1), 0);
    lv_obj_set_width(addressLabel_, LV_PCT(100));
    lv_label_set_long_mode(addressLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_top(addressLabel_, 8, 0);

    // Refresh button.
    refreshButton_ = LVGLBuilder::actionButton(container_)
                         .text("Refresh")
                         .icon(LV_SYMBOL_REFRESH)
                         .mode(LVGLBuilder::ActionMode::Push)
                         .width(LV_PCT(95))
                         .callback(onRefreshClicked, this)
                         .buildOrLog();

    if (refreshButton_) {
        lv_obj_set_style_pad_top(refreshButton_, 16, 0);
    }

    // Initial display update.
    updateAddressDisplay();
}

void NetworkDiagnosticsPanel::refresh()
{
    updateAddressDisplay();
}

void NetworkDiagnosticsPanel::updateAddressDisplay()
{
    std::vector<NetworkInterfaceInfo> addresses = getLocalAddresses();

    if (addresses.empty()) {
        lv_label_set_text(addressLabel_, "No network");
        return;
    }

    // Build display string with all addresses.
    std::string displayText;
    for (const auto& info : addresses) {
        if (!displayText.empty()) {
            displayText += "\n";
        }
        displayText += info.name + ": " + info.address;
    }

    lv_label_set_text(addressLabel_, displayText.c_str());
    LOG_DEBUG(Controls, "Network addresses updated: {}", displayText);
}

std::vector<NetworkInterfaceInfo> NetworkDiagnosticsPanel::getLocalAddresses()
{
    std::vector<NetworkInterfaceInfo> result;

    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        LOG_WARN(Controls, "Failed to get network interfaces: {}", strerror(errno));
        return result;
    }

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) {
            continue;
        }

        // Only interested in IPv4.
        if (ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        // Skip loopback interfaces.
        if ((ifa->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        // Skip interfaces that are down.
        if ((ifa->ifa_flags & IFF_UP) == 0) {
            continue;
        }

        // Get the address string.
        char addrBuf[INET_ADDRSTRLEN];
        struct sockaddr_in* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sa->sin_addr, addrBuf, sizeof(addrBuf)) != nullptr) {
            result.push_back({ ifa->ifa_name, addrBuf });
            LOG_DEBUG(Controls, "Found interface {}: {}", ifa->ifa_name, addrBuf);
        }
    }

    freeifaddrs(ifaddr);
    return result;
}

void NetworkDiagnosticsPanel::onRefreshClicked(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    NetworkDiagnosticsPanel* self =
        static_cast<NetworkDiagnosticsPanel*>(lv_event_get_user_data(e));
    if (self) {
        self->refresh();
        LOG_INFO(Controls, "Network info refreshed by user");
    }
}

} // namespace Ui
} // namespace DirtSim
