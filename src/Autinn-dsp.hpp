#pragma once
#include <dsp/fft.hpp>
#include <dsp/window.hpp>
#include <rack.hpp>
#include <cmath>

using namespace rack;
using namespace rack::dsp;

/*

    Autinn VCV Rack Plugin
    Copyright (C) 2026  Nikolai V. Chr.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

**/

/**
 * PolyBLEP: Polynomial Band-Limited Step
 * Smooths the sharp discontinuity of sawtooth-ish osc. to remove aliasing of the sharp drop.
 *
 * Much faster than minBLEP, and kinda decent.
 *
 * @param t phase (0..1)
 * @param dt phase increment per sample
 * @return
 */
inline float polyBLEP(float t, const float dt) {
    if (t < dt) {
        // 0 < t < dt: Beginning of cycle (the rise)
        t /= dt;
        return t+t - t*t - 1.0f;
    }
	if (t > 1.0f - dt) {
        // 1 - dt < t < 1: End of cycle (the drop)
        t = (t - 1.0f) / dt;
        return t*t + t+t + 1.0f;
    }
    // Middle of cycle: No correction needed
    return 0.0f;
}

/** 1st order all-pass filter for dispersion */
struct AllPassFilter {
private:
    float x1 = 0.0f; // Previous input
    float y1 = 0.0f; // Previous output
    float c = 0.0f;  // Coefficient (tension)
public:

    /**
     * Set dispersion coefficient
     *
     * tension = 0.0: No dispersion. The signal is just delayed by 1 sample.
     * tension > 0.0: Low frequencies are delayed more than high frequencies.
     * tension < 0.0: High frequencies are delayed more than low frequencies.
     */
    void setTension(const float tension) {
        // For springs, 0.1 to 0.8 is the sweet spot.
        c = clamp(tension, -0.999f, 0.999f);
    }

    void reset() {
        x1 = 0.0f;
        y1 = 0.0f;
    }

    float process(const float x) {
        // y[n] = -c * x[n] + x[n-1] - c * y[n-1]
        float y = x1 + c * (y1 - x);
        // Denormal protection
        if (std::abs(y) < 1e-15f) y = 0.f;

        x1 = x;
        y1 = y;
        return y;
    }
};

/**
 * 1-pole HP filter, 6db/oct (20db/decade)
 * Should not be used for greater than 200 hz cutoff,
 * it will distort the cutoff, and at even higher it will become
 * unstable.
 */
struct DCBlocker {
private:
    float x_1 = 0.f;
    float y_1 = 0.f;
    float R = 0.999f;
public:
	float cutoff_hz = 7.0f;// call setSampleTime() after modifying this

	void setSampleTime(const float sampleTime) {
		// call only when sample rate changes
		R = float(1.0f - (cutoff_hz * 2.0f * M_PI * sampleTime));
	}

	float process(const float x) {
		// 1-pole HP: y[n] = x[n] - x[n-1] + R * y[n-1]
		const float y = x - x_1 + R * y_1;
		x_1 = x;
		y_1 = y;
		return y;
	}

	void reset() {
		x_1 = 0.0f;
		y_1 = 0.0f;
	}
};