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

	dsp::BiquadFilter lowS;
	dsp::BiquadFilter midP;
	dsp::BiquadFilter highS;

	const float Gd = 30.0f;
	const float Pm = 0.30f;
	const float Qp = 0.40f;
	const float Qs = 0.707107f;
	const float c1 = 250.0f;
	const float c2 = 700.0f;
	const float c3 = 2000.0f;

	float low_prev = -1;
	float mid_prev = -1;
	float high_prev = -1;
	float rate_prev = -1;

	TriBand() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(TriBand::LOW_PARAM, 0.5f, 100.0f*Pm, 100.0f*Pm, "Low", " dB", 0.0f, 1.0f, -100.0f*Pm);
		configParam(TriBand::MID_PARAM, 0.5f, 100.0f*Pm, 100.0f*Pm, "Mid", " dB", 0.0f, 1.0f, -100.0f*Pm);
		configParam(TriBand::HIGH_PARAM, 0.5f, 100.0f*Pm, 100.0f*Pm, "High", " dB", 0.0f, 1.0f, -100.0f*Pm);
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
	float in = inputs[TRIBAND_INPUT].getVoltage();
	float low    = params[LOW_PARAM].getValue();
	float mid    = params[MID_PARAM].getValue();
	float high   = params[HIGH_PARAM].getValue();
	float rate   = args.sampleRate;
	float out1;
	float out2;
	float out3;

	if (low != low_prev || mid != mid_prev || high != high_prev || rate != rate_prev) {
		lowS.setParameters(lowS.LOWSHELF, c1/rate, Qs, low);
		midP.setParameters(midP.PEAK, c2/rate, Qp, mid);
		highS.setParameters(highS.HIGHSHELF, c3/rate, Qs, high);
	}
	out1 = lowS.process(in);
	out2 = midP.process(in);
	out3 = highS.process(in);
	if(!std::isfinite(out1) || !std::isfinite(out2) || !std::isfinite(out3)) {
		out1 = 0.0f;
		out2 = 0.0f;
		out3 = 0.0f;
	}
	outputs[TRIBAND_OUTPUT].setVoltage((out1+out2+out3)/Gd);

	low_prev = low;
	mid_prev = mid;
	high_prev = high;
	rate_prev = rate;
}

struct TriBandWidget : ModuleWidget {
	TriBandWidget(TriBand *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/ImpModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 185), module, TriBand::LOW_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 130), module, TriBand::MID_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED,  75), module, TriBand::HIGH_PARAM));
		
		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 245), module, TriBand::TRIBAND_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, TriBand::TRIBAND_OUTPUT));
	}
};

Model *modelTriBand = createModel<TriBand, TriBandWidget>("TriBand");