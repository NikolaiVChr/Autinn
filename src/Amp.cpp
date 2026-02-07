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
		configParam<Param3Digits>(Amp::DIAL_PARAM, 0.0f, 2.0f, 1.0f, "Gain", " dB", -10, 20);
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
	if (channels == 0) channels = 1; // Default to 1 if nothing is connected to input
	outputs[AMP_OUTPUT].setChannels(channels);

	float dial = params[DIAL_PARAM].getValue();
	bool cvPoly = inputs[CV_INPUT].isPolyphonic();
	float cvMono = inputs[CV_INPUT].getVoltage() * 0.2f;

	for (int c = 0; c < channels; c++) {
		float in = inputs[AMP_INPUT].getPolyVoltage(c);
		float multiplier;

		if (cvPoly) {
			multiplier = inputs[CV_INPUT].getPolyVoltage(c) * 0.2f;
		} else {
			multiplier = cvMono;
		}

		multiplier = clamp(dial+multiplier, 0.0f,2.0f);

		float out = in * multiplier;

		outputs[AMP_OUTPUT].setVoltage(out, c);

		if (c == 0) {
			float light = fabsf(in);
			lights[BLINK_LIGHT].value = (light > 10.0f) ? 1.0 : 0.0;
		}
	}
}

struct AmpWidget : ModuleWidget {
	AmpWidget(Amp *module) {
		//INFO("AmpWidget: Starting constructor. Module is %s", module ? "VALID" : "NULL");

		if (!pluginInstance) {
			//DEBUG("AmpWidget: pluginInstance is NULL!");
			return;
		}
		//INFO("AmpWidget: Loading Panel SVG");
		auto panelSvg = asset::plugin(pluginInstance, "res/AmpModule.svg");
		//INFO("AmpWidget: Resolved path: %s", panelSvg.c_str());

		setPanel(createPanel(panelSvg));

		//box.size = Vec(3 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
		//INFO("AmpWidget: Adding Screw Widget 1");
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		//INFO("AmpWidget: Adding Screw Widget 2");
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		//INFO("AmpWidget: Adding port Widgets");
		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 115), module, Amp::CV_INPUT));
		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, Amp::AMP_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Amp::AMP_OUTPUT));
		//INFO("AmpWidget: Adding knob Widgets");
		//addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, Amp::DIAL_PARAM));
		auto pitchKnob = createParam<AutinnArcMidKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, Amp::DIAL_PARAM);
		pitchKnob->setModulation(Amp::CV_INPUT, [](float cv, float val, float att) {
					return clamp(val + cv*0.2f, 0.0f, 2.0f);
				});
		addParam(pitchKnob);

		//INFO("AmpWidget: Adding light Widgets");
		addChild(createLight<MediumLight<YellowLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Amp::BLINK_LIGHT));

		//INFO("AmpWidget: Constructor Finished Successfully");
		setModule(module);
	}
};

Model *modelAmp = createModel<Amp, AmpWidget>("Amp");