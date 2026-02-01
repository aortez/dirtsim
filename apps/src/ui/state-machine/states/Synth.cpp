#include "State.h"
#include "audio/api/NoteOn.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "ui/RemoteInputDevice.h"
#include "ui/UiComponentManager.h"
#include "ui/state-machine/StateMachine.h"
#include <algorithm>

namespace DirtSim {
namespace Ui {
namespace State {

namespace {
constexpr float kBlackKeyWidthRatio = 0.6f;
constexpr float kBlackKeyHeightRatio = 0.6f;
constexpr int kKeyboardPadding = 20;
constexpr int kWhiteKeyBorderWidth = 2;
constexpr uint32_t kWhiteKeyColor = 0xF2F2F2;
constexpr uint32_t kWhiteKeyPressedColor = 0xD0D0D0;
constexpr uint32_t kBlackKeyColor = 0x111111;
constexpr uint32_t kBlackKeyPressedColor = 0x3A3A3A;
constexpr int kAudioConnectTimeoutMs = 200;
constexpr double kKeyAmplitude = 0.2;
constexpr double kKeyAttackMs = 5.0;
constexpr double kKeyReleaseMs = 90.0;
constexpr double kKeyDurationMs = 140.0;
constexpr std::array<double, 7> kWhiteKeyFrequencies = {
    261.63, // C4
    293.66, // D4
    329.63, // E4
    349.23, // F4
    392.00, // G4
    440.00, // A4
    493.88, // B4
};
constexpr std::array<double, 5> kBlackKeyFrequencies = {
    277.18, // C#4
    311.13, // D#4
    369.99, // F#4
    415.30, // G#4
    466.16, // A#4
};
} // namespace

Synth::~Synth() = default;

void Synth::onEnter(StateMachine& sm)
{
    LOG_INFO(State, "Entering Synth state");

    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) {
        LOG_ERROR(State, "No UiComponentManager available");
        return;
    }

    uiManager->getMainMenuContainer();
    lv_obj_t* contentArea = uiManager->getMenuContentArea();
    if (!contentArea) {
        LOG_ERROR(State, "No menu content area available");
        return;
    }

    lv_obj_clean(contentArea);

    contentRoot_ = lv_obj_create(contentArea);
    lv_obj_set_size(contentRoot_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(contentRoot_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(contentRoot_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(contentRoot_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(contentRoot_, 0, 0);
    lv_obj_set_style_pad_row(contentRoot_, 0, 0);
    lv_obj_set_style_border_width(contentRoot_, 0, 0);
    lv_obj_clear_flag(contentRoot_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* keyboardRow = lv_obj_create(contentRoot_);
    lv_obj_set_size(keyboardRow, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(keyboardRow, 1);
    lv_obj_set_style_bg_opa(keyboardRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(keyboardRow, kKeyboardPadding, 0);
    lv_obj_set_style_border_width(keyboardRow, 0, 0);
    lv_obj_clear_flag(keyboardRow, LV_OBJ_FLAG_SCROLLABLE);

    keyboardContainer_ = lv_obj_create(keyboardRow);
    lv_obj_set_size(keyboardContainer_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(keyboardContainer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(keyboardContainer_, 0, 0);
    lv_obj_set_style_border_width(keyboardContainer_, 0, 0);
    lv_obj_clear_flag(keyboardContainer_, LV_OBJ_FLAG_SCROLLABLE);

    whiteKeysContainer_ = lv_obj_create(keyboardContainer_);
    lv_obj_set_size(whiteKeysContainer_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(whiteKeysContainer_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(whiteKeysContainer_, 0, 0);
    lv_obj_set_style_pad_column(whiteKeysContainer_, 0, 0);
    lv_obj_set_style_bg_opa(whiteKeysContainer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(whiteKeysContainer_, 0, 0);
    lv_obj_clear_flag(whiteKeysContainer_, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < whiteKeys_.size(); i++) {
        lv_obj_t* key = lv_obj_create(whiteKeysContainer_);
        lv_obj_set_flex_grow(key, 1);
        lv_obj_set_height(key, LV_PCT(100));
        lv_obj_set_style_bg_color(key, lv_color_hex(kWhiteKeyColor), 0);
        lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(key, kWhiteKeyBorderWidth, 0);
        lv_obj_set_style_border_color(key, lv_color_hex(0x202020), 0);
        lv_obj_set_style_radius(key, 0, 0);
        lv_obj_clear_flag(key, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(key, onKeyPressed, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(key, onKeyPressed, LV_EVENT_RELEASED, this);
        whiteKeys_[i] = key;
    }

    for (size_t i = 0; i < blackKeys_.size(); i++) {
        lv_obj_t* key = lv_obj_create(keyboardContainer_);
        lv_obj_set_style_bg_color(key, lv_color_hex(kBlackKeyColor), 0);
        lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(key, 1, 0);
        lv_obj_set_style_border_color(key, lv_color_hex(0x000000), 0);
        lv_obj_set_style_radius(key, 0, 0);
        lv_obj_add_flag(key, LV_OBJ_FLAG_FLOATING);
        lv_obj_clear_flag(key, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(key, onKeyPressed, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(key, onKeyPressed, LV_EVENT_RELEASED, this);
        blackKeys_[i] = key;
    }

    lv_obj_add_event_cb(keyboardContainer_, onKeyboardResized, LV_EVENT_SIZE_CHANGED, this);
    layoutKeyboard();

    lastKey_ = nullptr;
    lastKeyIndex_ = -1;
    lastKeyIsBlack_ = false;
    audioWarningLogged_ = false;
    ensureAudioConnected();

    bottomRow_ = lv_obj_create(contentRoot_);
    lv_obj_set_size(bottomRow_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(bottomRow_, 1);
    lv_obj_set_style_bg_opa(bottomRow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottomRow_, 0, 0);
    lv_obj_clear_flag(bottomRow_, LV_OBJ_FLAG_SCROLLABLE);

    if (auto* panel = uiManager->getExpandablePanel()) {
        panel->hide();
        panel->clearContent();
        panel->resetWidth();
    }

    IconRail* iconRail = uiManager->getIconRail();
    DIRTSIM_ASSERT(iconRail, "IconRail must exist");
    iconRail->setLayout(RailLayout::SingleColumn);
    iconRail->setVisibleIcons({ IconId::CORE, IconId::MUSIC });
    iconRail->selectIcon(IconId::MUSIC);
}

void Synth::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting Synth state");

    homePanel_.reset();
    if (audioClient_ && audioClient_->isConnected()) {
        audioClient_->disconnect();
    }
    audioClient_.reset();

    if (auto* uiManager = sm.getUiComponentManager()) {
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->clearContent();
            panel->hide();
            panel->resetWidth();
        }
    }

    if (contentRoot_) {
        lv_obj_del(contentRoot_);
        contentRoot_ = nullptr;
        keyboardContainer_ = nullptr;
        whiteKeysContainer_ = nullptr;
        bottomRow_ = nullptr;
        whiteKeys_.fill(nullptr);
        blackKeys_.fill(nullptr);
    }
}

State::Any Synth::onEvent(const IconSelectedEvent& evt, StateMachine& sm)
{
    LOG_INFO(
        State,
        "Icon selection changed: {} -> {}",
        static_cast<int>(evt.previousId),
        static_cast<int>(evt.selectedId));

    auto* uiManager = sm.getUiComponentManager();

    if (evt.selectedId == IconId::CORE) {
        LOG_INFO(State, "Home icon selected, showing Stop panel");
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->clearContent();
            panel->resetWidth();
            homePanel_ =
                std::make_unique<StopPanel>(panel->getContentArea(), sm, sm.getFractalAnimator());
            panel->show();
        }
        return std::move(*this);
    }

    if (evt.previousId == IconId::CORE) {
        LOG_INFO(State, "Home icon deselected, hiding Stop panel");
        homePanel_.reset();
        if (auto* panel = uiManager->getExpandablePanel()) {
            panel->hide();
            panel->clearContent();
            panel->resetWidth();
        }
    }

    if (evt.selectedId == IconId::COUNT || evt.selectedId == IconId::MUSIC) {
        return std::move(*this);
    }

    DIRTSIM_ASSERT(false, "Unexpected icon selection in Synth state");
    return std::move(*this);
}

State::Any Synth::onEvent(const RailAutoShrinkRequestEvent& /*evt*/, StateMachine& sm)
{
    LOG_INFO(State, "Auto-shrink requested, minimizing IconRail");

    if (auto* iconRail = sm.getUiComponentManager()->getIconRail()) {
        iconRail->setMode(RailMode::Minimized);
    }

    return std::move(*this);
}

State::Any Synth::onEvent(const RailModeChangedEvent& /*evt*/, StateMachine& /*sm*/)
{
    return std::move(*this);
}

State::Any Synth::onEvent(const StopButtonClickedEvent& /*evt*/, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Stop button clicked, returning to StartMenu");
    return StartMenu{};
}

State::Any Synth::onEvent(const ServerDisconnectedEvent& evt, StateMachine& sm)
{
    LOG_WARN(State, "Server disconnected (reason: {})", evt.reason);
    LOG_INFO(State, "Transitioning back to Disconnected");

    if (!sm.queueReconnectToLastServer()) {
        LOG_WARN(State, "No previous server address available for reconnect");
    }

    return Disconnected{};
}

State::Any Synth::onEvent(const UiApi::Exit::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Exit command received, shutting down");
    cwc.sendResponse(UiApi::Exit::Response::okay(std::monostate{}));
    return Shutdown{};
}

State::Any Synth::onEvent(const UiApi::SimStop::Cwc& cwc, StateMachine& /*sm*/)
{
    LOG_INFO(State, "SimStop command received, returning to StartMenu");
    cwc.sendResponse(UiApi::SimStop::Response::okay({ true }));
    return StartMenu{};
}

State::Any Synth::onEvent(const UiApi::SynthKeyPress::Cwc& cwc, StateMachine& /*sm*/)
{
    const bool isBlack = cwc.command.is_black;
    const int keyIndex = cwc.command.key_index;
    if (keyIndex < 0 || (isBlack && static_cast<size_t>(keyIndex) >= blackKeys_.size())
        || (!isBlack && static_cast<size_t>(keyIndex) >= whiteKeys_.size())) {
        cwc.sendResponse(UiApi::SynthKeyPress::Response::error(ApiError("Invalid key index")));
        return std::move(*this);
    }

    lv_obj_t* key = isBlack ? blackKeys_[static_cast<size_t>(keyIndex)]
                            : whiteKeys_[static_cast<size_t>(keyIndex)];
    if (!key || !lv_obj_is_valid(key)) {
        cwc.sendResponse(UiApi::SynthKeyPress::Response::error(ApiError("Synth key unavailable")));
        return std::move(*this);
    }

    applyKeyPress(key, keyIndex, isBlack, "api");

    UiApi::SynthKeyPress::Okay response{ .key_index = keyIndex, .is_black = isBlack };
    cwc.sendResponse(UiApi::SynthKeyPress::Response::okay(std::move(response)));
    return std::move(*this);
}

State::Any Synth::onEvent(const UiApi::MouseDown::Cwc& cwc, StateMachine& sm)
{
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
        sm.getRemoteInputDevice()->updatePressed(true);
    }

    cwc.sendResponse(UiApi::MouseDown::Response::okay({}));
    return std::move(*this);
}

State::Any Synth::onEvent(const UiApi::MouseMove::Cwc& cwc, StateMachine& sm)
{
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
    }

    cwc.sendResponse(UiApi::MouseMove::Response::okay({}));
    return std::move(*this);
}

State::Any Synth::onEvent(const UiApi::MouseUp::Cwc& cwc, StateMachine& sm)
{
    if (sm.getRemoteInputDevice()) {
        sm.getRemoteInputDevice()->updatePosition(cwc.command.pixelX, cwc.command.pixelY);
        sm.getRemoteInputDevice()->updatePressed(false);
    }

    cwc.sendResponse(UiApi::MouseUp::Response::okay({}));
    return std::move(*this);
}

void Synth::onKeyboardResized(lv_event_t* e)
{
    auto* self = static_cast<Synth*>(lv_event_get_user_data(e));
    if (!self) return;

    self->layoutKeyboard();
}

void Synth::onKeyPressed(lv_event_t* e)
{
    auto* self = static_cast<Synth*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    auto* key = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!key || !lv_obj_is_valid(key)) {
        return;
    }

    int keyIndex = -1;
    bool isBlack = false;
    if (!self->findKeyIndex(key, keyIndex, isBlack)) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        self->applyKeyPress(key, keyIndex, isBlack, "touch");
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        const uint32_t resetColor = isBlack ? kBlackKeyColor : kWhiteKeyColor;
        lv_obj_set_style_bg_color(key, lv_color_hex(resetColor), 0);
        return;
    }
}

void Synth::applyKeyPress(lv_obj_t* key, int keyIndex, bool isBlack, const char* source)
{
    if (!key || !lv_obj_is_valid(key)) {
        return;
    }

    if (lastKey_ && lv_obj_is_valid(lastKey_)) {
        const uint32_t resetColor = lastKeyIsBlack_ ? kBlackKeyColor : kWhiteKeyColor;
        lv_obj_set_style_bg_color(lastKey_, lv_color_hex(resetColor), 0);
    }

    const uint32_t pressedColor = isBlack ? kBlackKeyPressedColor : kWhiteKeyPressedColor;
    lv_obj_set_style_bg_color(key, lv_color_hex(pressedColor), 0);

    lastKey_ = key;
    lastKeyIndex_ = keyIndex;
    lastKeyIsBlack_ = isBlack;

    const double frequency = isBlack ? kBlackKeyFrequencies[static_cast<size_t>(keyIndex)]
                                     : kWhiteKeyFrequencies[static_cast<size_t>(keyIndex)];
    LOG_INFO(
        State,
        "Synth key pressed (index={}, black={}, freq={:.2f}Hz, source={})",
        keyIndex,
        isBlack,
        frequency,
        source);

    if (ensureAudioConnected()) {
        AudioApi::NoteOn::Command note{};
        note.frequency_hz = frequency;
        note.amplitude = kKeyAmplitude;
        note.attack_ms = kKeyAttackMs;
        note.release_ms = kKeyReleaseMs;
        note.duration_ms = kKeyDurationMs;
        note.waveform = Audio::Waveform::Square;

        const auto sendResult =
            audioClient_->sendCommandAndGetResponse<AudioApi::NoteOn::Okay>(note, 500);
        if (sendResult.isError()) {
            LOG_WARN(State, "Synth audio NoteOn failed: {}", sendResult.errorValue());
        }
        else if (sendResult.value().isError()) {
            LOG_WARN(
                State, "Synth audio NoteOn rejected: {}", sendResult.value().errorValue().message);
        }
    }
}

bool Synth::findKeyIndex(lv_obj_t* key, int& keyIndex, bool& isBlack) const
{
    for (size_t i = 0; i < whiteKeys_.size(); ++i) {
        if (whiteKeys_[i] == key) {
            keyIndex = static_cast<int>(i);
            isBlack = false;
            return true;
        }
    }

    for (size_t i = 0; i < blackKeys_.size(); ++i) {
        if (blackKeys_[i] == key) {
            keyIndex = static_cast<int>(i);
            isBlack = true;
            return true;
        }
    }

    return false;
}

void Synth::layoutKeyboard()
{
    if (!keyboardContainer_) {
        return;
    }

    const int width = lv_obj_get_width(keyboardContainer_);
    const int height = lv_obj_get_height(keyboardContainer_);
    if (width <= 0 || height <= 0) {
        return;
    }

    constexpr int whiteKeyCount = 7;
    const int whiteKeyWidth = width / whiteKeyCount;
    const int blackKeyWidth = static_cast<int>(whiteKeyWidth * kBlackKeyWidthRatio);
    const int blackKeyHeight = static_cast<int>(height * kBlackKeyHeightRatio);

    const std::array<int, 5> centers = { whiteKeyWidth * 1,
                                         whiteKeyWidth * 2,
                                         whiteKeyWidth * 4,
                                         whiteKeyWidth * 5,
                                         whiteKeyWidth * 6 };

    for (size_t i = 0; i < blackKeys_.size(); i++) {
        lv_obj_t* key = blackKeys_[i];
        if (!key) {
            continue;
        }
        int x = centers[i] - (blackKeyWidth / 2);
        x = std::clamp(x, 0, std::max(0, width - blackKeyWidth));
        lv_obj_set_size(key, blackKeyWidth, blackKeyHeight);
        lv_obj_set_pos(key, x, 0);
        lv_obj_move_foreground(key);
    }
}

bool Synth::ensureAudioConnected()
{
    if (!audioClient_) {
        audioClient_ = std::make_unique<DirtSim::Network::WebSocketService>();
    }

    if (audioClient_->isConnected()) {
        return true;
    }

    const auto connectResult = audioClient_->connect("ws://localhost:6060", kAudioConnectTimeoutMs);
    if (connectResult.isError()) {
        if (!audioWarningLogged_) {
            LOG_WARN(
                State, "Audio service unavailable for synth keys: {}", connectResult.errorValue());
            audioWarningLogged_ = true;
        }
        return false;
    }

    audioWarningLogged_ = false;
    return true;
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
