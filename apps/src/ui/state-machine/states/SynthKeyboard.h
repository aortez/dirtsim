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

    bool handleKeyEvent(
        int keyIndex, bool isBlack, bool isPressed, const char* source, std::string& error);

    void setVolumePercent(int volumePercent);

    int getLastKeyIndex() const { return lastKeyIndex_; }
    bool getLastKeyIsBlack() const { return lastKeyIsBlack_; }

private:
    static void onKeyboardResized(lv_event_t* e);
    static void onKeyPressed(lv_event_t* e);
    static void onTouchPollTimer(lv_timer_t* timer);

    bool decodeCommandKeyIndex(
        int keyIndex, bool isBlack, size_t& localIndex, size_t& octaveIndex) const;
    bool decodeUniqueKeyIndex(
        size_t uniqueKeyIndex, int& keyIndex, bool& isBlack, int& octaveIndex) const;
    bool findKeyAtPoint(
        const lv_point_t& point,
        lv_obj_t*& key,
        int& keyIndex,
        bool& isBlack,
        int& octaveIndex) const;
    bool findKeyIndex(lv_obj_t* key, int& keyIndex, bool& isBlack, int& octaveIndex) const;
    int getCommandKeyIndex(int keyIndex, bool isBlack, int octaveIndex) const;
    int getUniqueKeyIndex(int keyIndex, bool isBlack, int octaveIndex) const;
    uint32_t getKeyNoteId(int keyIndex, bool isBlack, int octaveIndex) const;
    void clearKeyNoteId(int keyIndex, bool isBlack, int octaveIndex);
    void pressKey(lv_obj_t* key, int keyIndex, bool isBlack, int octaveIndex, const char* source);
    void pressTouchKey(
        lv_obj_t* key, int keyIndex, bool isBlack, int octaveIndex, const char* source);
    void releaseKey(lv_obj_t* key, int keyIndex, bool isBlack, int octaveIndex, const char* source);
    void releaseTouchKey(
        lv_obj_t* key, int keyIndex, bool isBlack, int octaveIndex, const char* source);
    bool isTouchKeyPressed(int keyIndex, bool isBlack, int octaveIndex) const;
    void setTouchKeyPressed(int keyIndex, bool isBlack, int octaveIndex, bool isPressed);
    void setKeyVisual(lv_obj_t* key, bool isBlack, bool isPressed);
    void releaseAllActiveKeys(const char* source);
    void syncEvdevTouchState();
    void layoutKeyboard();
    bool ensureAudioConnected();

    static constexpr size_t kOctaveCount = 2;
    static constexpr size_t kBlackKeysPerOctave = 5;
    static constexpr size_t kWhiteKeysPerOctave = 7;
    static constexpr size_t kBlackKeyCount = kOctaveCount * kBlackKeysPerOctave;
    static constexpr size_t kWhiteKeyCount = kOctaveCount * kWhiteKeysPerOctave;
    static constexpr size_t kTotalKeyCount = kBlackKeyCount + kWhiteKeyCount;
    struct OctaveKeys {
        lv_obj_t* container = nullptr;
        lv_obj_t* whiteKeysContainer = nullptr;
        std::array<lv_obj_t*, kWhiteKeysPerOctave> whiteKeys{};
        std::array<lv_obj_t*, kBlackKeysPerOctave> blackKeys{};
    };

    lv_obj_t* keyboardRow_ = nullptr;
    lv_obj_t* keyboardContainer_ = nullptr;
    lv_timer_t* touchPollTimer_ = nullptr;
    std::array<OctaveKeys, kOctaveCount> octaves_{};
    std::unique_ptr<DirtSim::Network::WebSocketService, WebSocketServiceDeleter> audioClient_;
    std::array<uint32_t, kTotalKeyCount> keyNoteIds_{};
    std::array<bool, kTotalKeyCount> touchKeyPressed_{};
    std::array<bool, kTotalKeyCount> evdevTouchKeyPressed_{};
    bool audioWarningLogged_ = false;
    int volumePercent_ = 50;
    int lastKeyIndex_ = -1;
    bool lastKeyIsBlack_ = false;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
