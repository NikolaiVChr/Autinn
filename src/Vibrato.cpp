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

struct Vibrato : Module {
	enum ParamIds {
		FREQ_PARAM,
		WIDTH_PARAM,
		FLANGER_PARAM,
		CV_WIDTH_PARAM,
		CV_FREQ_PARAM,
		CV_FLANGER_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		VIBRATO_INPUT,
		WIDTH_INPUT,
		FREQ_INPUT,
		FLANGER_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		VIBRATO_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	Vibrato() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Vibrato::FREQ_PARAM, 1.0f, 20.0f, 5.0f, "Frequency", " Hz", 0.0f, 1.0f);
		configParam(Vibrato::WIDTH_PARAM, 0.001f, 0.020f, 0.005f, "Width", " ms", 0.0f, 1000.0f);
		configParam(Vibrato::FLANGER_PARAM, 0.0f, 1.0f, 0.0f, "Flanger", "%", 0.0f, 100.0f);
		configParam(Vibrato::CV_FREQ_PARAM, 0.0f, 0.2f, 0.0f, "Frequency CV", "%", 0.0f, 500.0f);
		configParam(Vibrato::CV_WIDTH_PARAM, 0.0f, 0.2f, 0.0f, "Width CV", "%", 0.0f, 500.0f);
		configParam(Vibrato::CV_FLANGER_PARAM, 0.0f, 0.2f, 0.0f, "Flanger CV", "%", 0.0f, 500.0f);
		configBypass(VIBRATO_INPUT, VIBRATO_OUTPUT);
		configInput(WIDTH_INPUT, "Width CV");
		configInput(FREQ_INPUT, "Frequency CV");
		configInput(FLANGER_INPUT, "Flanger CV");
		configInput(VIBRATO_INPUT, "Audio");
		configOutput(VIBRATO_OUTPUT, "Audio");
	}

	// 16 Channels, 16384 samples (Power of 2 for fast masking)
	static const int MAX_CHANNELS = 16;
	static const int BUFFER_SIZE = 16384;
	static const int BUFFER_MASK = BUFFER_SIZE - 1;

	float phase[MAX_CHANNELS] = {};
	float buffer[MAX_CHANNELS][BUFFER_SIZE] = {};
	int writeIndex[MAX_CHANNELS] = {};

	float smoothedWidth[MAX_CHANNELS] = {};

	void process(const ProcessArgs &args) override;
	//float slew(float value);
	int sign(float x);
};

void Vibrato::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[VIBRATO_OUTPUT].isConnected()) {
		return;
	}

	int channels = std::max(1, inputs[VIBRATO_INPUT].getChannels());
	outputs[VIBRATO_OUTPUT].setChannels(channels);

	float freqParam = params[FREQ_PARAM].getValue();
	float widthParam = params[WIDTH_PARAM].getValue();
	float flangerParam = params[FLANGER_PARAM].getValue();

	float cvFreqDepth = params[CV_FREQ_PARAM].getValue() * 19.0f;
	float cvWidthDepth = params[CV_WIDTH_PARAM].getValue() * 0.019f;
	float cvFlangerDepth = params[CV_FLANGER_PARAM].getValue();

	float rate = args.sampleRate;
	if (rate == 0.0f) {
		for (int c = 0; c < channels; c++) {
			outputs[VIBRATO_OUTPUT].setVoltage(0.0f, c);
		}
		return;
	}

	float period = 2.0f*M_PI;

	for (int c = 0; c < channels; c++) {
		// If CV cable is Monophonic, apply it to all Polyphonic voices.
		float freqCV = (inputs[FREQ_INPUT].getChannels() == 1) ? inputs[FREQ_INPUT].getVoltage() : inputs[FREQ_INPUT].getPolyVoltage(c);
		float widthCV = (inputs[WIDTH_INPUT].getChannels() == 1) ? inputs[WIDTH_INPUT].getVoltage() : inputs[WIDTH_INPUT].getPolyVoltage(c);
		float flangerCV = (inputs[FLANGER_INPUT].getChannels() == 1) ? inputs[FLANGER_INPUT].getVoltage() : inputs[FLANGER_INPUT].getPolyVoltage(c);

		float in = inputs[VIBRATO_INPUT].getPolyVoltage(c);

		float freq = clamp(freqParam + cvFreqDepth * freqCV, 1.0f, 20.0f);
		float widthRaw = clamp(widthParam + cvWidthDepth * widthCV, 0.001f, 0.020f);
		float flanger = clamp(flangerParam + cvFlangerDepth * flangerCV, 0.0f, 1.0f);

		// Initialize on first run to avoid starting at 0.0 width
		if (smoothedWidth[c] == 0.0f) smoothedWidth[c] = widthRaw;

		// Calculate smoothing coefficient (Lower Hz = Slower)
		// 5.0f Hz is fast enough to be responsive, but slow enough to kill zipper noise.
		float slew = 5.0f * args.sampleTime;

		// Apply the One-Pole Filter
		smoothedWidth[c] += (widthRaw - smoothedWidth[c]) * slew;

		float width_samples = smoothedWidth[c]*rate; // samples
		float delay_samples = width_samples; // samples

		float deltaPhase = freq * args.sampleTime * period;
		phase[c] += deltaPhase;
		phase[c] = fmod(phase[c], period);

		// Ring Buffer Write
		int wIdx = writeIndex[c];
		buffer[c][wIdx] = in;

		float modulationFactor = sin(phase[c]);
		float tapper = 4.0f+delay_samples+width_samples*modulationFactor;//1 changed to 4 to give room for spline.
		int i = floor(tapper);
		float portion=tapper-i;


		auto getSample = [&](int delay) -> float {
			return buffer[c][(wIdx - delay + BUFFER_SIZE) & BUFFER_MASK];
		};

		float line    = getSample(i);
		float line_m1 = getSample(i - 1);
		float line_m2 = getSample(i - 2);
		float line_m3 = getSample(i - 3);

		//linear
		//float out=line*portion+line_m1*(1.0f-portion);
		//allpass
		//float out = (line+(1.0f-portion)*line_m1-(1.0f-portion)*out_prev);
		//out_prev=out;
		//Spline
		float out = line*pow(portion,3.0f)/6.0f+line_m1*(pow(1.0f+portion,3.0f)-4.0f*pow(portion,3.0f))/6.0f+line_m2*(pow(2.0f-portion,3.0f)-4.0f*pow(1.0f-portion,3.0f))/6.0f+line_m3*pow(1.0f-portion,3.0f)/6.0f;// order: 3rd
		/*
		median.push_front(out);


		if (median.size() == 4) {
			// this is a declicker (moving median filter with window of size 3)
			out = median.back();
			median.pop_back();
			float v1 = median[0];
			float v2 = median[1];
			float v3 = median[2];
			if (fabs(v2-v1) > 0.001 and fabs(v2-v3) > 0.001) {// and this->sign(v2-v1) == this->sign(v2-v3)) {
				median[1] = (v1+v3)*0.5f;
			}
		} else {
			out = 0.0f;
		}**/

		outputs[VIBRATO_OUTPUT].setVoltage(out+in*flanger, c);
		writeIndex[c] = (wIdx + 1) & BUFFER_MASK;
	}
}

int Vibrato::sign (float x) {
	if (x > 0.0f) return 1;
	if (x < 0.0f) return -1;
	return 0;
}

/*
float Vibrato::slew(float value) {//linear slew to prevent static noise when turning width knob.
	float max_change = 12/0.003;//12V per 0.003s = 4000V/sec
	float change = value - lastValue;
	float change_per_sec = fabs(change)/args.sampleTime;
	float factor = 1.0f;

	if (change_per_sec>max_change and change_per_sec != 0.0f) {
		factor = max_change/change_per_sec;
	}
	float newValue = lastValue+change*factor;
	lastValue = newValue;
	return newValue;
}**/

struct VibratoWidget : ModuleWidget {
	VibratoWidget(Vibrato *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/VibratoModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 75), module, Vibrato::FREQ_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(6 * RACK_GRID_WIDTH*0.75-HALF_KNOB_MED, 75), module, Vibrato::WIDTH_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(9 * RACK_GRID_WIDTH*0.8333-HALF_KNOB_MED, 75), module, Vibrato::FLANGER_PARAM));

		addInput(createInput<InPortAutinn>(Vec(6 * RACK_GRID_WIDTH*0.25-HALF_PORT, 140), module, Vibrato::FREQ_INPUT));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(6 * RACK_GRID_WIDTH*0.25-HALF_KNOB_SMALL, 175), module, Vibrato::CV_FREQ_PARAM));

		addInput(createInput<InPortAutinn>(Vec(6 * RACK_GRID_WIDTH*0.75-HALF_PORT, 140), module, Vibrato::WIDTH_INPUT));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(6 * RACK_GRID_WIDTH*0.75-HALF_KNOB_SMALL, 175), module, Vibrato::CV_WIDTH_PARAM));

		addInput(createInput<InPortAutinn>(Vec(9 * RACK_GRID_WIDTH*0.8333-HALF_PORT, 140), module, Vibrato::FLANGER_INPUT));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(9 * RACK_GRID_WIDTH*0.8333-HALF_KNOB_SMALL, 175), module, Vibrato::CV_FLANGER_PARAM));

		addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Vibrato::VIBRATO_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(7.5 * RACK_GRID_WIDTH-HALF_PORT, 300), module, Vibrato::VIBRATO_OUTPUT));
	}
};

Model *modelVibrato = createModel<Vibrato, VibratoWidget>("Vibrato");