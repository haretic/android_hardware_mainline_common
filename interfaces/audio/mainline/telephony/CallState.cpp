/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "MainlineAudio_Call"

#include "telephony/CallState.h"

#include <cstdio>

#include <android-base/logging.h>
#include <android-base/properties.h>

namespace aidl::android::hardware::audio::core::mainline::telephony {

const char* ToString(CallState::Route route) {
    switch (route) {
        case CallState::Route::kOff:
            return "off";
        case CallState::Route::kCodec:
            return "codec";
        case CallState::Route::kBridge:
            return "bridge";
    }
    return "off";
}

CallState::CallState(std::string property) : property_(std::move(property)) {
    // A HAL restart mid call: nothing of the old call is left, the policy sets it up again
    Publish("", ToString(Effective()));
}

void CallState::SetRoute(Route route) {
    std::lock_guard guard(lock_);
    if (route == route_) return;
    const Route before = Effective();
    route_ = route;
    if (Effective() == before) return;
    LOG(INFO) << __func__ << ": " << ToString(before) << " -> " << ToString(Effective());
    Publish("", ToString(Effective()));
}

void CallState::SetInCall(bool in_call) {
    std::lock_guard guard(lock_);
    if (in_call == in_call_) return;
    const Route before = Effective();
    in_call_ = in_call;
    if (Effective() == before) return;
    LOG(INFO) << __func__ << ": " << ToString(before) << " -> " << ToString(Effective());
    Publish("", ToString(Effective()));
}

void CallState::SetVolume(float volume) {
    std::lock_guard guard(lock_);
    volume_ = volume;
    char text[16];
    snprintf(text, sizeof(text), "%.3f", volume);
    Publish(".volume", text);
}

void CallState::SetMicMute(bool mute) {
    std::lock_guard guard(lock_);
    mute_ = mute;
    Publish(".mute", mute ? "1" : "0");
}

std::string CallState::Dump() const {
    std::lock_guard guard(lock_);
    char text[192];
    snprintf(text, sizeof(text),
             "Call: route %s (patched %s, %s), voice volume %.3f, mic %s, property \"%s\"\n",
             ToString(Effective()), ToString(route_), in_call_ ? "in call" : "no call", volume_,
             mute_ ? "muted" : "on", property_.c_str());
    return text;
}

void CallState::Publish(const std::string& key, const std::string& value) {
    if (property_.empty()) return;
    if (!::android::base::SetProperty(property_ + key, value)) {
        LOG(WARNING) << __func__ << ": could not set " << property_ << key << " to " << value;
    }
}

}  // namespace aidl::android::hardware::audio::core::mainline::telephony
