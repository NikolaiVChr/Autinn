#include "Autinn.hpp"
#include <cmath>
#define POLY_CHANNELS 4

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

	struct Channel {
		float rotationSpeed = 0.0f; // degs/sec
		float x = 0.0f; // -5 to 5 V
		float y = 0.0f;
		float angle = 0.0f; // degrees
		bool firstRun = true;
		float tim = 0.0f;
	};

	Channel channels[POLY_CHANNELS];

	VectorDriver() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<Param3Digits>(VectorDriver::SPEED_PARAM, 0.25f, 5.0f, 3.5f, "");
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

	outputs[X_OUTPUT].setChannels(POLY_CHANNELS);
	outputs[Y_OUTPUT].setChannels(POLY_CHANNELS);

	float dt = args.sampleTime;
	float movementSpeed = params[SPEED_PARAM].getValue(); // 2-5V/sec
	float limit = 100.0f * movementSpeed;

	for (int c = 0; c < POLY_CHANNELS; c++) {
		Channel &ch = channels[c];

		if (ch.firstRun) {
			ch.firstRun = false;
			float ran = random::uniform();
			ch.rotationSpeed = (ran * 2 - 1.0f) * 135.0f;
			ch.x = (random::uniform() * 10.0f) - 5.0f;
			ch.y = (random::uniform() * 10.0f) - 5.0f;
		}

		ch.tim += dt;

		if (ch.tim > 0.05f) {
			// Only change steering every 0.1 seconds
			// This allows the car to actually complete a turn before changing its mind

			ch.tim = 0.0f;

			// Randomly push the steering wheel left or right
			// We add to the current speed rather than resetting it
			float nudge = (random::uniform() * 2.f - 1.f) * (50.0f * movementSpeed);
			ch.rotationSpeed += nudge;

			// slowly return steering to center so it doesn't spin forever
			ch.rotationSpeed *= 0.9f;

			// Hard limit on how fast it can spin
			ch.rotationSpeed = clamp(ch.rotationSpeed, -limit, limit);
		}

		ch.angle += ch.rotationSpeed * dt;

		if (ch.angle > 360.f) ch.angle -= 360.f;
		if (ch.angle < 0.f) ch.angle += 360.f;

		// Move Position
		float rad = ch.angle * (M_PI / 180.0f);
		ch.x += std::cos(rad) * movementSpeed * dt;
		ch.y += std::sin(rad) * movementSpeed * dt;

		// Bounce
		// We reflect the angle when hitting a wall.
		if (ch.x > 5.0f) {
			ch.x = 5.0f;
			ch.angle = 180.0f - ch.angle;
			ch.rotationSpeed *= -0.5f;// Lose some turning energy on impact
		} else if (ch.x < -5.0f) {
			ch.x = -5.0f;
			ch.angle = 180.0f - ch.angle;
			ch.rotationSpeed *= -0.5f;
		}

		if (ch.y > 5.0f) {
			ch.y = 5.0f;
			ch.angle = 360.0f - ch.angle;
			ch.rotationSpeed *= -0.5f;
		} else if (ch.y < -5.0f) {
			ch.y = -5.0f;
			ch.angle = 360.0f - ch.angle;
			ch.rotationSpeed *= -0.5f;
		}

		outputs[X_OUTPUT].setVoltage(ch.x, c);
		outputs[Y_OUTPUT].setVoltage(ch.y, c);
	}
}

struct VectorDriverWidget : ModuleWidget {
	VectorDriverWidget(VectorDriver *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/VxyModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		//addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5f-HALF_KNOB_MED, 150), module, VectorDriver::SPEED_PARAM));

		//addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, VectorDriver::VEC_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5f-HALF_PORT, 300), module, VectorDriver::Y_OUTPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5f-HALF_PORT, 250), module, VectorDriver::X_OUTPUT));

	}
};

Model *modelVectorDriver = createModel<VectorDriver, VectorDriverWidget>("Vector");