#include "Autinn.hpp"
#include <cmath>
#include <cstdlib>
#include <ctime>

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

struct VectorDriver : Module {
	enum ParamIds {
		SPEED_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		X_OUTPUT,
		Y_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	float rotationSpeed = 0.0f;// radians/sec
	float x = 0.0f;// -5 to 5 V
	float y = 0.0f;
	float angle = 0.0f;// degrees
	bool firstRun = true;
	float tim = 0.0f;

	VectorDriver() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(VectorDriver::SPEED_PARAM, 2.0f, 5.0f, 3.5f, "");
		configOutput(X_OUTPUT, "±5V X CV");
		configOutput(Y_OUTPUT, "±5V Y CV");
	}

	void process(const ProcessArgs &args) override;
};

void VectorDriver::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[X_OUTPUT].isConnected() and !outputs[Y_OUTPUT].isConnected()) {
		return;
	}
	if (firstRun) {
		firstRun = false;
		srand (static_cast <unsigned> (time(0)));
		float ran = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);// 0-1 random number
		rotationSpeed = (ran*2-1.0f)*135.0f;
	}
	float dt = args.sampleTime;
	float movementSpeed = params[SPEED_PARAM].getValue();// 2-5V/sec

	if (tim > 2.5f) {
		float r = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);// 0-1 random number
		rotationSpeed = (r*2-1.0f)*135*movementSpeed*0.2f;// +-135deg/s
		tim = 0.0f;
	}

	angle += rotationSpeed*dt;
	angle = fmod(angle, 360.0f);

	x = clamp(x+cos(angle*(M_PI/180.0f))*movementSpeed*dt,-5.0f,5.0f);
	y = clamp(y+sin(angle*(M_PI/180.0f))*movementSpeed*dt,-5.0f,5.0f);
	
    outputs[X_OUTPUT].setVoltage(x);
    outputs[Y_OUTPUT].setVoltage(y);
    tim += dt;
}

struct VectorDriverWidget : ModuleWidget {
	VectorDriverWidget(VectorDriver *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/VxyModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, VectorDriver::SPEED_PARAM));

		//addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, VectorDriver::VEC_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, VectorDriver::Y_OUTPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 250), module, VectorDriver::X_OUTPUT));

	}
};

Model *modelVectorDriver = createModel<VectorDriver, VectorDriverWidget>("Vector");