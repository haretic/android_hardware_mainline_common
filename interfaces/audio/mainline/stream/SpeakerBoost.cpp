/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "stream/SpeakerBoost.h"

#include <algorithm>
#include <cmath>
#include <limits>

#ifdef __ANDROID__
#include <android-base/properties.h>

#include "Properties.h"
#endif

namespace aidl::android::hardware::audio::core::mainline {

namespace {

// The limiter keeps the peaks 1 dB under full scale.
constexpr float kCeiling = 0.891f;
constexpr float kLookaheadMs = 2.0f;
constexpr float kReleaseMs = 120.0f;
// A change of the gain is spread over this long.
constexpr float kGainRampMs = 50.0f;
constexpr unsigned int kPollMs = 500;

#ifdef __ANDROID__
std::string Key(const char* name) {
    return std::string(Properties::kPrefix) + name;
}

const std::string& GainProperty() {
    static const std::string name =
            ::android::base::GetProperty(Key("speaker_boost.property"), "");
    return name;
}

int GainFromProperty() {
    return ::android::base::GetIntProperty(GainProperty(), 0, 0, 100);
}
#endif

}  // namespace

#ifdef __ANDROID__
SpeakerBoost::SpeakerBoost()
    : SpeakerBoost(GainProperty().empty() ? nullptr : &GainFromProperty,
                   ::android::base::GetIntProperty(Key("speaker_boost.max_db"), 6, 0, 24)) {}
#else
SpeakerBoost::SpeakerBoost() = default;
#endif

SpeakerBoost::SpeakerBoost(GainSource source, int max_db) : source_(source), max_db_(max_db) {}

void SpeakerBoost::Reset() {
    std::fill(line_.begin(), line_.end(), 0.0f);
    line_pos_ = 0;
    line_fill_ = 0;
    envelope_ = 1.0f;
    applied_ = 1.0f;
    gain_ = target_gain_;
}

void SpeakerBoost::Configure(unsigned int channels, unsigned int rate) {
    channels_ = channels;
    rate_ = rate;
    lookahead_ = std::max<size_t>(1, static_cast<size_t>(rate * kLookaheadMs / 1000.0f));
    line_.assign(lookahead_ * channels, 0.0f);
    // The attenuation in use is within 2 % of the wanted one when the peak
    // that asked for it leaves the look-ahead line.
    attack_ = 1.0f - std::exp(-4.0f / static_cast<float>(lookahead_));
    release_ = 1.0f - std::exp(-1.0f / (rate * kReleaseMs / 1000.0f));
    gain_step_ = 1.0f / (rate * kGainRampMs / 1000.0f);
    frames_to_poll_ = 0;
    Reset();
}

template <typename T>
void SpeakerBoost::Run(T* data, size_t frames, float full_scale) {
    const float inverse = 1.0f / full_scale;
    const bool integer = std::numeric_limits<T>::is_integer;
    for (size_t f = 0; f < frames; ++f) {
        if (gain_ != target_gain_) {
            // In dB-like steps: a fixed share of the gain a frame.
            const float step = std::max(gain_, 1.0f) * gain_step_ * 2.0f;
            gain_ = gain_ < target_gain_ ? std::min(gain_ + step, target_gain_)
                                         : std::max(gain_ - step, target_gain_);
        }
        T* frame = data + f * channels_;
        float* held = line_.data() + line_pos_ * channels_;
        float peak = 0.0f;
        for (unsigned int c = 0; c < channels_; ++c) {
            const float in = static_cast<float>(frame[c]) * inverse * gain_;
            peak = std::max(peak, std::fabs(in));
            // The frame that entered lookahead_ frames ago leaves now.
            const float out = held[c];
            held[c] = in;
            scratch_[c] = out;
        }
        const float wanted = peak > kCeiling ? kCeiling / peak : 1.0f;
        // Falls at once, recovers slowly; the channels share it, so the
        // stereo image stays where it is.
        envelope_ = std::min(wanted, envelope_ + (1.0f - envelope_) * release_);
        applied_ += (envelope_ - applied_) * (envelope_ < applied_ ? attack_ : release_);
        for (unsigned int c = 0; c < channels_; ++c) {
            const float out = std::clamp(scratch_[c] * applied_, -1.0f, 1.0f);
            if (integer) {
                const double scaled = static_cast<double>(out) * full_scale;
                frame[c] = static_cast<T>(std::clamp<double>(
                        std::lrint(scaled), std::numeric_limits<T>::min(),
                        std::numeric_limits<T>::max()));
            } else {
                frame[c] = static_cast<T>(out);
            }
        }
        line_pos_ = (line_pos_ + 1) % lookahead_;
        if (peak > 0.0f) {
            line_fill_ = lookahead_;
        } else if (line_fill_ > 0) {
            --line_fill_;
        }
    }
}

void SpeakerBoost::Process(void* buffer, size_t frames, unsigned int channels,
                           snd_pcm_format_t format, unsigned int rate) {
    if (source_ == nullptr || buffer == nullptr || frames == 0 || channels == 0 || rate == 0) {
        return;
    }
    if (format != SND_PCM_FORMAT_S16_LE && format != SND_PCM_FORMAT_S32_LE &&
        format != SND_PCM_FORMAT_FLOAT_LE) {
        return;
    }
    if (frames_to_poll_ <= frames) {
        const int db = std::clamp(source_(), 0, max_db_);
        target_gain_ = std::pow(10.0f, static_cast<float>(db) / 20.0f);
        frames_to_poll_ = static_cast<size_t>(rate) * kPollMs / 1000;
    } else {
        frames_to_poll_ -= frames;
    }
    const bool idle = target_gain_ == 1.0f && gain_ == 1.0f && line_fill_ == 0 &&
                      applied_ > 0.999f;
    if (idle) {
        // Nothing to add and nothing held back: the stream passes as it is,
        // without the look-ahead's delay.
        if (channels_ != 0 && envelope_ != 1.0f) Reset();
        return;
    }
    if (channels != channels_ || rate != rate_) Configure(channels, rate);
    scratch_.resize(channels_);
    switch (format) {
        case SND_PCM_FORMAT_S16_LE:
            Run(static_cast<int16_t*>(buffer), frames, 32768.0f);
            break;
        case SND_PCM_FORMAT_S32_LE:
            Run(static_cast<int32_t*>(buffer), frames, 2147483648.0f);
            break;
        default:
            Run(static_cast<float*>(buffer), frames, 1.0f);
            break;
    }
}

}  // namespace aidl::android::hardware::audio::core::mainline
