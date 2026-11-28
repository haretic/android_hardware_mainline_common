/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <aidl/android/hardware/audio/core/BnTelephony.h>

#include "telephony/CallState.h"

namespace aidl::android::hardware::audio::core::mainline::telephony {

// ITelephony of a module with a modem's voice: the audio modes and the voice
// volume, which goes to the modem through CallState.
class TelephonyMainline : public BnTelephony {
  public:
    explicit TelephonyMainline(std::shared_ptr<CallState> call);

  private:
    ndk::ScopedAStatus getSupportedAudioModes(
            std::vector<::aidl::android::media::audio::common::AudioMode>* _aidl_return) override;
    ndk::ScopedAStatus switchAudioMode(
            ::aidl::android::media::audio::common::AudioMode in_mode) override;
    ndk::ScopedAStatus setTelecomConfig(const TelecomConfig& in_config,
                                        TelecomConfig* _aidl_return) override;

    const std::shared_ptr<CallState> call_;
    std::mutex lock_;
    TelecomConfig config_;
};

}  // namespace aidl::android::hardware::audio::core::mainline::telephony
