/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <mutex>
#include <string>

namespace aidl::android::hardware::audio::core::mainline::telephony {

// What a call's audio does, published to whatever drives the modem's voice
// (the `telephony.property` property and its .volume / .mute children). A
// route is published only in the IN_CALL mode: the policy keeps the telephony
// mix ports open, and so patched to their devices, outside of calls as well.
class CallState {
  public:
    enum class Route {
        kOff,     // No call patch.
        kCodec,   // Bridged in hardware: the modem's card joins it to the device itself.
        kBridge,  // Through the telephony PCMs, AudioFlinger bridging to another device.
    };

    // `property` empty: nothing is published.
    explicit CallState(std::string property);

    void SetRoute(Route route);
    // Whether the audio mode is IN_CALL.
    void SetInCall(bool in_call);
    // 0 to 1, as ITelephony's voice volume.
    void SetVolume(float volume);
    void SetMicMute(bool mute);

    std::string Dump() const;

  private:
    void Publish(const std::string& key, const std::string& value);
    // The route as published. Call with lock_ held.
    Route Effective() const { return in_call_ ? route_ : Route::kOff; }

    const std::string property_;
    mutable std::mutex lock_;
    Route route_ = Route::kOff;
    bool in_call_ = false;
    float volume_ = 1.0f;
    bool mute_ = false;
};

const char* ToString(CallState::Route route);

}  // namespace aidl::android::hardware::audio::core::mainline::telephony
