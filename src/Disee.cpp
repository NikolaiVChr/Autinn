#include "Autinn.hpp"
#include <cmath>
#include <queue>

using std::queue;

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

	const int SIZE = 12500;// fixed for now.
	std::vector<float> buffer;
    int head = 0;
    float runningDC = 0.0f;
    bool bufferFull = false;

	Disee() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configBypass(AC_INPUT, DC_OUTPUT);
		configLight(DC_RED_LIGHT, "Positive DC");
		configLight(DC_GREEN_LIGHT, "DC near zero");
		configLight(DC_BLUE_LIGHT, "Negative DC");
		configInput(AC_INPUT, "AC");
		configOutput(DC_OUTPUT, "DC");
		buffer.resize(SIZE, 0.0f);
	}

	void process(const ProcessArgs &args) override;
};

void Disee::process(const ProcessArgs &args) {
	// Implements a DC detector

	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	// TODO: Make buffer size depend on bitrate and review the time it should average over.
	//       Right now its 0.28 seconds for 44.1KHz
	//       Right now its 0.26 seconds for 48.0KHz

	float in = inputs[AC_INPUT].getVoltage() / SIZE;

    // Ring Buffer Logic (No allocations)
    float oldVal = buffer[head];
    buffer[head] = in;
    
    // Update Running Sum
    runningDC = runningDC - oldVal + in;

    // Advance Head
    head++;
    if (head >= SIZE) {
        head = 0;
        bufferFull = true;
    }

    // Output (Wait for buffer to fill)
    if (!bufferFull) {
        lights[DC_GREEN_LIGHT].value = 0.0f;
		lights[DC_RED_LIGHT].value = 0.0f;
		lights[DC_BLUE_LIGHT].value = 0.0f;
        return;
    }

    float dc = runningDC; 
    outputs[DC_OUTPUT].setVoltage(clamp(dc, -10000.0f, 10000.0f));
	
	if (fabs(dc) < 0.05f) {
		lights[DC_GREEN_LIGHT].value = 1.0f;
		lights[DC_RED_LIGHT].value = 0.0f;
		lights[DC_BLUE_LIGHT].value = 0.0f;
	} else if (dc < 0.0f) {
		lights[DC_GREEN_LIGHT].value = 0.0f;
		lights[DC_RED_LIGHT].value = 0.0f;
		lights[DC_BLUE_LIGHT].value = clamp(-dc,0.25f,1.0f);
	} else {
		lights[DC_GREEN_LIGHT].value = 0.0f;
		lights[DC_RED_LIGHT].value = clamp(dc,0.25f,1.0f);
		lights[DC_BLUE_LIGHT].value = 0.0f;
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
