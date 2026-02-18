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

static constexpr int oversample = 2;

struct Deadband : Module {
	enum ParamIds {
		WIDTH_PARAM,
		CV_PARAM,
		GAP_PARAM,
		CV_GAP_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		DEADBAND_INPUT,
		CV_INPUT,
		CV_GAP_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		DEADBAND_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};
	
	dsp::Upsampler<oversample, 8> upsampler[16];
	dsp::Decimator<oversample, 8> decimator[16];

	Deadband() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<Param3Digits>(Deadband::WIDTH_PARAM, 0.0f, 5.0f, 0.0f, "Width", " Volt",0.0f,1.0f);
		configParam<Param4Digits>(Deadband::CV_PARAM, 0.0f, 1.0f, 0.0f, "Width CV", "%", 0.0f, 100.0f);
		configParam<Param3Digits>(Deadband::GAP_PARAM, 1.0f, 0.0f, 1.0f, "Gap");
		configParam<Param4Digits>(Deadband::CV_GAP_PARAM, 0.0f, 0.2f, 0.0f, "Gap CV", "%", 0.0f, 500.0f);
		configBypass(DEADBAND_INPUT, DEADBAND_OUTPUT);
		configInput(CV_INPUT, "Width CV");
		configInput(CV_GAP_INPUT, "Gap CV");
		configInput(DEADBAND_INPUT, "");
		configOutput(DEADBAND_OUTPUT, "");

		for (int c = 0; c < 16; c++) {
			upsampler[c] = dsp::Upsampler<oversample, 8>(0.9f);
			decimator[c] = dsp::Decimator<oversample, 8>(0.9f);
		}
	}

	void process(const ProcessArgs &args) override;
};

void Deadband::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[DEADBAND_OUTPUT].isConnected()) {
		return;
	}

	int channels = std::max(1, inputs[DEADBAND_INPUT].getChannels());
	outputs[DEADBAND_OUTPUT].setChannels(channels);

	int widthCVChannels = inputs[CV_INPUT].getChannels();
	int gapCVChannels = inputs[CV_GAP_INPUT].getChannels();

	for (int c = 0; c < channels; c++) {
		float input = inputs[DEADBAND_INPUT].getPolyVoltage(c);

		// If CV has 1 channel, use index 0 for everyone.
		// If CV has multiple channels, use index 'c'.
		float modWidth = (widthCVChannels == 1) ? inputs[CV_INPUT].getVoltage() : inputs[CV_INPUT].getPolyVoltage(c);
		float modGap   = (gapCVChannels == 1)   ? inputs[CV_GAP_INPUT].getVoltage() : inputs[CV_GAP_INPUT].getPolyVoltage(c);

		float width = clamp(params[WIDTH_PARAM].getValue() + params[CV_PARAM].getValue() * modWidth, 0.0f, 5.0f);
		float gap   = clamp(params[GAP_PARAM].getValue() - params[CV_GAP_PARAM].getValue() * modGap, 0.0f, 1.0f);

		float inBuf[oversample];
		float outBuf[oversample];

		upsampler[c].process(input, inBuf);

		for (int i = 0; i < oversample; i++) {
			float unlimit = inBuf[i];
			float limit   = 0.0f;

			if (width == 0.0f) {
				// width of zero means bypass
				limit = unlimit;
			} else if (unlimit > width) {
				limit = unlimit - width * gap;
			} else if (unlimit < -width) {
				limit = unlimit + width * gap;
			}
			outBuf[i] = limit;
		}
		outputs[DEADBAND_OUTPUT].setVoltage(decimator[c].process(outBuf), c);
	}
}

struct DeadbandWidget : ModuleWidget {
	DeadbandWidget(Deadband *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/DeadbandModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 75), module, Deadband::WIDTH_PARAM));
		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 140), module, Deadband::CV_INPUT));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_SMALL, 175), module, Deadband::CV_PARAM));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(6 * RACK_GRID_WIDTH*0.75-HALF_KNOB_MED, 75), module, Deadband::GAP_PARAM));
		addInput(createInput<InPortAutinn>(Vec(6 * RACK_GRID_WIDTH*0.75-HALF_PORT, 140), module, Deadband::CV_GAP_INPUT));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(6 * RACK_GRID_WIDTH*0.75-HALF_KNOB_SMALL, 175), module, Deadband::CV_GAP_PARAM));

		addInput(createInput<InPortAutinn>(Vec(6 * RACK_GRID_WIDTH*0.25-HALF_PORT, 300), module, Deadband::DEADBAND_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(6 * RACK_GRID_WIDTH*0.75-HALF_PORT, 300), module, Deadband::DEADBAND_OUTPUT));

	}
};

Model *modelDeadband = createModel<Deadband, DeadbandWidget>("Deadband");
