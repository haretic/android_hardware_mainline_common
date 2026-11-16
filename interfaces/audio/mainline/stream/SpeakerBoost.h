/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <alsa/asoundlib.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "stream/SpeakerTone.h"

namespace aidl::android::hardware::audio::core::mainline {

// Gain for the built-in speaker past what its hardware has, with a limiter.
//
// A device whose analog gains are at their top can still play quiet material
// louder: the samples are scaled up and a look-ahead limiter holds the peaks
// under full scale, so that loud material gets denser rather than clipped.
//
// The gain in dB is the value of the Android property named by
// vendor.audio.mainline.speaker_boost.property, between 0 and
// vendor.audio.mainline.speaker_boost.max_db. It is read again about twice a
// second, so a change is heard without a restart. Without the first property
// the stage does nothing and costs nothing.
//
// In front of the gain sits the speaker's tone correction (SpeakerTone), if
// the device describes one in vendor.audio.mainline.speaker_tone.filters. It
// is always on, or, if vendor.audio.mainline.speaker_tone.property names a
// property, while that one reads true; it fades in and out when that changes. The limiter then also holds what the correction raised.
class SpeakerBoost {
  public:
    // Reads the gain from the property. Negative: there is no such property.
    using GainSource = int (*)();

    SpeakerBoost();
    SpeakerBoost(GainSource source, int max_db);
    // Whether the tone correction is wanted now.
    using ToneSource = bool (*)();
    SpeakerBoost(GainSource source, int max_db, const std::string& tone, ToneSource tone_on);

    // In place, on interleaved frames. Formats other than S16_LE, S32_LE and
    // FLOAT_LE are left as they are. While the gain is 0 and nothing is left
    // in the look-ahead line the samples are not touched and not delayed.
    void Process(void* buffer, size_t frames, unsigned int channels, snd_pcm_format_t format,
                 unsigned int rate);
    // Forgets the look-ahead line (after a flush or a reopened device).
    void Reset();

  private:
    void Configure(unsigned int channels, unsigned int rate);
    template <typename T>
    void Run(T* data, size_t frames, float full_scale);

    GainSource source_ = nullptr;
    int max_db_ = 0;

    unsigned int channels_ = 0;
    unsigned int rate_ = 0;
    size_t lookahead_ = 0;      // Frames.
    std::vector<float> line_;   // lookahead_ frames, scaled, not yet limited.
    std::vector<float> scratch_;  // One frame leaving the line.
    size_t line_pos_ = 0;
    size_t line_fill_ = 0;      // Frames in the line that carry sound.
    float target_gain_ = 1.0f;  // From the property.
    float gain_ = 1.0f;         // Ramped towards target_gain_.
    float envelope_ = 1.0f;     // The limiter's wanted attenuation.
    float applied_ = 1.0f;      // The attenuation in use, smoothed.
    float gain_step_ = 0.0f;
    float attack_ = 0.0f;
    float release_ = 0.0f;
    size_t frames_to_poll_ = 0;

    SpeakerTone tone_;
    ToneSource tone_source_ = nullptr;
    float tone_target_ = 0.0f;  // 1 with the correction, 0 without.
    float tone_mix_ = 0.0f;     // Ramped towards tone_target_.
};

}  // namespace aidl::android::hardware::audio::core::mainline
