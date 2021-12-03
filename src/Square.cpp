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

static const int oversample = 4;

struct Square : Module {
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
	
	dsp::Decimator<oversample, 8> decimator = dsp::Decimator<oversample, 8>(0.9f);
	
	int szLow = 38;
	int szHigh = 24;
	float squareInLow[38] = {
0.00000f,
0.01900,
0.04038,
0.06176,
0.09026,
0.11876,
0.14727,
0.20428,
0.23753,
0.26603,
0.29454,
0.32779,
0.36342,
0.40380,
0.44181,
0.48931,
0.54157,
0.56532,
0.57482,
0.58432,
0.59857,
0.61283,
0.62233,
0.63895,
0.66033,
0.68646,
0.70784,
0.72684,
0.74584,
0.76485,
0.79810,
0.84561,
0.88836,
0.92637,
0.95012,
0.96437,
0.97387,
1.00000f,

};
	float squareOutLow[38] = {
-0.97516,
-0.62733,
-0.32919,
-0.05590,
 0.16770,
 0.31677,
 0.37888,
 0.37888,
 0.39130,
 0.39130,
 0.34161,
 0.27329,
 0.18012,
 0.08075,
 0.01863,
-0.03106,
-0.06832,
-0.08075,
-0.05590,
 0.98758,
 0.72671,
 0.47826,
 0.25466,
 0.01863,
-0.20497,
-0.34161,
-0.39130,
-0.40994,
-0.40373,
-0.37888,
-0.30435,
-0.20497,
-0.13665,
-0.07453,
-0.04348,
-0.04348,
-0.11801,
-1.00000f,
};

	float squareInHigh[24] = {
0.00000f,
0.04369,
0.07217,
0.12291,
0.18122,
0.25493,
0.29199,
0.34421,
0.39654,
0.40892,
0.42182,
0.43633,
0.48239,
0.52886,
0.56024,
0.59896,
0.63756,
0.69872,
0.72920,
0.75975,
0.80528,
0.85818,
0.91102,
1.00000f,
};

	float squareOutHigh[24] = {
-1.00787,
-0.81890,
-0.60630,
-0.35433,
-0.11024,
 0.08661,
 0.15748,
 0.21260,
 0.25197,
 0.60630,
 0.88976,
 0.96063,
 0.83465,
 0.65354,
 0.48031,
 0.33071,
 0.19685,
 0.06299,
 0.00787,
-0.05512,
-0.11024,
-0.14567,
-0.17323,
-1.00787,
};

	float lowHigh [2] = {20.0f,100.0f};// use the low waveform at 20 hz, the high at 100 Hz.

	Square() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Square::PITCH_PARAM, -4.0f, 4.0f, 0.0f, "Frequency"," Hz", 2.0f, dsp::FREQ_C4);
		configInput(PITCH_INPUT, "1V/Oct CV");
		configOutput(BUZZ_OUTPUT, "Audio");
	}

	float lut (int size, float in [], float out [], float test);
	float range(float value, float valueRangeL, float valueRangeH, float rangeL, float rangeH);
	void process(const ProcessArgs &args) override;
};

float Square::lut (int size, float in [], float out [], float test) {
  int i;
  
  if (test > in[size - 1]) {
    return out[size - 1];
  } else if (test < in[0]) {
    return out[0];
  }

  for (i = 0; i < size - 1; ++i) {
    if (test >= in[i] && test <= in[i + 1]) {

      float o_l   = out[i];
      float i_l   = in[i];
      float i_d = in[i + 1] - in[i];
      float o_d = out[i + 1] - out[i];
      
      if (i_d == 0.0f) {
        return o_l;
      }
      
      return o_l + ((test - i_l) * o_d) / i_d;
    }
  }
  return 20.0f;
}

float Square::range(float value, float valueRangeL, float valueRangeH, float rangeL, float rangeH) {
    return rangeL + (value-valueRangeL)*(rangeH-rangeL)/(valueRangeH-valueRangeL);
}

void Square::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[BUZZ_OUTPUT].isConnected()) {
		return;
	}

	float deltaTime = args.sampleTime/oversample;

	float pitch = params[PITCH_PARAM].getValue();
	pitch += inputs[PITCH_INPUT].getVoltage();
	pitch = clamp(pitch, -4.0f, 5.0f);
	float freq = dsp::FREQ_C4 * powf(2.0f, pitch);

	float period = 1.0f;
	float deltaPhase = freq * deltaTime * period;
	
	float outBuf  [oversample];
	
	for (int i = 0; i < oversample; i++) {
		phase += deltaPhase;
		phase = fmod(phase, period);

		float buzzL = this->lut(szLow, squareInLow, squareOutLow, phase);
		float buzzH = this->lut(szHigh, squareInHigh, squareOutHigh, phase);
	
		//float lowHighValues [2] = {buzzL,buzzH};
		//outBuf[i] = this->lut(2, lowHigh, lowHighValues, freq);
		outBuf[i] = this->range(clamp(freq,lowHigh[0], lowHigh[1]), lowHigh[0], lowHigh[1], buzzL, buzzH);
	}
	outputs[BUZZ_OUTPUT].setVoltage(decimator.process(outBuf) * 5.0f);// keep its peaks within approx 5V.

	blinkTime += args.sampleTime;
	float blinkPeriod = 1.0f/(freq*0.01f);
	blinkTime = fmod(blinkTime, blinkPeriod);
	lights[BLINK_LIGHT].value = (blinkTime < blinkPeriod*0.5f) ? 1.0 : 0.0;
}

struct SquareWidget : ModuleWidget {
	SquareWidget(Square *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SquareModule.svg")));


		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, Square::PITCH_PARAM));

		addInput(createInput<InPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, Square::PITCH_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Square::BUZZ_OUTPUT));

		addChild(createLight<MediumLight<GreenLight>>(Vec(5 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Square::BLINK_LIGHT));
	}
};

Model *modelSquare = createModel<Square, SquareWidget>("Square");