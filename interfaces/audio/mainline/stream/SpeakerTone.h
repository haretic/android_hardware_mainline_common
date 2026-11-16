/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace aidl::android::hardware::audio::core::mainline {

// A fixed correction for a built-in speaker: a short chain of second-order
// filters, described by a device in one line of text.
//
//   hp:<Hz>:<Q>          high-pass, for what the speaker cannot move anyway
//   peak:<Hz>:<dB>:<Q>   a bell up or down, for a resonance or a hollow
//   lowshelf:<Hz>:<dB>   highshelf:<Hz>:<dB>
//   gain:<dB>            what the cuts gave back, to spend on level
//
// separated by commas, as in "hp:300:0.7,peak:1100:-6:1.8,gain:3". A line
// that does not parse gives a chain that changes nothing. The chain may push
// samples past full scale; a limiter belongs behind it.
class SpeakerTone {
  public:
    // False if the line is empty or wrong; the chain then has no stages.
    bool Describe(const std::string& line);
    bool empty() const { return stages_.empty() && gain_ == 1.0f; }

    void Configure(unsigned int channels, unsigned int rate);
    // Forgets what the filters remember of earlier samples.
    void Reset();
    // One sample of one channel, in the range the caller uses.
    float Run(unsigned int channel, float in) {
        float* state = state_.data() + channel * stages_.size() * 2;
        float x = in;
        for (const Stage& s : stages_) {
            // Transposed direct form II.
            const float y = s.b0 * x + state[0];
            state[0] = s.b1 * x - s.a1 * y + state[1];
            state[1] = s.b2 * x - s.a2 * y;
            x = y;
            state += 2;
        }
        return x * gain_;
    }

  private:
    struct Shape {
        enum Kind { kHighPass, kPeak, kLowShelf, kHighShelf } kind;
        float hz;
        float db;
        float q;
    };
    struct Stage {
        float b0, b1, b2, a1, a2;
    };

    std::vector<Shape> shapes_;
    std::vector<Stage> stages_;
    std::vector<float> state_;
    float gain_ = 1.0f;
    unsigned int channels_ = 0;
    unsigned int rate_ = 0;
};

}  // namespace aidl::android::hardware::audio::core::mainline
