#include "Autinn.hpp"
#include "Autinn-dsp.hpp"
#include <cmath>

/*

    Autinn VCV Rack Plugin
    Copyright (C) 2021  Nikolai V. Chr.

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

#define OVERSAMPLE 4

struct Oxcart : Module {
	enum ParamIds {
		PITCH_PARAM,
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
	float blinkTime = 0.0f;
	static constexpr int zeroCrossings = 16;
	static constexpr int overSample = 32;
	dsp::MinBlepGenerator<zeroCrossings,overSample,float> oxMinBLEP[16];// 16 zero crossings, x32 oversample
	DCBlocker dcBlocker[16];
	float discontinuity = non_lin_func(4.0f);
	float lastSampleTime = 1.0f/44100.0f;
	std::vector<dsp::Decimator<OVERSAMPLE, 8>> decimators;

	Oxcart() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(Oxcart::PITCH_PARAM, -3.0f, 3.0f, 0.0f, "Frequency"," Hz", 2.0f, dsp::FREQ_C4);
		configInput(PITCH_INPUT, "1V/Oct CV");
		configOutput(BUZZ_OUTPUT, "Audio");
		decimators.resize(16);
		for (int ch = 0; ch < 16; ch++) {
			dcBlocker[ch].cutoff_hz = 1.0f;
			dcBlocker[ch].setSampleTime(lastSampleTime);
		}
	}

	void onReset(const ResetEvent& e) override {
		for (auto & filter : dcBlocker) {
			filter.reset();
		}
	}

	void process(const ProcessArgs &args) override;
};

void Oxcart::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[BUZZ_OUTPUT].isConnected()) {
		lights[BLINK_LIGHT].value = 0.0f;
		return;
	}

	if (lastSampleTime != args.sampleTime) {
		for (auto & chDcBlocker : dcBlocker) {
			chDcBlocker.setSampleTime(args.sampleTime);
		}
	}
	lastSampleTime = args.sampleTime;

	int channels = std::max(1, inputs[PITCH_INPUT].getChannels());
    outputs[BUZZ_OUTPUT].setChannels(channels);
	float deltaTime = args.sampleTime;

	float pitchBase = params[PITCH_PARAM].getValue();
	int pitchInputChannels = inputs[PITCH_INPUT].getChannels();

	for (int ch = 0; ch < channels; ch++) {
		float pitch = pitchBase + (pitchInputChannels>ch?inputs[PITCH_INPUT].getPolyVoltage(ch):inputs[PITCH_INPUT].getVoltage());
		pitch = clamp(pitch, -4.0f, 6.0f);
		//float freq = dsp::FREQ_C4 * powf(2.0f, pitch);
		float freq = dsp::FREQ_C4 * std::exp2f(pitch);//faster
	
		float period = 4.0f;
		float deltaPhase = freq * deltaTime * period / float(OVERSAMPLE);
		float outBuf  [OVERSAMPLE];

		for (float & i : outBuf) {
			phase[ch] += deltaPhase;

			if (phase[ch] >= period) {
				phase[ch] -= period;
				float crossing = -phase[ch] / deltaPhase;
				oxMinBLEP[ch].insertDiscontinuity(crossing, discontinuity);
			}

			i = -non_lin_func(phase[ch])+oxMinBLEP[ch].process();
		}

		const float out = decimators[ch].process(outBuf);
		const float buzz = dcBlocker[ch].process(out);

		// x4.5 to keep its peak within approx 5V
		// minBLEP will increase peak (x1.15 approx)
		// dcBlocker will reduce it.
		// Used to be x6.0 in pre-v2.6.28, when used offset instead of dcBlocker,
		// so I know this will mess up some patches slightly.
		// Actually, for now I will keep the x6, backward compatibility is important.
		outputs[BUZZ_OUTPUT].setVoltage(6.0f * buzz, ch);

		if (ch == 0) {
            blinkTime += deltaTime;
            float blinkPeriod = 1.0f/(freq*0.01f);
            if (blinkTime >= blinkPeriod) blinkTime = 0.0f;
            lights[BLINK_LIGHT].value = (blinkTime < blinkPeriod*0.5f) ? 1.0f : 0.0f;
        }
	}
}

struct OxcartWidget : ModuleWidget {
	OxcartWidget(Oxcart *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/OxcartModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, Oxcart::PITCH_PARAM));

		addInput(createInput<InPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, Oxcart::PITCH_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Oxcart::BUZZ_OUTPUT));

		addChild(createLight<MediumLight<GreenLight>>(Vec(5 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Oxcart::BLINK_LIGHT));
	}
};

Model *modelOxcart = createModel<Oxcart, OxcartWidget>("Oxcart");
