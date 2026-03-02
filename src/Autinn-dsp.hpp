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

/** 1st order all-pass filter for dispersion */
struct AllPassFilter {
private:
    // 8192 is plenty for spring reverb scatters (typically 1ms - 15ms)
    static constexpr int MAX_BUFFER = 8192;
    float x_hist[MAX_BUFFER] = {};
    float y_hist[MAX_BUFFER] = {};
    int writeHead = 0;
    float currentLength = 1.0f;
    float c = 0.0f; // Coefficient (tension)
public:

    /**
     * Set the delay length in seconds.
     * Pass 0.0f to lock it to 1-sample (Legacy Delay Mode).
     */
    void setDelayTime(const float delaySeconds, const float sampleRate) {
        if (delaySeconds <= 0.0f) {
            currentLength = 1.f; // Legacy Mode
        } else {
            currentLength = clamp((delaySeconds * sampleRate), 1.f, MAX_BUFFER - 2.f);
        }
    }

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
        for(int i = 0; i < MAX_BUFFER; i++) {
            x_hist[i] = 0.0f;
            y_hist[i] = 0.0f;
        }
        writeHead = 0;
    }

    static float readSmooth(const float* buffer, float readPos) {
        if (readPos < 0.0f) readPos += MAX_BUFFER;
        if (readPos >= MAX_BUFFER) readPos -= MAX_BUFFER;

        const int indexA = (int)readPos;
        const float frac = readPos - (float)indexA;
        const int indexB = (indexA + 1) & (MAX_BUFFER - 1);

        return buffer[indexA] + frac * (buffer[indexB] - buffer[indexA]);
    }

    float process(const float x) {
        // Calculate read head position
        float readPos = (float)writeHead - currentLength;

        float x_delayed = readSmooth(x_hist, readPos);
        float y_delayed = readSmooth(y_hist, readPos);

        // y[n] = -c * x[n] + x[n-1] - c * y[n-1]
        float y = x_delayed + c * (y_delayed - x);

        // Denormal protection
        y += 1e-18f;

        // Write to buffers
        x_hist[writeHead] = x;
        y_hist[writeHead] = y;

        writeHead++;
        if (writeHead >= MAX_BUFFER) writeHead = 0;

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
    // must use doubles as R can get very close to 1.0, so when doing (1.0f-R) it can give zero when it should not.
    double y_1 = 0.0;
    double R = 0.999;
    const double PI2 = 2.0 * M_PI;
public:
    float cutoff_hz = 7.0f;// call setSampleTime() after modifying this

    void setSampleTime(const float sampleTime) {
        // call only when sample rate changes
        const double rc = 1.0 / (PI2 * cutoff_hz);
        R = rc / (rc + sampleTime);
    }

    float process(const float x) {
        // 1-pole HP: y[n] = x[n] - x[n-1] + R * y[n-1]    old
        // 1-pole HP: y[n] = R * (x[n] - x[n-1] + y[n-1])  current
        double y = (x * (1.0 - R)) + R * y_1 + 1e-18;
        if (!std::isfinite(y)) y = 0.0;
        y_1 = y;
        return float(x - y);
    }

    void reset() {
        y_1 = 0.0;
    }
};

/**
 * 2-pole Butterworth Low-Pass Filter (Biquad)
 * 12dB/octave slope.
 */
struct RoofFilter {
private:
    float x1 = 0.f, x2 = 0.f;
    float y1 = 0.f, y2 = 0.f;
    float b0 = 0.f, b1 = 0.f, b2 = 0.f;
    float a1 = 0.f, a2 = 0.f;
    const float PI = float(M_PI);
public:
    /** Call setSampleTime() after modifying this.
     *  Do not set it lower than 1000 Hz without upgrading filter to use doubles.
     */
    float cutoff_hz = 20000.0f;

    void setSampleTime(const float sampleTime) {
        const float fs = 1.0f / sampleTime;
        const float safeCutoff = std::min(cutoff_hz, fs * 0.49f);
        const float w0 = 2.0f * PI * safeCutoff / fs;
        const float cos_w0 = std::cos(w0);

        // Q = 1/sqrt(2) (0.707) for a Butterworth response
        const float alpha = std::sin(w0) / (2.0f * 0.70710678f);

        const float a0 = 1.0f + alpha;
        b0 = ((1.0f - cos_w0) / 2.0f) / a0;
        b1 = (1.0f - cos_w0) / a0;
        b2 = ((1.0f - cos_w0) / 2.0f) / a0;
        a1 = (-2.0f * cos_w0) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    float process(const float x) {
        // Direct Form I Biquad Equation
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2 + 1e-18f;
        if (!std::isfinite(y)) y = 0.0f;

        // Shift the delay lines
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;

        return y;
    }

    void reset() {
        x1 = x2 = y1 = y2 = 0.0f;
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

/**
 * A fast tanh saturator that produce less aliasing than a regular tanh
 * Alternative to oversampling.
 */
struct ADAATanhLow {
private:
    float lastX = 0.0f;

    // antiderivative of tanh_fast_low
    static float antiderivative(float x) {
        float absX = std::abs(x);

        if (absX >= 3.0f) {
            // Clamped region integral: F(3) + 1.0 * (absX - 3)
            return absX + 0.813208866f;
        }

        // Padé region integral
        float x2 = absX * absX;
        return (1.0f / 18.0f) * x2 + (4.0f / 3.0f) * std::log(x2 + 3.0f);
    }

public:
    float process(float x) {
        float out;
        float diff = x - lastX;

        if (std::abs(diff) < 1e-4f) {
            // Fallback for when delta is too small to avoid division by zero
            out = tanh_fast_low((x + lastX) * 0.5f);
        } else {
            out = (antiderivative(x) - antiderivative(lastX)) / diff;
        }

        lastX = x;
        return out;
    }

    void reset() {
        lastX = 0.0f;
    }
};

struct ADAATanhMid {
private:
    float lastX = 0.0f;
    // Constants derived from the partial fraction decomposition of the [5/4] polynomial
    float c1 = 1.0f / 30.0f;
    float c2 = 77.0f / 60.0f;
    float c3 = 49.f/(15.f*std::sqrt(133.f));
    float root1 = 14.f-std::sqrt(133.f);
    float root2 = 14.f+std::sqrt(133.f);

    // Antiderivative of the [5/4] Padé tanh_fast_mid
    float antiderivative(const float x) const {
        const float absX = std::abs(x);

        if (absX >= 3.6447f) {
            // Clamped region integral: F(3.6447) + 1.0 * (absX - 3.6447)
            // F(3.6447) ~ 8.931825. 8.931825 - 3.6447 = 5.2871254
            return absX + 5.28713327121f;
        }

        // Padé region integral
        const float x2 = absX * absX;
        const float x4 = x2 * x2;

        return c1 * x2
             + c2 * std::log(x4 + 28.0f * x2 + 63.0f)
             - c3 * std::log((x2 + root1) / (x2 + root2));
    }

public:
    float process(const float x) {
        float out;
        const float diff = x - lastX;

        if (std::abs(diff) < 1e-4f) {
            // Fallback for when delta is too small to avoid division by zero
            out = tanh_fast_mid((x + lastX) * 0.5f);
        } else {
            out = (antiderivative(x) - antiderivative(lastX)) / diff;
        }

        lastX = x;
        return out;
    }

    void reset() {
        lastX = 0.0f;
    }
};

struct ADAATanhMidLUT {
private:
    static constexpr int LUT_SIZE = 4096;
    static constexpr double LUT_MAX = 3.6447;
    static constexpr double LUT_STEP = LUT_SIZE / LUT_MAX;

    // Static arrays to share the LUT across all instances
    static double lut[LUT_SIZE + 1];
    static bool initialized;

    double c1 = 1.0 / 30.0;
    double c2 = 77.0 / 60.0;
    double c3 = 49.0 / (15.0 * std::sqrt(133.0));
    double root1 = 14.0 - std::sqrt(133.0);
    double root2 = 14.0 + std::sqrt(133.0);

    double lastX = 0.0;

    void initLUT() const {
        if (initialized) return;
        for (int i = 0; i <= LUT_SIZE; ++i) {
            const double absX = i / LUT_STEP;
            const double x2 = absX * absX;
            const double x4 = x2 * x2;

            lut[i] = c1 * x2
                   + c2 * std::log(x4 + 28.0 * x2 + 63.0)
                   - c3 * std::log((x2 + root1) / (x2 + root2));
        }
        initialized = true;
    }

    static double antiderivative(const double x) {
        const double absX = std::abs(x);

        // Clamped region
        if (absX >= LUT_MAX) {
            return absX + 5.28713327121;
        }

        // Fast linear interpolation
        const double floatIndex = absX * LUT_STEP;
        const int index = (int)floatIndex;
        const double frac = floatIndex - index;

        return lut[index] + frac * (lut[index + 1] - lut[index]);
    }

public:
    ADAATanhMidLUT() {
        initLUT();
    }

    float process(const float x_in) {
        const auto x = static_cast<double>(x_in);
        double out;
        const double diff = x - lastX;

        // Tighter threshold for double precision
        if (std::abs(diff) < 1e-7) {
            out = tanh_fast_mid(static_cast<float>((x + lastX) * 0.5));
        } else {
            out = (antiderivative(x) - antiderivative(lastX)) / diff;
        }

        lastX = x;
        return static_cast<float>(out);
    }

    void reset() {
        lastX = 0.0;
    }
};

// When using it, add these lines to the module file:
//double ADAATanhMidLUT::lut[ADAATanhMidLUT::LUT_SIZE + 1];
//bool ADAATanhMidLUT::initialized = false;