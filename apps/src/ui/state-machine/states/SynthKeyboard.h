#pragma once

#include <array>
#include <lvgl/lvgl.h>
#include <memory>
#include <string>

namespace DirtSim {
namespace Network {
class WebSocketService;
} // namespace Network

namespace Ui {
namespace State {

struct WebSocketServiceDeleter {
    void operator()(DirtSim::Network::WebSocketService* service) const;
};

class SynthKeyboard {
public:
    SynthKeyboard() = default;
    ~SynthKeyboard();
    SynthKeyboard(const SynthKeyboard&) = delete;
    SynthKeyboard& operator=(const SynthKeyboard&) = delete;
    SynthKeyboard(SynthKeyboard&&) noexcept;
    SynthKeyboard& operator=(SynthKeyboard&&) noexcept;

    void create(lv_obj_t* parent);
    void destroy();

    bool handleKeyPress(int keyIndex, bool isBlack, const char* source, std::string& error);

    void setVolumePercent(int volumePercent);

    int getLastKeyIndex() const { return lastKeyIndex_; }
    bool getLastKeyIsBlack() const { return lastKeyIsBlack_; }

private:
    static void onKeyboardResized(lv_event_t* e);
    static void onKeyPressed(lv_event_t* e);

    void applyKeyPress(lv_obj_t* key, int keyIndex, bool isBlack, const char* source);
    bool findKeyIndex(lv_obj_t* key, int& keyIndex, bool& isBlack) const;
    void layoutKeyboard();
    bool ensureAudioConnected();
    void resetLastKeyVisual();

    lv_obj_t* keyboardRow_ = nullptr;
    lv_obj_t* keyboardContainer_ = nullptr;
    lv_obj_t* whiteKeysContainer_ = nullptr;
    std::array<lv_obj_t*, 7> whiteKeys_{};
    std::array<lv_obj_t*, 5> blackKeys_{};
    std::unique_ptr<DirtSim::Network::WebSocketService, WebSocketServiceDeleter> audioClient_;
    bool audioWarningLogged_ = false;
    int volumePercent_ = 50;
    lv_obj_t* lastKey_ = nullptr;
    int lastKeyIndex_ = -1;
    bool lastKeyIsBlack_ = false;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
