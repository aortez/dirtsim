#pragma once

#include "StateForward.h"
#include "ui/controls/StopPanel.h"
#include "ui/state-machine/Event.h"
#include <array>
#include <lvgl/lvgl.h>
#include <memory>

namespace DirtSim {
namespace Network {
class WebSocketService;
} // namespace Network

namespace Ui {
namespace State {

/**
 * @brief Synth screen state with a single-octave keyboard.
 */
struct Synth {
    Synth() = default;
    Synth(const Synth&) = delete;
    Synth& operator=(const Synth&) = delete;
    Synth(Synth&&) = default;
    Synth& operator=(Synth&&) = default;
    ~Synth();
    void onEnter(StateMachine& sm);
    void onExit(StateMachine& sm);

    Any onEvent(const IconSelectedEvent& evt, StateMachine& sm);
    Any onEvent(const RailAutoShrinkRequestEvent& evt, StateMachine& sm);
    Any onEvent(const RailModeChangedEvent& evt, StateMachine& sm);
    Any onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm);
    Any onEvent(const StopButtonClickedEvent& evt, StateMachine& sm);
    Any onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::SynthKeyPress::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm);
    Any onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm);

    static constexpr const char* name() { return "Synth"; }
    int getLastKeyIndex() const { return lastKeyIndex_; }
    bool getLastKeyIsBlack() const { return lastKeyIsBlack_; }

private:
    static void onKeyPressed(lv_event_t* e);
    static void onKeyboardResized(lv_event_t* e);
    void applyKeyPress(lv_obj_t* key, int keyIndex, bool isBlack, const char* source);
    bool findKeyIndex(lv_obj_t* key, int& keyIndex, bool& isBlack) const;
    void layoutKeyboard();
    bool ensureAudioConnected();

    lv_obj_t* contentRoot_ = nullptr;
    lv_obj_t* keyboardContainer_ = nullptr;
    lv_obj_t* whiteKeysContainer_ = nullptr;
    lv_obj_t* bottomRow_ = nullptr;
    std::array<lv_obj_t*, 7> whiteKeys_{};
    std::array<lv_obj_t*, 5> blackKeys_{};
    std::unique_ptr<StopPanel> homePanel_;
    std::unique_ptr<DirtSim::Network::WebSocketService> audioClient_;
    bool audioWarningLogged_ = false;
    lv_obj_t* lastKey_ = nullptr;
    int lastKeyIndex_ = -1;
    bool lastKeyIsBlack_ = false;
};

} // namespace State
} // namespace Ui
} // namespace DirtSim
