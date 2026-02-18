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
 * Generates a Minimum-Phase Bandlimited Step (minBLEP) table using Cepstral analysis.
 *
 * Uses standard algorithm with Rack's FFT and window functions:
 * Generate a windowed sinc pulse (bandlimited step derivative).
 * Calculate real Cepstrum to separate magnitude and phase.
 * Force minimum-phase causality (remove anti-causal components).
 * Reconstruct signal and integrate to get the step.
 */
static void generateCleanMinBLEP(const int zeroCrossings, const int oversample, float* outputBuffer) {

    const int n = 2 * zeroCrossings * oversample;
    constexpr float epsilon = 1e-9f;

    // std::vector for automatic cleanup to avoid memory leaks
    std::vector<float> timeDomain(n);
    std::vector<float> freqDomain(n);

    // Generate bandlimited impulse
    for (int i = 0; i < n; i++) {
        // Map index to range [-zeroCrossings, +zeroCrossings]
        const float windowPos = (float)i / (float)(n - 1);
        float x = (windowPos - 0.5f) * (2.0f * float(zeroCrossings));

        // Sinc(x) = sin(pi*x) / (pi*x)
        if (std::abs(x) < epsilon) {
            timeDomain[i] = 1.0f;
        } else {
            x *= float(M_PI);
            timeDomain[i] = std::sin(x) / x;
        }
    }

    // Blackman-Harris is standard window for this
    dsp::blackmanHarrisWindow(timeDomain.data(), n);

    dsp::RealFFT fft(n);
    fft.rfft(timeDomain.data(), freqDomain.data());

    // Layout: [DC, Nyquist, Re1, Im1, Re2, Im2, ...]

    // Handle DC and Nyquist (real)
    // Add epsilon to avoid log(0)
    freqDomain[0] = std::log(std::abs(freqDomain[0]) + epsilon);
    freqDomain[1] = std::log(std::abs(freqDomain[1]) + epsilon);

    // Handle complex bins
    for (int i = 1; i < n / 2; i++) {
        const int real = 2 * i;
        const int imaginary = 2 * i + 1;

        // this is safe because we use epsilon inside the log in case it comes back zero
        float mag = std::sqrt(freqDomain[real] * freqDomain[real] + freqDomain[imaginary] * freqDomain[imaginary]);

        // Real Cepstrum = log(|Mag|)
        freqDomain[real] = std::log(mag + epsilon);
        freqDomain[imaginary] = 0.0f; // Phase is discarded
    }

    // Get Cepstrum time-domain
    fft.irfft(freqDomain.data(), timeDomain.data());
    fft.scale(timeDomain.data());

    // Multiply causal part by 2, keep Nyquist, zero anti-causal part
    for (int i = 1; i < n / 2; i++) {
        timeDomain[i] *= 2.0f;
    }
    // timeDomain[n/2] stays * 1.0f
    for (int i = n / 2 + 1; i < n; i++) {
        timeDomain[i] = 0.0f;
    }

    fft.rfft(timeDomain.data(), freqDomain.data());

    // exp(real + j*imaginary)
    // restore the magnitude and generates the new minimum phase
    freqDomain[0] = std::exp(freqDomain[0]);
    freqDomain[1] = std::exp(freqDomain[1]);

    for (int i = 1; i < n / 2; i++) {
        const int real = 2 * i;
        const int imaginary = 2 * i + 1;

        const float modulus = std::exp(freqDomain[real]);
        const float phase   = freqDomain[imaginary];

        freqDomain[real] = modulus * std::cos(phase);
        freqDomain[imaginary] = modulus * std::sin(phase);
    }

    fft.irfft(freqDomain.data(), timeDomain.data());
    fft.scale(timeDomain.data());

    // Impulse to Step integration
    float accumulator = 0.0f;
    for (int i = 0; i < n; i++) {
        accumulator += timeDomain[i];
        timeDomain[i] = accumulator;
    }

    // ensure it leads to final value being 1.0
    const float finalValue = timeDomain[n - 1];

    // prevent division by zero or massive gain explosions
    if (std::abs(finalValue) > 1e-6f) {
        const float gain = 1.0f / finalValue;
        for (int i = 0; i < n; i++) {
            timeDomain[i] *= gain;
        }
    } else {
        // should never happen
        for (int i = 0; i < n; i++) timeDomain[i] = (float)i / float(n - 1);
    }

    std::memcpy(outputBuffer, timeDomain.data(), n * sizeof(float));

    outputBuffer[n - 1] = 1.0f;
    // the next item (essentially outputBuffer[n]), will become 1.0f also.
}

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