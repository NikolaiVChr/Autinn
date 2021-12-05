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

const static int num_mono_channels = 6;

struct Mixer6 : Module {
	enum ParamIds {
		LOW_PARAM,
		MID_PARAM = LOW_PARAM+num_mono_channels,
		HIGH_PARAM = MID_PARAM+num_mono_channels,
		LEVEL = HIGH_PARAM+num_mono_channels,
		PAN = LEVEL+num_mono_channels,
		FX_A = PAN+num_mono_channels,
		FX_B = FX_A+num_mono_channels,
		FXA = FX_B+num_mono_channels,
		FXB,
		LEVEL_MAIN,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		FX_RETURN_L_A = INPUT+num_mono_channels,
		FX_RETURN_R_A,
		FX_RETURN_L_B,
		FX_RETURN_R_B,
		NUM_INPUTS
	};
	enum OutputIds {
		MIXER_OUTPUT_L,
		MIXER_OUTPUT_R,
		FX_SENDA,
		FX_SENDB,
		NUM_OUTPUTS
	};
	enum LightIds {
		VU_FXA_LEFT_LIGHT,
		VU_OUT_LEFT_LIGHT = VU_FXA_LEFT_LIGHT + 8,
		VU_FXB_LEFT_LIGHT = VU_OUT_LEFT_LIGHT + 15,
		VU_FXA_RIGHT_LIGHT = VU_FXB_LEFT_LIGHT + 8,
		VU_OUT_RIGHT_LIGHT = VU_FXA_RIGHT_LIGHT + 8,
		VU_FXB_RIGHT_LIGHT = VU_OUT_RIGHT_LIGHT + 15,
		NUM_LIGHTS   = VU_FXB_RIGHT_LIGHT + 8
	};

	dsp::BiquadFilter lowS[num_mono_channels];
	dsp::BiquadFilter midP[num_mono_channels];
	dsp::BiquadFilter highS[num_mono_channels];

	

	const float Gd = 30.0f;
	const float Pm = 30.0f;
	const float Qp = 0.40f;
	const float Qs = 0.707107f;
	const float c1 = 250.0f;
	const float c2 = 700.0f;
	const float c3 = 2000.0f;

	float low_prev[num_mono_channels];
	float mid_prev[num_mono_channels];
	float high_prev[num_mono_channels];

	const float vuMaxDB = 22.5f;
	const float intervalDB_FX = vuMaxDB/8.0f;
	const float intervalDB_main = vuMaxDB/15.0f;
	dsp::VuMeter2 vuMeterFXA_L;
	dsp::VuMeter2 vuMeterFXA_R;
	dsp::VuMeter2 vuMeterFXB_L;
	dsp::VuMeter2 vuMeterFXB_R;
	dsp::VuMeter2 vuMeterOut;
	dsp::VuMeter2 vuMeterOut2;
	unsigned short int step = 0;

	//vuMeterOut.mode = vuMeterOut.RMS;

	float rate_prev = -1.0f;

	Mixer6() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		for (int ch = 0; ch < num_mono_channels; ch++) {
			configInput(INPUT+ch, "Channel "+std::to_string(ch+1)+" Audio");
			configParam(HIGH_PARAM+ch, 0.5f, Pm, Pm, "Channel "+std::to_string(ch+1)+" EQ High", " dB", 0.0f, 1.0f, -Pm);
			configParam(MID_PARAM+ch, 0.5f, Pm, Pm, "Channel "+std::to_string(ch+1)+" EQ Mid", " dB", 0.0f, 1.0f, -Pm);
			configParam(LOW_PARAM+ch, 0.5f, Pm, Pm, "Channel "+std::to_string(ch+1)+" EQ Low", " dB", 0.0f, 1.0f, -Pm);
			configParam(FX_A+ch, 0.0f, 2.0f, 0.0f, "Channel "+std::to_string(ch+1)+" FX A Send", " dB", -10, 20);
			configParam(FX_B+ch, 0.0f, 2.0f, 0.0f, "Channel "+std::to_string(ch+1)+" FX B Send", " dB", -10, 20);
			configParam(LEVEL+ch, 0.0f, 2.0f, 1.0f, "Channel "+std::to_string(ch+1)+" Level", " dB", -10, 20);
			configParam(PAN+ch, 0.0f, M_PI*0.5f, M_PI*0.25f, "Channel "+std::to_string(ch+1)+" Pan", " ", 0.0f, 2.0f/M_PI, -0.5f);
		}
		configParam(LEVEL_MAIN, 0.0f, 2.0f, 1.0f, "Main Level", " dB", -10, 20);
		configParam(Mixer6::FXA, 0.0f, 2.0f, 1.0f, "FX A To Main", " dB", -10, 20);
		configParam(Mixer6::FXB, 0.0f, 2.0f, 1.0f, "FX B To Main", " dB", -10, 20);
		//configBypass(MIXER_INPUT, MIXER_OUTPUT);
		configInput(FX_RETURN_L_A, "FX A Return Left");
		configInput(FX_RETURN_R_A, "FX A Return Right");
		configOutput(FX_SENDA, "FX A Mono Send");
		configInput(FX_RETURN_L_B, "FX B Return Left");
		configInput(FX_RETURN_R_B, "FX B Return Right");
		configOutput(FX_SENDB, "FX B Mono Send");
		configOutput(MIXER_OUTPUT_L, "Left Audio");
		configOutput(MIXER_OUTPUT_R, "Right Audio");
	}

	void process(const ProcessArgs &args) override;
};

void Mixer6::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V
	step++;
	float rate   = args.sampleRate;

	float fx_send_A = 0;
	float fx_send_B = 0;

	float main_left  = 0;
	float main_right = 0;

	for (int ch = 0; ch < num_mono_channels; ch++) {
		if (!inputs[INPUT+ch].isConnected()) {
			continue;
		}
		float in = inputs[INPUT+ch].getVoltage();
		float low    = params[LOW_PARAM+ch].getValue();
		float mid    = params[MID_PARAM+ch].getValue();
		float high   = params[HIGH_PARAM+ch].getValue();
		
		if (low != Pm || mid != Pm || high != Pm) {
			float out1;
			float out2;
			float out3;

			if (low != low_prev[ch] || mid != mid_prev[ch] || high != high_prev[ch] || rate != rate_prev) {
				lowS[ch].setParameters(lowS[ch].LOWSHELF, c1/rate, Qs, low);
				midP[ch].setParameters(midP[ch].PEAK, c2/rate, Qp, mid);
				highS[ch].setParameters(highS[ch].HIGHSHELF, c3/rate, Qs, high);
			}
			low_prev[ch] = low;
			mid_prev[ch] = mid;
			high_prev[ch] = high;
			out1 = lowS[ch].process(in);
			out2 = midP[ch].process(in);
			out3 = highS[ch].process(in);

			if(std::isfinite(out1) && std::isfinite(out2) && std::isfinite(out3)) {
				float out = (out1+out2+out3)/Gd;
				fx_send_A += params[FX_A+ch].getValue() * out;
				fx_send_B += params[FX_B+ch].getValue() * out;
				main_left  += cos(params[PAN+ch].getValue()) * params[LEVEL+ch].getValue() * out;
				main_right += sin(params[PAN+ch].getValue()) * params[LEVEL+ch].getValue() * out;
			}
		} else{
			fx_send_A += params[FX_A+ch].getValue() * in;
			fx_send_B += params[FX_B+ch].getValue() * in;
			main_left  += cos(params[PAN+ch].getValue()) * params[LEVEL+ch].getValue() * in;
			main_right += sin(params[PAN+ch].getValue()) * params[LEVEL+ch].getValue() * in;
		}
	}
	
	// FX
	outputs[FX_SENDA].setVoltage(fx_send_A);
	outputs[FX_SENDB].setVoltage(fx_send_B);
	float fx_return_left_A  = params[FXA].getValue() * inputs[FX_RETURN_L_A].getVoltage();
	float fx_return_right_A = params[FXA].getValue() * inputs[FX_RETURN_R_A].getVoltage();
	float fx_return_left_B  = params[FXB].getValue() * inputs[FX_RETURN_L_B].getVoltage();
	float fx_return_right_B = params[FXB].getValue() * inputs[FX_RETURN_R_B].getVoltage();

	// Main out
	main_left  = fx_return_left_A  + fx_return_left_B  + main_left;
	main_right = fx_return_right_A + fx_return_right_B + main_right;
	main_left *= params[LEVEL_MAIN].getValue();
	main_right *= params[LEVEL_MAIN].getValue();
	outputs[MIXER_OUTPUT_L].setVoltage(main_left);
	outputs[MIXER_OUTPUT_R].setVoltage(main_right);

	rate_prev = rate;

	// VU meters
	vuMeterFXA_L.process(args.sampleTime, fx_return_left_A * 0.1f);
	vuMeterFXA_R.process(args.sampleTime, fx_return_right_A * 0.1f);
	vuMeterFXB_L.process(args.sampleTime, fx_return_left_B * 0.1f);
	vuMeterFXB_R.process(args.sampleTime, fx_return_right_B * 0.1f);
	vuMeterOut.process(args.sampleTime, main_left * 0.1f);
	vuMeterOut2.process(args.sampleTime, main_right * 0.1f);
	for (int v = 0; step == 512 && v < 15; v++) {
		if (v < 8) {
			lights[VU_FXA_LEFT_LIGHT + 7 - v].setBrightness(vuMeterFXA_L.getBrightness(-intervalDB_FX * (v + 1), -intervalDB_FX * v));
			lights[VU_FXA_RIGHT_LIGHT + 7 - v].setBrightness(vuMeterFXA_R.getBrightness(-intervalDB_FX * (v + 1), -intervalDB_FX * v));
			lights[VU_FXB_LEFT_LIGHT + 7 - v].setBrightness(vuMeterFXB_L.getBrightness(-intervalDB_FX * (v + 1), -intervalDB_FX * v));
			lights[VU_FXB_RIGHT_LIGHT + 7 - v].setBrightness(vuMeterFXB_R.getBrightness(-intervalDB_FX * (v + 1), -intervalDB_FX * v));
		}
		lights[VU_OUT_LEFT_LIGHT + 14 - v].setBrightness(vuMeterOut.getBrightness(-intervalDB_main * (v + 1), -intervalDB_main * v));
		lights[VU_OUT_RIGHT_LIGHT + 14 - v].setBrightness(vuMeterOut2.getBrightness(-intervalDB_main * (v + 1), -intervalDB_main * v));
	}
	if (step == 512) {
		step = 0;
	}
}

struct Mixer6Widget : ModuleWidget {
	Mixer6Widget(Mixer6 *module) {
		setModule(module);
		int moduleWidth  = 32 * RACK_GRID_WIDTH;// height is 380

		setPanel(createPanel(asset::plugin(pluginInstance, "res/Mixer6Module.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(moduleWidth - RACK_GRID_WIDTH*2, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(moduleWidth - RACK_GRID_WIDTH*2, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (int ch = 0; ch < num_mono_channels; ch++) {
			addInput(createInput<InPortAutinn>(        Vec( (ch+1) * 4 * RACK_GRID_WIDTH-HALF_PORT-RACK_GRID_WIDTH*2,        20), module, Mixer6::INPUT+ch));
			addParam(createParam<RoundSmallTyrkAutinnKnob>(Vec( (ch+1) * 4 * RACK_GRID_WIDTH-HALF_KNOB_SMALL-RACK_GRID_WIDTH*2,  60), module, Mixer6::HIGH_PARAM+ch));
			addParam(createParam<RoundSmallTyrkAutinnKnob>(Vec( (ch+1) * 4 * RACK_GRID_WIDTH-HALF_KNOB_SMALL-RACK_GRID_WIDTH*2, 100), module, Mixer6::MID_PARAM+ch));
			addParam(createParam<RoundSmallTyrkAutinnKnob>(Vec( (ch+1) * 4 * RACK_GRID_WIDTH-HALF_KNOB_SMALL-RACK_GRID_WIDTH*2, 140), module, Mixer6::LOW_PARAM+ch));
			addParam(createParam<RoundSmallPinkAutinnKnob>(Vec( (ch+1) * 4 * RACK_GRID_WIDTH-HALF_KNOB_SMALL-RACK_GRID_WIDTH*2, 190), module, Mixer6::FX_A+ch));
			addParam(createParam<RoundSmallPinkAutinnKnob>(Vec( (ch+1) * 4 * RACK_GRID_WIDTH-HALF_KNOB_SMALL-RACK_GRID_WIDTH*2, 230), module, Mixer6::FX_B+ch));
			addParam(createParam<RoundSmallYelAutinnKnob>(Vec( (ch+1) * 4 * RACK_GRID_WIDTH-HALF_KNOB_SMALL-RACK_GRID_WIDTH*2, 280), module, Mixer6::PAN+ch));
			addParam(createParam<RoundSmallAutinnKnob>(Vec( (ch+1) * 4 * RACK_GRID_WIDTH-HALF_KNOB_SMALL-RACK_GRID_WIDTH*2, 330), module, Mixer6::LEVEL+ch));
		}

		// FX to Main knobs
		float fx_to_main_x = moduleWidth*0.87;
		float fx_to_main_y = 180+HALF_KNOB_SMALL;
		addParam(createParam<RoundSmallPinkAutinnKnob>(Vec(-moduleWidth*0.05+fx_to_main_x-HALF_KNOB_SMALL, fx_to_main_y-HALF_KNOB_SMALL), module, Mixer6::FXA));
		addParam(createParam<RoundSmallPinkAutinnKnob>(Vec(moduleWidth*0.05+fx_to_main_x-HALF_KNOB_SMALL, fx_to_main_y-HALF_KNOB_SMALL), module, Mixer6::FXB));

		//Main Level
		float main_level_x = fx_to_main_x;
		float main_level_y = RACK_GRID_HEIGHT*0.77f;
		addParam(createParam<RoundMediumAutinnKnob>(Vec(main_level_x-HALF_KNOB_MED, main_level_y-HALF_KNOB_MED), module, Mixer6::LEVEL_MAIN));
		
		
		// FX A ports
		addOutput(createOutput<OutPortAutinn>(Vec(moduleWidth*0.81-HALF_PORT, 20), module, Mixer6::FX_SENDA));
		addInput(createInput<InPortAutinn>(Vec(moduleWidth*0.89-HALF_PORT, 20), module, Mixer6::FX_RETURN_L_A));
		addInput(createInput<InPortAutinn>(Vec(moduleWidth*0.95-HALF_PORT, 20), module, Mixer6::FX_RETURN_R_A));

		// FX B ports
		addOutput(createOutput<OutPortAutinn>(Vec(moduleWidth*0.81-HALF_PORT, 70), module, Mixer6::FX_SENDB));
		addInput(createInput<InPortAutinn>(Vec(moduleWidth*0.89-HALF_PORT, 70), module, Mixer6::FX_RETURN_L_B));
		addInput(createInput<InPortAutinn>(Vec(moduleWidth*0.95-HALF_PORT, 70), module, Mixer6::FX_RETURN_R_B));

		// Main ports out
		addOutput(createOutput<OutPortAutinn>(Vec(fx_to_main_x-moduleWidth*0.04-HALF_PORT, 320), module, Mixer6::MIXER_OUTPUT_L));
		addOutput(createOutput<OutPortAutinn>(Vec(fx_to_main_x+moduleWidth*0.04-HALF_PORT, 320), module, Mixer6::MIXER_OUTPUT_R));

		float light_x_pos_main = main_level_x;
		float light_y_pos_main = main_level_y + HALF_KNOB_MED;

		float light_x_pos_a = fx_to_main_x-0.05f*moduleWidth;
		float light_x_pos_b = fx_to_main_x+0.05f*moduleWidth;
		float light_y_pos = fx_to_main_y-HALF_KNOB_SMALL * 2.0f;
		//float light_column_dist = HALF_LIGHT_SMALL * 6.0f;
		float light_y_spacing = HALF_LIGHT_SMALL * 2.0f;
		float light_x_spacing_main = HALF_KNOB_MED*2.0f + HALF_LIGHT_SMALL * 4.0f;
		for (int i = 0; i < 11; i++) {
			addChild(createLight<SmallLight<GreenLight>>(Vec(light_x_pos_main - light_x_spacing_main*0.5f - HALF_LIGHT_SMALL, light_y_pos_main - light_y_spacing * i), module, Mixer6::VU_OUT_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<GreenLight>>(Vec(light_x_pos_main + light_x_spacing_main*0.5f - HALF_LIGHT_SMALL, light_y_pos_main - light_y_spacing * i), module, Mixer6::VU_OUT_RIGHT_LIGHT + i));
		}
		for (int i = 11; i < 14; i++) {			
			addChild(createLight<SmallLight<YellowLight>>(Vec(light_x_pos_main - light_x_spacing_main*0.5f - HALF_LIGHT_SMALL, light_y_pos_main - light_y_spacing * i), module, Mixer6::VU_OUT_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<YellowLight>>(Vec(light_x_pos_main + light_x_spacing_main*0.5f - HALF_LIGHT_SMALL, light_y_pos_main - light_y_spacing * i), module, Mixer6::VU_OUT_RIGHT_LIGHT + i));
		}
		for (int i = 14; i < 15; i++) {			
			addChild(createLight<SmallLight<RedLight>>(Vec(light_x_pos_main - light_x_spacing_main*0.5f - HALF_LIGHT_SMALL, light_y_pos_main - light_y_spacing * i), module, Mixer6::VU_OUT_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<RedLight>>(Vec(light_x_pos_main + light_x_spacing_main*0.5f - HALF_LIGHT_SMALL, light_y_pos_main - light_y_spacing * i), module, Mixer6::VU_OUT_RIGHT_LIGHT + i));
		}
		for (int i = 0; i < 5; i++) {
			addChild(createLight<SmallLight<GreenLight>>(Vec(light_x_pos_a - HALF_LIGHT_SMALL*2 - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Mixer6::VU_FXA_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<GreenLight>>(Vec(light_x_pos_a + HALF_LIGHT_SMALL*2 - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Mixer6::VU_FXA_RIGHT_LIGHT + i));
			addChild(createLight<SmallLight<GreenLight>>(Vec(light_x_pos_b - HALF_LIGHT_SMALL*2 - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Mixer6::VU_FXB_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<GreenLight>>(Vec(light_x_pos_b + HALF_LIGHT_SMALL*2 - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Mixer6::VU_FXB_RIGHT_LIGHT + i));
		}
		for (int i = 5; i < 7; i++) {			
			addChild(createLight<SmallLight<YellowLight>>(Vec(light_x_pos_a - HALF_LIGHT_SMALL*2 - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Mixer6::VU_FXA_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<YellowLight>>(Vec(light_x_pos_a + HALF_LIGHT_SMALL*2 - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Mixer6::VU_FXA_RIGHT_LIGHT + i));
			addChild(createLight<SmallLight<YellowLight>>(Vec(light_x_pos_b - HALF_LIGHT_SMALL*2 - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Mixer6::VU_FXB_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<YellowLight>>(Vec(light_x_pos_b + HALF_LIGHT_SMALL*2 - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Mixer6::VU_FXB_RIGHT_LIGHT + i));
		}
		for (int i = 7; i < 8; i++) {			
			addChild(createLight<SmallLight<RedLight>>(Vec(light_x_pos_a - HALF_LIGHT_SMALL*2 - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Mixer6::VU_FXA_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<RedLight>>(Vec(light_x_pos_a + HALF_LIGHT_SMALL*2 - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Mixer6::VU_FXA_RIGHT_LIGHT + i));
			addChild(createLight<SmallLight<RedLight>>(Vec(light_x_pos_b - HALF_LIGHT_SMALL*2 - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Mixer6::VU_FXB_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<RedLight>>(Vec(light_x_pos_b + HALF_LIGHT_SMALL*2 - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Mixer6::VU_FXB_RIGHT_LIGHT + i));
		}
	}
};

Model *modelMixer6 = createModel<Mixer6, Mixer6Widget>("Mixer6");