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


struct Oxcart : Module {
	enum ParamIds {
		PITCH_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PITCH_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		BUZZ_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		BLINK_LIGHT,
		NUM_LIGHTS
	};

	float phase = 0.0f;
	float blinkTime = 0.0f;
	dsp::MinBlepGenerator<16,32,float> oxMinBLEP;// 16 zero crossings, x32 oversample

	Oxcart() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(Oxcart::PITCH_PARAM, -3.0f, 3.0f, 0.0f, "Frequency"," Hz", 2.0f, dsp::FREQ_C4);
		configInput(PITCH_INPUT, "1V/Oct CV");
		configOutput(BUZZ_OUTPUT, "Audio");
	}

	void process(const ProcessArgs &args) override;
};

void Oxcart::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[BUZZ_OUTPUT].isConnected()) {
		return;
	}

	float deltaTime = args.sampleTime;

	float pitch = params[PITCH_PARAM].getValue();
	pitch += inputs[PITCH_INPUT].getVoltage();
	pitch = clamp(pitch, -4.0f, 4.0f);
	float freq = dsp::FREQ_C4 * powf(2.0f, pitch);

	float period = 4.0f;
	float deltaPhase = freq * deltaTime * period;
	
	phase += deltaPhase;

	if (phase >= period) {
		phase -= period;
		float crossing = -phase / deltaPhase;
		oxMinBLEP.insertDiscontinuity(crossing, 1.0);
	}
	
    float buzz = -non_lin_func(phase)+oxMinBLEP.process()+0.826795f;
	outputs[BUZZ_OUTPUT].setVoltage(6.0f * buzz);// keep its peaks within approx 5V.
	
	blinkTime += args.sampleTime;
	float blinkPeriod = 1.0f/(freq*0.01f);
	blinkTime = fmod(blinkTime, blinkPeriod);
	lights[BLINK_LIGHT].value = (blinkTime < blinkPeriod*0.5f) ? 1.0f : 0.0f;
}

struct OxcartWidget : ModuleWidget {
	OxcartWidget(Oxcart *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/OxcartModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, Oxcart::PITCH_PARAM));

		addInput(createInput<InPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, Oxcart::PITCH_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Oxcart::BUZZ_OUTPUT));

		addChild(createLight<MediumLight<GreenLight>>(Vec(5 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Oxcart::BLINK_LIGHT));
	}
};

Model *modelOxcart = createModel<Oxcart, OxcartWidget>("Oxcart");