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

static const int oversample2 = 2;
static const int oversample4 = 4;

#define SNORING_MAX 24.0f
#define SNORING_MIN 1.0f
#define DREAMING_MIN 0.5f
#define DREAMING_MAX 2.0f

struct Nap : Module {
	enum ParamIds {
		SNORING_PARAM,
		DREAMING_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		NAP_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		NAP_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		LOW_LIGHT,
		MID_LIGHT,
		HIGH_LIGHT,
		NUM_LIGHTS
	};

	Nap() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SNORING_PARAM, 0.0f, 1.0f, 0.0f, "Snore amount", " ", SNORING_MAX/SNORING_MIN, SNORING_MIN);
		configParam(DREAMING_PARAM, DREAMING_MIN, DREAMING_MAX, 1.00f, "Dream amount", " ", 0.0f, 1.0f);
		configBypass(NAP_INPUT, NAP_OUTPUT);
		configInput(NAP_INPUT, "Audio");
		configOutput(NAP_OUTPUT, "Audio");

		configLight(HIGH_LIGHT, "Heavy snoring.. ");
		configLight(MID_LIGHT, "REM sleep.. ");
		configLight(LOW_LIGHT, "Trying to fall asleep.. ");
	}

	float out_prev = 0.1f;

	int current_oversample = 2;

	dsp::Upsampler<oversample2, 10> upsampler2;
	dsp::Decimator<oversample2, 10> decimator2;
	dsp::Upsampler<oversample4, 10> upsampler4;
	dsp::Decimator<oversample4, 10> decimator4;

	json_t *dataToJson() override {
		json_t *root = json_object();
		json_object_set_new(root, "oversample", json_integer(current_oversample));
		return root;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *ext3 = json_object_get(rootJ, "oversample");
		if (ext3) {
			current_oversample = json_integer_value(ext3);
			if (current_oversample != 2 and current_oversample != 4) {
				current_oversample = 4;
			}
		}
	}

	void onReset(const ResetEvent& e) override {
		current_oversample = 2;
		Module::onReset(e);
	}

	float fwdEuler(float out_prev, float in);
	float distort(float out_prev, float in);
	void process(const ProcessArgs &args) override;
};

void Nap::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[NAP_OUTPUT].isConnected()) {
		return;
	}

	float pre = inputs[NAP_INPUT].getVoltage() * (params[SNORING_PARAM].getValue() * (SNORING_MAX - SNORING_MIN) + SNORING_MIN);

	float inInter [current_oversample];
	float outBuf  [current_oversample];

	if (current_oversample == oversample2) {
		upsampler2.process(clamp(pre, -4.5f, 4.5f), inInter);
	} else if (current_oversample == oversample4) {
		upsampler4.process(clamp(pre, -4.5f, 4.5f), inInter);
	}

	for (int i = 0; i < current_oversample; i++) {
		float out = this->fwdEuler(out_prev, inInter[i]);
		outBuf[i] = non_lin_func(out/12.0f);
		out_prev = out;
	}
	float final;
	if (current_oversample == oversample2) {
		final = decimator2.process(outBuf);
	} else {
		final = decimator4.process(outBuf);
	}

	//outputs[NAP_OUTPUT].setChannels(2);
    //outputs[NAP_OUTPUT].setVoltage(pre, 1);
    outputs[NAP_OUTPUT].setVoltage(final*12.0f, 0);

    float pre_abs = fabs(pre);
	lights[HIGH_LIGHT].value = fmax(0,(pre_abs-18.0f)*4.0f);
	lights[MID_LIGHT].value = fmax(0,(pre_abs-4.5f)*2.0f);
	lights[LOW_LIGHT].value = fmax(0,rescale(pre_abs, 0.0f, 4.5f, 1.0f, 0.0f));
}

float Nap::fwdEuler(float out_prv, float in) {
    return out_prv + this->distort(out_prv, in)*params[DREAMING_PARAM].getValue();
}

float Nap::distort(float out_prv, float in) {
    return (in - out_prv) / 22.0f - 0.504f * non_lin_func2(out_prv / 45.3f);
}

struct OversampleNapMenuItem : MenuItem {
	Nap* _module;
	int _os;

	OversampleNapMenuItem(Nap* module, const char* label, int os)
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

struct NapWidget : ModuleWidget {
	NapWidget(Nap *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/NapModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 130), module, Nap::SNORING_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 185), module, Nap::DREAMING_PARAM));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 245), module, Nap::NAP_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Nap::NAP_OUTPUT));

		addChild(createLight<MediumLight<RedLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 65), module, Nap::HIGH_LIGHT));
		addChild(createLight<MediumLight<GreenLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Nap::MID_LIGHT));
		addChild(createLight<MediumLight<BlueLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 85), module, Nap::LOW_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		Nap* a = dynamic_cast<Nap*>(module);
		assert(a);
		
		menu->addChild(new MenuLabel());
		menu->addChild(new OversampleNapMenuItem(a, "Oversample x2", 2));
		menu->addChild(new OversampleNapMenuItem(a, "Oversample x4", 4));		
	}
};

Model *modelNap = createModel<Nap, NapWidget>("Distortion");