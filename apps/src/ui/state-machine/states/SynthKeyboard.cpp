#include "SynthKeyboard.h"
#include "audio/api/NoteOff.h"
#include "audio/api/NoteOn.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
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
constexpr std::array<double, 7> kWhiteKeyFrequencies = {
    261.63, // C4.
    293.66, // D4.
    329.63, // E4.
    349.23, // F4.
    392.00, // G4.
    440.00, // A4.
    493.88, // B4.
};
constexpr std::array<double, 5> kBlackKeyFrequencies = {
    277.18, // C#4.
    311.13, // D#4.
    369.99, // F#4.
    415.30, // G#4.
    466.16, // A#4.
};
constexpr std::array<double, 2> kOctaveFrequencyMultipliers = { 1.0, 0.5 };

bool isPointInside(lv_obj_t* obj, const lv_point_t& point)
{
    if (!obj || !lv_obj_is_valid(obj)) {
        return false;
    }

    lv_area_t area{};
    lv_obj_get_coords(obj, &area);
    return point.x >= area.x1 && point.x <= area.x2 && point.y >= area.y1 && point.y <= area.y2;
}
} // namespace

void WebSocketServiceDeleter::operator()(DirtSim::Network::WebSocketService* service) const
{
    delete service;
}

SynthKeyboard::~SynthKeyboard() = default;
SynthKeyboard::SynthKeyboard(SynthKeyboard&&) noexcept = default;
SynthKeyboard& SynthKeyboard::operator=(SynthKeyboard&&) noexcept = default;

void SynthKeyboard::create(lv_obj_t* parent)
{
    destroy();

    if (!parent) {
        LOG_ERROR(State, "SynthKeyboard: parent container missing");
        return;
    }

    keyboardRow_ = lv_obj_create(parent);
    lv_obj_set_size(keyboardRow_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(keyboardRow_, 1);
    lv_obj_set_style_bg_opa(keyboardRow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(keyboardRow_, kKeyboardPadding, 0);
    lv_obj_set_style_border_width(keyboardRow_, 0, 0);
    lv_obj_clear_flag(keyboardRow_, LV_OBJ_FLAG_SCROLLABLE);

    keyboardContainer_ = lv_obj_create(keyboardRow_);
    lv_obj_set_size(keyboardContainer_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(keyboardContainer_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(keyboardContainer_, 0, 0);
    lv_obj_set_style_bg_opa(keyboardContainer_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(keyboardContainer_, 0, 0);
    lv_obj_set_style_border_width(keyboardContainer_, 0, 0);
    lv_obj_clear_flag(keyboardContainer_, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t octaveIndex = 0; octaveIndex < octaves_.size(); ++octaveIndex) {
        auto& octave = octaves_[octaveIndex];

        octave.container = lv_obj_create(keyboardContainer_);
        lv_obj_set_size(
            octave.container, LV_PCT(100), LV_PCT(100 / static_cast<int>(kOctaveCount)));
        lv_obj_set_flex_grow(octave.container, 0);
        lv_obj_set_style_bg_opa(octave.container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(octave.container, 0, 0);
        lv_obj_set_style_border_width(octave.container, 0, 0);
        lv_obj_clear_flag(octave.container, LV_OBJ_FLAG_SCROLLABLE);

        octave.whiteKeysContainer = lv_obj_create(octave.container);
        lv_obj_set_size(octave.whiteKeysContainer, LV_PCT(100), LV_PCT(100));
        lv_obj_set_flex_flow(octave.whiteKeysContainer, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(octave.whiteKeysContainer, 0, 0);
        lv_obj_set_style_pad_column(octave.whiteKeysContainer, 0, 0);
        lv_obj_set_style_bg_opa(octave.whiteKeysContainer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(octave.whiteKeysContainer, 0, 0);
        lv_obj_clear_flag(octave.whiteKeysContainer, LV_OBJ_FLAG_SCROLLABLE);

        for (size_t i = 0; i < octave.whiteKeys.size(); i++) {
            lv_obj_t* key = lv_obj_create(octave.whiteKeysContainer);
            lv_obj_set_flex_grow(key, 1);
            lv_obj_set_height(key, LV_PCT(100));
            lv_obj_set_style_bg_color(key, lv_color_hex(kWhiteKeyColor), 0);
            lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(key, kWhiteKeyBorderWidth, 0);
            lv_obj_set_style_border_color(key, lv_color_hex(0x202020), 0);
            lv_obj_set_style_radius(key, 0, 0);
            lv_obj_clear_flag(key, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(key, onKeyPressed, LV_EVENT_PRESSED, this);
            lv_obj_add_event_cb(key, onKeyPressed, LV_EVENT_PRESSING, this);
            lv_obj_add_event_cb(key, onKeyPressed, LV_EVENT_RELEASED, this);
            lv_obj_add_event_cb(key, onKeyPressed, LV_EVENT_PRESS_LOST, this);
            octave.whiteKeys[i] = key;
        }

        for (size_t i = 0; i < octave.blackKeys.size(); i++) {
            lv_obj_t* key = lv_obj_create(octave.container);
            lv_obj_set_style_bg_color(key, lv_color_hex(kBlackKeyColor), 0);
            lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(key, 1, 0);
            lv_obj_set_style_border_color(key, lv_color_hex(0x000000), 0);
            lv_obj_set_style_radius(key, 0, 0);
            lv_obj_add_flag(key, LV_OBJ_FLAG_FLOATING);
            lv_obj_clear_flag(key, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(key, onKeyPressed, LV_EVENT_PRESSED, this);
            lv_obj_add_event_cb(key, onKeyPressed, LV_EVENT_PRESSING, this);
            lv_obj_add_event_cb(key, onKeyPressed, LV_EVENT_RELEASED, this);
            lv_obj_add_event_cb(key, onKeyPressed, LV_EVENT_PRESS_LOST, this);
            octave.blackKeys[i] = key;
        }
    }

    lv_obj_update_layout(keyboardContainer_);
    lv_obj_add_event_cb(keyboardContainer_, onKeyboardResized, LV_EVENT_SIZE_CHANGED, this);
    layoutKeyboard();

    keyNoteIds_.fill(0);
    touchKeyPressed_.fill(false);
    evdevTouchKeyPressed_.fill(false);
    lastKeyIndex_ = -1;
    lastKeyIsBlack_ = false;
    audioWarningLogged_ = false;

    touchPollTimer_ = lv_timer_create(onTouchPollTimer, 16, this);
    if (!touchPollTimer_) {
        LOG_WARN(State, "Failed to create synth touch poll timer");
    }
}

void SynthKeyboard::destroy()
{
    if (touchPollTimer_) {
        lv_timer_delete(touchPollTimer_);
        touchPollTimer_ = nullptr;
    }

    releaseAllActiveKeys("destroy");

    if (audioClient_ && audioClient_->isConnected()) {
        audioClient_->disconnect();
    }
    audioClient_.reset();

    if (keyboardRow_ && lv_obj_is_valid(keyboardRow_)) {
        lv_obj_del(keyboardRow_);
    }

    keyboardRow_ = nullptr;
    keyboardContainer_ = nullptr;
    for (auto& octave : octaves_) {
        octave.container = nullptr;
        octave.whiteKeysContainer = nullptr;
        octave.whiteKeys.fill(nullptr);
        octave.blackKeys.fill(nullptr);
    }
    keyNoteIds_.fill(0);
    touchKeyPressed_.fill(false);
    evdevTouchKeyPressed_.fill(false);
    lastKeyIndex_ = -1;
    lastKeyIsBlack_ = false;
}

bool SynthKeyboard::handleKeyEvent(
    int keyIndex, bool isBlack, bool isPressed, const char* source, std::string& error)
{
    size_t localIndex = 0;
    size_t octaveIndex = 0;
    if (!decodeCommandKeyIndex(keyIndex, isBlack, localIndex, octaveIndex)) {
        error = "Invalid key index";
        return false;
    }

    lv_obj_t* key = isBlack ? octaves_[octaveIndex].blackKeys[localIndex]
                            : octaves_[octaveIndex].whiteKeys[localIndex];
    if (!key || !lv_obj_is_valid(key)) {
        error = "Synth key unavailable";
        return false;
    }

    if (isPressed) {
        pressKey(key, static_cast<int>(localIndex), isBlack, static_cast<int>(octaveIndex), source);
    }
    else {
        releaseKey(
            key, static_cast<int>(localIndex), isBlack, static_cast<int>(octaveIndex), source);
    }

    return true;
}

void SynthKeyboard::setVolumePercent(int volumePercent)
{
    volumePercent_ = std::clamp(volumePercent, 0, 100);
}

void SynthKeyboard::onKeyboardResized(lv_event_t* e)
{
    auto* self = static_cast<SynthKeyboard*>(lv_event_get_user_data(e));
    if (!self) return;

    self->layoutKeyboard();
}

void SynthKeyboard::onKeyPressed(lv_event_t* e)
{
    auto* self = static_cast<SynthKeyboard*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

#if LV_USE_EVDEV
    lv_indev_t* indev = lv_indev_get_act();
    if (indev && lv_evdev_is_indev(indev)) {
        return;
    }
#endif

    const lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* key = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!key || !lv_obj_is_valid(key)) {
        return;
    }

    int keyIndex = -1;
    bool isBlack = false;
    int octaveIndex = -1;
    if (!self->findKeyIndex(key, keyIndex, isBlack, octaveIndex)) {
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        self->releaseTouchKey(key, keyIndex, isBlack, octaveIndex, "touch-lvgl");
        return;
    }

    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) {
        return;
    }

    self->pressTouchKey(key, keyIndex, isBlack, octaveIndex, "touch-lvgl");
}

void SynthKeyboard::onTouchPollTimer(lv_timer_t* timer)
{
    auto* self = static_cast<SynthKeyboard*>(lv_timer_get_user_data(timer));
    if (!self) {
        return;
    }

    self->syncEvdevTouchState();
}

bool SynthKeyboard::decodeCommandKeyIndex(
    int keyIndex, bool isBlack, size_t& localIndex, size_t& octaveIndex) const
{
    const size_t keysPerOctave = isBlack ? kBlackKeysPerOctave : kWhiteKeysPerOctave;
    const size_t maxKeys = keysPerOctave * octaves_.size();
    if (keyIndex < 0 || static_cast<size_t>(keyIndex) >= maxKeys) {
        return false;
    }

    octaveIndex = static_cast<size_t>(keyIndex) / keysPerOctave;
    localIndex = static_cast<size_t>(keyIndex) % keysPerOctave;
    if (octaveIndex >= octaves_.size()) {
        return false;
    }

    return true;
}

bool SynthKeyboard::decodeUniqueKeyIndex(
    size_t uniqueKeyIndex, int& keyIndex, bool& isBlack, int& octaveIndex) const
{
    if (uniqueKeyIndex < kWhiteKeyCount) {
        isBlack = false;
        octaveIndex = static_cast<int>(uniqueKeyIndex / kWhiteKeysPerOctave);
        keyIndex = static_cast<int>(uniqueKeyIndex % kWhiteKeysPerOctave);
        return octaveIndex >= 0 && static_cast<size_t>(octaveIndex) < kOctaveCount;
    }

    if (uniqueKeyIndex >= kTotalKeyCount) {
        return false;
    }

    isBlack = true;
    const size_t blackOffset = uniqueKeyIndex - kWhiteKeyCount;
    octaveIndex = static_cast<int>(blackOffset / kBlackKeysPerOctave);
    keyIndex = static_cast<int>(blackOffset % kBlackKeysPerOctave);
    return octaveIndex >= 0 && static_cast<size_t>(octaveIndex) < kOctaveCount;
}

void SynthKeyboard::pressKey(
    lv_obj_t* key, int keyIndex, bool isBlack, int octaveIndex, const char* source)
{
    if (!key || !lv_obj_is_valid(key)) {
        return;
    }

    if (octaveIndex < 0 || static_cast<size_t>(octaveIndex) >= kOctaveFrequencyMultipliers.size()) {
        return;
    }

    setKeyVisual(key, isBlack, true);

    lastKeyIndex_ = getCommandKeyIndex(keyIndex, isBlack, octaveIndex);
    lastKeyIsBlack_ = isBlack;

    const size_t localIndex = static_cast<size_t>(keyIndex);
    const double baseFrequency =
        isBlack ? kBlackKeyFrequencies[localIndex] : kWhiteKeyFrequencies[localIndex];
    const double frequency =
        baseFrequency * kOctaveFrequencyMultipliers[static_cast<size_t>(octaveIndex)];

    AudioApi::NoteOn::Command note{};
    note.frequency_hz = frequency;
    note.amplitude = kKeyAmplitude * (static_cast<double>(volumePercent_) / 100.0);
    note.attack_ms = kKeyAttackMs;
    note.release_ms = kKeyReleaseMs;
    note.duration_ms = 0.0;
    note.waveform = Audio::Waveform::Square;
    note.note_id = getKeyNoteId(keyIndex, isBlack, octaveIndex);

    LOG_INFO(
        State,
        "Synth key pressed (index={}, black={}, freq={:.2f}Hz, note_id={}, source={})",
        lastKeyIndex_,
        isBlack,
        frequency,
        note.note_id,
        source);

    if (!ensureAudioConnected()) {
        return;
    }

    const auto sendResult =
        audioClient_->sendCommandAndGetResponse<AudioApi::NoteOn::Okay>(note, 500);
    if (sendResult.isError()) {
        LOG_WARN(State, "Synth audio NoteOn failed: {}", sendResult.errorValue());
        return;
    }

    if (sendResult.value().isError()) {
        LOG_WARN(State, "Synth audio NoteOn rejected: {}", sendResult.value().errorValue().message);
        return;
    }

    const uint32_t acceptedNoteId = sendResult.value().value().note_id;
    keyNoteIds_[static_cast<size_t>(getUniqueKeyIndex(keyIndex, isBlack, octaveIndex))] =
        acceptedNoteId;
}

void SynthKeyboard::pressTouchKey(
    lv_obj_t* key, int keyIndex, bool isBlack, int octaveIndex, const char* source)
{
    if (isTouchKeyPressed(keyIndex, isBlack, octaveIndex)) {
        return;
    }

    pressKey(key, keyIndex, isBlack, octaveIndex, source);
    setTouchKeyPressed(keyIndex, isBlack, octaveIndex, true);
}

void SynthKeyboard::releaseKey(
    lv_obj_t* key, int keyIndex, bool isBlack, int octaveIndex, const char* source)
{
    if (!key || !lv_obj_is_valid(key)) {
        return;
    }

    setKeyVisual(key, isBlack, false);

    const int commandKeyIndex = getCommandKeyIndex(keyIndex, isBlack, octaveIndex);
    const uint32_t noteId = getKeyNoteId(keyIndex, isBlack, octaveIndex);

    LOG_INFO(
        State,
        "Synth key released (index={}, black={}, note_id={}, source={})",
        commandKeyIndex,
        isBlack,
        noteId,
        source);

    if (noteId != 0 && ensureAudioConnected()) {
        AudioApi::NoteOff::Command noteOff{ .note_id = noteId };
        const auto sendResult =
            audioClient_->sendCommandAndGetResponse<AudioApi::NoteOff::Okay>(noteOff, 500);
        if (sendResult.isError()) {
            LOG_WARN(State, "Synth audio NoteOff failed: {}", sendResult.errorValue());
        }
        else if (sendResult.value().isError()) {
            LOG_WARN(
                State, "Synth audio NoteOff rejected: {}", sendResult.value().errorValue().message);
        }
    }

    clearKeyNoteId(keyIndex, isBlack, octaveIndex);

    if (lastKeyIndex_ == commandKeyIndex && lastKeyIsBlack_ == isBlack) {
        lastKeyIndex_ = -1;
        lastKeyIsBlack_ = false;
    }
}

void SynthKeyboard::releaseTouchKey(
    lv_obj_t* key, int keyIndex, bool isBlack, int octaveIndex, const char* source)
{
    if (!isTouchKeyPressed(keyIndex, isBlack, octaveIndex)) {
        return;
    }

    releaseKey(key, keyIndex, isBlack, octaveIndex, source);
    setTouchKeyPressed(keyIndex, isBlack, octaveIndex, false);
}

bool SynthKeyboard::findKeyAtPoint(
    const lv_point_t& point, lv_obj_t*& key, int& keyIndex, bool& isBlack, int& octaveIndex) const
{
    for (size_t octave = 0; octave < octaves_.size(); ++octave) {
        const auto& octaveKeys = octaves_[octave];
        for (size_t i = 0; i < octaveKeys.blackKeys.size(); ++i) {
            lv_obj_t* candidate = octaveKeys.blackKeys[i];
            if (isPointInside(candidate, point)) {
                key = candidate;
                keyIndex = static_cast<int>(i);
                isBlack = true;
                octaveIndex = static_cast<int>(octave);
                return true;
            }
        }
    }

    for (size_t octave = 0; octave < octaves_.size(); ++octave) {
        const auto& octaveKeys = octaves_[octave];
        for (size_t i = 0; i < octaveKeys.whiteKeys.size(); ++i) {
            lv_obj_t* candidate = octaveKeys.whiteKeys[i];
            if (isPointInside(candidate, point)) {
                key = candidate;
                keyIndex = static_cast<int>(i);
                isBlack = false;
                octaveIndex = static_cast<int>(octave);
                return true;
            }
        }
    }

    return false;
}

bool SynthKeyboard::findKeyIndex(
    lv_obj_t* key, int& keyIndex, bool& isBlack, int& octaveIndex) const
{
    for (size_t octave = 0; octave < octaves_.size(); ++octave) {
        const auto& octaveKeys = octaves_[octave];
        for (size_t i = 0; i < octaveKeys.whiteKeys.size(); ++i) {
            if (octaveKeys.whiteKeys[i] == key) {
                keyIndex = static_cast<int>(i);
                isBlack = false;
                octaveIndex = static_cast<int>(octave);
                return true;
            }
        }
        for (size_t i = 0; i < octaveKeys.blackKeys.size(); ++i) {
            if (octaveKeys.blackKeys[i] == key) {
                keyIndex = static_cast<int>(i);
                isBlack = true;
                octaveIndex = static_cast<int>(octave);
                return true;
            }
        }
    }

    return false;
}

int SynthKeyboard::getCommandKeyIndex(int keyIndex, bool isBlack, int octaveIndex) const
{
    const int keysPerOctave =
        isBlack ? static_cast<int>(kBlackKeysPerOctave) : static_cast<int>(kWhiteKeysPerOctave);
    return (octaveIndex * keysPerOctave) + keyIndex;
}

int SynthKeyboard::getUniqueKeyIndex(int keyIndex, bool isBlack, int octaveIndex) const
{
    const int offset = isBlack ? static_cast<int>(kWhiteKeyCount) : 0;
    return offset + getCommandKeyIndex(keyIndex, isBlack, octaveIndex);
}

uint32_t SynthKeyboard::getKeyNoteId(int keyIndex, bool isBlack, int octaveIndex) const
{
    const int uniqueKeyIndex = getUniqueKeyIndex(keyIndex, isBlack, octaveIndex);
    if (uniqueKeyIndex < 0 || static_cast<size_t>(uniqueKeyIndex) >= keyNoteIds_.size()) {
        return 0;
    }
    return keyNoteIds_[static_cast<size_t>(uniqueKeyIndex)];
}

void SynthKeyboard::clearKeyNoteId(int keyIndex, bool isBlack, int octaveIndex)
{
    const int uniqueKeyIndex = getUniqueKeyIndex(keyIndex, isBlack, octaveIndex);
    if (uniqueKeyIndex < 0 || static_cast<size_t>(uniqueKeyIndex) >= keyNoteIds_.size()) {
        return;
    }
    keyNoteIds_[static_cast<size_t>(uniqueKeyIndex)] = 0;
}

bool SynthKeyboard::isTouchKeyPressed(int keyIndex, bool isBlack, int octaveIndex) const
{
    const int uniqueKeyIndex = getUniqueKeyIndex(keyIndex, isBlack, octaveIndex);
    if (uniqueKeyIndex < 0 || static_cast<size_t>(uniqueKeyIndex) >= touchKeyPressed_.size()) {
        return false;
    }

    return touchKeyPressed_[static_cast<size_t>(uniqueKeyIndex)];
}

void SynthKeyboard::setTouchKeyPressed(int keyIndex, bool isBlack, int octaveIndex, bool isPressed)
{
    const int uniqueKeyIndex = getUniqueKeyIndex(keyIndex, isBlack, octaveIndex);
    if (uniqueKeyIndex < 0 || static_cast<size_t>(uniqueKeyIndex) >= touchKeyPressed_.size()) {
        return;
    }

    touchKeyPressed_[static_cast<size_t>(uniqueKeyIndex)] = isPressed;
}

void SynthKeyboard::setKeyVisual(lv_obj_t* key, bool isBlack, bool isPressed)
{
    if (!key || !lv_obj_is_valid(key)) {
        return;
    }

    const uint32_t color = isPressed ? (isBlack ? kBlackKeyPressedColor : kWhiteKeyPressedColor)
                                     : (isBlack ? kBlackKeyColor : kWhiteKeyColor);
    lv_obj_set_style_bg_color(key, lv_color_hex(color), 0);
}

void SynthKeyboard::releaseAllActiveKeys(const char* source)
{
    for (size_t uniqueKeyIndex = 0; uniqueKeyIndex < keyNoteIds_.size(); ++uniqueKeyIndex) {
        const bool wasTouchPressed =
            touchKeyPressed_[uniqueKeyIndex] || evdevTouchKeyPressed_[uniqueKeyIndex];
        const uint32_t noteId = keyNoteIds_[uniqueKeyIndex];
        if (!wasTouchPressed && noteId == 0) {
            continue;
        }

        int keyIndex = -1;
        bool isBlack = false;
        int octaveIndex = -1;
        if (!decodeUniqueKeyIndex(uniqueKeyIndex, keyIndex, isBlack, octaveIndex)) {
            keyNoteIds_[uniqueKeyIndex] = 0;
            touchKeyPressed_[uniqueKeyIndex] = false;
            evdevTouchKeyPressed_[uniqueKeyIndex] = false;
            continue;
        }

        lv_obj_t* key = isBlack
            ? octaves_[static_cast<size_t>(octaveIndex)].blackKeys[static_cast<size_t>(keyIndex)]
            : octaves_[static_cast<size_t>(octaveIndex)].whiteKeys[static_cast<size_t>(keyIndex)];
        if (key && lv_obj_is_valid(key)) {
            setKeyVisual(key, isBlack, false);
        }

        const int commandKeyIndex = getCommandKeyIndex(keyIndex, isBlack, octaveIndex);
        LOG_INFO(
            State,
            "Synth key released (index={}, black={}, note_id={}, source={})",
            commandKeyIndex,
            isBlack,
            noteId,
            source);

        if (noteId != 0 && audioClient_ && audioClient_->isConnected()) {
            AudioApi::NoteOff::Command noteOff{ .note_id = noteId };
            const auto sendResult =
                audioClient_->sendCommandAndGetResponse<AudioApi::NoteOff::Okay>(noteOff, 500);
            if (sendResult.isError()) {
                LOG_WARN(State, "Synth audio NoteOff failed: {}", sendResult.errorValue());
            }
            else if (sendResult.value().isError()) {
                LOG_WARN(
                    State,
                    "Synth audio NoteOff rejected: {}",
                    sendResult.value().errorValue().message);
            }
        }

        keyNoteIds_[uniqueKeyIndex] = 0;
        touchKeyPressed_[uniqueKeyIndex] = false;
        evdevTouchKeyPressed_[uniqueKeyIndex] = false;

        if (lastKeyIndex_ == commandKeyIndex && lastKeyIsBlack_ == isBlack) {
            lastKeyIndex_ = -1;
            lastKeyIsBlack_ = false;
        }
    }
}

void SynthKeyboard::syncEvdevTouchState()
{
#if !LV_USE_EVDEV
    return;
#else
    if (!keyboardContainer_ || !lv_obj_is_valid(keyboardContainer_)) {
        return;
    }

    std::array<bool, kTotalKeyCount> touchedNow{};
    const lv_display_t* display = lv_obj_get_display(keyboardContainer_);

    for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
            continue;
        }
        if (display && lv_indev_get_display(indev) != display) {
            continue;
        }
        if (!lv_evdev_is_indev(indev)) {
            continue;
        }

        std::array<lv_evdev_touch_point_t, 8> touchPoints{};
        const uint8_t touchCount =
            lv_evdev_get_active_touches(indev, touchPoints.data(), touchPoints.size());
        for (size_t i = 0; i < touchCount; ++i) {
            lv_obj_t* key = nullptr;
            int keyIndex = -1;
            bool isBlack = false;
            int octaveIndex = -1;
            if (!findKeyAtPoint(touchPoints[i].point, key, keyIndex, isBlack, octaveIndex)) {
                continue;
            }

            const int uniqueKeyIndex = getUniqueKeyIndex(keyIndex, isBlack, octaveIndex);
            if (uniqueKeyIndex < 0 || static_cast<size_t>(uniqueKeyIndex) >= touchedNow.size()) {
                continue;
            }
            touchedNow[static_cast<size_t>(uniqueKeyIndex)] = true;
        }
    }

    for (size_t uniqueKeyIndex = 0; uniqueKeyIndex < evdevTouchKeyPressed_.size();
         ++uniqueKeyIndex) {
        const bool wasPressedByEvdev = evdevTouchKeyPressed_[uniqueKeyIndex];
        const bool isPressedByEvdev = touchedNow[uniqueKeyIndex];
        if (wasPressedByEvdev == isPressedByEvdev) {
            continue;
        }

        int keyIndex = -1;
        bool isBlack = false;
        int octaveIndex = -1;
        if (!decodeUniqueKeyIndex(uniqueKeyIndex, keyIndex, isBlack, octaveIndex)) {
            evdevTouchKeyPressed_[uniqueKeyIndex] = isPressedByEvdev;
            continue;
        }

        lv_obj_t* key = isBlack
            ? octaves_[static_cast<size_t>(octaveIndex)].blackKeys[static_cast<size_t>(keyIndex)]
            : octaves_[static_cast<size_t>(octaveIndex)].whiteKeys[static_cast<size_t>(keyIndex)];
        if (!key || !lv_obj_is_valid(key)) {
            evdevTouchKeyPressed_[uniqueKeyIndex] = isPressedByEvdev;
            continue;
        }

        if (isPressedByEvdev) {
            if (!isTouchKeyPressed(keyIndex, isBlack, octaveIndex)) {
                pressKey(key, keyIndex, isBlack, octaveIndex, "touch-evdev");
                setTouchKeyPressed(keyIndex, isBlack, octaveIndex, true);
            }
        }
        else {
            if (isTouchKeyPressed(keyIndex, isBlack, octaveIndex)) {
                releaseKey(key, keyIndex, isBlack, octaveIndex, "touch-evdev");
                setTouchKeyPressed(keyIndex, isBlack, octaveIndex, false);
            }
        }

        evdevTouchKeyPressed_[uniqueKeyIndex] = isPressedByEvdev;
    }
#endif
}

void SynthKeyboard::layoutKeyboard()
{
    if (!keyboardContainer_) {
        return;
    }

    const std::array<int, kBlackKeysPerOctave> centersMultiplier = { 1, 2, 4, 5, 6 };

    for (auto& octave : octaves_) {
        if (!octave.container) {
            continue;
        }

        const int width = lv_obj_get_width(octave.container);
        const int height = lv_obj_get_height(octave.container);
        if (width <= 0 || height <= 0) {
            continue;
        }

        const int whiteKeyWidth = width / static_cast<int>(kWhiteKeysPerOctave);
        const int blackKeyWidth = static_cast<int>(whiteKeyWidth * kBlackKeyWidthRatio);
        const int blackKeyHeight = static_cast<int>(height * kBlackKeyHeightRatio);

        for (size_t i = 0; i < octave.blackKeys.size(); i++) {
            lv_obj_t* key = octave.blackKeys[i];
            if (!key) {
                continue;
            }
            int x = (whiteKeyWidth * centersMultiplier[i]) - (blackKeyWidth / 2);
            x = std::clamp(x, 0, std::max(0, width - blackKeyWidth));
            lv_obj_set_size(key, blackKeyWidth, blackKeyHeight);
            lv_obj_set_pos(key, x, 0);
            lv_obj_move_foreground(key);
        }
    }
}

bool SynthKeyboard::ensureAudioConnected()
{
    if (!audioClient_) {
        audioClient_.reset(new DirtSim::Network::WebSocketService());
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
