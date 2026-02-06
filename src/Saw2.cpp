#include "Autinn.hpp"
#include <cmath>

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

struct Saw2 : Module {
	enum ParamIds {
		PITCH_PARAM,
		AGE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PITCH_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		BUZZ_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		BLINK_LIGHT,
		NUM_LIGHTS
	};

	float phase[16] = {};
	float hp_state[16] = {}; // Capacitor state for the "Acid" curve
	float blinkTime = 0.0f;
	bool square = false;

	Saw2() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Saw2::PITCH_PARAM, -4.0f, 4.0f, 0.0f, "Frequency", " Hz", 2.0f, dsp::FREQ_C4);
		configParam(Saw2::AGE_PARAM, 0.0f, 30.0f, 15.0f, "Age", " Years");
		configInput(PITCH_INPUT, "1V/Oct CV");
		configOutput(BUZZ_OUTPUT, "Audio");
	}

	// PolyBLEP: Polynomial Band-Limited Step
	// Smooths the sharp discontinuity of the sawtooth to remove aliasing.
	// t = phase (0..1)
	// dt = phase increment per sample
	float poly_blep(float t, float dt) {
		if (t < dt) {
			// 0 < t < dt: Beginning of cycle (the rise)
			t /= dt;
			return t+t - t*t - 1.0f;
		} else if (t > 1.0f - dt) {
			// 1 - dt < t < 1: End of cycle (the drop)
			t = (t - 1.0f) / dt;
			return t*t + t+t + 1.0f;
		}
		// Middle of cycle: No correction needed
		return 0.0f;
	}

	void process(const ProcessArgs &args) override {
		if (!outputs[BUZZ_OUTPUT].isConnected()) {
			return;
		}

		int channels = std::max(1, inputs[PITCH_INPUT].getChannels());
		outputs[BUZZ_OUTPUT].setChannels(channels);

		float pitchBase = params[PITCH_PARAM].getValue();
		int pitchInputChannels = inputs[PITCH_INPUT].getChannels();

		// 30Hz is the magic number for a new TB-303 capacitor droop
		const float cutoff_hz = 30.0f + params[AGE_PARAM].getValue()*4.0f;
		const float rc = 1.0f / (2.0f * M_PI * cutoff_hz);
		const float alpha = rc / (rc + args.sampleTime);

		for (int c = 0; c < channels; c++) {
			// Calculate Frequency
			float pitch = pitchBase + (pitchInputChannels > c ? inputs[PITCH_INPUT].getPolyVoltage(c) : inputs[PITCH_INPUT].getVoltage());
			pitch = clamp(pitch, -4.0f, 6.0f); // Allow slightly higher range
			float freq = dsp::FREQ_C4 * std::exp2f(pitch);
			
			// Clamp to prevent explosions near Nyquist
			freq = clamp(freq, 1.0f, args.sampleRate / 2.0f - 1.0f);

			// Increment Phase
			float dt = freq * args.sampleTime;
			phase[c] += dt;
			if (phase[c] >= 1.0f) phase[c] -= 1.0f;

			// Generate Naive Saw (-1 to 1)
			// A simple ramp: 2 * phase - 1
			float saw = 2.0f * phase[c] - 1.0f;

			if (!square) {
				// Apply PolyBLEP
				saw -= poly_blep(phase[c], dt);
			} else {
				// Subtract a DC-offset saw from the original saw
				// This creates a pulse wave without needing a separate oscillator
				// 0.5f is the phase shift (50% pulse width)
				// Calculate the shifted phase (180 degrees / 0.5 offset)
				float phase_shifted = phase[c] + 0.5f;
				if (phase_shifted >= 1.0f) phase_shifted -= 1.0f;

				// Generate the Naive Shifted Saw
				float saw_shifted = 2.0f * phase_shifted - 1.0f;

				saw_shifted -= poly_blep(phase_shifted, dt);

				// Subtract to create the pulse
				// Saw - InvertedSaw = Square
				saw -= saw_shifted;

				// The subtraction results in a slightly denser signal.
				// We attenuate slightly to match the perceived loudness of the saw.
				saw *= 0.7f;
			}

			// Apply "Acid" High Pass Filter (The 303 Shape)
			// This mimics the AC coupling capacitor that bends the saw into a shark fin.
			// 30-40Hz is the sweet spot for that hardware look.
			// Simple 1-pole High Pass: y[n] = alpha * (y[n-1] + x[n] - x[n-1])

			// High Pass Logic: output = input - low_passed_state
			// We use a simple leaky integrator to track the DC offset
			hp_state[c] = (hp_state[c] * alpha) + (saw * (1.0f - alpha));
			float acid_saw = saw - hp_state[c];

			// Output Gain Staging
			// Bass will gain it a bit, so we keep the voltage down. +/- 2.5V is a good standard level.
			outputs[BUZZ_OUTPUT].setVoltage(acid_saw * 2.5f, c);

			// Blink Light
			if (c == 0) {
				blinkTime += args.sampleTime;
				float blinkPeriod = 1.0f / (freq * 0.05f);
				if (blinkTime >= blinkPeriod) blinkTime -= blinkPeriod;
				lights[BLINK_LIGHT].value = (blinkTime < blinkPeriod * 0.5f) ? 1.0f : 0.0f;
			}
		}
	}
};

struct Saw2Widget : ModuleWidget {
	Saw2Widget(Saw2 *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/AxeModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, Saw2::PITCH_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 100), module, Saw2::AGE_PARAM));

		addInput(createInput<InPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, Saw2::PITCH_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Saw2::BUZZ_OUTPUT));

		addChild(createLight<MediumLight<GreenLight>>(Vec(5 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Saw2::BLINK_LIGHT));
	}
};

Model *modelSaw2 = createModel<Saw2, Saw2Widget>("Saw2");