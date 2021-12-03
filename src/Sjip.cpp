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

struct Sjip : Module {
	enum ParamIds {
		PITCH_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PITCH_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OSC_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		BLINK_LIGHT,
		NUM_LIGHTS
	};

	float phase = 0.0f;
	float blinkTime = 0.0f;

	Sjip() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Sjip::PITCH_PARAM,  -4.0f, 4.0f, 0.0f, "Frequency"," Hz", 2.0f, dsp::FREQ_C4);
		configInput(PITCH_INPUT, "1V/Oct CV");
		configOutput(OSC_OUTPUT, "Audio");
	}

	void process(const ProcessArgs &args) override;
};

void Sjip::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	//TODO: Need anti-aliasing.

	if (!outputs[OSC_OUTPUT].isConnected()) {
		return;
	}

	float deltaTime = args.sampleTime;

	float pitch = params[PITCH_PARAM].getValue();
	pitch += inputs[PITCH_INPUT].getVoltage();
	pitch = clamp(pitch, -4.0f, 6.0f);
	float freq = dsp::FREQ_C4 * powf(2.0f, pitch);

//	float radius = 5.0f;
//	float slow_period = 0.5f;
	float period = 2.0f*M_PI;//radius*4.0f/slow_period;
	float deltaPhase = freq * deltaTime * period;
	phase += deltaPhase;
	phase = fmod(phase, period);

	//float a = 1.0f;
	float b = 1.0f;
	float c = 1.0f;
	float d = 1.0f;
	float e = 1.0f;
	float f = 1.0f;
	float g = 1.0f;
	float h = 1.0f;

	float nyquist = args.sampleRate*0.5f;
	if (freq * 15.0f > nyquist) {
		h = 0.0f;
		if (freq * 13.0f > nyquist) {
			g = 0.0f;
			if (freq * 11.0f > nyquist) {
				f = 0.0f;
				if (freq * 9.0f > nyquist) {
					e = 0.0f;
					if (freq * 7.0f > nyquist) {
						d = 0.0f;
						if (freq * 5.0f > nyquist) {
							c = 0.0f;
							if (freq * 3.0f > nyquist) {
								b = 0.0f;
							}
						}
					}
				}
			}
		}
	}

	// BesselJ[1, Pi/2] Sin[x] - (BesselJ[1, (3 Pi)/2] Sin[3 x])/ 3 + (BesselJ[1, (5 Pi)/2] Sin[5 x])/ 5 - (BesselJ[1, (7 Pi)/2] Sin[7 x])/ 7 + (BesselJ[1, (9 Pi)/2] Sin[9 x])/9
	// 0.566824088906  -0.281657908751/3  0.211263431004/5  -0.176096934095/7  0.154115070794/9  -0.138723869537/11  0.127177675472/13  -0.118103773801/15
	float y = 0.566824088906*sin(phase)+ b*0.281657908751*sin(3.0f*phase)/3.0f + c*0.211263431004*sin(5.0f*phase)/5.0f + d*0.176096934095*sin(7.0f*phase)/7.0f
			+ e*0.154115070794*sin(9.0f*phase)/9.0f + f*0.138723869537*sin(11.0f*phase)/11.0f + g*0.127177675472*sin(13.0f*phase)/13.0f + h*0.118103773801*sin(15.0f*phase)/15.0f;
	//float y = 1.78073*sin(phase + M_PI/2)+ 0.29494*sin(3*(phase + M_PI/2)) + 0.13274*sin(5*(phase + M_PI/2)) + 0.07903*sin(7*(phase+M_PI/2));
//	if (phase <= period*slow_period) {
//		y = sqrt(radius*radius-(phase*slow_period-radius)*(phase*slow_period-radius));
//	} else {
//		y = -sqrt(radius*radius-(phase*slow_period-radius-period*0.5f*slow_period)*(phase*slow_period-radius-period*0.5f*slow_period));
//	}

	outputs[OSC_OUTPUT].setVoltage(y*10.0f);//10V PP

	blinkTime += deltaTime;
	float blinkPeriod = 1.0f/(freq*0.01f);
	blinkTime = fmod(blinkTime, blinkPeriod);
	lights[BLINK_LIGHT].value = (blinkTime < blinkPeriod*0.5f) ? 1.0 : 0.0;
}

struct SjipWidget : ModuleWidget {
	SjipWidget(Sjip *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SjipModule.svg")));
		//box.size = Vec(5 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, Sjip::PITCH_PARAM));

		addInput(createInput<InPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, Sjip::PITCH_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Sjip::OSC_OUTPUT));

		addChild(createLight<MediumLight<GreenLight>>(Vec(5 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Sjip::BLINK_LIGHT));
	}
};

Model *modelSjip = createModel<Sjip, SjipWidget>("Sjip");