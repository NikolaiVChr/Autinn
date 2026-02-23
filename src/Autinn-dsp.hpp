#pragma once
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
 * Slightly attenuates high frequencies.
 * Should not be used for greater than 3000 hz cutoff,
 * since it will shift the actual cutoff.
 */
struct DCBlocker {
private:
    float y_1 = 0.f;
    float R = 0.999f;
    const float PI2 = float(2.0 * M_PI);
public:
    float cutoff_hz = 7.0f;// call setSampleTime() after modifying this

    void setSampleTime(const float sampleTime) {
        // call only when sample rate changes
        const float rc = 1.0f / (PI2 * cutoff_hz);
        R = rc / (rc + sampleTime);
    }

    float process(const float x) {
        // 1-pole HP: y[n] = x[n] - x[n-1] + R * y[n-1]    old
        // 1-pole HP: y[n] = R * (x[n] - x[n-1] + y[n-1])  current
        float y = (x * (1.0f - R)) + R * y_1 + 1e-18f;
        if (!std::isfinite(y)) y = 0.0f;
        y_1 = y;
        return x - y;
    }

    void reset() {
        y_1 = 0.0f;
    }
};

/**
 * When I know in advance the discontinuity is coming and when.
 * Supports multiple discontinuities per sample.
 */
struct PredictiveBLEP {
private:
    float nextCorrection = 0.0f;
    float currentCorrection = 0.0f;
public:

    /**
     * Use every sample (or oversample)
     *
     * @param naive the naive aliased signal
     * @return anti-aliased signal
     */
    float process(const float naive) {
        const float out = naive + currentCorrection;
        currentCorrection = nextCorrection;
        nextCorrection = 0.0f;
        return out;
    }

    /**
     * Insert a PolyBLEP correction for a discontinuity.
     *
     * @param fraction how far towards the next sample the discontinuity lands (0.0 [next] to 1.0 [current])
     * @param mag magnitude and direction of the discontinuity
     */
    void jump(const float fraction, const float mag) {
        currentCorrection += mag * 0.5f * fraction * fraction;
        const float fraction1 = fraction - 1.0f;
        nextCorrection -= mag * 0.5f * fraction1 * fraction1;
    }

    /**
     * Insert a PolyBLAMP correction for a slope change.
     *
     * slope = magnitude_change/phase_amount
     *
     * @param fraction how far towards the current sample the corner lands (0.0 [next] to 1.0 [current])
     * @param dt the phase increment per sample (sampleTime/phaseTime)
     * @param slopeChange the new slope minus the old slope
     */
    void corner(const float fraction, const float dt, const float slopeChange) {
        currentCorrection += slopeChange * dt * (fraction * fraction * fraction) / 6.0f;

        const float fraction1 = 1.0f - fraction;
        nextCorrection += slopeChange * dt * (fraction1 * fraction1 * fraction1) / 6.0f;
    }
};

/**
 * When the discontinuity/slope-change happened in the past.
 * The signal is delayed by 1 sample.
 */
struct ReactiveBLEP {
private:
    float buffer[2] = {0.0f, 0.0f};
    float lastNaive = 0.0f;
public:

    /**
     * Insert a PolyBLEP correction for a discontinuity.
     *
     * @param fraction how far towards the previous sample the discontinuity landed (0.0 [current] to 1.0 [prev])
     * @param mag magnitude and direction of the discontinuity
     */
    void jump(const float fraction, const float mag) {
        // Correct the sample before the jump (which will output right now)
        const float t0 = fraction;
        buffer[0] += mag * 0.5f * t0 * t0;

        // Correct the sample after the jump (which will output on the next step)
        const float t1 = fraction - 1.0f;
        buffer[1] -= mag * 0.5f * t1 * t1;
    }

    /**
     * Insert a PolyBLAMP correction for a slope change.
     *
     * slope = magnitude_change/phase_amount
     *
     * @param fraction how far towards the previous sample the slopeChange happened (0.0 [current] to 1.0 [prev])
     * @param dt The phase increment per sample (sampleTime/phaseTime)
     * @param slopeChange The new slope minus the old slope
     */
    void corner(const float fraction, const float dt, const float slopeChange) {
        // Correct the sample before the corner
        const float u0 = fraction;
        buffer[0] += slopeChange * dt * (u0 * u0 * u0) / 6.0f;

        // Correct the sample after the corner
        const float u1 = 1.0f - fraction;
        buffer[1] += slopeChange * dt * (u1 * u1 * u1) / 6.0f;
    }

    /**
     * Use every sample (or oversample)
     *
     * @param naive the naive aliased signal
     * @return anti-aliased signal
     */
    float process(const float naive) {
        const float out = lastNaive + buffer[0];
        lastNaive = naive;

        buffer[0] = buffer[1];
        buffer[1] = 0.0f;

        return out;
    }
};