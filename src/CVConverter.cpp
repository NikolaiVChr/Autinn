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

struct CVConverter : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		FIVE_INPUT,
		TEN_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		TEN_OUTPUT,
		FIVE_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	CVConverter() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configBypass(FIVE_INPUT, FIVE_OUTPUT);
		configBypass(TEN_INPUT, TEN_OUTPUT);
		configInput(FIVE_INPUT, "±5V CV");
		configInput(TEN_INPUT, "0V to 10V CV");
		configOutput(FIVE_OUTPUT, "±5V CV");
		configOutput(TEN_OUTPUT, "0V to 10V CV");
	}

	float range(float value, float valueRangeL, float valueRangeH, float rangeL, float rangeH);
	void process(const ProcessArgs &args) override;
};

float CVConverter::range(float value, float valueRangeL, float valueRangeH, float rangeL, float rangeH) {
    return rangeL + (value-valueRangeL)*(rangeH-rangeL)/(valueRangeH-valueRangeL);
}

void CVConverter::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (outputs[TEN_OUTPUT].isConnected()) {
		float in = inputs[FIVE_INPUT].getVoltage();
		outputs[TEN_OUTPUT].setVoltage(this->range(in,-5.0f,5.0f,0.0f,10.0f));
	}

	if (outputs[FIVE_OUTPUT].isConnected()) {
		float in = inputs[TEN_INPUT].getVoltage();
		outputs[FIVE_OUTPUT].setVoltage(this->range(in,0.0f,10.0f,-5.0f,5.0f));
	}
	
}

struct CVConverterWidget : ModuleWidget {
	CVConverterWidget(CVConverter *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/CVConverterModule.svg")));
		//box.size = Vec(3 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 100), module, CVConverter::FIVE_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 150), module, CVConverter::TEN_OUTPUT));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 250), module, CVConverter::TEN_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, CVConverter::FIVE_OUTPUT));
	}
};

Model *modelCVConverter = createModel<CVConverter, CVConverterWidget>("CVConverter");