#include "Autinn.hpp"
#include <cmath>
//#include "dsp/resampler.hpp"
//#include "dsp/minblep.hpp"

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

//static const int oversample = 4;

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
	/*float buffer [4] = {0.0f,0.0f,0.0f,0.0f};
	float sixth = 1.0f/6.0f;
	float eleventh = 11.0f/6.0f;
	float third = 1.0f/3.0f;*/
	//dsp::Decimator<oversample, 8> decimator = dsp::Decimator<oversample, 8>(0.9f);
	dsp::MinBlepGenerator<16,32,float> oxMinBLEP;// 16 zero crossings, x32 oversample

	Oxcart() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		//oxMinBLEP.minblep = minblep_16_32;
		//oxMinBLEP.oversample = 32;
		configParam(Oxcart::PITCH_PARAM, -3.0f, 3.0f, 0.0f, "Frequency"," Hz", 2.0f, dsp::FREQ_C4);
		
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

	float deltaTime = args.sampleTime;///oversample;

	float pitch = params[PITCH_PARAM].getValue();
	pitch += inputs[PITCH_INPUT].getVoltage();
	pitch = clamp(pitch, -4.0f, 4.0f);
	float freq = dsp::FREQ_C4 * powf(2.0f, pitch);

	float period = 4.0f;
	float deltaPhase = freq * deltaTime * period;
	
	//float outBuf  [oversample];
	
	//for (int i = 0; i < oversample; i++) {
		phase += deltaPhase;
		//phase = fmod(phase, period);
	if (phase >= period) {
		phase -= period;
		float crossing = -phase / deltaPhase;
		oxMinBLEP.insertDiscontinuity(crossing, 1.0);
	}
	//	outBuf[i] = -non_lin_func(phase)+0.826795f;// the factor is to almost remove DC bias.
	//}
	//anti-alias using polyBlep
	//float t = phase / period;//normalized position in period
	//float dt = deltaPhase / period;// normalized phasestep
/*	float polyBLEP = 0.0f;
	if (periodPosition < dt) {
		float t = periodPosition/dt;//normalized position inside the first step
        //poly = t+t - t*t - 1.0;// wrong
        polyBLEP = t - 0.5f*t*t - 0.5f;
	} else if (periodPosition > 1.0f - dt) {
		float t = (periodPosition-1.0f)/dt;//normalized position inside the last step
        //poly = t+t + t*t + 1.0;// wrong
        polyBLEP = t + 0.5f*t*t + 0.5f;
	}
	//poly *= step;//step = transition size (positive for rising), since its +1.0 in this case we omit this line.

	float buzz = -non_lin_func(phase)+polyBLEP+0.826795f;// the factor is to almost remove DC bias.
*/	
/*	buffer[0] = buffer[1];
	buffer[1] = buffer[2];
	buffer[2] = buffer[3];
	buffer[3] = -non_lin_func(phase)+0.826795f;// the factor is to almost remove DC bias.

	// delay by 4 samples
	// fit the polynomium to the 4 samples available when (dt < periodPosition < dt*2.0f). Replace the samples with the poly.
	if (periodPosition > dt && periodPosition < dt*2.0f) {
		float d = (1.0f-(periodPosition-dt*2.0f))/dt;
		
		float a = -sixth*buffer[0]+0.5f*buffer[1]-0.5f*buffer[2]+sixth*buffer[3];
		float b = buffer[0]-2.5f*buffer[1]+2.0f*buffer[2]-0.5f*buffer[3];
		float c = -eleventh*buffer[0]+3.0f*buffer[1]-1.5f*buffer[2]+third*buffer[3];
		float e = buffer[0];
		
		buffer[0] = a*powf(d,3.0f)+b*powf(d,2.0f)+c*d+e;
		buffer[1] = a*powf(d+1.0f,3.0f)+b*powf(d+1.0f,2.0f)+c*(d+1.0f)+e;
		buffer[2] = a*powf(d+2.0f,3.0f)+b*powf(d+2.0f,2.0f)+c*(d+2.0f)+e;
		buffer[3] = a*powf(d+3.0f,3.0f)+b*powf(d+3.0f,2.0f)+c*(d+3.0f)+e;
    }*/
    /*
    float polyBLAMP = 0.0f;
	if (t < dt) {
		float d = (t)/dt;
		//float d = (dt-t)/dt;
		//polyBLAMP = powf(d,5.0f)/40.0f-powf(d,4.0f)/12.0f+powf(d,2.0f)/3.0f-d/2.0f+7.0f/30.0f;
		polyBLAMP = (3.0f*powf(d,5.0f)-10.0f*powf(d,4.0f)+40.0f*powf(d,2.0f)-60.0f*d/2.0f+28.0f)/120.0f;
    } else if (t < dt*2.0f) {
		float d = (t-dt)/dt;
		//float d = (2.0f*dt-t)/dt;
		//polyBLAMP = -powf(d,5.0f)/120.0f+powf(d,4.0f)/24.0f-powf(d,3.0f)/12.0f+powf(d,2.0f)/12.0f-d/24.0f+1.0f/120.0f;
		polyBLAMP = (-powf(d,5.0f)+5.0f*powf(d,4.0f)-10.0f*powf(d,3.0f)+10.0f*powf(d,2.0f)-5.0f*d+1.0f)/120.0f;
	} else if (t > 1.0f - dt) {
		float d = (dt-1.0f+t)/dt;
		//float d = (dt-dt+1.0f-t)/dt;
        //polyBLAMP = -powf(d,5.0f)/40.0f+powf(d,4.0f)/24.0f+powf(d,3.0f)/12.0f+powf(d,2.0f)/12.0f+d/24.0f+1.0f/120.0f;
        polyBLAMP = (-3.0f*powf(d,5.0f)+5.0f*powf(d,4.0f)+10.0f*powf(d,3.0f)+10.0f*powf(d,2.0f)+5.0f*d+1.0f)/120.0f;
    } else if (t > 1.0f - dt*2.0f) {
    	float d = (2.0f*dt-1.0f+t)/dt;
    	//float d = (dt-2.0f*dt+1.0f-t)/dt;
        polyBLAMP = powf(d,5.0f)/120.0f;
	}*/
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
		//box.size = Vec(5 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

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