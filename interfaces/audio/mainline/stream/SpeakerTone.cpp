/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "stream/SpeakerTone.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace aidl::android::hardware::audio::core::mainline {

namespace {

std::vector<std::string> Split(const std::string& text, char at) {
    std::vector<std::string> parts;
    size_t from = 0;
    while (true) {
        const size_t to = text.find(at, from);
        parts.push_back(text.substr(from, to == std::string::npos ? to : to - from));
        if (to == std::string::npos) return parts;
        from = to + 1;
    }
}

bool Number(const std::string& text, float low, float high, float* out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (*end != '\0' || !std::isfinite(value) || value < low || value > high) return false;
    *out = value;
    return true;
}

}  // namespace

bool SpeakerTone::Describe(const std::string& line) {
    std::vector<Shape> shapes;
    float gain_db = 0.0f;
    bool ok = !line.empty();
    for (const std::string& word : Split(line, ',')) {
        if (!ok) break;
        const std::vector<std::string> p = Split(word, ':');
        Shape shape{Shape::kPeak, 0.0f, 0.0f, 0.7071f};
        if (p[0] == "gain" && p.size() == 2) {
            ok = Number(p[1], -24.0f, 24.0f, &gain_db);
            continue;
        } else if (p[0] == "hp" && p.size() == 3) {
            shape.kind = Shape::kHighPass;
            ok = Number(p[1], 20.0f, 20000.0f, &shape.hz) && Number(p[2], 0.3f, 4.0f, &shape.q);
        } else if (p[0] == "peak" && p.size() == 4) {
            ok = Number(p[1], 20.0f, 20000.0f, &shape.hz) &&
                 Number(p[2], -24.0f, 12.0f, &shape.db) && Number(p[3], 0.2f, 12.0f, &shape.q);
        } else if ((p[0] == "lowshelf" || p[0] == "highshelf") && p.size() == 3) {
            shape.kind = p[0] == "lowshelf" ? Shape::kLowShelf : Shape::kHighShelf;
            ok = Number(p[1], 20.0f, 20000.0f, &shape.hz) &&
                 Number(p[2], -24.0f, 12.0f, &shape.db);
        } else {
            ok = false;
        }
        if (ok) shapes.push_back(shape);
    }
    // More stages than a speaker needs is a mistake in the line.
    if (shapes.size() > 12) ok = false;
    if (!ok) {
        shapes.clear();
        gain_db = 0.0f;
    }
    shapes_ = shapes;
    gain_ = std::pow(10.0f, gain_db / 20.0f);
    stages_.clear();
    rate_ = 0;
    return ok;
}

void SpeakerTone::Configure(unsigned int channels, unsigned int rate) {
    channels_ = channels;
    rate_ = rate;
    stages_.clear();
    for (const Shape& shape : shapes_) {
        // A filter at or past half the rate has nothing to act on.
        if (shape.hz >= rate * 0.45f) continue;
        // Audio EQ Cookbook, R. Bristow-Johnson.
        const double w = 2.0 * M_PI * shape.hz / rate;
        const double cs = std::cos(w), sn = std::sin(w);
        const double a = std::pow(10.0, shape.db / 40.0);
        const double alpha = sn / (2.0 * shape.q);
        double b0, b1, b2, a0, a1, a2;
        switch (shape.kind) {
            case Shape::kHighPass:
                b0 = (1 + cs) / 2, b1 = -(1 + cs), b2 = (1 + cs) / 2;
                a0 = 1 + alpha, a1 = -2 * cs, a2 = 1 - alpha;
                break;
            case Shape::kPeak:
                b0 = 1 + alpha * a, b1 = -2 * cs, b2 = 1 - alpha * a;
                a0 = 1 + alpha / a, a1 = -2 * cs, a2 = 1 - alpha / a;
                break;
            case Shape::kLowShelf: {
                const double k = 2 * std::sqrt(a) * alpha;
                b0 = a * ((a + 1) - (a - 1) * cs + k), b1 = 2 * a * ((a - 1) - (a + 1) * cs);
                b2 = a * ((a + 1) - (a - 1) * cs - k);
                a0 = (a + 1) + (a - 1) * cs + k, a1 = -2 * ((a - 1) + (a + 1) * cs);
                a2 = (a + 1) + (a - 1) * cs - k;
                break;
            }
            default: {
                const double k = 2 * std::sqrt(a) * alpha;
                b0 = a * ((a + 1) + (a - 1) * cs + k), b1 = -2 * a * ((a - 1) + (a + 1) * cs);
                b2 = a * ((a + 1) + (a - 1) * cs - k);
                a0 = (a + 1) - (a - 1) * cs + k, a1 = 2 * ((a - 1) - (a + 1) * cs);
                a2 = (a + 1) - (a - 1) * cs - k;
                break;
            }
        }
        stages_.push_back({static_cast<float>(b0 / a0), static_cast<float>(b1 / a0),
                           static_cast<float>(b2 / a0), static_cast<float>(a1 / a0),
                           static_cast<float>(a2 / a0)});
    }
    state_.assign(static_cast<size_t>(channels) * stages_.size() * 2, 0.0f);
}

void SpeakerTone::Reset() {
    std::fill(state_.begin(), state_.end(), 0.0f);
}

}  // namespace aidl::android::hardware::audio::core::mainline
