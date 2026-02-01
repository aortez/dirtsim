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
constexpr int kAudioConnectTimeoutMs = 200;
constexpr double kKeyAmplitude = 0.2;
constexpr double kKeyAttackMs = 5.0;
constexpr double kKeyReleaseMs = 90.0;
constexpr double kKeyDurationMs = 140.0;
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
    lv_obj_set_size(keyboardRow_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(keyboardRow_, 1);
    lv_obj_set_style_bg_opa(keyboardRow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(keyboardRow_, kKeyboardPadding, 0);
    lv_obj_set_style_border_width(keyboardRow_, 0, 0);
    lv_obj_clear_flag(keyboardRow_, LV_OBJ_FLAG_SCROLLABLE);

    keyboardContainer_ = lv_obj_create(keyboardRow_);
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
    whiteKeysContainer_ = nullptr;
    whiteKeys_.fill(nullptr);
    blackKeys_.fill(nullptr);
    lastKey_ = nullptr;
    lastKeyIndex_ = -1;
    lastKeyIsBlack_ = false;
}

bool SynthKeyboard::handleKeyPress(
    int keyIndex, bool isBlack, const char* source, std::string& error)
{
    if (keyIndex < 0 || (isBlack && static_cast<size_t>(keyIndex) >= blackKeys_.size())
        || (!isBlack && static_cast<size_t>(keyIndex) >= whiteKeys_.size())) {
        error = "Invalid key index";
        return false;
    }

    lv_obj_t* key = isBlack ? blackKeys_[static_cast<size_t>(keyIndex)]
                            : whiteKeys_[static_cast<size_t>(keyIndex)];
    if (!key || !lv_obj_is_valid(key)) {
        error = "Synth key unavailable";
        return false;
    }

    applyKeyPress(key, keyIndex, isBlack, source);
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

void SynthKeyboard::applyKeyPress(lv_obj_t* key, int keyIndex, bool isBlack, const char* source)
{
    if (!key || !lv_obj_is_valid(key)) {
        return;
    }

    resetLastKeyVisual();

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

bool SynthKeyboard::findKeyIndex(lv_obj_t* key, int& keyIndex, bool& isBlack) const
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

void SynthKeyboard::layoutKeyboard()
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

} // namespace State
} // namespace Ui
} // namespace DirtSim
