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

static constexpr int OVERSAMPLE = 4;

struct Bunker : Module {
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
	PredictiveBLEP blep[16];
	DCBlocker dcBlocker[16] = {};
	DCBlocker hp1[16] = {};
	DCBlocker hp2[16] = {};
	float lastSampleTime = 1.0f/44100.0f;
	float blinkTime = 0.0f;
	float squareGain = 1.0f;// attenuate square to match the perceived loudness of the saw.
	bool square = false;
	dsp::SchmittTrigger schmittButton;
	std::vector<dsp::Decimator<OVERSAMPLE, 8>> decimators;

	Bunker() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Bunker::PITCH_PARAM, -4.0f, 4.0f, 0.0f, "Frequency", " Hz", 2.0f, dsp::FREQ_C4);
		configParam<Param3Digits>(Bunker::AGE_PARAM, 0.0f, 40.0f, 15.0f, "Age", " Years");
		configButton(TYPE_PARAM, "Saw or Square");
		configInput(CV_PITCH_INPUT, "1V/Oct CV");
		configInput(CV_AGE_INPUT, "1V/decade CV");
		configInput(CV_TYPE_INPUT, "Type trigger");
		configOutput(BUZZ_OUTPUT, "Audio");
		decimators.resize(16);

		for (auto & filter : dcBlocker) {
			// extremely slow. It corrects the DC drift without touching the bass.
			filter.cutoff_hz = 2.0f;
			filter.setSampleTime(lastSampleTime);
		}
	}

	void onReset(const ResetEvent& e) override {
		square = false;
		for (int c = 0; c < 16; c++) {
			hp1[c].reset();
			hp2[c].reset();
			phase[c] = 0.0f;
			dcBlocker[c].reset();
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

		const int channels = std::max(1, inputs[CV_PITCH_INPUT].getChannels());
		outputs[BUZZ_OUTPUT].setChannels(channels);

		const float pitchBase = params[PITCH_PARAM].getValue();
		const int pitchInputChannels = inputs[CV_PITCH_INPUT].getChannels();

		if (lastSampleTime != args.sampleTime) {
			for (auto & chDcBlocker : dcBlocker) {
				chDcBlocker.setSampleTime(args.sampleTime);
			}
		}
		lastSampleTime = args.sampleTime;

		for (int c = 0; c < channels; c++) {
			float cv_age = inputs[CV_AGE_INPUT].getChannels() > c? inputs[CV_AGE_INPUT].getPolyVoltage(c):inputs[CV_AGE_INPUT].getVoltage();
			cv_age *= 4.0f;

			// 30Hz is the magic number for a new capacitor droop
			const float age = clamp(cv_age+params[AGE_PARAM].getValue(), 0.0f, 60.0f);
			hp1[c].cutoff_hz = 30.0f + age*9.0f;
			hp1[c].setSampleTime(args.sampleTime/float(OVERSAMPLE));

			hp2[c].cutoff_hz = 0.5f + age*4.0f;
			hp2[c].setSampleTime(args.sampleTime/float(OVERSAMPLE));

			// As the capacitor dries out (age increases), bass is lost and the signal thins out.
			// We add gain to compensate, making the bulge even bigger.
			const float makeupGain = 1.0f + (age * 0.1f); // Up to 5x boost at max age

			// Calculate Frequency
			float pitch = pitchBase + (pitchInputChannels > c ? inputs[CV_PITCH_INPUT].getPolyVoltage(c) : inputs[CV_PITCH_INPUT].getVoltage());
			pitch = clamp(pitch, -4.0f, 5.0f); // Allow a slightly higher range
			float freq = dsp::FREQ_C4 * std::exp2f(pitch+1);

			float outBuf  [OVERSAMPLE];

			for (int i = 0; i < OVERSAMPLE; i++) {
				const float dt = freq * args.sampleTime / (float)OVERSAMPLE;
				const float nextPhase = phase[c] + dt;

				const float magDown = square ? squareGain * -2.0f : -2.0f;
				const float magUp   = square ? squareGain *  2.0f :  0.0f;

				if (nextPhase >= 1.0f) {
					square = !square;
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

				outBuf[i] = tanh_fast_high(stage2 * makeupGain);
			}

			float out = decimators[c].process(outBuf);

			// remove DC offset
			// Measure the current offset (Accumulate average)
			out = dcBlocker[c].process(out);

			// Output Gain Staging
			// Bass will gain it a bit, so we keep the voltage down.
			outputs[BUZZ_OUTPUT].setVoltage(out * 2.75f, c);

			// Blink Light
			if (c == 0) {
				blinkTime += args.sampleTime;
				const float blinkPeriod = 1.0f / (freq * 0.05f);
				if (blinkTime >= blinkPeriod) blinkTime -= blinkPeriod;
				lights[BLINK_LIGHT].value = (blinkTime < blinkPeriod * 0.5f) ? 1.0f : 0.0f;
			}
		}
	}
};

struct BunkerWidget : ModuleWidget {
	BunkerWidget(Bunker *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/BunkerModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(box.size.x*0.25f, 125.0f+HALF_KNOB_MED), module, Bunker::PITCH_PARAM));
		/*
		auto pitchKnob = createParamCentered<AutinnArcMidKnob>(Vec(box.size.x*0.25, 125+HALF_KNOB_MED), module, Bunker::PITCH_PARAM);
		pitchKnob->setModulation(Bunker::CV_PITCH_INPUT, [](float cv, float val, float att) {
					return clamp(val + cv, -4.0f, 6.0f);
				});
		addParam(pitchKnob);
		*/

		auto ageKnob = createParamCentered<AutinnArcMidKnob>(Vec(box.size.x*0.25f, 75.0f+HALF_KNOB_MED), module, Bunker::AGE_PARAM);

		// Link modulation:
		// 1. Source: CV_AGE_INPUT
		// 2. Math:   Simple Linear. 5V input adds 1.0 to the parameter (Full Sweep).
		//            (Input * 0.2 means 5V becomes 1.0)
		ageKnob->setModulation(Bunker::CV_AGE_INPUT, [](float cv, float val, float att) {
			return clamp(val + (cv * 4.0f), 0.0f, 60.0f);
		});

		addParam(ageKnob);
		//addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(box.size.x*0.25, 75+HALF_KNOB_MED), module, Bunker::AGE_PARAM));

		addInput(createInputCentered<InPortAutinn>(Vec(box.size.x*0.75f, 75.0f+HALF_KNOB_MED), module, Bunker::CV_AGE_INPUT));

		//addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(box.size.x*0.75f, 5.0f + (75.0f+HALF_KNOB_MED+162.0f)/2.0f), module, Bunker::TYPE_PARAM));

		addInput(createInputCentered<InPortAutinn>(Vec(box.size.x*0.25f, 200.0f+HALF_PORT), module, Bunker::CV_PITCH_INPUT));
		addInput(createInputCentered<InPortAutinn>(Vec(box.size.x*0.75f, 200.0f+HALF_PORT), module, Bunker::CV_TYPE_INPUT));
		addOutput(createOutputCentered<OutPortAutinn>(Vec(box.size.x*0.25f, 300.0f+HALF_PORT), module, Bunker::BUZZ_OUTPUT));

		addChild(createLightCentered<MediumLight<GreenLight>>(Vec(box.size.x*0.5f, 50.0f), module, Bunker::BLINK_LIGHT));
		//addChild(createLightCentered<SmallLight<RedLight>>(Vec(box.size.x*0.6f, 162.0f), module, Bunker::SAW_LIGHT));
		//addChild(createLightCentered<SmallLight<BlueLight>>(Vec(box.size.x*0.6f, 177.0f), module, Bunker::SQUARE_LIGHT));
	}
};

Model *modelBunker = createModel<Bunker, BunkerWidget>("Bunker");