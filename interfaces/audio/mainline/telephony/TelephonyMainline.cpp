/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "MainlineAudio_Telephony"

#include "telephony/TelephonyMainline.h"

#include <algorithm>

#include <Utils.h>
#include <android-base/logging.h>

namespace aidl::android::hardware::audio::core::mainline::telephony {

using ::aidl::android::hardware::audio::common::isValidAudioMode;
using ::aidl::android::media::audio::common::AudioMode;
using ::aidl::android::media::audio::common::Boolean;
using ::aidl::android::media::audio::common::Float;

namespace {

const std::vector<AudioMode> kModes = {
        AudioMode::NORMAL,
        AudioMode::RINGTONE,
        AudioMode::IN_CALL,
        AudioMode::IN_COMMUNICATION,
        AudioMode::CALL_SCREEN,
};

}  // namespace

TelephonyMainline::TelephonyMainline(std::shared_ptr<CallState> call) : call_(std::move(call)) {
    config_.voiceVolume = Float{TelecomConfig::VOICE_VOLUME_MAX};
    config_.ttyMode = TelecomConfig::TtyMode::OFF;
    config_.isHacEnabled = Boolean{false};
}

ndk::ScopedAStatus TelephonyMainline::getSupportedAudioModes(std::vector<AudioMode>* _aidl_return) {
    *_aidl_return = kModes;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TelephonyMainline::switchAudioMode(AudioMode in_mode) {
    if (!isValidAudioMode(in_mode)) {
        LOG(ERROR) << __func__ << ": invalid mode " << toString(in_mode);
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (std::find(kModes.begin(), kModes.end(), in_mode) == kModes.end()) {
        LOG(ERROR) << __func__ << ": unsupported mode " << toString(in_mode);
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    // The call's audio follows its patches, and only in a call: the policy
    // has the telephony mix ports patched all the time
    LOG(INFO) << __func__ << ": " << toString(in_mode);
    call_->SetInCall(in_mode == AudioMode::IN_CALL);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TelephonyMainline::setTelecomConfig(const TelecomConfig& in_config,
                                                       TelecomConfig* _aidl_return) {
    if (in_config.voiceVolume.has_value() &&
        (in_config.voiceVolume->value < TelecomConfig::VOICE_VOLUME_MIN ||
         in_config.voiceVolume->value > TelecomConfig::VOICE_VOLUME_MAX)) {
        LOG(ERROR) << __func__ << ": invalid voice volume " << in_config.voiceVolume->value;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    std::lock_guard guard(lock_);
    if (in_config.voiceVolume.has_value()) {
        config_.voiceVolume = in_config.voiceVolume;
        call_->SetVolume(in_config.voiceVolume->value);
    }
    if (in_config.ttyMode != TelecomConfig::TtyMode::UNSPECIFIED) {
        config_.ttyMode = in_config.ttyMode;
    }
    if (in_config.isHacEnabled.has_value()) {
        config_.isHacEnabled = in_config.isHacEnabled;
    }
    *_aidl_return = config_;
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::audio::core::mainline::telephony
