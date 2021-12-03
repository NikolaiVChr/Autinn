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

struct Amp : Module {
	enum ParamIds {
		DIAL_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		AMP_INPUT,
		CV_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		AMP_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		BLINK_LIGHT,
		NUM_LIGHTS
	};

	Amp() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Amp::DIAL_PARAM, 0.0f, 2.0f, 1.0f, "Gain", " dB", -10, 20);
		configBypass(AMP_INPUT, AMP_OUTPUT);
		configInput(CV_INPUT, "CV");
		configInput(AMP_INPUT, "Audio");
		configOutput(AMP_OUTPUT, "Audio");
	}

	void process(const ProcessArgs &args) override;
};

void Amp::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[AMP_OUTPUT].isConnected()) {
		return;
	}
	int channels = inputs[AMP_INPUT].getChannels();
	simd::float_4 in[4];
	
	//float level = clamp(params[DIAL_PARAM].getValue()+inputs[CV_INPUT].getVoltage()*0.2f,0.0f,2.0f);//-infinite to +6 dB
	
	for (int channel = 0; channel < channels; channel += 4) {
		in[channel / 4] = simd::float_4::load(inputs[AMP_INPUT].getVoltages(channel));
	}
	//float in = inputs[AMP_INPUT].getVoltage();

	if (inputs[CV_INPUT].isPolyphonic()) {
		for (int channel = 0; channel < channels; channel += 4) {
			simd::float_4 multiplier = simd::float_4::load(inputs[CV_INPUT].getVoltages(channel))*0.2f;
			multiplier = clamp(params[DIAL_PARAM].getValue()+multiplier, 0.0f,2.0f);
			in[channel / 4] *= multiplier;
		}
	} else {
		float multiplier = inputs[CV_INPUT].getVoltage()*0.2f;
		multiplier = clamp(params[DIAL_PARAM].getValue()+multiplier, 0.0f,2.0f);
		for (int channel = 0; channel < channels; channel += 4) {
			in[channel / 4] *= multiplier;
		}
	}
	
	outputs[AMP_OUTPUT].setChannels(channels);
    for (int channel = 0; channel < channels; channel += 4) {
		in[channel / 4].store(outputs[AMP_OUTPUT].getVoltages(channel));
	}
	float light = fabs(in[0][0]);
	lights[BLINK_LIGHT].value = (light > 10.0f) ? 1.0 : 0.0;
}

struct AmpWidget : ModuleWidget {
	AmpWidget(Amp *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/AmpModule.svg")));
		//box.size = Vec(3 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 115), module, Amp::CV_INPUT));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, Amp::DIAL_PARAM));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, Amp::AMP_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Amp::AMP_OUTPUT));

		addChild(createLight<MediumLight<YellowLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Amp::BLINK_LIGHT));
	}
};

Model *modelAmp = createModel<Amp, AmpWidget>("Amp");