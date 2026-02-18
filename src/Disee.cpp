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

struct Disee : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		AC_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		DC_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		DC_RED_LIGHT,
		DC_GREEN_LIGHT,
		DC_BLUE_LIGHT,
		NUM_LIGHTS
	};

	float dcFilter[16] = {};

	Disee() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configBypass(AC_INPUT, DC_OUTPUT);
		configLight(DC_RED_LIGHT, "Positive DC");
		configLight(DC_GREEN_LIGHT, "DC near zero");
		configLight(DC_BLUE_LIGHT, "Negative DC");
		configInput(AC_INPUT, "AC");
		configOutput(DC_OUTPUT, "DC");
	}

	void process(const ProcessArgs &args) override;
};

void Disee::process(const ProcessArgs &args) {
	// Implements a DC detector

	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	int channels = std::max(1, inputs[AC_INPUT].getChannels());
	outputs[DC_OUTPUT].setChannels(channels);

	// Calculate a coefficient for a Lowpass filter
	// averages the signal over ~1 second
	float cutoffFreq = 1.0f;
	float lambda = 2.0f * M_PI * cutoffFreq * args.sampleTime;

	for (int c = 0; c < channels; c++) {
		// One-Pole LP Filter
		dcFilter[c] += (inputs[AC_INPUT].getPolyVoltage(c) - dcFilter[c]) * lambda;

		outputs[DC_OUTPUT].setVoltage(clamp(dcFilter[c], -10.0f, 10.0f));

		if (c == 0) {
			// Update lights based on the filtered DC value
			if (std::abs(dcFilter[c]) < 0.05f) {
				lights[DC_GREEN_LIGHT].value = 1.0f;
				lights[DC_RED_LIGHT].value = 0.0f;
				lights[DC_BLUE_LIGHT].value = 0.0f;
			} else if (dcFilter[c] < 0.0f) {
				lights[DC_GREEN_LIGHT].value = 0.0f;
				lights[DC_RED_LIGHT].value = 0.0f;
				lights[DC_BLUE_LIGHT].value = clamp(-dcFilter[c], 0.25f, 1.0f);
			} else {
				lights[DC_GREEN_LIGHT].value = 0.0f;
				lights[DC_RED_LIGHT].value = clamp(dcFilter[c], 0.25f, 1.0f);
				lights[DC_BLUE_LIGHT].value = 0.0f;
			}
		}
	}
}

struct DiseeWidget : ModuleWidget {
	DiseeWidget(Disee *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/DiseeModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, Disee::AC_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Disee::DC_OUTPUT));

		addChild(createLight<MediumLight<RedGreenBlueLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Disee::DC_RED_LIGHT));
	}
};

Model *modelDC = createModel<Disee, DiseeWidget>("Disee");
