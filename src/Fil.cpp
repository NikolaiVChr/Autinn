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

static const int oversample2 = 2;
static const int oversample4 = 4;
static const int oversample8 = 8;

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

	int current_oversample = 4;
	float th=1.0f/3.0f;

	dsp::Upsampler<oversample2, 10> upsampler2;
	dsp::Decimator<oversample2, 10> decimator2;
	dsp::Upsampler<oversample4, 10> upsampler4;
	dsp::Decimator<oversample4, 10> decimator4;
	dsp::Upsampler<oversample8, 10> upsampler8;
	dsp::Decimator<oversample8, 10> decimator8;

	json_t *dataToJson() override {
		json_t *root = json_object();
		json_object_set_new(root, "oversample", json_integer(current_oversample));
		return root;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *ext3 = json_object_get(rootJ, "oversample");
		if (ext3) {
			current_oversample = json_integer_value(ext3);
			if (current_oversample != 2 and current_oversample != 4 and current_oversample != 8) {
				current_oversample = 4;
			}
		}
	}

	void onReset(const ResetEvent& e) override {
		current_oversample = 4;
		Module::onReset(e);
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

	float inInter [current_oversample];
	float outBuf  [current_oversample];
	if (current_oversample == oversample2) {
		upsampler2.process(in, inInter);
	} else if (current_oversample == oversample4) {
		upsampler4.process(in, inInter);
	} else {
		upsampler8.process(in, inInter);
	}
	
	for (int i = 0; i < current_oversample; i++) {
		if (fabs(inInter[i]) < th) {
			out = 2.0f*inInter[i];
			if (i==0) {
				lights[LOW_LIGHT].value = fabs(out)/(th*2.0f);
				lights[MID_LIGHT].value = 0.0f;
				lights[HIGH_LIGHT].value = 0.0f;
			}
		} else if (fabs(inInter[i]) <= 2.0f*th) {
			if (inInter[i] > 0.0f) {
				out = (3.0f-(2.0f-inInter[i]*3.0f)*(2.0f-inInter[i]*3.0f))/3.0f;
			} else {
			    out = -(3.0f-(2.0f-fabs(inInter[i])*3.0f)*(2.0f-fabs(inInter[i])*3.0f))/3.0f;
			}
			if (i==0) {
				lights[MID_LIGHT].value = (fabs(out)-th*2.0f)*3.0f;
				lights[LOW_LIGHT].value = 0.0f;
				lights[HIGH_LIGHT].value = 0.0f;
			}
		} else {
			if (inInter[i] > 0.0f) {
				out =  1.0f;
			} else {
				out = -1.0f;
			}
			if (i==0) {
				lights[HIGH_LIGHT].value = (fabs(inInter[i])-th*2.0f)*2.0f;
				lights[MID_LIGHT].value = 0.0f;
				lights[LOW_LIGHT].value = 0.0f;
			}
		}
		outBuf[i] = non_lin_func(out);
	}
	if (current_oversample == oversample2) {
		out = decimator2.process(outBuf);
	} else if (current_oversample == oversample4) {
		out = decimator4.process(outBuf);
	} else {
		out = decimator8.process(outBuf);
	}

    outputs[FIL_OUTPUT].setVoltage(out*5.0f);
}

struct OversampleFilMenuItem : MenuItem {
	Fil* _module;
	int _os;

	OversampleFilMenuItem(Fil* module, const char* label, int os)
	: _module(module), _os(os)
	{
		this->text = label;
	}

	void onAction(const event::Action &e) override {
		_module->current_oversample = _os;
	}

	void step() override {
		rightText = _module->current_oversample == _os ? "✔" : "";
	}
};

struct FilWidget : ModuleWidget {
	FilWidget(Fil *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/FilModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 130), module, Fil::DIAL_PARAM));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 245), module, Fil::FIL_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Fil::FIL_OUTPUT));

		addChild(createLight<MediumLight<RedLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 65), module, Fil::HIGH_LIGHT));
		addChild(createLight<MediumLight<GreenLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Fil::MID_LIGHT));
		addChild(createLight<MediumLight<BlueLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 85), module, Fil::LOW_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		Fil* a = dynamic_cast<Fil*>(module);
		assert(a);
		
		menu->addChild(new MenuLabel());
		menu->addChild(new OversampleFilMenuItem(a, "Oversample x2", 2));
		menu->addChild(new OversampleFilMenuItem(a, "Oversample x4", 4));
		menu->addChild(new OversampleFilMenuItem(a, "Oversample x8", 8));
	}
};

Model *modelFil = createModel<Fil, FilWidget>("Overdrive");