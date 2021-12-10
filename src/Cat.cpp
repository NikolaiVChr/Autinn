#include "Autinn.hpp"
#include <cmath>

/*

    Autinn VCV Rack Plugin
    Copyright (C) 2021  Alex Jilkin, Nikolai V. Chr.

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

// Converted to real-time C++ for VCV Rack from
// algorithm by Alex Jilkin, who wrote a non-real-time python distortion program.


#define PURR_MAX 24.0f
#define PURR_MIN 1.0f

struct Cat : Module {
	enum ParamIds {
		PURR_PARAM,
		MEOW_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CAT_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		CAT_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		LOW_LIGHT,
		MID_LIGHT,
		HIGH_LIGHT,
		NUM_LIGHTS
	};

	Cat() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PURR_PARAM, 0.0f, 1.0f, 0.0f, "Purr", " ", PURR_MAX/PURR_MIN, PURR_MIN);
		configParam(MEOW_PARAM, 0.5f, 2.0f, 1.00f, "Meow", " ", 0.0f, 1.0f);
		configBypass(CAT_INPUT, CAT_OUTPUT);
		configInput(CAT_INPUT, "Audio");
		configOutput(CAT_OUTPUT, "Audio");

		configLight(HIGH_LIGHT, "Cat in attack mode.. ");
		configLight(MID_LIGHT, "Playful cat.. ");
		configLight(LOW_LIGHT, "Bored cat.. ");
	}

	float out_prev = 0.1f;

	float fwdEuler(float out_prev, float in);
	float distort(float out_prev, float in);
	void process(const ProcessArgs &args) override;
};

void Cat::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[CAT_OUTPUT].isConnected()) {
		return;
	}

	float pre = inputs[CAT_INPUT].getVoltage() * (params[PURR_PARAM].getValue() * (PURR_MAX - PURR_MIN) + PURR_MIN);
	float in = clamp(pre, -4.5f, 4.5f);
	float out = this->fwdEuler(out_prev, in);
	//outputs[CAT_OUTPUT].setChannels(2);
    //outputs[CAT_OUTPUT].setVoltage(pre, 1);
    outputs[CAT_OUTPUT].setVoltage(non_lin_func(out/12.0f)*12.0f, 0);    

    float pre_abs = fabs(pre);
	lights[HIGH_LIGHT].value = fmax(0,(pre_abs-18.0f)*4.0f);
	lights[MID_LIGHT].value = fmax(0,(pre_abs-4.5f)*2.0f);
	lights[LOW_LIGHT].value = fmax(0,rescale(pre_abs, 0.0f, 4.5f, 1.0f, 0.0f));

    out_prev = out;
}

float Cat::fwdEuler(float out_prev, float in) {
    return out_prev + this->distort(out_prev, in)*params[MEOW_PARAM].getValue();
}

float Cat::distort(float out_prev, float in) {
    return (in - out_prev) / 22.0f - 0.504f * non_lin_func2(out_prev / 45.3f);
}

struct CatWidget : ModuleWidget {
	CatWidget(Cat *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/CatModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 100), module, Cat::PURR_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, Cat::MEOW_PARAM));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 245), module, Cat::CAT_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Cat::CAT_OUTPUT));

		addChild(createLight<MediumLight<RedLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 65), module, Cat::HIGH_LIGHT));
		addChild(createLight<MediumLight<GreenLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Cat::MID_LIGHT));
		addChild(createLight<MediumLight<BlueLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 85), module, Cat::LOW_LIGHT));
	}
};

Model *modelCat = createModel<Cat, CatWidget>("Distortion");