#include "Autinn.hpp"
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

struct TriBand : Module {
	enum ParamIds {
		LOW_PARAM,
		MID_PARAM,
		HIGH_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		TRIBAND_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		TRIBAND_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	dsp::BiquadFilter lowS[16];
	dsp::BiquadFilter midP[16];
	dsp::BiquadFilter highS[16];

	const float Qp = 0.8f;
	const float Qs = 0.707107f; // Butterworth Q for shelves
	const float c1 = 250.0f;    // Low
	const float c2 = 700.0f;    // Mid
	const float c3 = 2000.0f;   // High

	float low_prev = -1.0f;
	float mid_prev = -1.0f;
	float high_prev = -1.0f;
	float rate_prev = -1.0f;

	TriBand() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<Param3Digits>(TriBand::LOW_PARAM, 0.25f, 4.0f, 1.0f, "Low", " dB", -10.0f, 20.0f, 0);
		configParam<Param3Digits>(TriBand::MID_PARAM, 0.25f, 4.0f, 1.0f, "Mid", " dB", -10.0f, 20.0f, 0);
		configParam<Param3Digits>(TriBand::HIGH_PARAM, 0.25f, 4.0f, 1.0f, "High", " dB", -10.0f, 20.0f, 0);
		configBypass(TRIBAND_INPUT, TRIBAND_OUTPUT);
		configInput(TRIBAND_INPUT, "Audio");
		configOutput(TRIBAND_OUTPUT, "Audio");
	}

	void process(const ProcessArgs &args) override;
};

void TriBand::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[TRIBAND_OUTPUT].isConnected()) {
		return;
	}

	float low  = params[LOW_PARAM].getValue();
	float mid  = params[MID_PARAM].getValue();
	float high = params[HIGH_PARAM].getValue();
	float rate = args.sampleRate;

	if (low != low_prev || mid != mid_prev || high != high_prev || rate != rate_prev) {
		for (int c = 0; c < 16; c++) {
			lowS[c].setParameters(dsp::BiquadFilter::LOWSHELF, c1 / rate, Qs, low);
			midP[c].setParameters(dsp::BiquadFilter::PEAK,     c2 / rate, Qp, mid);
			highS[c].setParameters(dsp::BiquadFilter::HIGHSHELF, c3 / rate, Qs, high);
		}
		low_prev = low;
		mid_prev = mid;
		high_prev = high;
		rate_prev = rate;
	}

	int channels = std::max(1, inputs[TRIBAND_INPUT].getChannels());
	outputs[TRIBAND_OUTPUT].setChannels(channels);

	for (int c = 0; c < channels; c++) {
		float sample = inputs[TRIBAND_INPUT].getPolyVoltage(c);

		// Input -> LowShelf -> Peak -> HighShelf -> Output
		sample = lowS[c].process(sample);
		sample = midP[c].process(sample);
		sample = highS[c].process(sample);

		if (!std::isfinite(sample)) {
			sample = 0.0f;
			lowS[c].reset();
			midP[c].reset();
			highS[c].reset();
		}

		outputs[TRIBAND_OUTPUT].setVoltage(sample, c);
	}
}

struct TriBandWidget : ModuleWidget {
	TriBandWidget(TriBand *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/ImpModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5f-HALF_KNOB_MED, 185), module, TriBand::LOW_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5f-HALF_KNOB_MED, 130), module, TriBand::MID_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5f-HALF_KNOB_MED,  75), module, TriBand::HIGH_PARAM));
		
		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5f-HALF_PORT, 245), module, TriBand::TRIBAND_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5f-HALF_PORT, 300), module, TriBand::TRIBAND_OUTPUT));
	}
};

Model *modelTriBand = createModel<TriBand, TriBandWidget>("TriBand");