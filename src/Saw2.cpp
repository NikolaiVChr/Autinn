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

static const int OVERSAMPLE = 4;

struct Saw2 : Module {
	enum ParamIds {
		PITCH_PARAM,
		AGE_PARAM,
		TYPE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CV_PITCH_INPUT,
		CV_TYPE_INPUT,
		CV_AGE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		BUZZ_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		BLINK_LIGHT,
		SAW_LIGHT,
		SQUARE_LIGHT,
		NUM_LIGHTS
	};

	float phase[16] = {};
	float hp_state[16] = {}; // Capacitor state for the Acid curve
	float hp_state2[16] = {};
	float dc_integrator[16] = {};
	float blinkTime = 0.0f;
	bool square = false;
	dsp::SchmittTrigger schmittButton;
	std::vector<dsp::Decimator<OVERSAMPLE, 8>> decimators;

	Saw2() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Saw2::PITCH_PARAM, -4.0f, 4.0f, 0.0f, "Frequency", " Hz", 2.0f, dsp::FREQ_C4);
		configParam<Param3Digits>(Saw2::AGE_PARAM, 0.0f, 40.0f, 15.0f, "Age", " Years");
		configButton(TYPE_PARAM, "Saw or Square");
		configInput(CV_PITCH_INPUT, "1V/Oct CV");
		configInput(CV_AGE_INPUT, "1V/decade CV");
		configInput(CV_TYPE_INPUT, "Type trigger");
		configOutput(BUZZ_OUTPUT, "Audio");
		decimators.resize(16);
	}

	// PolyBLEP: Polynomial Band-Limited Step
	// Smooths the sharp discontinuity of the sawtooth to remove aliasing.
	// t = phase (0..1)
	// dt = phase increment per sample
	static float poly_blep(float t, float dt) {
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

	void onReset(const ResetEvent& e) override {
		square = false;
		for (int c = 0; c < 16; c++) {
			hp_state[c] = 0.0f;
			hp_state2[c] = 0.0f;
			phase[c] = 0.0f;
			dc_integrator[c] = 0.0f;
		}
		blinkTime = 0.0f;
		schmittButton.reset();
		Module::onReset(e);
	}

	void onRandomize(const RandomizeEvent& e) override {
		Module::onRandomize(e);
		square = bool(random::uniform() < 0.5f);
	}

	json_t *dataToJson() override {
		json_t *root = json_object();
		json_object_set_new(root, "square", json_boolean(square));
		return root;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *ext = json_object_get(rootJ, "square");
		if (ext)
			square = json_boolean_value(ext);
	}

	void process(const ProcessArgs &args) override {
		if (!outputs[BUZZ_OUTPUT].isConnected()) {
			return;
		}

		if (schmittButton.process(params[TYPE_PARAM].getValue()+inputs[CV_TYPE_INPUT].getVoltage())) {
			square = !square;
		}
		lights[SAW_LIGHT].setBrightness(square ? 0.0f : 1.0f);
		lights[SQUARE_LIGHT].setBrightness(square ? 1.0f : 0.0f);

		int channels = std::max(1, inputs[CV_PITCH_INPUT].getChannels());
		outputs[BUZZ_OUTPUT].setChannels(channels);

		float pitchBase = params[PITCH_PARAM].getValue();
		int pitchInputChannels = inputs[CV_PITCH_INPUT].getChannels();

		// extremely slow. It corrects the DC drift without touching the bass.
		constexpr float servo_hz = 2.0f;
		constexpr float servo_rc = 1.0f / (2.0f * M_PI * servo_hz);
		const float servo_alpha = args.sampleTime / (servo_rc + args.sampleTime);

		for (int c = 0; c < channels; c++) {
			float cv_age = inputs[CV_AGE_INPUT].getChannels() > c? inputs[CV_AGE_INPUT].getPolyVoltage(c):inputs[CV_AGE_INPUT].getVoltage();
			cv_age *= 4.0f;

			// 30Hz is the magic number for a new TB-303 capacitor droop
			const float age = clamp(cv_age+params[AGE_PARAM].getValue(), 0.0f, 60.0f);
			const float cutoff_hz = 30.0f + age*9.0f;
			const float rc = 1.0f / (2.0f * M_PI * cutoff_hz);
			const float alpha = rc / (rc + args.sampleTime/(float)OVERSAMPLE);

			const float cutoff_hz2 = 0.5f + age*4.0f;
			const float rc2 = 1.0f / (2.0f * M_PI * cutoff_hz2);
			const float alpha2 = rc2 / (rc2 + args.sampleTime/(float)OVERSAMPLE);
			// As the capacitor dries out (age increases), bass is lost and the signal thins out.
			// We add gain to compensate, making the Bulge even bigger.
			float makeupGain = 1.0f + (age * 0.1f); // Up to 5x boost at max age

			// Calculate Frequency
			float pitch = pitchBase + (pitchInputChannels > c ? inputs[CV_PITCH_INPUT].getPolyVoltage(c) : inputs[CV_PITCH_INPUT].getVoltage());
			pitch = clamp(pitch, -4.0f, 6.0f); // Allow a slightly higher range
			float freq = dsp::FREQ_C4 * std::exp2f(pitch);
			
			// Clamp to prevent explosions near Nyquist
			freq = clamp(freq, 1.0f, args.sampleRate / 2.0f - 1.0f);

			//float inBuf   [OVERSAMPLE];
			float outBuf  [OVERSAMPLE];
			//upsamplers[c].process(stage2, inBuf);

			for (int i = 0; i < OVERSAMPLE; i++) {
				// Increment Phase
				float dt = freq * args.sampleTime;
				dt = dt / (float)OVERSAMPLE;
				phase[c] += dt;
				if (phase[c] >= 1.0f) phase[c] -= 1.0f;

				// Generate Naive Saw (-1 to 1)
				// A simple ramp: 2 * phase - 1
				float saw = 2.0f * phase[c] - 1.0f;

				// Apply PolyBLEP
				saw -= poly_blep(phase[c], dt);

				if (square) {
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

				// Apply Acid High Pass Filter (The 303 Shape)
				// This mimics the AC coupling capacitor that bends the saw into a shark fin.
				// 30-40Hz is the sweet spot for that hardware sound.
				// Simple 1-pole High Pass: y[n] = alpha * (y[n-1] + x[n] - x[n-1])

				// High Pass Logic: output = input - low_passed_state
				// We use a simple leaky integrator to track the DC offset
				// Stage 1: The Curve (Shark Fin)
				hp_state[c] = (hp_state[c] * alpha) + (saw * (1.0f - alpha));
				if (!std::isfinite(hp_state[c])) {
					hp_state[c] = 0.0f;
				}
				float stage1 = saw - hp_state[c];

				// Stage 2: Creates the Overshoot
				// We apply the high pass logic again to the output of Stage 1.
				hp_state2[c] = (hp_state2[c] * alpha2) + (stage1 * (1.0f - alpha2));
				if (!std::isfinite(hp_state2[c])) {
					hp_state2[c] = 0.0f;
				}
				float stage2 = stage1 - hp_state2[c];


				outBuf[i] = non_lin_func(stage2 * makeupGain);
			}

			float out = decimators[c].process(outBuf);

			// remove DC offset
			// Measure the current offset (Accumulate average)
			dc_integrator[c] += (out - dc_integrator[c]) * servo_alpha;

			// Subtract the measured offset from the signal
			// This gently moves the whole wave up or down to center it.
			out -= dc_integrator[c];

			// Output Gain Staging
			// Bass will gain it a bit, so we keep the voltage down.
			outputs[BUZZ_OUTPUT].setVoltage(out * 2.75f, c);

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

		addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(box.size.x*0.25, 125+HALF_KNOB_MED), module, Saw2::PITCH_PARAM));
		/*
		auto pitchKnob = createParamCentered<AutinnArcMidKnob>(Vec(box.size.x*0.25, 125+HALF_KNOB_MED), module, Saw2::PITCH_PARAM);
		pitchKnob->setModulation(Saw2::CV_PITCH_INPUT, [](float cv, float val, float att) {
					return clamp(val + cv, -4.0f, 6.0f);
				});
		addParam(pitchKnob);
		*/

		auto ageKnob = createParamCentered<AutinnArcMidKnob>(Vec(box.size.x*0.25, 75+HALF_KNOB_MED), module, Saw2::AGE_PARAM);

		// Link modulation:
		// 1. Source: CV_AGE_INPUT
		// 2. Math:   Simple Linear. 5V input adds 1.0 to the parameter (Full Sweep).
		//            (Input * 0.2 means 5V becomes 1.0)
		ageKnob->setModulation(Saw2::CV_AGE_INPUT, [](float cv, float val, float att) {
			return clamp(val + (cv * 4.0f), 0.0f, 60.0f);
		});

		addParam(ageKnob);
		//addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(box.size.x*0.25, 75+HALF_KNOB_MED), module, Saw2::AGE_PARAM));

		addInput(createInputCentered<InPortAutinn>(Vec(box.size.x*0.75, 75+HALF_KNOB_MED), module, Saw2::CV_AGE_INPUT));

		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(box.size.x*0.75, 5.0f + (75+HALF_KNOB_MED+162)/2.0f), module, Saw2::TYPE_PARAM));

		addInput(createInputCentered<InPortAutinn>(Vec(box.size.x*0.25, 200+HALF_PORT), module, Saw2::CV_PITCH_INPUT));
		addInput(createInputCentered<InPortAutinn>(Vec(box.size.x*0.75, 200+HALF_PORT), module, Saw2::CV_TYPE_INPUT));
		addOutput(createOutputCentered<OutPortAutinn>(Vec(box.size.x*0.25, 300+HALF_PORT), module, Saw2::BUZZ_OUTPUT));

		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(box.size.x*0.5, 50), module, Saw2::BLINK_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(Vec(box.size.x*0.6, 162), module, Saw2::SAW_LIGHT));
		addChild(createLightCentered<SmallLight<BlueLight>>(Vec(box.size.x*0.6, 177), module, Saw2::SQUARE_LIGHT));
	}
};

Model *modelSaw2 = createModel<Saw2, Saw2Widget>("Saw2");