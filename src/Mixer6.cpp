#include "Autinn.hpp"
#include <cmath>
#include <algorithm>

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
		ENUMS(LOW_PARAM, num_mono_channels),
		ENUMS(MID_PARAM, num_mono_channels),
		ENUMS(HIGH_PARAM, num_mono_channels),
		ENUMS(CHANNEL_LEVEL_PARAM, num_mono_channels),
		ENUMS(PAN_PARAM, num_mono_channels),
		ENUMS(FX_A_SEND_PARAM, num_mono_channels),
		ENUMS(FX_B_SEND_PARAM, num_mono_channels),
		FX_A_TO_MAIN_PARAM,
		FX_B_TO_MAIN_PARAM,
		LEVEL_MAIN,
		ENUMS(MUTE_BUTTON_PARAM, num_mono_channels),
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INPUT,num_mono_channels),
		FX_RETURN_L_A,
		FX_RETURN_R_A,
		FX_RETURN_L_B,
		FX_RETURN_R_B,
		NUM_INPUTS
	};
	enum OutputIds {
		MIXER_OUTPUT_L,
		MIXER_OUTPUT_R,
		FX_SEND_A,
		FX_SEND_B,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(VU_FXA_LEFT_LIGHT,8),
		ENUMS(VU_OUT_LEFT_LIGHT,15),
		ENUMS(VU_FXB_LEFT_LIGHT,8),
		ENUMS(VU_FXA_RIGHT_LIGHT,8),
		ENUMS(VU_OUT_RIGHT_LIGHT,15),
		ENUMS(VU_FXB_RIGHT_LIGHT,8),
		ENUMS(MUTE_LIGHT, 3*num_mono_channels),
		NUM_LIGHTS
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
	int mute_solo_state[num_mono_channels];// -1: mute  0: norm  +1: solo
	bool mute_solo_button_prev[num_mono_channels];
	bool solo = false;

	const float vuMaxDB = 30.0f;
	const float intervalDB_FX = vuMaxDB/8.0f;
	const float intervalDB_main = vuMaxDB/15.0f;
	dsp::VuMeter2 vuMeterFXA_L;
	dsp::VuMeter2 vuMeterFXA_R;
	dsp::VuMeter2 vuMeterFXB_L;
	dsp::VuMeter2 vuMeterFXB_R;
	dsp::VuMeter2 vuMeterOut;
	dsp::VuMeter2 vuMeterOut2;
	unsigned short int step = 0;

	float rate_prev = -1.0f;

	Mixer6() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		for (int ch = 0; ch < num_mono_channels; ch++) {
			configInput(INPUT+ch, "Channel "+std::to_string(ch+1)+" Audio");
			configParam(HIGH_PARAM+ch, 0.5f, Pm, Pm, "Channel "+std::to_string(ch+1)+" EQ High", " dB", 0.0f, 1.0f, -Pm);
			configParam(MID_PARAM+ch, 0.5f, Pm, Pm, "Channel "+std::to_string(ch+1)+" EQ Mid", " dB", 0.0f, 1.0f, -Pm);
			configParam(LOW_PARAM+ch, 0.5f, Pm, Pm, "Channel "+std::to_string(ch+1)+" EQ Low", " dB", 0.0f, 1.0f, -Pm);
			configParam(FX_A_SEND_PARAM+ch, 0.0f, 2.0f, 0.0f, "Channel "+std::to_string(ch+1)+" FX A Send", " dB", -10, 20);
			configParam(FX_B_SEND_PARAM+ch, 0.0f, 2.0f, 0.0f, "Channel "+std::to_string(ch+1)+" FX B Send", " dB", -10, 20);
			configParam(CHANNEL_LEVEL_PARAM+ch, 0.0f, 2.0f, 1.0f, "Channel "+std::to_string(ch+1)+" Level", " dB", -10, 20);
			configParam(PAN_PARAM+ch, 0.0f, M_PI*0.5f, M_PI*0.25f, "Channel "+std::to_string(ch+1)+" Pan", " ", 0.0f, 2.0f/M_PI, -0.5f);
			configLight(MUTE_LIGHT+ch*3, "Mute (red)/Solo (blue)");
			configButton(MUTE_BUTTON_PARAM+ch, "Mute/Solo");
		}
		configParam(LEVEL_MAIN, 0.0f, 2.0f, 1.0f, "Main Level", " dB", -10, 20);
		configParam(Mixer6::FX_A_TO_MAIN_PARAM, 0.0f, 2.0f, 1.0f, "FX A To Main", " dB", -10, 20);
		configParam(Mixer6::FX_B_TO_MAIN_PARAM, 0.0f, 2.0f, 1.0f, "FX B To Main", " dB", -10, 20);
		//configBypass(MIXER_INPUT, MIXER_OUTPUT);
		configInput(FX_RETURN_L_A, "FX A Return Left");
		configInput(FX_RETURN_R_A, "FX A Return Right");
		configOutput(FX_SEND_A, "FX A Mono Send");
		configInput(FX_RETURN_L_B, "FX B Return Left");
		configInput(FX_RETURN_R_B, "FX B Return Right");
		configOutput(FX_SEND_B, "FX B Mono Send");
		configOutput(MIXER_OUTPUT_L, "Left Audio");
		configOutput(MIXER_OUTPUT_R, "Right Audio");

		std::fill_n(mute_solo_button_prev, num_mono_channels, false);
		std::fill_n(mute_solo_state, num_mono_channels, 0);
	}

	json_t *dataToJson() override {
	    json_t *root = json_object();

	    json_t *mute_json_array = json_array();
	    for(int state : mute_solo_state) {
	        json_array_append_new(mute_json_array, json_integer(state));
	    }
	    json_object_set(root, "mute_solo", mute_json_array);
	    json_decref(mute_json_array);

	    return root;
	}

	void dataFromJson(json_t *root) override
	{
	    json_t *mute_json_array = json_object_get(root, "mute_solo");
	    if(mute_json_array) {
			size_t i;
			json_t *json_int;

			json_array_foreach(mute_json_array, i, json_int) {
			    mute_solo_state[i] = json_integer_value(json_int);
			}
	    }
	}

	void handleMuteButtons();

	void process(const ProcessArgs &args) override;
};

void Mixer6::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V
	step++;

	this->handleMuteButtons();

	float rate   = args.sampleRate;

	float fx_send_A = 0;
	float fx_send_B = 0;

	float main_left  = 0;
	float main_right = 0;

	for (int ch = 0; ch < num_mono_channels; ch++) {
		if (!inputs[INPUT+ch].isConnected() || mute_solo_state[ch] == -1 || (solo && mute_solo_state[ch] != 1)) {
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
				fx_send_A += params[FX_A_SEND_PARAM+ch].getValue() * out;
				fx_send_B += params[FX_B_SEND_PARAM+ch].getValue() * out;
				main_left  += cos(params[PAN_PARAM+ch].getValue()) * params[CHANNEL_LEVEL_PARAM+ch].getValue() * out;
				main_right += sin(params[PAN_PARAM+ch].getValue()) * params[CHANNEL_LEVEL_PARAM+ch].getValue() * out;
			}
		} else{
			fx_send_A += params[FX_A_SEND_PARAM+ch].getValue() * in;
			fx_send_B += params[FX_B_SEND_PARAM+ch].getValue() * in;
			main_left  += cos(params[PAN_PARAM+ch].getValue()) * params[CHANNEL_LEVEL_PARAM+ch].getValue() * in;
			main_right += sin(params[PAN_PARAM+ch].getValue()) * params[CHANNEL_LEVEL_PARAM+ch].getValue() * in;
		}
	}
	
	// FX
	outputs[FX_SEND_A].setVoltage(fx_send_A);
	outputs[FX_SEND_B].setVoltage(fx_send_B);
	float fx_return_left_A  = params[FX_A_TO_MAIN_PARAM].getValue() * inputs[FX_RETURN_L_A].getVoltage();
	float fx_return_right_A = params[FX_A_TO_MAIN_PARAM].getValue() * inputs[FX_RETURN_R_A].getVoltage();
	float fx_return_left_B  = params[FX_B_TO_MAIN_PARAM].getValue() * inputs[FX_RETURN_L_B].getVoltage();
	float fx_return_right_B = params[FX_B_TO_MAIN_PARAM].getValue() * inputs[FX_RETURN_R_B].getVoltage();

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

void Mixer6::handleMuteButtons() {
	solo = false;
	for (int ch = 0; ch < num_mono_channels; ch++) {
		bool state = params[MUTE_BUTTON_PARAM+ch].getValue() >= 1.0f;
		
		if (state && !mute_solo_button_prev[ch]) {
			mute_solo_state[ch] = mute_solo_state[ch] - 1;
			if (mute_solo_state[ch] < -1) {
				mute_solo_state[ch] = 1;
			}
		}
		if (mute_solo_state[ch] == 1) {
			solo = true;
			lights[MUTE_LIGHT+ch*3+0].setBrightness( 0.00f);
			lights[MUTE_LIGHT+ch*3+2].setBrightness( 1.00f);
		} else if (mute_solo_state[ch] == -1) {
			lights[MUTE_LIGHT+ch*3+0].setBrightness( 1.00f);
			lights[MUTE_LIGHT+ch*3+2].setBrightness( 0.00f);
		} else {
			lights[MUTE_LIGHT+ch*3+0].setBrightness( 0.00f);
			lights[MUTE_LIGHT+ch*3+2].setBrightness( 0.00f);
		}

		mute_solo_button_prev[ch] = state;
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
			addParam(createParam<RoundSmallPinkAutinnKnob>(Vec( (ch+1) * 4 * RACK_GRID_WIDTH-HALF_KNOB_SMALL-RACK_GRID_WIDTH*2, 190), module, Mixer6::FX_A_SEND_PARAM+ch));
			addParam(createParam<RoundSmallPinkAutinnKnob>(Vec( (ch+1) * 4 * RACK_GRID_WIDTH-HALF_KNOB_SMALL-RACK_GRID_WIDTH*2, 230), module, Mixer6::FX_B_SEND_PARAM+ch));
			addParam(createParam<RoundSmallYelAutinnKnob>(Vec(  (ch+1) * 4 * RACK_GRID_WIDTH-HALF_KNOB_SMALL-RACK_GRID_WIDTH*2, 280), module, Mixer6::PAN_PARAM+ch));
			addParam(createParam<RoundSmallAutinnKnob>(Vec(     (ch+1) * 4 * RACK_GRID_WIDTH-HALF_KNOB_SMALL-RACK_GRID_WIDTH*2, 330), module, Mixer6::CHANNEL_LEVEL_PARAM+ch));
			addChild(createLight<MediumLight<RedGreenBlueLight>>(Vec( (ch+1) * 4 * RACK_GRID_WIDTH-HALF_LIGHT_MEDIUM-RACK_GRID_WIDTH*2, 270-HALF_LIGHT_MEDIUM), module, Mixer6::MUTE_LIGHT + ch*3));
			addParam(createParam<RoundButtonSmallAutinn>(Vec(         (ch+1) * 4 * RACK_GRID_WIDTH-HALF_BUTTON_SMALL-RACK_GRID_WIDTH*0.5, 270-HALF_BUTTON_SMALL), module, Mixer6::MUTE_BUTTON_PARAM + ch));
		}

		// FX to Main knobs
		float fx_to_main_x = moduleWidth*0.87;
		float fx_to_main_y = 180+HALF_KNOB_SMALL;
		addParam(createParam<RoundSmallPinkAutinnKnob>(Vec(-moduleWidth*0.05+fx_to_main_x-HALF_KNOB_SMALL, fx_to_main_y-HALF_KNOB_SMALL), module, Mixer6::FX_A_TO_MAIN_PARAM));
		addParam(createParam<RoundSmallPinkAutinnKnob>(Vec(moduleWidth*0.05+fx_to_main_x-HALF_KNOB_SMALL, fx_to_main_y-HALF_KNOB_SMALL), module, Mixer6::FX_B_TO_MAIN_PARAM));

		//Main Level
		float main_level_x = fx_to_main_x;
		float main_level_y = RACK_GRID_HEIGHT*0.77f;
		addParam(createParam<RoundMediumAutinnKnob>(Vec(main_level_x-HALF_KNOB_MED, main_level_y-HALF_KNOB_MED), module, Mixer6::LEVEL_MAIN));
		
		
		// FX A ports
		addOutput(createOutput<OutPortAutinn>(Vec(moduleWidth*0.81-HALF_PORT, 20), module, Mixer6::FX_SEND_A));
		addInput(createInput<InPortAutinn>(Vec(moduleWidth*0.89-HALF_PORT, 20), module, Mixer6::FX_RETURN_L_A));
		addInput(createInput<InPortAutinn>(Vec(moduleWidth*0.95-HALF_PORT, 20), module, Mixer6::FX_RETURN_R_A));

		// FX B ports
		addOutput(createOutput<OutPortAutinn>(Vec(moduleWidth*0.81-HALF_PORT, 70), module, Mixer6::FX_SEND_B));
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