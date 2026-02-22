#include "Autinn.hpp"
#include <cmath>
#include "Autinn-dsp.hpp"

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
	ReactiveBLEP blep[16];
	DCBlocker dcBlocker[16];
	DCBlocker hp1[16];
	DCBlocker hp2[16];
	float roofLPF[16] = {};
	float lastSampleTime[16];
	float lastAge[16]; // Init to -1 so it triggers on frame 1
	float blinkTime = 0.0f;
	float squareGain = 0.7f;// attenuate square to match the perceived loudness of the saw.
	bool square = false;
	dsp::SchmittTrigger schmittButton;
	std::vector<dsp::Decimator<4, 8>> decimators4;
	std::vector<dsp::Decimator<2, 8>> decimators2;

	static int getOversampleAmount(const float sampleRate) {
		if (sampleRate < 50000.0f) return 4;
		if (sampleRate < 100000.0f) return 2;
		return 1;
	}

	Saw2() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Saw2::PITCH_PARAM, -4.0f, 4.0f, 0.0f, "Frequency", " Hz", 2.0f, dsp::FREQ_C4);
		configParam<Param3Digits>(Saw2::AGE_PARAM, 0.0f, 40.0f, 15.0f, "Age", " Years");
		configButton(TYPE_PARAM, "Saw or Square");
		configInput(CV_PITCH_INPUT, "1V/Oct CV");
		configInput(CV_AGE_INPUT, "4 Years/V CV");
		configInput(CV_TYPE_INPUT, "Type trigger");
		configOutput(BUZZ_OUTPUT, "Audio");
		decimators4.resize(16);
		decimators2.resize(16);

		for (int ch = 0; ch < 16; ch++) {
			// extremely slow. It corrects the DC drift without touching the bass.
			dcBlocker[ch].cutoff_hz = 2.0f;
			lastAge[ch] = -1.0f;
			lastSampleTime[ch] = 0.0f;
		}
	}

	void onReset(const ResetEvent& e) override {
		square = false;
		for (int c = 0; c < 16; c++) {
			hp1[c].reset();
			hp2[c].reset();
			phase[c] = 0.0f;
			dcBlocker[c].reset();
			roofLPF[c] = 0.0f;
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

	inline float processSubSample(int c, float dt, float makeupGain) {
		const float nextPhase = phase[c] + dt;
		const float magDown = square ? squareGain * -2.0f : -2.0f;
		const float magUp   = square ? squareGain *  2.0f :  0.0f;

		if (nextPhase >= 1.0f) {
			const float overshoot = nextPhase - 1.0f;
			const float fraction = overshoot / dt;
			blep[c].jump(fraction, magDown);
			phase[c] = overshoot;
		} else if (square && phase[c] < 0.5f && nextPhase >= 0.5f) {
			const float overshoot = nextPhase - 0.5f;
			const float fraction = overshoot / dt;
			blep[c].jump(fraction, magUp);
			phase[c] = nextPhase;
		} else {
			phase[c] = nextPhase;
		}

		float naive = 0.0f;
		if (!square) {
			naive = 2.0f * phase[c] - 1.0f;
		} else {
			naive = (phase[c] < 0.5f) ? -1.0f : 1.0f;
			naive *= squareGain;
		}

		const float out = blep[c].process(naive);

		// Apply HP Filter
		// This mimics the AC coupling capacitor that bends the saw into a shark fin.

		// We use a simple leaky integrator to track the DC offset
		// Stage 1: The Curve (Shark Fin)
		const float stage1 = hp1[c].process(out);

		// Stage 2: Creates the Overshoot
		// We apply the high pass logic again to the output of Stage 1.
		const float stage2 = hp2[c].process(stage1);

		// roof filters (1-Pole LP to kill IMD before saturation)
		// 20.3 to 22.1khz cutoff, depending on rack samplerate
		roofLPF[c] += 0.5f * (stage2 - roofLPF[c]);

		return tanh_fast_high(roofLPF[c] * makeupGain);
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

		const int channels = std::max(1, inputs[CV_PITCH_INPUT].getChannels());
		outputs[BUZZ_OUTPUT].setChannels(channels);

		const float pitchBase = params[PITCH_PARAM].getValue();
		const int pitchInputChannels = inputs[CV_PITCH_INPUT].getChannels();

		const int oversample = getOversampleAmount(args.sampleRate);
		const float osSampleTime = args.sampleTime / (float)oversample;

		for (int c = 0; c < channels; c++) {
			if (lastSampleTime[c] != args.sampleTime) {
				dcBlocker[c].setSampleTime(args.sampleTime);
			}

			float cv_age = inputs[CV_AGE_INPUT].getChannels() > c? inputs[CV_AGE_INPUT].getPolyVoltage(c):inputs[CV_AGE_INPUT].getVoltage();
			cv_age *= 4.0f;

			// 30Hz is the magic number for a new capacitor droop
			const float age = clamp(cv_age+params[AGE_PARAM].getValue(), 0.0f, 60.0f);

			if (age != lastAge[c] || lastSampleTime[c] != args.sampleTime) {
				hp1[c].cutoff_hz = 30.0f + age*9.0f;
				hp1[c].setSampleTime(osSampleTime);

				hp2[c].cutoff_hz = 0.5f + age*4.0f;
				hp2[c].setSampleTime(osSampleTime);

				lastAge[c] = age;
				lastSampleTime[c] = args.sampleTime;
			}

			// As the capacitor dries out (age increases), bass is lost and the signal thins out.
			// We add gain to compensate, making the bulge even bigger.
			const float makeupGain = 1.0f + (age * 0.1f); // Up to 5x boost at max age

			// Calculate Frequency
			float pitch = pitchBase + (pitchInputChannels > c ? inputs[CV_PITCH_INPUT].getPolyVoltage(c) : inputs[CV_PITCH_INPUT].getVoltage());
			pitch = clamp(pitch, -4.0f, 6.0f); // Allow a slightly higher range
			const float freq = dsp::FREQ_C4 * std::exp2f(pitch);

			const float dt = freq * osSampleTime;
			float finalOut = 0.0f;

			if (oversample == 4) {
				float outBuf[4];
				for (float & i : outBuf) {
					i = processSubSample(c, dt, makeupGain);
				}
				finalOut = decimators4[c].process(outBuf);
			} else if (oversample == 2) {
				float outBuf[2];
				for (float & i : outBuf) {
					i = processSubSample(c, dt, makeupGain);
				}
				finalOut = decimators2[c].process(outBuf);
			} else {
				// 1x Bypass
				finalOut = processSubSample(c, dt, makeupGain);
			}

			// remove DC offset
			// Measure the current offset (Accumulate average)
			float out = dcBlocker[c].process(finalOut);

			// Output Gain Staging
			// Bass will gain it a bit, so we keep the voltage down.
			outputs[BUZZ_OUTPUT].setVoltage(out * 2.75f, c);

			// Blink Light
			if (c == 0) {
				blinkTime += args.sampleTime;
				float blinkPeriod = 1.0f / (freq * 0.05f);
				while (blinkTime >= blinkPeriod) blinkTime -= blinkPeriod;
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

		addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(box.size.x*0.25f, 125.0f+HALF_KNOB_MED), module, Saw2::PITCH_PARAM));
		/*
		auto pitchKnob = createParamCentered<AutinnArcMidKnob>(Vec(box.size.x*0.25, 125+HALF_KNOB_MED), module, Saw2::PITCH_PARAM);
		pitchKnob->setModulation(Saw2::CV_PITCH_INPUT, [](float cv, float val, float att) {
					return clamp(val + cv, -4.0f, 6.0f);
				});
		addParam(pitchKnob);
		*/

		auto ageKnob = createParamCentered<AutinnArcMidKnob>(Vec(box.size.x*0.25f, 75.0f+HALF_KNOB_MED), module, Saw2::AGE_PARAM);

		// Link modulation:
		// 1. Source: CV_AGE_INPUT
		// 2. Math:   Simple Linear. 5V input adds 1.0 to the parameter (Full Sweep).
		//            (Input * 0.2 means 5V becomes 1.0)
		ageKnob->setModulation(Saw2::CV_AGE_INPUT, [](float cv, float val, float att) {
			return clamp(val + (cv * 4.0f), 0.0f, 60.0f);
		});

		addParam(ageKnob);
		//addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(box.size.x*0.25, 75+HALF_KNOB_MED), module, Saw2::AGE_PARAM));

		addInput(createInputCentered<InPortAutinn>(Vec(box.size.x*0.75f, 75.0f+HALF_KNOB_MED), module, Saw2::CV_AGE_INPUT));

		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(box.size.x*0.75f, 5.0f + (75.0f+HALF_KNOB_MED+162.0f)/2.0f), module, Saw2::TYPE_PARAM));

		addInput(createInputCentered<InPortAutinn>(Vec(box.size.x*0.25f, 200.0f+HALF_PORT), module, Saw2::CV_PITCH_INPUT));
		addInput(createInputCentered<InPortAutinn>(Vec(box.size.x*0.75f, 200.0f+HALF_PORT), module, Saw2::CV_TYPE_INPUT));
		addOutput(createOutputCentered<OutPortAutinn>(Vec(box.size.x*0.25f, 300.0f+HALF_PORT), module, Saw2::BUZZ_OUTPUT));

		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(box.size.x*0.5f, 50.0f), module, Saw2::BLINK_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(Vec(box.size.x*0.6f, 162.0f), module, Saw2::SAW_LIGHT));
		addChild(createLightCentered<SmallLight<BlueLight>>(Vec(box.size.x*0.6f, 177.0f), module, Saw2::SQUARE_LIGHT));
	}
};

Model *modelSaw2 = createModel<Saw2, Saw2Widget>("Saw2");