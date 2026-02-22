#include "Autinn.hpp"
#include "Autinn-dsp.hpp"
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

	float phase[16] = {};
	float blinkTime = 0.0f;
	static constexpr int zeroCrossings = 16;
	static constexpr int overSample = 32;
	dsp::MinBlepGenerator<zeroCrossings,overSample,float> oxMinBLEP[16];// 16 zero crossings, x32 oversample
	DCBlocker dcBlocker[16];
	float discontinuity = tanh_fast_high(4.0f);
	float lastSampleRate = 0.0f;
	std::vector<dsp::Decimator<2, 8>> decimators2;
	std::vector<dsp::Decimator<4, 8>> decimators4;

	static int getOversampleAmount(const float sampleRate) {
		if (sampleRate < 50000.0f) return 4;
		if (sampleRate < 100000.0f) return 2;
		return 1;
	}

	Oxcart() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(Oxcart::PITCH_PARAM, -3.0f, 3.0f, 0.0f, "Frequency"," Hz", 2.0f, dsp::FREQ_C4);
		configInput(PITCH_INPUT, "1V/Oct CV");
		configOutput(BUZZ_OUTPUT, "Audio");
		decimators4.resize(16);
		decimators2.resize(16);
		for (auto & filter : dcBlocker) {
			filter.cutoff_hz = 1.0f;
		}
	}

	void onReset(const ResetEvent& e) override {
		for (int c = 0; c < 16; c++) {
			dcBlocker[c].reset();
			phase[c] = 0.0f;
		}
		blinkTime = 0.0f;
	}

	float processSubSample(int ch, float deltaPhase, float period);
	void process(const ProcessArgs &args) override;
};

void Oxcart::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[BUZZ_OUTPUT].isConnected()) {
		lights[BLINK_LIGHT].value = 0.0f;
		return;
	}

	const int oversample = getOversampleAmount(args.sampleRate);
	const float osSampleTime = args.sampleTime / (float)oversample;

	if (lastSampleRate != args.sampleRate) {
		for (auto & filter : dcBlocker) {
			filter.setSampleTime(args.sampleTime);
		}
	}
	lastSampleRate = args.sampleRate;

	int channels = std::max(1, inputs[PITCH_INPUT].getChannels());
    outputs[BUZZ_OUTPUT].setChannels(channels);
	float deltaTime = args.sampleTime;

	float pitchBase = params[PITCH_PARAM].getValue();
	int pitchInputChannels = inputs[PITCH_INPUT].getChannels();
	const float period = 4.0f;

	for (int ch = 0; ch < channels; ch++) {
		float pitch = pitchBase + (pitchInputChannels>ch?inputs[PITCH_INPUT].getPolyVoltage(ch):inputs[PITCH_INPUT].getVoltage());
		pitch = clamp(pitch, -4.0f, 6.0f);
		float freq = dsp::FREQ_C4 * std::exp2f(pitch);//faster
	

		float deltaPhase = freq * osSampleTime * period;

		float finalOut = 0.0f;

		switch (oversample) {
			case 4: {
				float outBuf[4];
				for (float & buf : outBuf) {
					buf = processSubSample(ch, deltaPhase, period);
				}
				finalOut = decimators4[ch].process(outBuf);
				break;
			}
			case 2: {
				float outBuf[2];
				for (float & buf : outBuf) {
					buf = processSubSample(ch, deltaPhase, period);
				}
				finalOut = decimators2[ch].process(outBuf);
				break;
			}
			default: {
				// 1x Bypass
				finalOut = processSubSample(ch, deltaPhase, period);
			}
		}

		const float buzz = dcBlocker[ch].process(finalOut);

		// x4.5 to keep its peak within approx 5V
		// minBLEP will increase peak (x1.15 approx)
		// dcBlocker will reduce it.
		// Used to be x6.0 in pre-v2.6.28, when used offset instead of dcBlocker,
		// so I know this will mess up some patches slightly.
		// Actually, for now I will keep the x6, backward compatibility is important.
		outputs[BUZZ_OUTPUT].setVoltage(6.0f * buzz, ch);

		if (ch == 0) {
            blinkTime += deltaTime;
            float blinkPeriod = 1.0f/(freq*0.01f);
			while (blinkTime >= blinkPeriod) blinkTime -= blinkPeriod;
            lights[BLINK_LIGHT].value = (blinkTime < blinkPeriod*0.5f) ? 1.0f : 0.0f;
        }
	}
}

inline float Oxcart::processSubSample(const int ch, const float deltaPhase, const float period) {
	phase[ch] += deltaPhase;

	if (phase[ch] >= period) {
		phase[ch] -= period;
		const float crossing = -phase[ch] / deltaPhase;
		// since we oversample we don't need to apply a polyBLAMP table (minBLAMP) also,
		// despite there is both a discontinuity (minBLEP fixable) and slope change (minBLAMP fixable).
		oxMinBLEP[ch].insertDiscontinuity(crossing, discontinuity);
	}

	// The core Oxcart wave: inverted tanh ramp + MinBlep residual
	return -tanh_fast_high(phase[ch]) + oxMinBLEP[ch].process();
}

struct OxcartWidget : ModuleWidget {
	OxcartWidget(Oxcart *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/OxcartModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0.0f)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2.0f * RACK_GRID_WIDTH, 0.0f)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2.0f * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(5.0f * RACK_GRID_WIDTH*0.5f-HALF_KNOB_MED, 150.0f), module, Oxcart::PITCH_PARAM));

		addInput(createInput<InPortAutinn>(Vec(5.0f * RACK_GRID_WIDTH*0.5f-HALF_PORT, 200.0f), module, Oxcart::PITCH_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(5.0f * RACK_GRID_WIDTH*0.5f-HALF_PORT, 300.0f), module, Oxcart::BUZZ_OUTPUT));

		addChild(createLight<MediumLight<GreenLight>>(Vec(5.0f * RACK_GRID_WIDTH*0.5f-9.378f*0.5f, 75.0f), module, Oxcart::BLINK_LIGHT));
	}
};

Model *modelOxcart = createModel<Oxcart, OxcartWidget>("Oxcart");
