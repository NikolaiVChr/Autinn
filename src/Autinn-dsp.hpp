#pragma once
#include <dsp/fft.hpp>
#include <dsp/window.hpp>
#include <rack.hpp>
#include <cmath>

using namespace rack;
using namespace rack::dsp;

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
inline float polyBLEP(float t, float dt) {
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

/**
 * minBLEP table generation function from Rack, but with some fixes.
 *
 * @param z zero-crossings
 * @param o oversamplings
 * @param output
 */
static void minBlepImpulseFixed(int z, int o, float* output) {
	// Symmetric sinc array with `z` zero-crossings on each side
	int n = 2 * z * o;
	float* x = new float[n];
	for (int i = 0; i < n; i++) {
		float p = math::rescale((float)i, 0.f, (float)(n - 1), (float)-z, (float)z);
		x[i] = sinc(p);
	}

	// Apply window
	blackmanHarrisWindow(x, n);

	// Real cepstrum
	float* fx = new float[n];
	// Valgrind complains that the array is uninitialized for some reason, unless we clear it.
	//std::memset(fx, 0, sizeof(float) * 2 * n);
	RealFFT rfft(n);
	rfft.rfft(x, fx);
	// fx = log(abs(fx))

	fx[0] = std::log(std::fabs(fx[0]) + 1e-9f); // Add epsilon to avoid -inf
	fx[1] = std::log(std::fabs(fx[1]) + 1e-9f);
	for (int k = 1; k < n / 2; k++) {
		int re_idx = 2 * k;
		int im_idx = 2 * k + 1;
		float mag = std::hypot(fx[re_idx], fx[im_idx]);
		fx[re_idx] = std::log(mag + 1e-9f);
		fx[im_idx] = 0.f;
	}

	// Clamp values in case we have -inf
	for (int i = 0; i < n; i++) {
		fx[i] = std::fmax(-100.f, fx[i]);
	}
	rfft.irfft(fx, x);
	rfft.scale(x);

	// Minimum-phase reconstruction
	for (int i = 1; i < n / 2; i++) {
		x[i] *= 2.f;
	}
	for (int i = n / 2 + 1; i < n; i++) {
		x[i] = 0.f;
	}
	rfft.rfft(x, fx);
	// fx = exp(fx)
	fx[0] = std::exp(fx[0]);
	fx[1] = std::exp(fx[1]);
	for (int k = 1; k < n / 2; k++) {
		int re_idx = 2 * k;
		int im_idx = 2 * k + 1;

		float logMag = fx[re_idx];
		float phase  = fx[im_idx];

		float mag = std::exp(logMag);
		fx[re_idx] = mag * std::cos(phase);
		fx[im_idx] = mag * std::sin(phase);
	}
	rfft.irfft(fx, x);
	rfft.scale(x);

	// Integrate
	float total = 0.f;
	for (int i = 0; i < n; i++) {
		total += x[i];
		x[i] = total;
	}

	// Normalize
	if (std::fabs(x[n - 1]) > 1e-6f) {
		float norm = 1.f / x[n - 1];
		for (int i = 0; i < n; i++) {
			x[i] *= norm;
		}
	}

	std::memcpy(output, x, n * sizeof(float));

	// Cleanup
	delete[] x;
	delete[] fx;
}

struct DCBlocker {
	float cutoff_hz = 7.0f;// call setSampleTime() after modifying this

	void setSampleTime(float sampleTime) {
		// call only when sample rate changes
		R = float(1.0f - (cutoff_hz * 2.0f * M_PI * sampleTime));
	}

	float process(float x) {
		// 1-pole HP: y[n] = x[n] - x[n-1] + R * y[n-1]
		float y = x - x_1 + R * y_1;
		x_1 = x;
		y_1 = y;
		return y;
	}

	void reset() {
		x_1 = 0.0f;
		y_1 = 0.0f;
	}
private:
	float x_1 = 0.f;
	float y_1 = 0.f;

	// R = 1 - (2 * pi * freq / sampleRate).
	// 0.999 ~7Hz at 44.1kHz.
	// 0.995 ~35Hz (too high for bass).
	float R = 0.999f;
};