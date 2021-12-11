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

#define DRIVE_MAX 10.0f
#define DRIVE_MIN 0.1f

struct Fil : Module {
	enum ParamIds {
		DIAL_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		FIL_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		FIL_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		LOW_LIGHT,
		MID_LIGHT,
		HIGH_LIGHT,
		NUM_LIGHTS
	};

	Fil() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(DIAL_PARAM, 0.0f, 1.0f, 0.25f, "Drive", " ", DRIVE_MAX/DRIVE_MIN, DRIVE_MIN);
		configBypass(FIL_INPUT, FIL_OUTPUT);
		configInput(FIL_INPUT, "Audio");
		configOutput(FIL_OUTPUT, "Audio");

		configLight(HIGH_LIGHT, "Serious grinding going on.. ");
		configLight(MID_LIGHT, "Moderate filing.. ");
		configLight(LOW_LIGHT, "Hungry, feed me!  ");
	}

	void process(const ProcessArgs &args) override;
};

void Fil::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[FIL_OUTPUT].isConnected()) {
		return;
	}

	float in = 0.20f * inputs[FIL_INPUT].getVoltage() * (params[DIAL_PARAM].getValue() * (DRIVE_MAX - DRIVE_MIN) + DRIVE_MIN);
	float out = 0.0f;

	float th=1.0f/3.0f;
	if (fabs(in) < th) {
		out = 2.0f*in;
		lights[LOW_LIGHT].value = fabs(out)/(th*2.0f);
		lights[MID_LIGHT].value = 0.0f;
		lights[HIGH_LIGHT].value = 0.0f;
	} else if (fabs(in) <= 2.0f*th) {
		if (in > 0.0f) {
			out = (3.0f-(2.0f-in*3.0f)*(2.0f-in*3.0f))/3.0f;
		} else {
		    out = -(3.0f-(2.0f-fabs(in)*3.0f)*(2.0f-fabs(in)*3.0f))/3.0f;
		}
		lights[MID_LIGHT].value = (fabs(out)-th*2.0f)*3.0f;
		lights[LOW_LIGHT].value = 0.0f;
		lights[HIGH_LIGHT].value = 0.0f;
	} else {
		if (in > 0.0f) {
			out =  1.0f;
		} else {
			out = -1.0f;
		}
		lights[HIGH_LIGHT].value = (fabs(in)-th*2.0f)*2.0f;
		lights[MID_LIGHT].value = 0.0f;
		lights[LOW_LIGHT].value = 0.0f;
	}

    outputs[FIL_OUTPUT].setVoltage(non_lin_func(out)*5.0f);
}

struct FilWidget : ModuleWidget {
	FilWidget(Fil *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/FilModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, Fil::DIAL_PARAM));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 245), module, Fil::FIL_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Fil::FIL_OUTPUT));

		addChild(createLight<MediumLight<RedLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 65), module, Fil::HIGH_LIGHT));
		addChild(createLight<MediumLight<GreenLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Fil::MID_LIGHT));
		addChild(createLight<MediumLight<BlueLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 85), module, Fil::LOW_LIGHT));
	}
};

Model *modelFil = createModel<Fil, FilWidget>("Overdrive");