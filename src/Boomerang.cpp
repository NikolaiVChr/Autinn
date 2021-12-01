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

struct Boomerang : Module {
	enum ParamIds {
		DIAL_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PRE_INPUT,
		POST_INPUT,
		CV_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		PRE_OUTPUT,
		POST_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	Boomerang() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Boomerang::DIAL_PARAM, 0.001f, 2.0f, 1.0f, "Gain", " dB", -10, 20);
		configBypass(PRE_INPUT, PRE_OUTPUT);
		configBypass(POST_INPUT, POST_OUTPUT);
		configInput(PRE_INPUT, "Pre");
		configInput(POST_INPUT, "Post");
		configOutput(PRE_OUTPUT, "Pre");
		configOutput(POST_OUTPUT, "Post");
	}

	void process(const ProcessArgs &args) override;
};

void Boomerang::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (outputs[PRE_OUTPUT].isConnected()) {
		float in = inputs[PRE_INPUT].getVoltage();
		float level = clamp(params[DIAL_PARAM].getValue()+inputs[CV_INPUT].getVoltage()*0.2f,0.001f,2.0f);;
		float out = in*level;
	    outputs[PRE_OUTPUT].setVoltage(out);
	}

	if (outputs[POST_OUTPUT].isConnected()) {
		float in = inputs[POST_INPUT].getVoltage();
		float level = clamp(params[DIAL_PARAM].getValue()+inputs[CV_INPUT].getVoltage()*0.2f,0.001f,2.0f);;
		float out = in/level;
	    outputs[POST_OUTPUT].setVoltage(out);
	}
	
}

struct BoomerangWidget : ModuleWidget {
	BoomerangWidget(Boomerang *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/MeraModule.svg")));
		//box.size = Vec(3 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		
		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 65), module, Boomerang::CV_INPUT));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 100), module, Boomerang::DIAL_PARAM));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 150), module, Boomerang::PRE_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, Boomerang::PRE_OUTPUT));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 250), module, Boomerang::POST_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Boomerang::POST_OUTPUT));
	}
};

Model *modelBoomerang = createModel<Boomerang, BoomerangWidget>("Boomerang");