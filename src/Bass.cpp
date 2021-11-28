#include "Autinn.hpp"
#include <cmath>
//#include "dsp/digital.hpp"
//#include "dsp/resampler.hpp"
//#include <iostream>
//#include <string>

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

#define ATTACK_VCA 0.003f // various sources. Used for VCA as declicker.
#define ATTACK_VCF 0.0f // various sources. Used for VCF.
#define ATTACK_VCF_ACCENT 0.050f //   DECAY_VCF_ACCENT/3
#define PEAK_ACCENT 0.000f // Used only for VCF.
#define DECAY_VCF_ACCENT 0.200f //
#define DECAY_VCF_MIN 0.200f // from NAM and other sources
#define DECAY_VCF_MAX 2.000f // from NAM and other sources
#define DECAY_MAX_VCA 3.500f // http://www.ladyada.net:3s devilfish_say:2s devilfishManual:3-4s (note that when Roland said a number it was to when it had reached 10%, not 0%)
#define DECAY_VCA_NOTE_END 0.008f // just a declicker
#define DECAY_VCA_EXP true // exp or linear VCA decay (looking at waveforms of TB-303 it looks linear) (some sources say exp though)
#define DECAY_VCF_EXP true  // exp or linear VCF decay
#define CUTOFF_KNOB_MIN 150.0f // Multiple sources: 300.
#define CUTOFF_KNOB_MAX 4500.0f// multiple sources: 2400
#define CUTOFF_MIN 20.0f // Min absolute that can be asked of the filter.
#define CUTOFF_MAX 18000.0f // Max absolute that can be asked of the filter.
#define CUTOFF_RANGE_FOR_ENVELOPE 4500.0f // ENV MOD range
#define CUTOFF_MAX_STACKING 2.0f // Max normalized value that stacked VCF envelope will go up to
#define VCA_MAX_STACKING 1.60f // Max normalized value that stacked VCA envelope will go up to
#define CUTOFF_OLD_FACTOR 1.0f // When stacking VCF envelopes old envelopes gets multiplied by this
#define SATURATION_VOLT 12.0f
#define ACCENT_ENVELOPE_VCA_FACTOR 0.40f // source: largely doubled, but keeping to 160% so not to reach 10-12V. If OCS input is higher than 7.5V it can result in hard clipping of accented notes.
//#define CUTOFF_ENVELOPE_LOWERING 0.333f // gimmick
//#define OLD_DECAY_TIME_FACTOR 0.5f
#define ACCENT_KNOB_MINIMUM 0.25f // makes no sense to have it at 0.0 then what is the point of accent..
#define RESONANCE_MAX 1.25f
//#define SLEW_MIN 5.0f // 5=200ms attack
//#define SLEW_MAX 333.333f // slightly higher than required for linear 3ms attack
#define INPUT_TO_CAPACITOR 0.05f
#define VCV_TO_MOOG 0.18f
// SLEW, CUTOFF_OLD_FACTOR, OLD_DECAY_TIME_FACTOR and CUTOFF_ENVELOPE_LOWERING not used atm.

static const int oversample = 2;

struct Bass : Module {
	enum ParamIds {
		CUTOFF_PARAM,
		ENVMOD_PARAM,
		RESONANCE_PARAM,
		ACCENT_PARAM,
		ENV_DECAY_PARAM,
		CV_CUTOFF_PARAM,
		CV_RESONANCE_PARAM,
		CV_DECAY_PARAM,
		CV_ENVMOD_PARAM,
		BUTTON_PARAM,
//		DECAY2_PARAM,
//		DECAY3_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		NOTE_GATE_INPUT,
		ACCENT_GATE_INPUT,
		OSC_INPUT,
		CV_CUTOFF_INPUT,
		CV_RESONANCE_INPUT,
		CV_DECAY_INPUT,
		CV_ENVMOD_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		BASS_OUTPUT,
		//ENV_VCA_OUTPUT,
		//ENV_VCF_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		A_LIGHT,
		B_LIGHT,
		C_LIGHT,
		D_LIGHT,
		E_LIGHT,
		A2_LIGHT,
		B2_LIGHT,
		C2_LIGHT,
		D2_LIGHT,
		GATE_LIGHT,
		TRIG_LIGHT,
		GAIN_LIGHT,
		NUM_LIGHTS
	};

	//float V_t = 1;//1.25f;
	float V_t = 0.026f*2.0f;
	float x = 0.0f;
	float Gres = 1.0f;
	float input_cutoff = 0.0f;
	float F_c = 0.0f;
	float F_s = 0.0f;
	float g = 0.0f; // tuning parameter
	//float g2 = 0.0f; // tuning parameter for first pole
	float y_a = 0.0f;
	float y_b = 0.0f;
	float y_c = 0.0f;
	float y_d = 0.0f;
	float W_a = 0.0f;
	float W_b = 0.0f;
	float W_c = 0.0f;

	float y_a_prev = 0.0f;
	float y_b_prev = 0.0f;
	float y_c_prev = 0.0f;
	float y_d_prev = 0.0f;
	float y_d_prev_prev = 0.0f;

	float W_a_prev = 0.0f;
	float W_b_prev = 0.0f;
	float W_c_prev = 0.0f;

	dsp::Upsampler<oversample, 8> upsampler = dsp::Upsampler<oversample, 8>(0.9f);
	dsp::Decimator<oversample, 8> decimator = dsp::Decimator<oversample, 8>(0.9f);

	bool gate_prev = false;
	float minimum = 0.0001f;
	bool accentBool = false;

	unsigned number_vca = 1;
	unsigned mode_vca = 3;
	unsigned target_vca = 0;
	float current_vca = minimum;
	float factor_vca = 0.0f;

	int number_cutoff = 1;//must not be unsigned as used in minus operation where it might get below 0
	unsigned mode_cutoff = 3;
	int target_cutoff = 0;
	float current_cutoff = minimum;
	float factor_cutoff = 0.0f;
	float cutoff_env_prev = 0.0f;

	dsp::SchmittTrigger schmittGate;
	//dsp::SchmittTrigger schmittAccent;
	float note_prev = 0.0f;

	//float tim = 0.0f;
	bool gateInput = true;
	bool button_prev = false;
	
	float priority = 1.0f;
	
	float accentAttackBase = 0.0f;
	float accentAttackPeak = 0.0f;
	float accent_env_vca_factor = 0.0f;
	
	
	//int counter = 17;
	//int fast_counter = 0;

	//std::cerr <<     "Start Bass Test\n";

	Bass() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Bass::CUTOFF_PARAM, 0.0f, 1.0f, 0.25f, "Cutoff"," Hz",CUTOFF_KNOB_MAX/CUTOFF_KNOB_MIN, CUTOFF_KNOB_MIN);
		configParam(Bass::RESONANCE_PARAM, 0.0f, RESONANCE_MAX, 0.85f, "Resonance", "%", 0.0f, 100.0f);
		configParam(Bass::ENV_DECAY_PARAM, DECAY_VCF_MIN, DECAY_VCF_MAX, (DECAY_VCF_MIN+DECAY_VCF_MAX)*0.5f, "Decay", " ms", 0.0f, 1000.0f);
		configParam(Bass::ENVMOD_PARAM, 0.0f, 1.0f, 0.25f, "EnvMod", " Hz", 0.0f, CUTOFF_RANGE_FOR_ENVELOPE);
		configParam(Bass::ACCENT_PARAM, ACCENT_KNOB_MINIMUM, 1.0f, 0.75f, "Accent", "%", 0.0f, 100.0f);
		configParam(Bass::CV_CUTOFF_PARAM, 0.0f, 0.2f, 0.0f, "Cutoff CV", "%", 0.0f, 500.0f);
		configParam(Bass::CV_RESONANCE_PARAM, 0.0f, RESONANCE_MAX/5.0f, 0.0f, "Resonance CV", "%", 0.0f, 100.0f*1.0f/(RESONANCE_MAX/5.0f));
		configParam(Bass::CV_DECAY_PARAM, 0.0f, (DECAY_VCF_MAX-DECAY_VCF_MIN)*0.2f, 0.0f, "Decay CV", "%", 0.0f, 100.0f*1.0f/((DECAY_VCF_MAX-DECAY_VCF_MIN)*0.2f));
		configParam(Bass::CV_ENVMOD_PARAM, 0.0f, 0.2f, 0.0f, "EnvMod CV", "%", 0.0f, 500.0f);
		configParam(Bass::BUTTON_PARAM, 0, 1, 0, "Toggle Gate or Trigger");
		//configParam(Bass::DECAY2_PARAM, 0.003, 0.050, 0.003, "");
		//configParam(Bass::DECAY3_PARAM, 0.200, 0.500, 0.350, "");

		//configSwitch(Bass::BUTTON_PARAM, 0, 1, 0, "Toggle Gate or Trigger", {"Gate", "Trigger"});

		configBypass(Bass::OSC_INPUT, Bass::BASS_OUTPUT);

		configInput(Bass::NOTE_GATE_INPUT, "Gate/Trigger");
		configInput(Bass::ACCENT_GATE_INPUT, "Accent Gate");
		configInput(Bass::OSC_INPUT, "Oscillator tone");
		configInput(Bass::CV_CUTOFF_INPUT, "Cutoff CV");
		configInput(Bass::CV_RESONANCE_INPUT, "Resonance CV");
		configInput(Bass::CV_DECAY_INPUT, "Decay CV");
		configInput(Bass::CV_ENVMOD_INPUT, "ENVMOD CV");

		configOutput(Bass::BASS_OUTPUT, "Audio output");

		configLight(Bass::A_LIGHT, "VCA ENV Attack Phase");
		configLight(Bass::B_LIGHT, "VCA ENV Sustain Phase (not used)");
		configLight(Bass::C_LIGHT, "VCA ENV Decay Phase");
		configLight(Bass::D_LIGHT, "VCA ENV Ended Phase");
		configLight(Bass::E_LIGHT, "Accent active");
		configLight(Bass::A2_LIGHT, "Filter ENV Attack Phase");
		configLight(Bass::B2_LIGHT, "Filter ENV Sustain Phase (not used)");
		configLight(Bass::C2_LIGHT, "Filter ENV Decay Phase");
		configLight(Bass::D2_LIGHT, "Filter ENV Ended Phase");
		configLight(Bass::GATE_LIGHT, "Input function as gate");
		configLight(Bass::TRIG_LIGHT, "Input function as trigger");
		configLight(Bass::GAIN_LIGHT, "Warning that oscillator input has too big magnitude (7+ Voltage)");
	}

	float vca_env(bool gate,float note, float resonance,float knob_accent,float cutoff_env);
	float filter_env(bool gate,float note,float decay_cutoff_time, float accent, float r, float knob_accent);
	float acid_filter(float in, float r, float cutoff);
	float attackCurve(float x, unsigned target);
	float accentAttackCurve(float x);
	float toExp(float x, float min, float max);
	//float slew(float value, float resonance_knob);
	void process(const ProcessArgs &args) override;

	json_t *dataToJson() override {
		json_t *root = json_object();
		json_object_set_new(root, "gateInput", json_boolean(gateInput));
		json_object_set_new(root, "Gcomp", json_real((double) priority));
		return root;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *ext = json_object_get(rootJ, "gateInput");
		if (ext)
			gateInput = json_boolean_value(ext);
		json_t *ext2 = json_object_get(rootJ, "Gcomp");
		if (ext2)
			priority = (float) json_number_value(ext2);
	}

	void onReset(const ResetEvent& e) override {
		priority = 1.0f;
		gateInput = true;
		Module::onReset(e);
	}

	void onRandomize(const RandomizeEvent& e) override {
		Module::onRandomize(e);
		gateInput = bool(rand() % static_cast<int>(2));// min + (rand() % static_cast<int>(max - min + 1)) [including min and max]
	}
};

/*
declick attack only [done]
no sustain, only decay [done]
decay stopped when note released? [guess no]
when notes release, does vca cut also? [guess yes]
more envmod lowers cutoff env (gimmick circuit) [done, but removed again]
at max envmod acc notes are not higher pitched [done, but removed again]
the accented doubling of cutoff_env is slewed (max voltage change/time), the more resonance the more slew (devil fish say its the acc env on top of vca that is slewed) [faked]
env building on unfinished decay is the acc filter env. (devil fish) [done]
vca decay and attack is fixed [done]

TODO:

Make more params for a Bass+ module:
- vca decay
- attacks
- accent decay
Make first filter pole's cutoff be 1 octave higher like in the TB-303 (use the float g2) Maybe means increasing the max resonance again. https://www.kvraudio.com/forum/viewtopic.php?f=33&t=257220
- Tried it, did not sound good, this needs to be done proper if done, and all filter tunings redone.
Gimmick for VCF lowering


From devilfish manual:  (devilfish was a modified TB-303 that has more knobs and greater range on the knobs)
======================
slide: 60 ms   (df 60-360 ms)
attack non-accent: 3ms with 4ms delay (df 0.3-30 ms, 0.5ms delay)
vol decay: 3-4 s (df 16-3000 ms)
filter decay: 200-2000 ms   (df 30-3000 ms)
filter decay accent: 200 ms (df 30-3000 ms)
cutoff max: 5Khz minus 1 octave (df 5 Khz)
decay non-accent: 16 ms – 8 ms of normal volume and 8 ms of linear decay (df exp decay)

Questions
=========
Was there any sustain on VCA or VCF? What happens when the note gate closes? [closed]
What was VCF attack for accent? And was it linear, exp or something else? [soft, not linear]
What was ENV MOD range? Was it absolute or a fraction of current cutoff setting.
Was the slide really at beginning of next note and 60ms?

Some of the sources used:
- Devilfish manual and webpage
- Various online webpages and forums
- x0xb0x info from http://www.ladyada.net
- Manuals from various TB-303 clones

**/



void Bass::process(const ProcessArgs &args) {
	// Implements a Bass Synth ala. Roland TB-303.

	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[BASS_OUTPUT].isConnected()) {
		return;
	}

	if (params[BUTTON_PARAM].getValue() > 0.0f and !button_prev) {
		gateInput = !gateInput;
	}

	float osc = inputs[OSC_INPUT].getVoltage();
	float resonance = clamp(params[RESONANCE_PARAM].getValue()+inputs[CV_RESONANCE_INPUT].getVoltage()*params[CV_RESONANCE_PARAM].getValue(),0.0f,RESONANCE_MAX);
	float knob_cutoff = clamp(params[CUTOFF_PARAM].getValue()+inputs[CV_CUTOFF_INPUT].getVoltage()*params[CV_CUTOFF_PARAM].getValue(),0.0f,1.0f);
	float knob_accent = params[ACCENT_PARAM].getValue();
	float knob_envmod = clamp(params[ENVMOD_PARAM].getValue()+inputs[CV_ENVMOD_INPUT].getVoltage()*params[CV_ENVMOD_PARAM].getValue(),0.0f,1.0f);
	float knob_env_decay = clamp(params[ENV_DECAY_PARAM].getValue()+inputs[CV_DECAY_INPUT].getVoltage()*params[CV_DECAY_PARAM].getValue(),DECAY_VCF_MIN,DECAY_VCF_MAX);
	float accent = clamp(inputs[ACCENT_GATE_INPUT].getVoltage(),0.0f,1.0f);
	float note = inputs[NOTE_GATE_INPUT].getVoltage();

	lights[E_LIGHT].value = accentBool;

	/* 0.6:
	dsp::SchmittTrigger::setThresholds(float low, float high) has been removed, and the thresholds are now fixed at 0 and 1.
	Instead, rescale your input if needed with trigger.process(rescale(in, low, high, 0.f, 1.f)).
	**/
	bool gate = schmittGate.process(note);
	//bool accentHyst = schmittAccent.process(accent);

	float cutoff_env = this->filter_env(gate,note, knob_env_decay, accent, clamp(resonance,0.0f,1.0f),knob_accent);//params[DECAY3_PARAM].getValue()
	float vca_env = this->vca_env(gate,note,clamp(resonance,0.0f,1.0f),knob_accent,cutoff_env);//knob_env_decay

	float cutoff_setting = this->toExp(knob_cutoff,CUTOFF_KNOB_MIN,CUTOFF_KNOB_MAX);

	float range = knob_envmod * CUTOFF_RANGE_FOR_ENVELOPE;

	float cutoff_env_final = (cutoff_env * range);//-clamp(range*CUTOFF_ENVELOPE_LOWERING,0.0f,cutoff_setting-CUTOFF_MIN);
	
	float cutoff = cutoff_setting+cutoff_env_final;

	cutoff = clamp(cutoff, CUTOFF_MIN, CUTOFF_MAX);

	float out = this->acid_filter(osc, resonance, cutoff);
	outputs[BASS_OUTPUT].setVoltage(vca_env*out);//this->non_lin_func(vca*out/SATURATION_VOLT)*SATURATION_VOLT;
	//outputs[ENV_VCF_OUTPUT].setVoltage(cutoff_env);
	//outputs[ENV_VCA_OUTPUT].setVoltage(vca_env);
	/*if(inputs[CV_RESONANCE_INPUT].getVoltage() > 0) {
		counter = 0;
	}
	if (gate and !gate_prev and counter == 0) {
		tim = 0.0f;
		std::cerr <<     "Cutoff "+std::to_string(cutoff_setting)+"\n";
		std::cerr <<     "ModEnv "+std::to_string(range)+"\n";
		std::cerr <<     "Decay "+std::to_string(knob_env_decay)+"\n";
		std::cerr <<     "Res "+std::to_string(resonance)+"\n";
		std::cerr <<     "Time,VCF,VCA,Cutoff\n";
		counter += 1;
	}
	if (gate && !gate_prev) {
		counter += 1;
	}**/
	gate_prev = gate;
	/*if (counter <= 32 and fast_counter > 250) {
		fast_counter = 0;
		std::cerr <<     std::to_string(tim)+", "+std::to_string(cutoff_env_filter)+", "+std::to_string(vca)+", "+std::to_string(cutoff)+"\n";
	}
	tim += args.sampleTime;
	fast_counter += 1;**/
	note_prev = note;
	button_prev = params[BUTTON_PARAM].getValue();
	lights[GATE_LIGHT].value = gateInput;
	lights[TRIG_LIGHT].value = !gateInput;
	lights[GAIN_LIGHT].value = clamp(fabs(osc)-7.00f,0.0f,1.0f)*1.0f;//OSC input has too much gain. (7V+)
}

float Bass::attackCurve(float x, unsigned target) {
	return log10(x/target+1.0f)*3.321928f;
}

float Bass::accentAttackCurve(float x) {
	// x and y is normalized. Quadratic rise to 1 from zero.
	return -(x-1.0f)*(x-1.0f)+1.0f;
}


float Bass::toExp(float x, float min, float max) {
	// 0 to 1 to exp range
	return min * exp( x*log(max/min) );
}

/*float Bass::slew(float value, float resonance_knob) {
	float max_change = (1.0f-resonance_knob)*(SLEW_MAX - SLEW_MIN)+SLEW_MIN;
	float change = value - cutoff_env_prev;
	float change_per_sec = fabs(change)/args.sampleTime;
	float factor = 1.0f;
//	max_change += 10.0f* SLEW_MIN * fabs(change);//make it less linear
	if (change_per_sec>max_change and change_per_sec != 0.0f) {
		factor = max_change/change_per_sec;
	}
	cutoff_env_prev = cutoff_env_prev+change*factor;

	tim += args.sampleTime;
//	std::cerr <<     std::to_string(time)+", "+std::to_string(cutoff_env_prev)+"\n";
	return cutoff_env_prev;
}*/

float Bass::vca_env(bool gate, float note,  float resonance, float knob_accent,float cutoff_env) {
	float dt = APP->engine->getSampleTime();
	float level = 0.0f;

	number_vca += 1;

	if (gate && !gate_prev) {
		// start attack
		mode_vca = 0;
		number_vca = 1;
		
		if (accentBool) {
			target_vca = ATTACK_VCA/dt;
			factor_vca = (1.0f-current_vca)/float(target_vca); // linear attack from previous level, to avoid clicking
		} else {
			target_vca = ATTACK_VCA/dt;
			factor_vca = (1.0f-current_vca)/float(target_vca); // linear attack from previous level, to avoid clicking
		}
	} else if (gateInput and mode_vca < 2 and note < 1.0f and note_prev >= 1.0f) {
		// note ended
		mode_vca = 2;
		number_vca = 1;
		target_vca = DECAY_VCA_NOTE_END/dt;
		factor_vca = (current_vca-0.0f) / float(target_vca);
	} else if (mode_vca == 3) {
		// ended decay
		number_vca = 0;// we wont get it too high
		return 0.0f;
	} else if (mode_vca == 0 && number_vca > target_vca) {
		// start decay
		mode_vca = 1;
		number_vca = 1;
		target_vca = DECAY_MAX_VCA/dt;
		if (DECAY_VCA_EXP) {
			factor_vca = 1.0f + ((log(minimum) - log(current_vca)) / float(target_vca));
		} else {
			factor_vca = (current_vca-minimum) / float(target_vca);
		}
	} else if (number_vca > target_vca) {
		// end decay or end note decay
		mode_vca = 3;
	}

	if (mode_vca == 0) {//attack
		level = current_vca+factor_vca;
		current_vca = level;
		lights[A_LIGHT].value = 1;
		lights[B_LIGHT].value = 0;
		lights[C_LIGHT].value = 0;
		lights[D_LIGHT].value = 0;
	} else if (mode_vca == 1) {//decay
		if (DECAY_VCA_EXP) {
			level = current_vca*factor_vca;
		} else {
			level = current_vca-factor_vca;
		}
		current_vca = level;
		lights[A_LIGHT].value = 0;
		lights[B_LIGHT].value = 0;
		lights[C_LIGHT].value = level;
		lights[D_LIGHT].value = 0;
	} else if (mode_vca == 2) {//end note
		level = current_vca-factor_vca;
		current_vca = level;
		lights[A_LIGHT].value = 0;
		lights[B_LIGHT].value = 0;
		lights[C_LIGHT].value = 0;
		lights[D_LIGHT].value = clamp(1-level,0.0f,1.0f);
	} else {// silence
		level = 0.0f;
		current_vca = minimum;
		lights[A_LIGHT].value = 0;
		lights[B_LIGHT].value = 0;
		lights[C_LIGHT].value = 0;
		lights[D_LIGHT].value = 1;
	}
	return level+accent_env_vca_factor*knob_accent*cutoff_env;
}

float Bass::filter_env(bool gate, float note, float knob_env_decay, float accent, float resonance, float knob_accent) {
	float dt = APP->engine->getSampleTime();
	float level = 0.0f;

	number_cutoff += 1;
	
	

	if (gate && !gate_prev) {
		// start attack
		accentBool = accent>=1.0f;
		mode_cutoff = 0;
		float attack_time = float(accentBool)*ATTACK_VCF_ACCENT*resonance+ATTACK_VCF;//params[DECAY2_PARAM].getValue()
		//std::cerr << "Old target = "+std::to_string(dt*old_decay_target)+" ("+std::to_string(target_cutoff)+" , "+std::to_string(number_cutoff)+"\n";
		
		number_cutoff = 1;
		target_cutoff = attack_time/dt;
		
		if(accentBool) {
			accentAttackPeak = clamp(1.0f+float(accentBool)*knob_accent*current_cutoff,0.0f,CUTOFF_MAX_STACKING);
			accentAttackBase = current_cutoff;
			if (1.0f + accentAttackPeak*ACCENT_ENVELOPE_VCA_FACTOR > VCA_MAX_STACKING) {
				// we reduce how much VCF env is added to CVA env, to prevent VCA from going over VCA_MAX_STACKING
				// notice VCA will never reach VCA_MAX_STACKING as the 2 env will peak at different times at max resonance and accent knobs
				accent_env_vca_factor = (VCA_MAX_STACKING-1.0f)/accentAttackPeak;
			} else {
				accent_env_vca_factor = ACCENT_ENVELOPE_VCA_FACTOR;
			}
		} else {
			accent_env_vca_factor = 0.0f;
		}
		//std::cerr <<     "dt             = "+std::to_string(dt)+"\n";
		//std::cerr <<     "Attack time    = "+std::to_string(attack_time)+"\n";
		//std::cerr <<     "Attack base    = "+std::to_string(accentAttackBase)+"\n";
		//std::cerr <<     "Attack peak    = "+std::to_string(accentAttackPeak)+"\n";
		//time = 0.0f;
		//std::cerr <<     "Time, Env\n";

		
	} else if (mode_cutoff > 2) {
		// ended decay
		number_cutoff = 0;// we wont get it too high
		target_cutoff = 0;
		current_cutoff = 0.0f;
		return 0.0f;
	} else if (mode_cutoff == 1 && number_cutoff > target_cutoff) {
		// start decay
		mode_cutoff = 2;
		number_cutoff = 1;
		target_cutoff = (accentBool?DECAY_VCF_ACCENT:knob_env_decay)/dt;
		//std::cerr << "New target = "+std::to_string(decay_cutoff_time)+" ("+std::to_string(target_cutoff)+"\n";
		if(accentBool) {
			
		} else {
			if (DECAY_VCF_EXP) {
				factor_cutoff = 1.0f + ((log(minimum) - log(current_cutoff)) / float(target_cutoff));
			} else {
				factor_cutoff = (current_cutoff-minimum)/ float(target_cutoff);
			}
		}
	} else if (mode_cutoff == 0 && number_cutoff >= target_cutoff) {
		// start top
		mode_cutoff = 1;
		number_cutoff = 1;
		if (accentBool) {
			target_cutoff = PEAK_ACCENT/dt;//holding peak time
		} else {
			target_cutoff = 0;
		}
	} else if (number_cutoff > target_cutoff) {
		// end decay
		if (mode_cutoff == 2 and accentBool and current_cutoff > minimum) {
			// we allow decay to go on beyond DECAY_VCF_ACCENT until it gets to minimum
		} else {
			mode_cutoff += 1;
			number_cutoff = 0;
		}
	}

	if (mode_cutoff == 0) {//attack
		if (accentBool) {
			float fraction = target_cutoff==0?1.0f:float(number_cutoff)/float(target_cutoff);
			level = this->accentAttackCurve(fraction) * (accentAttackPeak - accentAttackBase) + accentAttackBase;
		} else {
			level = 1.0f;//instant
		}
		
		current_cutoff = level;
		
		lights[A2_LIGHT].value = 1;
		lights[B2_LIGHT].value = 0;
		lights[C2_LIGHT].value = 0;
		lights[D2_LIGHT].value = 0;
	} else if (mode_cutoff == 1) {//peak
		level = current_cutoff;
		lights[A2_LIGHT].value = 1;//fake flat curve peak in attack phase
		lights[B2_LIGHT].value = 0;
		lights[C2_LIGHT].value = 0;
		lights[D2_LIGHT].value = 0;
	} else if (mode_cutoff == 2) {//decay
		if(accentBool) {
			float fraction = float(number_cutoff)/float(target_cutoff);
			level = accentAttackPeak * powf(1+fraction*2.0f,-fraction*2.0f);
		} else {
			if (DECAY_VCF_EXP) {
				level = current_cutoff*factor_cutoff;
			} else {
				level = current_cutoff-factor_cutoff;
			}
		}
		current_cutoff = level;
		lights[A2_LIGHT].value = 0;
		lights[B2_LIGHT].value = 0;
		lights[C2_LIGHT].value = level;
		lights[D2_LIGHT].value = 0;
	} else {
		level = 0.0f;
		current_cutoff = minimum;
		number_cutoff = 0;
		target_cutoff = 0;
		lights[A2_LIGHT].value = 0;
		lights[B2_LIGHT].value = 0;
		lights[C2_LIGHT].value = 0;
		lights[D2_LIGHT].value = 1;
	}
	return level;
}

float Bass::acid_filter(float in, float r, float F_c) {// from diagram of resonance of TB-303
	float voltage_drive = VCV_TO_MOOG*INPUT_TO_CAPACITOR;// 0.18 to convert from VCV audio rate voltages. 0.035 to convert from input to voltage over first capacitor.
	in *= voltage_drive;
	F_s   = APP->engine->getSampleRate()*oversample;

	double w_c = double(2.0f*M_PI*F_c/F_s);// cutoff in radians per sample.
	g = V_t * (0.0008116984 + 0.9724111*w_c - 0.5077766*w_c*w_c + 0.1534058*w_c*w_c*w_c);// new auto tuned g for cutoff  4th order: y = 0.00007055354 + 0.9960577*x - 0.6082669*x^2 + 0.286043*x^3 - 0.05393212*x^4
	Gres = 1.15f;//1.037174 + 3.606925*w_c + 7.074555*w_c*w_c - 18.14674*w_c*w_c*w_c + 9.364587*w_c*w_c*w_c*w_c;//auto tuned resonance power for resonance <= 1.0
	//double w_c2 = 2.0f*w_c;// cutoff in radians per sample.
	//float g2 = V_t * (0.0008116984 + 0.9724111*w_c2 - 0.5077766*w_c2*w_c2 + 0.1534058*w_c2*w_c2*w_c2);

	float inInter [oversample];
	float outBuf  [oversample];
	upsampler.process(in, inInter);

	for (int i = 0; i < oversample; i++) {
		x   = inInter[i] - 2.0f*Gres*r*(y_d_prev+y_d_prev_prev-priority*inInter[i]);//unit and a half feedback delay. -inInter[i] is Gcomp, to make passband gain not decrease too much when turning up resonance.

		// 1st transistor stage:
		y_a = y_a_prev+g*(non_lin_func( x/V_t )-W_a_prev);
		W_a = non_lin_func( y_a/V_t );
		// 2nd transistor stage:
		y_b = y_b_prev+g*(W_a-W_b_prev);
		W_b = non_lin_func( y_b/V_t );
		// 3rd transistor stage:
		y_c = y_c_prev+g*(W_b-W_c_prev);
		W_c = non_lin_func( y_c/V_t );
		// 4th transistor stage:
		y_d = y_d_prev+g*(W_c-non_lin_func( y_d_prev/V_t ));

		// record stuff for next step
		y_d_prev_prev = y_d_prev;
		y_a_prev = y_a;
		y_b_prev = y_b;
		y_c_prev = y_c;
		y_d_prev = y_d;

		W_a_prev = W_a;
		W_b_prev = W_b;
		W_c_prev = W_c;
		
		outBuf[i] = y_d;
	}
	float out = decimator.process(outBuf)/voltage_drive;
	if(!std::isfinite(out)) {
		out = 0.0f;
	}
	return out;
}

/*struct PrioMenuItem : MenuItem {
	Bass* _module;
	float _priority;

	PrioMenuItem(Bass* module, const char* label, float prio)
	: _module(module), _priority(prio)
	{
		this->text = label;
	}

	void onAction(EventAction &e) override {
		_module->priority = _priority;
	}

	void process(const ProcessArgs &args) override {
		rightText = _module->priority == _priority ? "✔" : "";
	}
};*/

struct BassWidget : ModuleWidget {
	BassWidget(Bass *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/BassModule.svg")));
		//box.size = Vec(16 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundSmallAutinnKnob>(Vec(12 * RACK_GRID_WIDTH*0.25-HALF_KNOB_SMALL, 1*RACK_GRID_HEIGHT/4-HALF_KNOB_SMALL), module, Bass::CUTOFF_PARAM));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(12 * RACK_GRID_WIDTH*0.75-HALF_KNOB_SMALL, 1*RACK_GRID_HEIGHT/4-HALF_KNOB_SMALL), module, Bass::RESONANCE_PARAM));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(12 * RACK_GRID_WIDTH*0.5-HALF_KNOB_SMALL, 1.625*RACK_GRID_HEIGHT/4-HALF_KNOB_SMALL), module, Bass::ENV_DECAY_PARAM));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(12 * RACK_GRID_WIDTH*0.25-HALF_KNOB_SMALL, 2.25*RACK_GRID_HEIGHT/4-HALF_KNOB_SMALL), module, Bass::ENVMOD_PARAM));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(12 * RACK_GRID_WIDTH*0.75-HALF_KNOB_SMALL, 2.25*RACK_GRID_HEIGHT/4-HALF_KNOB_SMALL), module, Bass::ACCENT_PARAM));

		addInput(createInput<InPortAutinn>(Vec(12 * RACK_GRID_WIDTH*0.25-HALF_PORT, 270-HALF_PORT), module, Bass::ACCENT_GATE_INPUT));
		addInput(createInput<InPortAutinn>(Vec(12 * RACK_GRID_WIDTH*0.75-HALF_PORT, 270-HALF_PORT), module, Bass::NOTE_GATE_INPUT));

		addInput(createInput<InPortAutinn>(Vec(12 * RACK_GRID_WIDTH*0.25-HALF_PORT, 300), module, Bass::OSC_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(12 * RACK_GRID_WIDTH*0.75-HALF_PORT, 300), module, Bass::BASS_OUTPUT));
		
		//addOutput(createOutput<OutPortAutinn>(Vec(12 * RACK_GRID_WIDTH*0.5-HALF_PORT, 270), module, Bass::ENV_VCF_OUTPUT));
		//addOutput(createOutput<OutPortAutinn>(Vec(12 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Bass::ENV_VCA_OUTPUT));

		addChild(createLight<SmallLight<RedLight>>(Vec(12 * RACK_GRID_WIDTH*0.16-6.4252*0.5, RACK_GRID_HEIGHT/5.5), module, Bass::A_LIGHT));
		addChild(createLight<SmallLight<GreenLight>>(Vec(12 * RACK_GRID_WIDTH*0.32-6.4252*0.5, RACK_GRID_HEIGHT/5.5), module, Bass::B_LIGHT));
		addChild(createLight<SmallLight<YellowLight>>(Vec(12 * RACK_GRID_WIDTH*0.48-6.4252*0.5, RACK_GRID_HEIGHT/5.5), module, Bass::C_LIGHT));
		addChild(createLight<SmallLight<RedLight>>(Vec(12 * RACK_GRID_WIDTH*0.66-6.4252*0.5, RACK_GRID_HEIGHT/5.5), module, Bass::D_LIGHT));

		addChild(createLight<SmallLight<BlueLight>>(Vec(12 * RACK_GRID_WIDTH*0.82-6.4252*0.5, RACK_GRID_HEIGHT/6.0), module, Bass::E_LIGHT));

		addChild(createLight<SmallLight<RedLight>>(Vec(12 * RACK_GRID_WIDTH*0.16-6.4252*0.5, RACK_GRID_HEIGHT/6.5), module, Bass::A2_LIGHT));
		addChild(createLight<SmallLight<GreenLight>>(Vec(12 * RACK_GRID_WIDTH*0.32-6.4252*0.5, RACK_GRID_HEIGHT/6.5), module, Bass::B2_LIGHT));
		addChild(createLight<SmallLight<YellowLight>>(Vec(12 * RACK_GRID_WIDTH*0.48-6.4252*0.5, RACK_GRID_HEIGHT/6.5), module, Bass::C2_LIGHT));
		addChild(createLight<SmallLight<RedLight>>(Vec(12 * RACK_GRID_WIDTH*0.66-6.4252*0.5, RACK_GRID_HEIGHT/6.5), module, Bass::D2_LIGHT));

		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.875-HALF_PORT, 1*RACK_GRID_HEIGHT/5-HALF_PORT), module, Bass::CV_CUTOFF_INPUT));
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.875-HALF_PORT, 2*RACK_GRID_HEIGHT/5-HALF_PORT), module, Bass::CV_RESONANCE_INPUT));
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.875-HALF_PORT, 3*RACK_GRID_HEIGHT/5-HALF_PORT), module, Bass::CV_DECAY_INPUT));
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.875-HALF_PORT, 4*RACK_GRID_HEIGHT/5-HALF_PORT), module, Bass::CV_ENVMOD_INPUT));

		addParam(createParam<RoundSmallAutinnKnob>(Vec(16 * RACK_GRID_WIDTH*0.875-HALF_KNOB_SMALL, 1.35*RACK_GRID_HEIGHT/5-HALF_KNOB_SMALL), module, Bass::CV_CUTOFF_PARAM));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(16 * RACK_GRID_WIDTH*0.875-HALF_KNOB_SMALL, 2.35*RACK_GRID_HEIGHT/5-HALF_KNOB_SMALL), module, Bass::CV_RESONANCE_PARAM));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(16 * RACK_GRID_WIDTH*0.875-HALF_KNOB_SMALL, 3.35*RACK_GRID_HEIGHT/5-HALF_KNOB_SMALL), module, Bass::CV_DECAY_PARAM));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(16 * RACK_GRID_WIDTH*0.875-HALF_KNOB_SMALL, 4.35*RACK_GRID_HEIGHT/5-HALF_KNOB_SMALL), module, Bass::CV_ENVMOD_PARAM));

		addChild(createLight<SmallLight<GreenLight>>(Vec(12 * RACK_GRID_WIDTH*0.5-6.4252*0.5, 270-5-6.4252*0.5), module, Bass::GATE_LIGHT));
		addChild(createLight<SmallLight<GreenLight>>(Vec(12 * RACK_GRID_WIDTH*0.5-6.4252*0.5, 270+5-6.4252*0.5), module, Bass::TRIG_LIGHT));
		addChild(createLight<SmallLight<RedLight>>(Vec(12 * RACK_GRID_WIDTH*0.125-6.4252*0.5, 300-6.4252*0.5), module, Bass::GAIN_LIGHT));
		addParam(createParam<RoundButtonSmallAutinn>(Vec(12 * RACK_GRID_WIDTH*0.5+7.5, 270-17.5-HALF_BUTTON_SMALL), module, Bass::BUTTON_PARAM));
	//	addParam(createParam<RoundSmallAutinnKnob>(Vec(12 * RACK_GRID_WIDTH*0.5-HALF_KNOB_SMALL, 2.5*RACK_GRID_HEIGHT/4-HALF_KNOB_SMALL), module, Bass::DECAY2_PARAM));
	//	addParam(createParam<RoundSmallAutinnKnob>(Vec(12 * RACK_GRID_WIDTH*0.5-HALF_KNOB_SMALL, 3.5*RACK_GRID_HEIGHT/4-HALF_KNOB_SMALL), module, Bass::DECAY3_PARAM));
	}
	
	/*void appendContextMenu(Menu* menu) override {
		Bass* a = dynamic_cast<Bass*>(module);
		assert(a);

		menu->addChild(new MenuLabel());
		menu->addChild(new PrioMenuItem(a, "Prioritize passband",  1.0f));
		menu->addChild(new PrioMenuItem(a, "Balanced gain",  0.5f));
		menu->addChild(new PrioMenuItem(a, "Full resonance gain", 0.0f));
	}*/
};

Model *modelBass = createModel<Bass, BassWidget>("Bass");
