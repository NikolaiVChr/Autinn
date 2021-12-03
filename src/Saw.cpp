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

struct Saw : Module {
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

	float phase = 0;
	float blinkTime = 0;

/* experimental cleaner dataset
	int szLow = 28;
	int szHigh = 11;
	float sawInLow[28] = {
0.00000,
0.00798,
0.01367,
0.02431,
0.03228,
0.03874,
0.04823,
0.05963,
0.07482,
0.08887,
0.10292,
0.10938,
0.12002,
0.13445,
0.15192,
0.17622,
0.19863,
0.22560,
0.24915,
0.27877,
0.31447,
0.36005,
0.41170,
0.46639,
0.52260,
0.55678,
0.94113,
1.00000
};
	float sawOutLow[28] = {
-2.9965,
-2.2892,
-1.6712,
-0.8609,
-0.2017,
0.24464,
0.58112,
0.87639,
1.10987,
1.30901,
1.44635,
1.32275,
1.20601,
1.08927,
0.97940,
0.83519,
0.71159,
0.56052,
0.43691,
0.29957,
0.15536,
0.01116,
-0.0849,
-0.1536,
-0.1948,
-0.2085,
-0.1948,
-3.0000
};

	float sawInHigh[11] = {
0.00000,
0.12867,
0.22112,
0.29198,
0.35980,
0.39049,
0.42801,
0.50909,
0.60030,
0.93921,
1.00000
};

	float sawOutHigh[11] = {
-2.99131,
-1.03745,
 0.14231,
 0.94548,
 1.60867,
 1.39538,
 1.23542,
 0.97547,
 0.79282,
 0.17794,
-2.98997
};
**/

	int szLow = 24;
	int szHigh = 19;
	float meanLow  = -0.06;
	float meanHigh = -0.00f;
	float sawInLow[24] = {
	0.00000f,
	0.00750,// was 0.01584
	0.01339,
	0.02158,
	0.02810,
	0.03483,
	0.04400,// was 0.03889
	0.05792,
	0.07295,
	0.08878,
	0.10817,
	0.12805,
	0.15355,
	0.19019,
	0.22191,
	0.26141,
	0.30563,
	0.35714,
	0.41749,
	0.47754,
	0.54201,
	0.59624,
	0.94093,
	1.00000f
	};

	float sawOutLow[24] = {
	-2.99243,
	-2.27939,
	-1.68040,
	-1.14532,
	-0.67516,
	-0.22118,
	 0.24879,
	 0.70377,
	 0.88313,
	 0.99777,
	 1.02363,
	 1.00905,
	 0.93824,
	 0.76307,
	 0.58750,
	 0.38017,
	 0.18942,
	 0.00735,
	-0.09302,
	-0.16913,
	-0.20440,
	-0.20811,
	-0.19639,
	-2.99243
	};

	float sawInHigh[19] = {
	0.00000f,
	0.06107,
	0.10687,
	0.14000,//was 0.12977
	0.17557,
	0.20611,
	0.25954,
	0.31298,
	0.33588,
	0.37405,
	0.41985,
	0.48092,
	0.54198,
	0.60305,
	0.66412,
	0.75573,
	0.84733,
	0.93893,
	1.00000f
	};

	float sawOutHigh[19] = {
	-3.00470,
	-2.30340,
	-1.60333,
	-1.03468,
	-0.52894,
	-0.10542,
	 0.31996,
	 0.61579,
	 0.74720,
	 0.84745,
	 0.89974,
	 0.90469,
	 0.86105,
	 0.78502,
	 0.67661,
	 0.50589,
	 0.35136,
	 0.18064,
	-3.00470
	};

	float lowHigh [2] = {20.0f,100.0f};// use the low waveform at 20 hz, the high at 100 Hz.

	dsp::Decimator<oversample, 8> decimator = dsp::Decimator<oversample, 8>(0.9f);
	
	Saw() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Saw::PITCH_PARAM, -4.0f, 4.0f, 0.0f, "Frequency"," Hz", 2.0f, dsp::FREQ_C4);
		configInput(PITCH_INPUT, "1V/Oct CV");
		configOutput(BUZZ_OUTPUT, "Audio");
	}

	float lut (int size, float in [], float out [], float test);
	float range(float value, float valueRangeL, float valueRangeH, float rangeL, float rangeH);
	void process(const ProcessArgs &args) override;
};

float Saw::lut (int size, float in [], float out [], float test) {
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

float Saw::range(float value, float valueRangeL, float valueRangeH, float rangeL, float rangeH) {
    return rangeL + (value-valueRangeL)*(rangeH-rangeL)/(valueRangeH-valueRangeL);
}

void Saw::process(const ProcessArgs &args) {
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

		// Use the Look-up Table to read the output value
		float buzzL = this->lut(szLow, sawInLow, sawOutLow, phase);
		float buzzH = this->lut(szHigh, sawInHigh, sawOutHigh, phase);
	//	float lowHighValues [2] = {buzzL,buzzH};
	//	float buzz = this->lut(2, lowHigh, lowHighValues, freq);
		float out = 0.0f;
		if (freq < lowHigh[0]) {
			out = (buzzL-meanLow);
		} else if (freq > lowHigh[1]) {
			out = (buzzH-meanHigh);
		} else {
			float buzz = this->range(freq, lowHigh[0], lowHigh[1], buzzL, buzzH);
			float mean = this->range(freq, lowHigh[0], lowHigh[1], meanLow, meanHigh);
			out = (buzz-mean);
		}
		outBuf[i] = out;
	}
	outputs[BUZZ_OUTPUT].setVoltage(decimator.process(outBuf) * 1.666f);// keep its peaks within approx +-5V.

	blinkTime += args.sampleTime;
	float blinkPeriod = 1.0f/(freq*0.01f);
	blinkTime = fmod(blinkTime, blinkPeriod);
	lights[BLINK_LIGHT].value = (blinkTime < blinkPeriod*0.5f) ? 1.0 : 0.0;
}

struct SawWidget : ModuleWidget {
	SawWidget(Saw *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SawModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, Saw::PITCH_PARAM));

		addInput(createInput<InPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, Saw::PITCH_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Saw::BUZZ_OUTPUT));

		addChild(createLight<MediumLight<GreenLight>>(Vec(5 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Saw::BLINK_LIGHT));
	}
};

Model *modelSaw = createModel<Saw, SawWidget>("Saw");