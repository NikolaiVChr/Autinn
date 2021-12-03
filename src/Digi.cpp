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
	
	dsp::Upsampler<oversample, 8> upsampler = dsp::Upsampler<oversample, 8>(0.9f);
	dsp::Decimator<oversample, 8> decimator = dsp::Decimator<oversample, 8>(0.9f);

	Digi() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Digi::STEP_PARAM, 0.0f, 1.0f, 0.0f, "Quantization", " Volt",0.0f,1.0f);
		configParam(Digi::CV_PARAM, 0.0f, 0.2f, 0.0f, "CV", "%",0.0f,500.0f);
		configBypass(ANALOG_INPUT, DIGITAL_OUTPUT);
		configInput(CV_INPUT, "CV");
		configInput(ANALOG_INPUT, "Analog");
		configOutput(DIGITAL_OUTPUT, "Digital");
	}

	void process(const ProcessArgs &args) override;
};

void Digi::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[DIGITAL_OUTPUT].isConnected()) {
		return;
	}
	float input = inputs[ANALOG_INPUT].getVoltage();
	float jump = clamp(params[STEP_PARAM].getValue()+params[CV_PARAM].getValue()*inputs[CV_INPUT].getVoltage(),0.0f,1.0f);
	
	/*	if (jump == 0.0f) {
		outputs[DIGITAL_OUTPUT].setVoltage(input);
		return;
	}*/
	
	float inBuf   [oversample];
	float outBuf  [oversample];
	
	upsampler.process(input, inBuf);
	
	for (int i = 0; i < oversample; i++) {
		float analog  = inBuf[i];
		float digital = 0.0f;
		if (jump == 0.0f) {
			digital = analog;
		} else if (analog >= 0.0f) {
			digital = analog-fmod(analog, jump);
		} else {
			analog = -analog;
			digital = analog+(jump-fmod(analog, jump));//fmod return same sign as input value
			digital = -digital;
		}
		outBuf[i] = digital + 0.5f*jump;
	}
    outputs[DIGITAL_OUTPUT].setVoltage(decimator.process(outBuf));
}

struct DigiWidget : ModuleWidget {
	DigiWidget(Digi *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/DigiModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 75), module, Digi::STEP_PARAM));
		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 140), module, Digi::CV_INPUT));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_SMALL, 175), module, Digi::CV_PARAM));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 250), module, Digi::ANALOG_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Digi::DIGITAL_OUTPUT));

	}
};

Model *modelDigi = createModel<Digi, DigiWidget>("Digi");