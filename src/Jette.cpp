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

	float phase[16] = {};
	float blinkTime = 0.0f;
	int down = 0;
	int shape = 0;

	Jette() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(Jette::BUTTON_PARAM, "Toggle waveform");
		configParam(Jette::PITCH_PARAM, -4.0f, 4.0f, 0.0f, "Frequency"," Hz", 2.0f, dsp::FREQ_C4);
		configParam(Jette::A_PARAM, 0.0f, 1.0f, 1.0f, "Fundamental");
		configParam(Jette::B_PARAM, 0.0f, 1.0f, 1.0f, "Partial 2");
		configParam(Jette::C_PARAM, 0.0f, 1.0f, 1.0f, "Partial 3");
		configParam(Jette::D_PARAM, 0.0f, 1.0f, 1.0f, "Partial 4");
		configParam(Jette::E_PARAM, 0.0f, 1.0f, 1.0f, "Partial 5");
		configParam(Jette::F_PARAM, 0.0f, 1.0f, 1.0f, "Partial 6");
		configParam(Jette::G_PARAM, 0.0f, 1.0f, 1.0f, "Partial 7");
		configParam(Jette::H_PARAM, 0.0f, 1.0f, 1.0f, "Partial 8");
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
		shape = static_cast<int>(random::uniform() * 3.f);// 0,1 or 2
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

	int channels = std::max(1, inputs[PITCH_INPUT].getChannels());
    outputs[BUZZ_OUTPUT].setChannels(channels);
	
	float pitchBase = params[PITCH_PARAM].getValue();

	float sliders[8] = {
		params[A_PARAM].getValue(), params[B_PARAM].getValue(),
		params[C_PARAM].getValue(), params[D_PARAM].getValue(),
		params[E_PARAM].getValue(), params[F_PARAM].getValue(),
		params[G_PARAM].getValue(), params[H_PARAM].getValue()
	};

	float nyquist = args.sampleRate * 0.5f;
	float period = 2.0f*float(M_PI);

	for (int ch = 0; ch < channels; ch++) {
		float pitch = pitchBase + inputs[PITCH_INPUT].getPolyVoltage(ch);
		pitch = clamp(pitch, -4.0f, 6.0f);
		float freq = dsp::FREQ_C4 * std::exp2f(pitch);
	

		float deltaPhase = freq * dt * period;
		phase[ch] += deltaPhase;
		//phase[ch] = fmod(phase[ch], period);
		if (phase[ch] >= period) phase[ch] -= period; // Faster than fmod

		float buzz = 0.0f;
		float p = phase[ch];

		// --- Chebyshev Recursion ---
		if (shape == 0) { // SQUARE
			float s1 = sinf(p);
			float c1 = cosf(p);

			float val_curr = s1;
			float val_prev = 0.0f; // sin(0)

			// 1st Harmonic (Slider A)
			if (freq < nyquist) buzz += sliders[0] * val_curr;

			float two_c1 = 2.0f * c1;
			for (int k = 2; k <= 15; k++) {
				float val_next = two_c1 * val_curr - val_prev;
				val_prev = val_curr;
				val_curr = val_next;

				if (k % 2 != 0) { // Odd harmonics only
					float harmonicFreq = freq * (float)k;
					float amp = 1.0f;
					if (harmonicFreq >= nyquist) {
						break;
					} else if (harmonicFreq > (nyquist - 2000.0f)) {
						// Fade out in the top 2000Hz
						amp = (nyquist - harmonicFreq) / 2000.0f;
					}

					int sliderIdx = (k - 1) / 2;
					buzz += amp * sliders[sliderIdx] * val_curr / (float)k;
				}
			}
			buzz *= 20.0f / float(M_PI);
		} else if (shape == 1) { // TRIANGLE
			float c1 = cos(p);

			float val_curr = c1;
			float val_prev = 1.0f; // cos(0)

			// 1st Harmonic
			if (freq < nyquist) buzz += sliders[0] * val_curr;

			float two_c1 = 2.0f * c1;
			for (int k = 2; k <= 15; k++) {
				float val_next = two_c1 * val_curr - val_prev;
				val_prev = val_curr;
				val_curr = val_next;

				if (k % 2 != 0) { // Odd harmonics only
					float harmonicFreq = freq * (float)k;
					float amp = 1.0f;
					if (harmonicFreq >= nyquist) {
						break;
					} else if (harmonicFreq > (nyquist - 2000.0f)) {
						// Fade out in the top 2000Hz
						amp = (nyquist - harmonicFreq) / 2000.0f;
					}

					int sliderIdx = (k - 1) / 2;
					float div = (float)(k * k); // Falls off as 1/k^2
					buzz += amp * sliders[sliderIdx] * val_curr / div;
				}
			}
			buzz *= 40.0f / float(M_PI * M_PI);

		} else { // SAW
			float s1 = std::sin(p);
			float c1 = std::cos(p);

			float val_curr = s1;
			float val_prev = 0.0f;

			// 1st Harmonic
			if (freq < nyquist) buzz += sliders[0] * val_curr;

			float two_c1 = 2.0f * c1;
			for (int k = 2; k <= 8; k++) {
				float val_next = two_c1 * val_curr - val_prev;
				val_prev = val_curr;
				val_curr = val_next;

				float harmonicFreq = freq * (float)k;
				float amp = 1.0f;
				if (harmonicFreq >= nyquist) {
					break;
				} else if (harmonicFreq > (nyquist - 2000.0f)) {
					// Fade out in the top 2000Hz
					amp = (nyquist - harmonicFreq) / 2000.0f;
				}

				float sign = (k % 2 == 0) ? -1.0f : 1.0f; // Alternating signs
				buzz += amp * sign * sliders[k-1] * val_curr / (float)k;
			}
			buzz *= 10.0f / float(M_PI);
		}
		outputs[BUZZ_OUTPUT].setVoltage(buzz, ch);//approx 10V PP

		if (ch == 0) {
			blinkTime += dt;
			float blinkPeriod = 1.0f/(freq*0.01f);
			blinkTime = std::fmod(blinkTime, blinkPeriod);
			lights[BLINK_LIGHT].value = (blinkTime < blinkPeriod*0.5f) ? 1.0f : 0.0f;
		}
	}
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
