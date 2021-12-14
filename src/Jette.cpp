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

struct Jette : Module {
	enum ParamIds {
		PITCH_PARAM,
		A_PARAM,
		B_PARAM,
		C_PARAM,
		D_PARAM,
		E_PARAM,
		F_PARAM,
		G_PARAM,
		H_PARAM,
		BUTTON_PARAM,
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
		SQUARE_LIGHT,
		TRIANGLE_LIGHT,
		SAW_LIGHT,
		NUM_LIGHTS
	};

	float phase = 0.0f;
	float blinkTime = 0.0f;
	int down = 0;
	int shape = 0;

	Jette() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(Jette::BUTTON_PARAM, "Toggle waveform");
		configParam(Jette::PITCH_PARAM, -4.0f, 4.0f, 0.0f, "Frequency"," Hz", 2.0f, dsp::FREQ_C4);
		configParam(Jette::A_PARAM, 0.0f, 1.0f, 1.0f, "");
		configParam(Jette::B_PARAM, 0.0f, 1.0f, 1.0f, "");
		configParam(Jette::C_PARAM, 0.0f, 1.0f, 1.0f, "");
		configParam(Jette::D_PARAM, 0.0f, 1.0f, 1.0f, "");
		configParam(Jette::E_PARAM, 0.0f, 1.0f, 1.0f, "");
		configParam(Jette::F_PARAM, 0.0f, 1.0f, 1.0f, "");
		configParam(Jette::G_PARAM, 0.0f, 1.0f, 1.0f, "");
		configParam(Jette::H_PARAM, 0.0f, 1.0f, 1.0f, "");
		configInput(PITCH_INPUT, "1V/Oct CV");
		configLight(BLINK_LIGHT, "Activity");
		configLight(SQUARE_LIGHT, "Square Waveform");
		configLight(TRIANGLE_LIGHT, "Triangle Waveform");
		configLight(SAW_LIGHT, "Saw Waveform");
		configOutput(BUZZ_OUTPUT, "Audio");
	}
	void process(const ProcessArgs &args) override;

	json_t *dataToJson() override {
		json_t *root = json_object();
		json_object_set_new(root, "shape", json_integer((int) shape));
		return root;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *ext = json_object_get(rootJ, "shape");
		if (ext)
			shape = json_integer_value(ext);
	}

	void onReset(const ResetEvent& e) override {
		shape = 0;
		Module::onReset(e);
	}

	void onRandomize(const RandomizeEvent& e) override {
		Module::onRandomize(e);
		shape = rand() % static_cast<int>(3);// min + (rand() % static_cast<int>(max - min + 1)) [including min and max]
	}

};


void Jette::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (params[BUTTON_PARAM].getValue() > 0 && down == 0) {
		shape += 1;
		if (shape > 2) shape = 0;
	}
	down = params[BUTTON_PARAM].getValue();

	lights[SQUARE_LIGHT].value = (shape == 0) ? 1.0 : 0.0;
	lights[TRIANGLE_LIGHT].value = (shape == 1) ? 1.0 : 0.0;
	lights[SAW_LIGHT].value = (shape == 2) ? 1.0 : 0.0;

	if (!outputs[BUZZ_OUTPUT].isConnected()) {
		return;
	}

	float dt = args.sampleTime;

	float pitch = params[PITCH_PARAM].getValue();
	pitch += inputs[PITCH_INPUT].getVoltage();
	pitch = clamp(pitch, -4.0f, 6.0f);
	float freq = dsp::FREQ_C4 * powf(2.0f, pitch);

	float period = 2.0f*M_PI;
	float deltaPhase = freq * dt * period;
	phase += deltaPhase;
	phase = fmod(phase, period);

	float a = params[A_PARAM].getValue();
	float b = params[B_PARAM].getValue();
	float c = params[C_PARAM].getValue();
	float d = params[D_PARAM].getValue();
	float e = params[E_PARAM].getValue();
	float f = params[F_PARAM].getValue();
	float g = params[G_PARAM].getValue();
	float h = params[H_PARAM].getValue();

	float nyquist = args.sampleRate*0.5f;
	if (shape < 2.0f) {
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
	} else {
		if (freq * 8.0f > nyquist) {
			h = 0.0f;
			if (freq * 7.0f > nyquist) {
				g = 0.0f;
				if (freq * 6.0f > nyquist) {
					f = 0.0f;
					if (freq * 5.0f > nyquist) {
						e = 0.0f;
						if (freq * 4.0f > nyquist) {
							d = 0.0f;
							if (freq * 3.0f > nyquist) {
								c = 0.0f;
								if (freq * 2.0f > nyquist) {
									b = 0.0f;
								}
							}
						}
					}
				}
			}
		}
	}
	float buzz = 0.0f;
	if (shape == 0) {
		// square
		buzz = a*sin(phase) + b*sin(3.0f*phase)/3.0f + c*sin(5.0f*phase)/5.0f + d*sin(7.0f*phase)/7.0f + e*sin(9.0f*phase)/9.0f + f*sin(11.0f*phase)/11.0f + g*sin(13.0f*phase)/13.0f + h*sin(15.0f*phase)/15.0f;
		buzz *= 20.0f/M_PI;
	} else if (shape == 1 ) {
		// triangle
		buzz = a*cos(phase) + b*cos(3.0f*phase)/9.0f + c*cos(5.0f*phase)/25.0f + d*cos(7.0f*phase)/49.0f + e*cos(9.0f*phase)/81.0f + f*cos(11.0f*phase)/121.0f + g*cos(13.0f*phase)/169.0f + h*cos(15.0f*phase)/225.0f;
		buzz *= 40.0f/(M_PI*M_PI);
	} else {
		// saw
		buzz = a*sin(phase) - b*sin(2.0f*phase)/2.0f + c*sin(3.0f*phase)/3.0f - d*sin(4.0f*phase)/4.0f + e*sin(5.0f*phase)/5.0f - f*sin(6.0f*phase)/6.0f + g*sin(7.0f*phase)/7.0f - h*sin(8.0f*phase)/8.0f;
		// + sin(9*phase)/9 - sin(10*phase)/10 + sin(11*phase)/11 - sin(12*phase)/12 + sin(13*phase)/13 - sin(14*phase)/14;
		buzz *= 10.0f/M_PI;
	}
	outputs[BUZZ_OUTPUT].setVoltage(buzz);//aprox 10V PP 

	blinkTime += dt;
	float blinkPeriod = 1.0f/(freq*0.01f);
	blinkTime = fmod(blinkTime, blinkPeriod);
	lights[BLINK_LIGHT].value = (blinkTime < blinkPeriod*0.5f) ? 1.0 : 0.0;
}

struct JetteWidget : ModuleWidget {
	JetteWidget(Jette *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/JetteModule.svg")));
		//box.size = Vec(16 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);//????????????????????????? why remove this?

		

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(createLight<SmallLight<YellowLight>>(Vec(5 * RACK_GRID_WIDTH*0.25-6.4252*0.5, 95), module, Jette::SQUARE_LIGHT));
		addChild(createLight<SmallLight<YellowLight>>(Vec(5 * RACK_GRID_WIDTH*0.5-6.4252*0.5, 95), module, Jette::TRIANGLE_LIGHT));
		addChild(createLight<SmallLight<YellowLight>>(Vec(5 * RACK_GRID_WIDTH*0.75-6.4252*0.5, 95), module, Jette::SAW_LIGHT));
		addParam(createParam<RoundButtonAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_BUTTON, 112.5), module, Jette::BUTTON_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(5 * RACK_GRID_WIDTH*0.50-HALF_KNOB_MED, 150), module, Jette::PITCH_PARAM));

		addParam(createParam<AutinnSlider>(Vec(16 * RACK_GRID_WIDTH*0.36-HALF_SLIDER, 60), module, Jette::A_PARAM));
		addParam(createParam<AutinnSlider>(Vec(16 * RACK_GRID_WIDTH*0.44-HALF_SLIDER, 60), module, Jette::B_PARAM));
		addParam(createParam<AutinnSlider>(Vec(16 * RACK_GRID_WIDTH*0.52-HALF_SLIDER, 60), module, Jette::C_PARAM));
		addParam(createParam<AutinnSlider>(Vec(16 * RACK_GRID_WIDTH*0.60-HALF_SLIDER, 60), module, Jette::D_PARAM));
		addParam(createParam<AutinnSlider>(Vec(16 * RACK_GRID_WIDTH*0.68-HALF_SLIDER, 60), module, Jette::E_PARAM));
		addParam(createParam<AutinnSlider>(Vec(16 * RACK_GRID_WIDTH*0.76-HALF_SLIDER, 60), module, Jette::F_PARAM));
		addParam(createParam<AutinnSlider>(Vec(16 * RACK_GRID_WIDTH*0.84-HALF_SLIDER, 60), module, Jette::G_PARAM));
		addParam(createParam<AutinnSlider>(Vec(16 * RACK_GRID_WIDTH*0.92-HALF_SLIDER, 60), module, Jette::H_PARAM));	

		addInput(createInput<InPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, Jette::PITCH_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Jette::BUZZ_OUTPUT));

		addChild(createLight<MediumLight<GreenLight>>(Vec(5 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Jette::BLINK_LIGHT));
	}
};

Model *modelJette = createModel<Jette, JetteWidget>("Jette");