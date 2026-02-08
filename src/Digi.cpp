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

static const int oversample = 2;

struct Digi : Module {
	enum ParamIds {
		STEP_PARAM,
		CV_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ANALOG_INPUT,
		CV_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		DIGITAL_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	std::vector<dsp::Upsampler<oversample, 8>> upsamplers;
	std::vector<dsp::Decimator<oversample, 8>> decimators;

	Digi() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<Param3Digits>(Digi::STEP_PARAM, 0.0f, 1.0f, 0.0f, "Quantization", " Volt",0.0f,1.0f);
		configParam<Param4Digits>(Digi::CV_PARAM, 0.0f, 0.2f, 0.0f, "CV", "%",0.0f,500.0f);
		configBypass(ANALOG_INPUT, DIGITAL_OUTPUT);
		configInput(CV_INPUT, "CV");
		configInput(ANALOG_INPUT, "Analog");
		configOutput(DIGITAL_OUTPUT, "Digital");

		upsamplers.assign(16, dsp::Upsampler<oversample, 8>(0.9f));
		decimators.assign(16, dsp::Decimator<oversample, 8>(0.9f));
	}

	void process(const ProcessArgs &args) override;
};

void Digi::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[DIGITAL_OUTPUT].isConnected()) {
		return;
	}

	int channels = std::max(1, inputs[ANALOG_INPUT].getChannels());
	outputs[DIGITAL_OUTPUT].setChannels(channels);

	for (int c = 0; c < channels; c++) {
		float input = inputs[ANALOG_INPUT].getPolyVoltage(c);
		float jump = clamp(params[STEP_PARAM].getValue()+params[CV_PARAM].getValue()*inputs[CV_INPUT].getVoltage(),0.0f,1.0f);

		float inBuf   [oversample];
		float outBuf  [oversample];

		upsamplers[c].process(input, inBuf);

		for (int i = 0; i < oversample; i++) {
			float analog  = inBuf[i];
			float digital = 0.0f;
			if (jump > 0.001f) {
				// "floor" creates the step.
				// Adding 0.5f * jump aligns it to the center
				digital = std::floor(analog / jump) * jump + (0.5f * jump);
			} else {
				digital = analog;
			}
			outBuf[i] = digital;
		}
		outputs[DIGITAL_OUTPUT].setVoltage(decimators[c].process(outBuf), c);
	}
}

struct DigiWidget : ModuleWidget {
	DigiWidget(Digi *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/DigiModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		//addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 75), module, Digi::STEP_PARAM));
		auto stepKnob = createParam<AutinnArcMidKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 75), module, Digi::STEP_PARAM);
		stepKnob->setModulation(Digi::CV_INPUT, [](float cv, float val, float att) {
					return clamp(val + cv*att, 0.0f, 1.0f);
				}, Digi::CV_PARAM);
		addParam(stepKnob);

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 140), module, Digi::CV_INPUT));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_SMALL, 175), module, Digi::CV_PARAM));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 250), module, Digi::ANALOG_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Digi::DIGITAL_OUTPUT));

	}
};

Model *modelDigi = createModel<Digi, DigiWidget>("Digi");