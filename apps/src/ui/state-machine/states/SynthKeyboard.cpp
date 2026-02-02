#include "SynthKeyboard.h"
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
constexpr size_t kWhiteKeysPerOctave = 7;
constexpr size_t kBlackKeysPerOctave = 5;
constexpr int kAudioConnectTimeoutMs = 200;
constexpr double kKeyAmplitude = 0.2;
constexpr double kKeyAttackMs = 5.0;
constexpr double kKeyReleaseMs = 90.0;
constexpr double kKeyDurationMs = 140.0;
constexpr std::array<double, kWhiteKeysPerOctave> kWhiteKeyFrequencies = {
    261.63, // C4.
    293.66, // D4.
    329.63, // E4.
    349.23, // F4.
    392.00, // G4.
    440.00, // A4.
    493.88, // B4.
};
constexpr std::array<double, kBlackKeysPerOctave> kBlackKeyFrequencies = {
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

    lastKey_ = nullptr;
    lastKeyIndex_ = -1;
    lastKeyIsBlack_ = false;
    audioWarningLogged_ = false;
}

void SynthKeyboard::destroy()
{
    resetLastKeyVisual();

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
    lastKey_ = nullptr;
    lastKeyIndex_ = -1;
    lastKeyIsBlack_ = false;
}

bool SynthKeyboard::handleKeyPress(
    int keyIndex, bool isBlack, const char* source, std::string& error)
{
    const size_t keysPerOctave = isBlack ? kBlackKeysPerOctave : kWhiteKeysPerOctave;
    const size_t maxKeys = keysPerOctave * octaves_.size();
    if (keyIndex < 0 || static_cast<size_t>(keyIndex) >= maxKeys) {
        error = "Invalid key index";
        return false;
    }

    const size_t octaveIndex = static_cast<size_t>(keyIndex) / keysPerOctave;
    const size_t localIndex = static_cast<size_t>(keyIndex) % keysPerOctave;
    if (octaveIndex >= octaves_.size()) {
        error = "Invalid key index";
        return false;
    }

    lv_obj_t* key = isBlack ? octaves_[octaveIndex].blackKeys[localIndex]
                            : octaves_[octaveIndex].whiteKeys[localIndex];
    if (!key || !lv_obj_is_valid(key)) {
        error = "Synth key unavailable";
        return false;
    }

    applyKeyPress(
        key, static_cast<int>(localIndex), isBlack, static_cast<int>(octaveIndex), source);
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

    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        self->clearLastKey();
        return;
    }

    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) {
        return;
    }

    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) {
        return;
    }

    lv_point_t point{};
    lv_indev_get_point(indev, &point);

    lv_obj_t* key = nullptr;
    int keyIndex = -1;
    bool isBlack = false;
    int octaveIndex = -1;
    if (!self->findKeyAtPoint(point, key, keyIndex, isBlack, octaveIndex)) {
        return;
    }

    if (key == self->lastKey_) {
        return;
    }

    self->applyKeyPress(key, keyIndex, isBlack, octaveIndex, "touch");
}

void SynthKeyboard::applyKeyPress(
    lv_obj_t* key, int keyIndex, bool isBlack, int octaveIndex, const char* source)
{
    if (!key || !lv_obj_is_valid(key)) {
        return;
    }

    if (octaveIndex < 0 || static_cast<size_t>(octaveIndex) >= kOctaveFrequencyMultipliers.size()) {
        return;
    }

    resetLastKeyVisual();

    const uint32_t pressedColor = isBlack ? kBlackKeyPressedColor : kWhiteKeyPressedColor;
    lv_obj_set_style_bg_color(key, lv_color_hex(pressedColor), 0);

    lastKey_ = key;
    lastKeyIndex_ = getGlobalKeyIndex(keyIndex, isBlack, octaveIndex);
    lastKeyIsBlack_ = isBlack;

    const size_t localIndex = static_cast<size_t>(keyIndex);
    const double baseFrequency =
        isBlack ? kBlackKeyFrequencies[localIndex] : kWhiteKeyFrequencies[localIndex];
    const double frequency =
        baseFrequency * kOctaveFrequencyMultipliers[static_cast<size_t>(octaveIndex)];
    LOG_INFO(
        State,
        "Synth key pressed (index={}, black={}, freq={:.2f}Hz, source={})",
        lastKeyIndex_,
        isBlack,
        frequency,
        source);

    if (ensureAudioConnected()) {
        AudioApi::NoteOn::Command note{};
        note.frequency_hz = frequency;
        note.amplitude = kKeyAmplitude * (static_cast<double>(volumePercent_) / 100.0);
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

int SynthKeyboard::getGlobalKeyIndex(int keyIndex, bool isBlack, int octaveIndex) const
{
    const int keysPerOctave =
        isBlack ? static_cast<int>(kBlackKeysPerOctave) : static_cast<int>(kWhiteKeysPerOctave);
    return (octaveIndex * keysPerOctave) + keyIndex;
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

void SynthKeyboard::resetLastKeyVisual()
{
    if (!lastKey_ || !lv_obj_is_valid(lastKey_)) {
        return;
    }

    const uint32_t resetColor = lastKeyIsBlack_ ? kBlackKeyColor : kWhiteKeyColor;
    lv_obj_set_style_bg_color(lastKey_, lv_color_hex(resetColor), 0);
}

void SynthKeyboard::clearLastKey()
{
    resetLastKeyVisual();
    lastKey_ = nullptr;
    lastKeyIndex_ = -1;
    lastKeyIsBlack_ = false;
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
