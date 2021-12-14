#include "Autinn.hpp"
#include <cmath>
#include <iostream>
#include <string>

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

#define ACCENT_ENVELOPE_VCA_OFFSET 0.6f // source: largely doubled, but keeping to 160% so not to reach 12V. If OCS input is higher than 7V it can result in hard clipping of accented notes.
#define ATTACK_VCA 0.003f // various sources. Used for VCA as declicker.
#define ATTACK_VCF 0.0f // various sources. Used for VCF.
#define ATTACK_VCF_ACCENT 0.050f //   Accent attack time when resonance is at 1.0 or higher.
#define PEAK_ACCENT_SUSTAIN 0.000f // Only for debugging
#define DECAY_VCF_ACCENT 0.200f //
#define DECAY_VCF_MIN 0.200f // from NAM and other sources
#define DECAY_VCF_MAX 2.000f // from NAM and other sources
#define DECAY_VCA_SECS 2.500f // http://www.ladyada.net:3s devilfish_say:2s devilfishManual:3-4s (note that when Roland said a number it was to when it had reached 10%, not 0%)
#define DECAY_VCA_ACCENT 1.000f //
#define DECAY_VCA_NOTE_END 0.008f // just a declicker
#define DECAY_VCA_EXP true // exp or linear VCA decay
#define DECAY_VCF_EXP true  // exp or linear VCF decay
#define CUTOFF_ENVELOPE_BIAS 0.3137f // Portion of VCF envelope that is negative.
#define CUTOFF_RANGE_FOR_ENVELOPE 4500.0f // ENV MOD range
#define CUTOFF_ENVMOD_MIN 100.0f // Zero sweep makes no sense
#define CUTOFF_KNOB_MIN 40.0f // Multiple sources: 300.
#define CUTOFF_KNOB_MAX 4500.0f// multiple sources: 2400
#define CUTOFF_MIN 16.0f // Min absolute that can be asked of the filter.
#define CUTOFF_MAX 18000.0f // Max absolute that can be asked of the filter.
#define CUTOFF_MAX_STACKING 3.0f // Max normalized value that stacked VCF envelope will go up to
#define SATURATION_VOLT 12.0f // Not used, users should show discipline and not input too high OSC tone voltages.
#define ACCENT_KNOB_MINIMUM 0.25f // makes no sense to have it at 0.0 then what is the point of accent..
#define RESONANCE_MAX 1.25f
#define INPUT_TO_CAPACITOR 0.05f
#define VCV_TO_MOOG 0.18f
#define EXPECTED_PEAK_INPUT 7.0f // Do not input larger OSC tones, or accented notes might start hard clipping.

static const int oversample2 = 2;
static const int oversample4 = 4;

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

	float V_t = 0.026f*2.0f;
	float x = 0.0f;
	float Gres = 1.0f;
	float input_cutoff = 0.0f;
	float F_c = 0.0f;
	float F_s = 0.0f;
	float g = 0.0f; // tuning parameter
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

	//dsp::Upsampler<oversample, 8> upsampler = dsp::Upsampler<oversample, 8>(0.9f);
	//dsp::Decimator<oversample, 8> decimator = dsp::Decimator<oversample, 8>(0.9f);
	dsp::Upsampler<oversample2, 10> upsampler2;
	dsp::Decimator<oversample2, 10> decimator2;
	dsp::Upsampler<oversample4, 10> upsampler4;
	dsp::Decimator<oversample4, 10> decimator4;

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
	
	// Json saved options ==============
	float priority = 1.0f;// If 1.0f then will compensate for passband lowering at high resonances. If 0.0f then its just the raw filter.
	int current_oversample = 2;// 2 for minimal anti-aliasing or 4 for better if you can spare the CPU time.
	bool tunedResonance = false;// If true then resonance power will be tuned to equal power no matter the cutoff. However for this module it sounds best to have this false.
	bool firstPoleOneOctHigher = false;// For more accurate physical sim of TB-303 filter. However a 24dB transistor filter sounds better than what 303 had, so keeping it at false.
	// =================================
	
	float accentAttackBase = 0.0f;
	float accentAttackPeak = 0.0f;

	long int noteSteps = 0;
	
	
	//int counter = 17;
	//int fast_counter = 0;

	//std::cerr <<     "Start Bass Test\n";

	Bass() {
		//configParam (int paramId, float minValue, float maxValue, float defaultValue, string label="", string unit="", float displayBase=0.f, float displayMultiplier=1.f, float displayOffset=0.f)
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Bass::CUTOFF_PARAM, 0.0f, 1.0f, 0.25f, "Cutoff"," Hz",CUTOFF_KNOB_MAX/CUTOFF_KNOB_MIN, CUTOFF_KNOB_MIN);
		configParam(Bass::RESONANCE_PARAM, 0.0f, RESONANCE_MAX, 0.85f, "Resonance", "%", 0.0f, 100.0f);
		configParam(Bass::ENV_DECAY_PARAM, DECAY_VCF_MIN, DECAY_VCF_MAX, (DECAY_VCF_MIN+DECAY_VCF_MAX)*0.5f, "Decay", " ms", 0.0f, 1000.0f);
		configParam(Bass::ENVMOD_PARAM, 0.0f, 1.0f, 0.25f, "Sweep range", " Hz", 0.0f, CUTOFF_RANGE_FOR_ENVELOPE, CUTOFF_ENVMOD_MIN);
		configParam(Bass::ACCENT_PARAM, ACCENT_KNOB_MINIMUM, 1.0f, 0.75f, "Accent", "%", 0.0f, 100.0f);
		configParam(Bass::CV_CUTOFF_PARAM, 0.0f, 0.2f, 0.0f, "Cutoff CV", "%", 0.0f, 500.0f);
		configParam(Bass::CV_RESONANCE_PARAM, 0.0f, RESONANCE_MAX/5.0f, 0.0f, "Resonance CV", "%", 0.0f, 100.0f*1.0f/(RESONANCE_MAX/5.0f));
		configParam(Bass::CV_DECAY_PARAM, 0.0f, (DECAY_VCF_MAX-DECAY_VCF_MIN)*0.2f, 0.0f, "Decay CV", "%", 0.0f, 100.0f*1.0f/((DECAY_VCF_MAX-DECAY_VCF_MIN)*0.2f));
		configParam(Bass::CV_ENVMOD_PARAM, 0.0f, 0.2f, 0.0f, "EnvMod CV", "%", 0.0f, 500.0f);
		configButton(Bass::BUTTON_PARAM, "Toggle Gate or Trigger");
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

		configOutput(Bass::BASS_OUTPUT, "Audio");

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

	float vca_env(bool gate,float note, float resonance,float knob_accent);
	float vca_env_acc(bool gate,float note, float resonance,float knob_accent);
	float filter_env(bool gate,float note,float decay_cutoff_time, float accent, float r, float knob_accent);
	float acid_filter(float in, float r, float cutoff, int oversample_protected);
	float attackCurve(float x, unsigned target);
	float accentAttackCurve(float x);
	float accentAttackCurveInverse(float y);
	float toExp(float x, float min, float max);
	float accent_env(bool gate, float note, bool accent, float knob_accent);
	void process(const ProcessArgs &args) override;
	void vca_lights(float attack, float sustain, float decay, float end);
	void vcf_lights(float attack, float sustain, float decay, float end);

	json_t *dataToJson() override {
		json_t *root = json_object();
		json_object_set_new(root, "gateInput", json_boolean(gateInput));
		json_object_set_new(root, "oversample", json_integer(current_oversample));
		//json_object_set_new(root, "Gcomp", json_real((double) priority));
		return root;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *ext = json_object_get(rootJ, "gateInput");
		if (ext)
			gateInput = json_boolean_value(ext);
		json_t *ext3 = json_object_get(rootJ, "oversample");
		if (ext3) {
			current_oversample = json_integer_value(ext3);
			if (current_oversample != 2 and current_oversample != 4) {
				current_oversample = 4;
			}
		}
		//json_t *ext2 = json_object_get(rootJ, "Gcomp");
		//if (ext2)
		//	priority = (float) json_number_value(ext2);
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

TODO:

declick attack only [done]
no sustain, only decay [done]
decay stopped when note released? [added toggle button]
more envmod lowers cutoff env (gimmick circuit) [done]
at max envmod acc notes are not higher pitched [done, but removed again]
the accented doubling of cutoff_env is slewed (max voltage change/time), the more resonance the more slew (devil fish say its the acc env on top of vca that is slewed) [faked]
env building on unfinished decay is the acc filter env. (devil fish) [done]
vca decay and attack is fixed [done]
Make more params for a Bass+ module:
- vca decay
- attacks
- accent decay
Make first filter pole's cutoff be 1 octave higher like in the TB-303 (use the float g2) Maybe means increasing the max resonance again. https://www.kvraudio.com/forum/viewtopic.php?f=33&t=257220
- Tried it, did not sound super, this needs to be done proper if done, and all filter tunings redone.
Consider an option for oversampling like Flora has.
Proper slew on accent env on top of VCA [done]
Stacking is only VCF not VCA [done]
Filter env goes negative and gimmick circuit [done]
Accent is gate and can be done at any time

From devilfish manual:  (devilfish was a modified TB-303 that has more knobs and greater range on the knobs)
======================
slide: 60 ms   (df 60-360 ms)
attack non-accent: 3ms with 4ms delay (df 0.3-30 ms, 0.5ms delay)
vca decay: 3-4 s (df 16-3000 ms)
vcf decay: 200-2000 ms   (df 30-3000 ms)
vcf decay accent: 200 ms (df 30-3000 ms)
cutoff max: 5Khz minus 1 octave so about D#7 = 2489hz (df 5 Khz)
decay non-accent: 16 ms – 8 ms of normal volume and 8 ms of linear decay (df exp decay)

Questions
=========
Was there any sustain on VCA or VCF? What happens when the note gate closes? [found out]
What was VCF attack for accent? And was it linear, exp or something else? [soft, not linear]
What was ENV MOD range? Was it absolute or a fraction of current cutoff setting. [Tried both, settled for fixed]
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

	// We put this before connection check so the button and light works even when module not connected.
	if (params[BUTTON_PARAM].getValue() > 0.0f and !button_prev) {
		gateInput = !gateInput;
	}
	button_prev = bool(params[BUTTON_PARAM].getValue());
	lights[GATE_LIGHT].value = gateInput;
	lights[TRIG_LIGHT].value = !gateInput;

	if (!outputs[BASS_OUTPUT].isConnected()) {
		return;
	}
	int oversample_protected = current_oversample;// to be sure its not modified from another thread inside step.

	

	float osc = inputs[OSC_INPUT].getVoltage();
	float resonance = clamp(params[RESONANCE_PARAM].getValue()+inputs[CV_RESONANCE_INPUT].getVoltage()*params[CV_RESONANCE_PARAM].getValue(),0.0f,RESONANCE_MAX);
	float knob_cutoff = clamp(params[CUTOFF_PARAM].getValue()+inputs[CV_CUTOFF_INPUT].getVoltage()*params[CV_CUTOFF_PARAM].getValue(),0.0f,1.0f);
	float knob_accent = params[ACCENT_PARAM].getValue();
	float knob_envmod = clamp(params[ENVMOD_PARAM].getValue()+inputs[CV_ENVMOD_INPUT].getVoltage()*params[CV_ENVMOD_PARAM].getValue(),0.0f,1.0f);
	float knob_env_decay = clamp(params[ENV_DECAY_PARAM].getValue()+inputs[CV_DECAY_INPUT].getVoltage()*params[CV_DECAY_PARAM].getValue(),DECAY_VCF_MIN,DECAY_VCF_MAX);
	float accent = clamp(inputs[ACCENT_GATE_INPUT].getVoltage(),0.0f,1.0f);
	float note = inputs[NOTE_GATE_INPUT].getVoltage();

	

	/* 0.6:
	dsp::SchmittTrigger::setThresholds(float low, float high) has been removed, and the thresholds are now fixed at 0 and 1.
	Instead, rescale your input if needed with trigger.process(rescale(in, low, high, 0.f, 1.f)).
	**/
	bool gate = schmittGate.process(note);
	//bool accentHyst = schmittAccent.process(accent);
	//std::cout <<     "Gate "+std::to_string(gate)+"\n";
	//std::cout <<     "Note "+std::to_string(note)+"\n";

	if (gate && !gate_prev) {
		accentBool = accent >= 1.0f;
		//std::cout <<     "       ACCENT CHANGED "+std::to_string(accentBool)+"\n";
	}
	lights[E_LIGHT].value = accentBool;

	//float accent_envelope = this->accent_env(gate, note, accent, knob_accent);

	float cutoff_env_norm = this->filter_env(gate, note, knob_env_decay, accent, clamp(resonance, 0.0f, 1.0f), knob_accent);//params[DECAY3_PARAM].getValue()
	

	float vca_env;

	//float vca_env_sum = vca_env/(1.0f+ACCENT_ENVELOPE_VCA_OFFSET);

	if (accentBool) {
		//vca_env_sum = (vca_env+accent_envelope*ACCENT_ENVELOPE_VCA_OFFSET)/(1.0f+ACCENT_ENVELOPE_VCA_OFFSET);
		//std::cout << "Branch accent\n";
		vca_env = this->vca_env_acc(gate, note, clamp(resonance,0.0f,1.0f), knob_accent);
	} else {
		//std::cout << "Branch normal\n";
		vca_env = this->vca_env(gate, note, clamp(resonance,0.0f,1.0f), knob_accent);//knob_env_decay
	}

	float cutoff_setting = this->toExp(knob_cutoff, CUTOFF_KNOB_MIN, CUTOFF_KNOB_MAX);

	float range_hz = knob_envmod * CUTOFF_RANGE_FOR_ENVELOPE + CUTOFF_ENVMOD_MIN;//knob_envmod * maxf(cutoff_setting * 2.0f, CUTOFF_RANGE_FOR_ENVELOPE) + CUTOFF_ENVMOD_MIN;

	float cutoff_env_Hz = (cutoff_env_norm-CUTOFF_ENVELOPE_BIAS) * range_hz;// Can be negative
	
	float cutoff_hz = cutoff_setting+cutoff_env_Hz;

	cutoff_hz = clamp(cutoff_hz, CUTOFF_MIN, CUTOFF_MAX);

	float out = this->acid_filter(osc, resonance, cutoff_hz, oversample_protected);
	outputs[BASS_OUTPUT].setVoltage(vca_env*out, 0);//Audio output    //this->non_lin_func(vca*out/SATURATION_VOLT)*SATURATION_VOLT;
	//outputs[BASS_OUTPUT].setVoltage(vca_env, 1);//VCA Envelope output (0V to 1.6V)
	//outputs[BASS_OUTPUT].setVoltage(cutoff_env_norm-CUTOFF_ENVELOPE_BIAS, 2);//Normalized VCF cutoff envelope output (-0.31 to 3V)
	//float ext_cutoff_voltage = log2(cutoff_hz/dsp::FREQ_C4);
	//outputs[BASS_OUTPUT].setVoltage(ext_cutoff_voltage,3);// 1V/Oct cutoff output
	//outputs[BASS_OUTPUT].setChannels(4);

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
	
	/*if (counter <= 32 and fast_counter > 250) {
		fast_counter = 0;
		std::cerr <<     std::to_string(tim)+", "+std::to_string(cutoff_env_filter)+", "+std::to_string(vca)+", "+std::to_string(cutoff)+"\n";
	}
	tim += args.sampleTime;
	fast_counter += 1;**/
	gate_prev = gate;
	note_prev = note;
	
	
	lights[GAIN_LIGHT].value = clamp(fabs(osc)-EXPECTED_PEAK_INPUT,0.0f,1.0f)*1.0f;//OSC input has too much gain. (7V+)
}

float Bass::attackCurve(float x, unsigned target) {
	return log10(x/target+1.0f)*3.321928f;
}

float Bass::accentAttackCurve(float x) {
	// x and y is normalized. Quadratic rise to 1 from zero.
	return -(x-1.0f)*(x-1.0f)+1.0f;
}

float Bass::accentAttackCurveInverse(float y) {
	// x and y is normalized. Quadratic rise to 1 from zero.
	if (y > 1.0f) {
		return 1.0f;
	}
	return 1.0f-sqrt(1.0f-y);
}

float Bass::toExp(float x, float min, float max) {
	// 0 to 1 to exp range
	return min * exp( x*log(max/min) );
}


float Bass::accent_env(bool gate, float note, bool accent, float knob_accent) {
	// This method is not used atm. Does not simulate the accent envelope which is a modified vcf envelope good enough.
	float dt = APP->engine->getSampleTime();
	if (!accentBool) return 0.0f;
	if (gate && !gate_prev) {
		noteSteps = 0;
	} else {
		noteSteps++;
		if (noteSteps > 10000000) {
			noteSteps = 0;
		}
	}

	float x = ((float)noteSteps)*dt;

	float value;
	if (x < 0.0291f) {
		value = -200.0f*(x+0.023f)*(x+0.023f)+18.0f*(x+0.023f)+0.595f;
	} else {
		value = powf(20.0f, 0.02572f-x);
	}
	return knob_accent*clamp(value, 0.0f, 1.0f);
}

float Bass::vca_env(bool gate, float note,  float resonance, float knob_accent) {
	float dt = APP->engine->getSampleTime();
	float level = 0.0f;

	number_vca += 1; // steps progress counter
	//std::cout <<     "NrmMode "+std::to_string(mode_vca)+" Number "+std::to_string(number_vca)+" Target "+std::to_string(target_vca)+"\n";
	if (gate && !gate_prev) {
		mode_vca = 0;// attack phase
		number_vca = 1;// first step of this phase
		
		// linear attack from previous level, to avoid clicking
		target_vca = unsigned(ATTACK_VCA/dt);// How many steps to get amp to 1.0
		factor_vca = (1.0f-current_vca)/float(target_vca);// How much to increase amp each step until 1.0 is reached (offset)
	} else if (gateInput && mode_vca < 2 && note < 1.0f && note_prev >= 1.0f) {
		mode_vca = 2;// Input set to gate. Note ending.
		number_vca = 1;// first step of this phase
		target_vca = unsigned(DECAY_VCA_NOTE_END/dt);// fast declicker
		factor_vca = (current_vca-0.0f) / float(target_vca);// Linear go to 0.0 (offset)
	} else if (mode_vca > 2) {//can be 3 or 4 if just was in vca_env_acc()
		// ended decay
		number_vca = 0;// we wont get it too high
		target_vca = 0;//to prevent vca_env_acc() to go into infinite loop
		return 0.0f;
	} else if (mode_vca == 0 && number_vca > target_vca) {
		// We were in attack phase, now lets switch to decay
		mode_vca = 1;// decay phase
		number_vca = 1;// first step of this phase
		target_vca = unsigned(DECAY_VCA_SECS/dt);// Number of steps to decay
		if (DECAY_VCA_EXP) {
			factor_vca = 1.0f + ((log(minimum) - log(current_vca)) / float(target_vca));// Smooth exp decay down to minimum. (factor)
		} else {
			factor_vca = (current_vca-minimum) / float(target_vca);// Linear decay down to minimum. (offset)
		}
	} else if (number_vca > target_vca) {
		// end decay or end note decay
		mode_vca = 3;
	}

	switch (mode_vca) {
		case 0: {//attack
			level = current_vca+factor_vca;
			current_vca = level;
			this->vca_lights(1,0,0,0);
			break;
		} case 1: { //decay
			if (DECAY_VCA_EXP) {
				level = current_vca*factor_vca;
			} else {
				level = current_vca-factor_vca;
			}
			current_vca = level;
			this->vca_lights(0,0,level,0);
			break;
		} case 2: { //end note
			level = current_vca-factor_vca;
			current_vca = level;
			this->vca_lights(0,0,0,clamp(1-level,0.0f,1.0f));
			break;
		} default: {
			level = 0.0f;
			current_vca = minimum;
			this->vca_lights(0,0,0,1);
		}
	}
	return level;
}

float Bass::vca_env_acc(bool gate, float note,  float resonance, float knob_accent) {
	float dt = APP->engine->getSampleTime();
	float level = 0.0f;

	float attack_vca_accent_peak = (1.0f+ACCENT_ENVELOPE_VCA_OFFSET*knob_accent);

	number_vca += 1; // steps progress counter
	//std::cout <<     "AccMode "+std::to_string(mode_vca)+" Number "+std::to_string(number_vca)+" Target "+std::to_string(target_vca)+"\n";
	if (gate && !gate_prev) {
		mode_vca = 0;// attack phase
		float fraction = accentAttackCurveInverse(current_vca/attack_vca_accent_peak);
		float attack_time = ATTACK_VCF_ACCENT*resonance+ATTACK_VCA;
		target_vca = unsigned(attack_time/dt);// How many steps to get amp to attack_vca_accent_peak from zero
		number_vca = 1+unsigned(fraction*float(target_vca));// We do this to avoid click when rising from prev level.			
		//std::cout <<     "Attack "+std::to_string(fraction)+" Target "+std::to_string(target_vca)+" Number "+std::to_string(number_vca)+" Volts "+std::to_string(current_vca)+"\n";
	} else if (gateInput && mode_vca < 3 && note < 1.0f && note_prev >= 1.0f) {
		mode_vca = 3;// Input set to gate. Note ending.
		number_vca = 1;// first step of this phase
		target_vca = unsigned(DECAY_VCA_NOTE_END/dt);// fast declicker
		factor_vca = (current_vca-0.0f) / float(target_vca);// Linear go to 0.0 (offset)
	} else if (mode_vca > 3) {
		// ended decay
		number_vca = 0;// we wont get it too high
		target_vca = 0;
		return 0.0f;
	} else if (mode_vca == 0 && number_vca >= target_vca) {
		// ended attack
		mode_vca = 1;// peak phase
		number_vca = 1;// we wont get it too high
		target_vca = unsigned(PEAK_ACCENT_SUSTAIN/dt);//holding peak time
	} else if (mode_vca == 1 && number_vca > target_vca) {
		// We were in peak phase, now lets switch to decay
		mode_vca = 2;// decay phase
		number_vca = 1;// first step of this phase
		target_vca = unsigned(DECAY_VCA_ACCENT/dt);// Number of steps to decay
	} else if (number_vca > target_vca) {
		// end decay or end note decay
		mode_vca = 4;
	}

	// when switch to any accent timing, make sure no divide by zero (target_vca) due to switching method

	switch (mode_vca) {
		case 0: {//attack
			float fraction = target_vca==0?1.0f:float(number_vca)/float(target_vca);
			level = this->accentAttackCurve(fraction) * attack_vca_accent_peak;
			current_vca = level;
			this->vca_lights(1,0,0,0);
			break;
		} case 1: { //peak
			level = attack_vca_accent_peak;
			current_vca = level;
			this->vca_lights(0,1,0,0);
			break;
		} case 2: { //decay
			float fraction = float(number_vca)/float(target_vca);
			level = attack_vca_accent_peak * powf(1.0f+fraction*2.0f,-fraction*6.0f);
			current_vca = level;
			this->vca_lights(0,0,level,0);
			break;
		} case 3: { //end note
			level = current_vca-factor_vca;
			current_vca = level;
			this->vca_lights(0,0,0,clamp(1-level,0.0f,1.0f));
			break;
		} default: { // silence
			level = 0.0f;
			current_vca = minimum;
			this->vca_lights(0,0,0,1);
		}
	}
	return level;
}



float Bass::filter_env(bool gate, float note, float knob_env_decay, float accent, float resonance, float knob_accent) {
	float dt = APP->engine->getSampleTime();
	float level = 0.0f;

	number_cutoff += 1;
	
	

	if (gate && !gate_prev) {
		// start attack
		mode_cutoff = 0;
		float attack_time = float(accentBool)*ATTACK_VCF_ACCENT*resonance+ATTACK_VCF;//params[DECAY2_PARAM].getValue()
		//std::cerr << "Old target = "+std::to_string(dt*old_decay_target)+" ("+std::to_string(target_cutoff)+" , "+std::to_string(number_cutoff)+"\n";
		
		number_cutoff = 1;
		target_cutoff = int(attack_time/dt);
		
		if(accentBool) {
			accentAttackPeak = clamp(1.00f+0.25f*knob_accent+float(accentBool)*knob_accent*current_cutoff,0.0f,CUTOFF_MAX_STACKING);
			accentAttackBase = current_cutoff;
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
		target_cutoff = int((accentBool?DECAY_VCF_ACCENT:knob_env_decay)/dt);
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
			target_cutoff = int(PEAK_ACCENT_SUSTAIN/dt);//holding peak time
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

	switch (mode_cutoff) {
		case 0: { //attack
			if (accentBool) {
				float fraction = target_cutoff==0?1.0f:float(number_cutoff)/float(target_cutoff);
				level = this->accentAttackCurve(fraction) * (accentAttackPeak - accentAttackBase) + accentAttackBase;
			} else {
				level = 1.0f;//instant
			}
			
			current_cutoff = level;
			
			this->vcf_lights(1,0,0,0);
			break;
		} case 1: { //peak
			level = current_cutoff;
			this->vcf_lights(0,1,0,0);
			break;
		} case 2: { //decay
			if(accentBool) {
				float fraction = float(number_cutoff)/float(target_cutoff);
				level = accentAttackPeak * powf(1.0f+fraction*2.0f,-fraction*2.0f);
			} else {
				if (DECAY_VCF_EXP) {
					level = current_cutoff*factor_cutoff;
				} else {
					level = current_cutoff-factor_cutoff;
				}
			}
			current_cutoff = level;
			this->vcf_lights(0,0,level,0);
			break;
		} default: {
			level = 0.0f;
			current_cutoff = minimum;
			number_cutoff = 0;
			target_cutoff = 0;
			this->vcf_lights(0,0,0,1);
		}
	}
	return level;
}

void Bass::vca_lights(float attack, float sustain, float decay, float end) {
	lights[A_LIGHT].value = attack;
	lights[B_LIGHT].value = sustain;
	lights[C_LIGHT].value = decay;
	lights[D_LIGHT].value = end;
}

void Bass::vcf_lights(float attack, float sustain, float decay, float end) {
	lights[A2_LIGHT].value = attack;
	lights[B2_LIGHT].value = sustain;
	lights[C2_LIGHT].value = decay;
	lights[D2_LIGHT].value = end;
}

float Bass::acid_filter(float in, float r, float F_c, int oversample_protected) {// from diagram of resonance of TB-303
	float voltage_drive = VCV_TO_MOOG*INPUT_TO_CAPACITOR;// 0.18 to convert from VCV audio rate voltages. 0.035 to convert from input to voltage over first capacitor.
	in *= voltage_drive;
	F_s   = APP->engine->getSampleRate()*oversample_protected;

	double w_c = double(2.0f*M_PI*F_c/F_s);// cutoff in radians per sample.
	g = V_t * (0.0008116984 + 0.9724111*w_c - 0.5077766*w_c*w_c + 0.1534058*w_c*w_c*w_c);// new auto tuned g for cutoff  4th order: y = 0.00007055354 + 0.9960577*x - 0.6082669*x^2 + 0.286043*x^3 - 0.05393212*x^4

	if (tunedResonance) {
		Gres = 1.037174 + 3.606925*w_c + 7.074555*w_c*w_c - 18.14674*w_c*w_c*w_c + 9.364587*w_c*w_c*w_c*w_c;//auto tuned resonance power for resonance <= 1.0
	} else {
		Gres = 1.15f;
	}
	
	float g2;
	if (firstPoleOneOctHigher) {
		double w_c2 = 2.0f*w_c;
		g2 = V_t * (0.0008116984 + 0.9724111*w_c2 - 0.5077766*w_c2*w_c2 + 0.1534058*w_c2*w_c2*w_c2);
	} else {
		g2 = g;
	}

	float inInter [oversample_protected];
	float outBuf  [oversample_protected];
	if (oversample_protected == oversample2) {
		upsampler2.process(in, inInter);
	} else {
		upsampler4.process(in, inInter);
	}

	for (int i = 0; i < oversample_protected; i++) {
		x   = inInter[i] - 2.0f*Gres*r*(y_d_prev+y_d_prev_prev-priority*inInter[i]);//unit and a half feedback delay. -inInter[i] is Gcomp, to make passband gain not decrease too much when turning up resonance.

		// 1st transistor stage:
		y_a = y_a_prev+g2*(non_lin_func( x/V_t )-W_a_prev);
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
	float out;
	if (oversample_protected == oversample2) {
		out = decimator2.process(outBuf);
	} else {
		out = decimator4.process(outBuf);
	}
	if(!std::isfinite(out)) {
		out = 0.0f;
	}
	return out/voltage_drive;
}

struct OversampleBassMenuItem : MenuItem {
	Bass* _module;
	int _os;

	OversampleBassMenuItem(Bass* module, const char* label, int os)
	: _module(module), _os(os)
	{
		this->text = label;
	}

	void onAction(const event::Action &e) override {
		_module->current_oversample = _os;
	}

	void step() override {
		rightText = _module->current_oversample == _os ? "✔" : "";
	}
};

struct PoleMenuItem : MenuItem {
	Bass* _module;

	PoleMenuItem(Bass* module, const char* label)
	: _module(module)
	{
		this->text = label;
	}

	void onAction(const event::Action &e) override {
		_module->firstPoleOneOctHigher = !_module->firstPoleOneOctHigher;
	}

	void step() override {
		rightText = _module->firstPoleOneOctHigher == true ? "✔" : "";
	}
};

struct ResTuneMenuItem : MenuItem {
	Bass* _module;

	ResTuneMenuItem(Bass* module, const char* label)
	: _module(module)
	{
		this->text = label;
	}

	void onAction(const event::Action &e) override {
		_module->tunedResonance = !_module->tunedResonance;
	}

	void step() override {
		rightText = _module->tunedResonance == true ? "✔" : "";
	}
};

struct PrioMenuItem : MenuItem {
	Bass* _module;

	PrioMenuItem(Bass* module, const char* label)
	: _module(module)
	{
		this->text = label;
	}

	void onAction(const event::Action &e) override {
		if (_module->priority > 0.0f) {
			_module->priority = 0.0f;
		} else {
			_module->priority = 1.0f;
		}
	}

	void step() override {
		rightText = _module->priority > 0.0f ? "✔" : "";
	}
};

struct BassWidget : ModuleWidget {
	BassWidget(Bass *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/BassModule.svg")));
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

	void appendContextMenu(Menu* menu) override {
		Bass* a = dynamic_cast<Bass*>(module);
		assert(a);

		menu->addChild(new MenuLabel());
		menu->addChild(new OversampleBassMenuItem(a, "Oversample x2", 2));
		menu->addChild(new OversampleBassMenuItem(a, "Oversample x4", 4));
		//menu->addChild(new MenuLabel());
		//menu->addChild(new ResTuneMenuItem(a, "Tuned Resonance"));
		//menu->addChild(new MenuLabel());
		//menu->addChild(new PoleMenuItem(a, "1st Pole Oct Up"));
		//menu->addChild(new MenuLabel());
		//menu->addChild(new PrioMenuItem(a, "Compensate Passband"));
	}
};

Model *modelBass = createModel<Bass, BassWidget>("Bass");