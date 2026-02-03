#include "Autinn.hpp"
#include <cmath>
#include <cstdlib>

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
		float ran = random::uniform();// 0-1 random number
		rotationSpeed = (ran*2-1.0f)*135.0f;
	}
	float dt = args.sampleTime;
	float movementSpeed = params[SPEED_PARAM].getValue(); // 2-5V/sec

	tim += args.sampleTime;

	// Only change steering every 0.1 seconds
	// This allows the car to actually complete a turn before changing its mind
	if (tim > 0.1f) {
		tim = 0.0f;

		// Randomly push the steering wheel left or right
		// We add to the current speed rather than resetting it
		float nudge = (random::uniform() * 2.f - 1.f) * 100.0f;
		rotationSpeed += nudge;

		// slowly return steering to center so it doesn't spin forever
		rotationSpeed *= 0.9f;

		// Hard limit on how fast it can spin (±200 degrees/sec)
		rotationSpeed = clamp(rotationSpeed, -200.0f, 200.0f);
	}

	angle += rotationSpeed * args.sampleTime;

	// Normalize angle (0 to 360)
	if (angle > 360.f) angle -= 360.f;
	if (angle < 0.f) angle += 360.f;

	// Move Position
	float rad = angle * (M_PI / 180.0f);
	x += std::cos(rad) * movementSpeed * args.sampleTime;
	y += std::sin(rad) * movementSpeed * args.sampleTime;

	// The Fix: "Billiard Ball" Bounce
	// Instead of clamping (sticking), we reflect the angle when hitting a wall.

	// Hit Right or Left Wall? -> Flip X direction (Reflect across Y-axis)
	if (x > 5.0f) {
		x = 5.0f;
		angle = 180.0f - angle;
		rotationSpeed *= -0.5f; // Lose some turning energy on impact
	}
	else if (x < -5.0f) {
		x = -5.0f;
		angle = 180.0f - angle;
		rotationSpeed *= -0.5f;
	}

	// Hit Top or Bottom Wall? -> Flip Y direction (Reflect across X-axis)
	if (y > 5.0f) {
		y = 5.0f;
		angle = 360.0f - angle;
		rotationSpeed *= -0.5f;
	}
	else if (y < -5.0f) {
		y = -5.0f;
		angle = 360.0f - angle;
		rotationSpeed *= -0.5f;
	}
	
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