#include "Autinn.hpp"
#include <cmath>
//#include "dsp/resampler.hpp"

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

static const int oversample = 2;

struct Flopper : Module {
	enum ParamIds {
		DIAL_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ONE_INPUT,
		TWO_INPUT,
		CV_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ONE_OUTPUT,
		TWO_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		BLINK_LIGHT,
		NUM_LIGHTS
	};

	float blinkTime = 0;
	
	dsp::Upsampler<oversample, 8> upsampler1 = dsp::Upsampler<oversample, 8>(0.9f);
	dsp::Upsampler<oversample, 8> upsampler2 = dsp::Upsampler<oversample, 8>(0.9f);
	dsp::Decimator<oversample, 8> decimator1 = dsp::Decimator<oversample, 8>(0.9f);
	dsp::Decimator<oversample, 8> decimator2 = dsp::Decimator<oversample, 8>(0.9f);

	Flopper() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Flopper::DIAL_PARAM, -10.0f, 10.0f, 0.0f, "Flop", " Volt",0.0f,1.0f);
		configBypass(ONE_INPUT, ONE_OUTPUT);
		configBypass(TWO_INPUT, TWO_OUTPUT);
		configInput(CV_INPUT, "CV");
		configInput(ONE_INPUT, "First (these are NOT for stereo)");
		configInput(TWO_INPUT, "Second (these are NOT for stereo)");
		configOutput(ONE_OUTPUT, "First (these are NOT for stereo)");
		configOutput(TWO_OUTPUT, "Second (these are NOT for stereo)");
	}

	void process(const ProcessArgs &args) override;
};

void Flopper::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	// Build after an idea by Jim Patchell

	if (!outputs[ONE_OUTPUT].isConnected() && !outputs[TWO_OUTPUT].isConnected()) {
		return;
	}
	
	float inBuf1 [oversample];
	float inBuf2 [oversample];
	float outBuf1  [oversample];
	float outBuf2  [oversample];
	
	upsampler1.process(inputs[ONE_INPUT].getVoltage(), inBuf1);
	upsampler2.process(inputs[TWO_INPUT].getVoltage(), inBuf2);
	
	for (int i = 0; i < oversample; i++) {
		float in1 = inBuf1[i];
		float in2 = inBuf2[i];
		float level = params[DIAL_PARAM].getValue()+inputs[CV_INPUT].getVoltage();//-10 to +10

		float in1upper = clamp(in1-level,0.0f,15.0f);
		float in1lower = clamp(-(in1-level),0.0f,15.0f);
		float in2upper = clamp(in2-level,0.0f,15.0f);
		float in2lower = clamp(-(in2-level),0.0f,15.0f);

		outBuf1[i] = in1upper+level-in2lower;
		outBuf2[i] = in2upper+level-in1lower;
	}

    outputs[ONE_OUTPUT].setVoltage(decimator1.process(outBuf1));
    outputs[TWO_OUTPUT].setVoltage(decimator2.process(outBuf2));

	blinkTime += args.sampleTime;
	blinkTime = fmod(blinkTime, 1.0f);
	lights[BLINK_LIGHT].value = (blinkTime < 0.5f) ? 1.0 : 0.0;
}

struct FlopperWidget : ModuleWidget {
	FlopperWidget(Flopper *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/FlopperModule.svg")));
		//box.size = Vec(5 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInput<InPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 115), module, Flopper::CV_INPUT));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, Flopper::DIAL_PARAM));

		addInput(createInput<InPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.25-HALF_PORT, 200), module, Flopper::ONE_INPUT));
		addInput(createInput<InPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.75-HALF_PORT, 200), module, Flopper::TWO_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.25-HALF_PORT, 300), module, Flopper::ONE_OUTPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.75-HALF_PORT, 300), module, Flopper::TWO_OUTPUT));

		addChild(createLight<MediumLight<GreenLight>>(Vec(5 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Flopper::BLINK_LIGHT));
	}
};

Model *modelFlopper = createModel<Flopper, FlopperWidget>("Flopper");