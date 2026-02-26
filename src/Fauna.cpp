#include "Autinn.hpp"
#include <cmath>

/*

    Autinn VCV Rack Plugin
    Copyright (C) 2026  Nikolai V. Chr.

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

#define VCV_TO_MOOG 0.18f         // 0.18 to convert from VCV to Moog audio rate voltages.
#define INPUT_TO_CAPACITOR 0.05f  // factor for input voltage to voltage over capacitor in first stage. 0.035 is when +-5V input, will match the g tuning, but a little higher for effect.
#define DRIVE_MAX 4.0f
#define FREQ_MIN 20.0f            // standard Moog minimum cutoff
#define FREQ_MAX 20000.0f         // beyond 18000 it does not react well and g is very out of tune at higher frequencies anyway.
#define RESONANCE_MAX 1.0f

static const int oversample2 = 2;
static const int oversample4 = 4;
static const int oversample8 = 8;

struct Fauna : Module {
	enum ParamIds {
		CUTOFF_PARAM,
		CUTOFF_INFL_PARAM,
		RESONANCE_PARAM,
		RESONANCE_INFL_PARAM,
		DRIVE_PARAM,
		DRIVE_INFL_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		FAUNA_INPUT,
		CUTOFF_INPUT,
		RESONANCE_INPUT,
		DRIVE_INPUT,
		FAUNA_INPUT2,
		NUM_INPUTS
	};
	enum OutputIds {
		FAUNA_OUTPUT,
		FAUNA_OUTPUT2,
		//OUTPUT_W,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	//float Boltzman = 0.000086173303f;// eV/K
	//float t = 30.0f + 273.15f; // 30 celcius
	//float V_t = 2.0f * t * Boltzman; // thermal voltage * 2 (should be divided by q also). Thermal V should be around 0.026V, times 2 its 0.052. Something divided by that gets multiplied by 19.23.
	const float V_t = 2.0f * 0.026f;// more standard 2xthermalvoltage.
	const float inv_Vt = 1.0f / V_t;

	float r = 0.0f;
	float input_cutoff = 0.0f;
	float F_s = 0.0f;
	float g = 0.0f; // tuning parameter
	int current_oversample = 2;
	bool autoLevel = false;// This adjusts the output gain to compensate for drive.
	float F_c_prev = 0.0f;
	float F_s_prev = 0.0f;

	
	//LEFT

	float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
	
	dsp::Upsampler<oversample2, 10> upsampler2;
	dsp::Decimator<oversample2, 10> decimator2;
	dsp::Upsampler<oversample4, 10> upsampler4;
	dsp::Decimator<oversample4, 10> decimator4;
	dsp::Upsampler<oversample8, 10> upsampler8;
	dsp::Decimator<oversample8, 10> decimator8;

	// RIGHT

	float s1_r = 0.0f, s2_r = 0.0f, s3_r = 0.0f, s4_r = 0.0f;

	dsp::Upsampler<oversample2, 10> upsampler2_right;
	dsp::Decimator<oversample2, 10> decimator2_right;
	dsp::Upsampler<oversample4, 10> upsampler4_right;
	dsp::Decimator<oversample4, 10> decimator4_right;
	dsp::Upsampler<oversample8, 10> upsampler8_right;
	dsp::Decimator<oversample8, 10> decimator8_right;

	

	Fauna() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Fauna::CUTOFF_PARAM, 0.0f, 1.0f, 0.0f, "Cutoff"," Hz",FREQ_MAX/FREQ_MIN, FREQ_MIN);
		configParam<Param4Digits>(Fauna::CUTOFF_INFL_PARAM, 0.0f, 1.0f, 0.0f, "Cutoff CV", "%", 0.0f, 100.0f);
		configParam<Param4Digits>(Fauna::RESONANCE_PARAM, 0.0f, RESONANCE_MAX, 0.0f, "Resonance", "%", 0.0f, 100.0f);
		configParam<Param4Digits>(Fauna::RESONANCE_INFL_PARAM, 0.0f, RESONANCE_MAX/5.0f, 0.0f, "Resonance CV", "%", 0.0f, 500.0f);
		configParam<Param4Digits>(Fauna::DRIVE_INFL_PARAM, 0.0f, DRIVE_MAX/5.0f, 0.0f, "Drive CV", "%", 0.0f, 125.0f);
		configParam<Param3Digits>(Fauna::DRIVE_PARAM, 0.0f, DRIVE_MAX, 1.00f, "Drive", " dB", -10, 20);
		configBypass(FAUNA_INPUT, FAUNA_OUTPUT);
		configBypass(FAUNA_INPUT2, FAUNA_OUTPUT2);

		configInput(CUTOFF_INPUT, "1V/Oct Cutoff CV");
		configInput(RESONANCE_INPUT, "Resonance CV");
		configInput(DRIVE_INPUT, "Drive CV");

		configInput(FAUNA_INPUT, "Left");
		configInput(FAUNA_INPUT2, "Right");
		configOutput(FAUNA_OUTPUT, "Left");
		configOutput(FAUNA_OUTPUT2, "Right");
	}

	void process(const ProcessArgs &args) override;
	void process_left(const ProcessArgs &args, int oversample_protected, float drive, float inv_drive);
	void process_right(const ProcessArgs &args, int oversample_protected, float drive, float inv_drive);
	float toExp(float x);

	json_t *dataToJson() override {
		json_t *root = json_object();
		json_object_set_new(root, "oversample", json_integer(current_oversample));
		json_object_set_new(root, "autoLevel", json_boolean(autoLevel));
		return root;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *ext2 = json_object_get(rootJ, "autoLevel");
		if (ext2)
			autoLevel = json_boolean_value(ext2);
		json_t *ext3 = json_object_get(rootJ, "oversample");
		if (ext3) {
			current_oversample = json_integer_value(ext3);
			if (current_oversample != 2 and current_oversample != 4 and current_oversample != 8) {
				current_oversample = 4;
			}
		}
	}

	void onReset(const ResetEvent& e) override {
		autoLevel = false;
		current_oversample = 2;
		Module::onReset(e);
	}
};

static const float LOG_FREQ_RANGE = float(log(FREQ_MAX/FREQ_MIN));

float Fauna::toExp(float x) {
	// 0 to 1 to exp range
	return FREQ_MIN * exp( x*LOG_FREQ_RANGE );
}



void Fauna::process(const ProcessArgs &args) {
	// Implements a 4-Pole transistor ladder LP filter
	// notice conversion between Moog Volts and VCV Rack Volts
	// VCV Rack audio rate is +-5V, Moog is +-0.9V
	// VCV Rack CV is +-5V or 0V-10V

	// TODO: Need at least x2 over-sampling [done]
	// TODO: Implement non-linear as fast function [done]
	// TODO: Light that indicates saturation of circuit. [done, and removed again]
	// TODO: Tune F_c. [done]
	// TODO: Tune r. [done]
	// TODO: Setting for how many poles [wont do, cause g and resonance tuned to 4 right now]
	// TODO: In Rack 0.6: Tune the cutoff CV and infl param. [done]
	// TODO: Increase passband gain when turning up resonance. [done]
	// TODO: Make cutoff knob be logarithmic [done]
	// TODO: Convert to drive knob with CV [done]
	// TODO: Pre-decimation filtering for anti-aliasing [done]
	// TODO: Protection against non finite numbers. [done]
	// TODO: Passband gain compensation amount should be selectable via context menu. [done]
	// TODO: Higher pole decimation filters. [done]
	// TODO: Switch decimation and upsampling filters to Rack API. [done]
	// TODO: Add auto level option in context menu to counter low drive settings. [done]
	// TODO: Implement zero-delay feedback [done]


	if (!outputs[FAUNA_OUTPUT].isConnected()) {
		outputs[FAUNA_OUTPUT].setVoltage(0.0f);
	}
	if (!outputs[FAUNA_OUTPUT2].isConnected()) {
		outputs[FAUNA_OUTPUT2].setVoltage(0.0f);
	}
	if (!outputs[FAUNA_OUTPUT].isConnected() and !outputs[FAUNA_OUTPUT2].isConnected()) {
		return;
	}
	int oversample_protected = current_oversample;// to be sure its not modified from another thread inside step.
	float drive = clamp(params[DRIVE_PARAM].getValue()+inputs[DRIVE_INPUT].getVoltage()*params[DRIVE_INFL_PARAM].getValue(),0.0f,DRIVE_MAX);
	
	r     = clamp(params[RESONANCE_PARAM].getValue()+(inputs[RESONANCE_INPUT].getVoltage()*params[RESONANCE_INFL_PARAM].getValue()), 0.0f, RESONANCE_MAX);
	input_cutoff =  std::exp2f(inputs[CUTOFF_INPUT].getVoltage()*params[CUTOFF_INFL_PARAM].getValue());
	float F_c   = clamp(this->toExp(params[CUTOFF_PARAM].getValue())*input_cutoff, FREQ_MIN, FREQ_MAX);
	F_s   = args.sampleRate*oversample_protected;

	if (F_c != F_c_prev || F_s != F_s_prev) {
		// Bilinear Transform pre-warping
		g = std::tan(float(M_PI) * F_c / F_s);
	}
	
	float inv_drive = VCV_TO_MOOG*INPUT_TO_CAPACITOR*((autoLevel && drive != 0.0f)?clamp(drive,0.10f,DRIVE_MAX):1.0f);
	
	if (outputs[FAUNA_OUTPUT].isConnected()) {
		this->process_left(args, oversample_protected, drive, inv_drive);
	}
	
	if (outputs[FAUNA_OUTPUT2].isConnected()) {
		this->process_right(args, oversample_protected, drive, inv_drive);
	}
	
	F_c_prev = F_c;
	F_s_prev = F_s;
}

inline float get_sat_gain(const float s) {
	// If the signal is tiny, there is no compression (gain is 1.0)
	if (s > -1e-4f && s < 1e-4f) return 1.0f;
	// Otherwise, calculate the exact compression ratio
	return tanh_fast_low(s) / s;
}

void Fauna::process_left(const ProcessArgs &args, int oversample_protected, float drive, float inv_drive) {
	float in = inputs[FAUNA_INPUT].getVoltage()*drive*VCV_TO_MOOG*INPUT_TO_CAPACITOR;
	float inInter [oversample_protected];
	float outBuf  [oversample_protected];
	if (oversample_protected == oversample2) {
		upsampler2.process(in, inInter);
	} else if (oversample_protected == oversample4) {
		upsampler4.process(in, inInter);
	} else {
		upsampler8.process(in, inInter);
	}

	for (int i = 0; i < oversample_protected; i++) {
		float k = 4.0f * r;
		float drive_in = inInter[i] * inv_Vt;

		// Use the 4th capacitor state from the previous tick.
		// Zero prediction math, zero phantom feedback, super light on CPU.
		float x = drive_in - k * s4;

		// Stage 1
		float in1 = tanh_fast_high(x);
		float v1 = (in1 - s1) * G1;
		float y1 = s1 + v1;
		s1 += 2.0f * v1;

		// Stage 2
		float in2 = tanh_fast_high(y1);
		float v2 = (in2 - s2) * G2;
		float y2 = s2 + v2;
		s2 += 2.0f * v2;

		// Stage 3
		float in3 = tanh_fast_high(y2);
		float v3 = (in3 - s3) * G3;
		float y3 = s3 + v3;
		s3 += 2.0f * v3;

		// Stage 4
		float in4 = tanh_fast_high(y3);
		float v4 = (in4 - s4) * G4;
		float y4 = s4 + v4;
		s4 += 2.0f * v4;

		// Convert unitless tanh domain back to Moog voltages
		outBuf[i] = y4 * V_t;
	}
	float out;
	if (oversample_protected == oversample2) {
		out = decimator2.process(outBuf);
	} else if (oversample_protected == oversample4) {
		out = decimator4.process(outBuf);
	} else {
		out = decimator8.process(outBuf);
	}
	if(!std::isfinite(out) || out > 100.0f || out < -100.0f) {
		out = 0.0f;
		// Reset all State Variables to 0 to stop the NaN
		s1 = 0.0f; s2 = 0.0f; s3 = 0.0f; s4 = 0.0f;
	}
	outputs[FAUNA_OUTPUT].setVoltage(out/inv_drive);
}

void Fauna::process_right(const ProcessArgs &args, int oversample_protected, float drive, float inv_drive) {
	float in = inputs[FAUNA_INPUT2].getVoltage()*drive*VCV_TO_MOOG*INPUT_TO_CAPACITOR;
	float inInter [oversample_protected];
	float outBuf  [oversample_protected];
	if (oversample_protected == oversample2) {
		upsampler2.process(in, inInter);
	} else if (oversample_protected == oversample4) {
		upsampler4.process(in, inInter);
	} else {
		upsampler8.process(in, inInter);
	}

	for (int i = 0; i < oversample_protected; i++) {
		// Calculate the instantaneous feedback (Linear resolution)
		float G = g / (1.0f + g);
		float S1 = s1_r / (1.0f + g);
		float S2 = s2_r / (1.0f + g);
		float S3 = s3_r / (1.0f + g);
		float S4 = s4_r / (1.0f + g);

		// Resonance factor (4.0 = self oscillation)
		float k = 4.0f * r;

		// Convert input to thermal voltage domain
		float drive_in = inInter[i] * inv_Vt;

		float y4 = S4;
		float in1 = 0.0f, in2 = 0.0f, in3 = 0.0f, in4 = 0.0f;
		float out1 = 0.0f, out2 = 0.0f, out3 = 0.0f;

		for (int iter = 0; iter < 3; iter++) {
			float x = drive_in - k * y4;
			in1 = tanh_fast_low(x);
			out1 = in1 * G + S1;

			in2 = tanh_fast_low(out1);
			out2 = in2 * G + S2;

			in3 = tanh_fast_low(out2);
			out3 = in3 * G + S3;

			in4 = tanh_fast_low(out3);
			y4 = in4 * G + S4;
		}

		// Now that we have the exact feedback (y4), do a high-quality pass for the audio
		float x_final = drive_in - k * y4;

		// Stage 1
		float in1_final = tanh_fast_high(x_final);
		float v1 = (in1_final - s1_r) * G;
		float y1_final = s1_r + v1;
		s1_r += 2.0f * v1;

		// Stage 2
		float in2_final = tanh_fast_high(y1_final);
		float v2 = (in2_final - s2_r) * G;
		float y2_final = s2_r + v2;
		s2_r += 2.0f * v2;

		// Stage 3
		float in3_final = tanh_fast_high(y2_final);
		float v3 = (in3_final - s3_r) * G;
		float y3_final = s3_r + v3;
		s3_r += 2.0f * v3;

		// Stage 4
		float in4_final = tanh_fast_high(y3_final);
		float v4 = (in4_final - s4_r) * G;
		float final_out = s4_r + v4;
		s4_r += 2.0f * v4;

		// Convert unitless tanh domain back to Moog voltages
		outBuf[i] = final_out * V_t;
	}
	float out;
	if (oversample_protected == oversample2) {
		out = decimator2.process(outBuf);
	} else if (oversample_protected == oversample4) {
		out = decimator4.process(outBuf);
	} else {
		out = decimator8.process(outBuf);
	}
	if(!std::isfinite(out) || out > 100.0f || out < -100.0f) {
		out = 0.0f;

		// Reset all State Variables to 0 to stop the NaN
		s1_r = 0.0f; s2_r = 0.0f; s3_r = 0.0f; s4_r = 0.0f;
	}
	outputs[FAUNA_OUTPUT2].setVoltage(out/inv_drive);
}

struct AutoLevelMenuItem : MenuItem {
	Fauna* _module;

	AutoLevelMenuItem(Fauna* module, const char* label)
	: _module(module)
	{
		this->text = label;
	}
	
	void onAction(const event::Action &e) override {
		_module->autoLevel = !_module->autoLevel;
	}

	void step() override {
		rightText = _module->autoLevel == true ? "✔" : "";
	}
};


struct OversampleFaunaMenuItem : MenuItem {
	Fauna* _module;
	int _os;

	OversampleFaunaMenuItem(Fauna* module, const char* label, int os)
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

struct FaunaWidget : ModuleWidget {
	FaunaWidget(Fauna *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/FaunaModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		//addParam(createParam<RoundMediumAutinnKnob>(Vec(75, RACK_GRID_HEIGHT-275-HALF_KNOB_MED), module, Fauna::CUTOFF_PARAM));
		auto cutKnob = createParam<AutinnArcMidKnob>(Vec(75, RACK_GRID_HEIGHT-275-HALF_KNOB_MED), module, Fauna::CUTOFF_PARAM);
		cutKnob->setModulation(Fauna::CUTOFF_INPUT, [](float cv, float val, float att) {
					// Calculate how many Octaves the knob covers
					const float totalOctaves = std::log2f(FREQ_MAX / FREQ_MIN);
					// 2. Scale CV so 1V = 1 Octave of knob travel
					float cvNormalized = (cv*att) / totalOctaves;
					// 3. Add to knob position (Linear Pitch Space)
					return clamp(val + cvNormalized, 0.0f, 1.0f);
				}, Fauna::CUTOFF_INFL_PARAM);
		addParam(cutKnob);
		addParam(createParam<RoundSmallAutinnKnob>(Vec(40, RACK_GRID_HEIGHT-275-HALF_KNOB_SMALL), module, Fauna::CUTOFF_INFL_PARAM));

		//addParam(createParam<RoundMediumAutinnKnob>(Vec(75, RACK_GRID_HEIGHT-205-HALF_KNOB_MED), module, Fauna::RESONANCE_PARAM));
		auto qKnob = createParam<AutinnArcMidKnob>(Vec(75, RACK_GRID_HEIGHT-205-HALF_KNOB_MED), module, Fauna::RESONANCE_PARAM);
		qKnob->setModulation(Fauna::RESONANCE_INPUT, [](float cv, float val, float att) {
					return clamp(val + cv*att, 0.0f, RESONANCE_MAX);
				}, Fauna::RESONANCE_INFL_PARAM);
		addParam(qKnob);
		addParam(createParam<RoundSmallAutinnKnob>(Vec(40, RACK_GRID_HEIGHT-205-HALF_KNOB_SMALL), module, Fauna::RESONANCE_INFL_PARAM));

		addParam(createParam<RoundSmallAutinnKnob>(Vec(40, RACK_GRID_HEIGHT-135-HALF_KNOB_SMALL), module, Fauna::DRIVE_INFL_PARAM));
		//addParam(createParam<RoundMediumAutinnKnob>(Vec(75, RACK_GRID_HEIGHT-135-HALF_KNOB_MED), module, Fauna::DRIVE_PARAM));
		auto drvKnob = createParam<AutinnArcMidKnob>(Vec(75, RACK_GRID_HEIGHT-135-HALF_KNOB_MED), module, Fauna::DRIVE_PARAM);
		drvKnob->setModulation(Fauna::DRIVE_INPUT, [](float cv, float val, float att) {
					return clamp(val + cv*att, 0.0f, DRIVE_MAX);
				}, Fauna::DRIVE_INFL_PARAM);
		addParam(drvKnob);


		addInput(createInput<InPortAutinn>(Vec(10, RACK_GRID_HEIGHT-275-HALF_PORT), module, Fauna::CUTOFF_INPUT));
		addInput(createInput<InPortAutinn>(Vec(10, RACK_GRID_HEIGHT-205-HALF_PORT), module, Fauna::RESONANCE_INPUT));
		addInput(createInput<InPortAutinn>(Vec(10, RACK_GRID_HEIGHT-135-HALF_PORT), module, Fauna::DRIVE_INPUT));

		addInput(createInput<InPortAutinn>(Vec(9 * RACK_GRID_WIDTH*0.15-HALF_PORT, 300), module, Fauna::FAUNA_INPUT));
		addInput(createInput<InPortAutinn>(Vec(9 * RACK_GRID_WIDTH*0.35-HALF_PORT, 300), module, Fauna::FAUNA_INPUT2));
		addOutput(createOutput<OutPortAutinn>(Vec(9 * RACK_GRID_WIDTH*0.60-HALF_PORT, 300), module, Fauna::FAUNA_OUTPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(9 * RACK_GRID_WIDTH*0.85-HALF_PORT, 300), module, Fauna::FAUNA_OUTPUT2));
		//addOutput(createOutput<OutPortAutinn>(Vec(9 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Fauna::OUTPUT_W));
	}
	
	void appendContextMenu(Menu* menu) override {
		Fauna* a = dynamic_cast<Fauna*>(module);
		assert(a);

		//menu->addChild(new MenuLabel());
		//menu->addChild(new EmphasizeMenuItem(a, "Passband gain comp.",  1.0f));
		//menu->addChild(new EmphasizeMenuItem(a, "Medium compensation",  0.5f));
		//menu->addChild(new EmphasizeMenuItem(a, "No compensation", 0.0f));
		
		menu->addChild(new MenuLabel());
		menu->addChild(new OversampleFaunaMenuItem(a, "Oversample x2", 2));
		menu->addChild(new OversampleFaunaMenuItem(a, "Oversample x4", 4));
		menu->addChild(new OversampleFaunaMenuItem(a, "Oversample x8", 8));
		menu->addChild(new MenuLabel());
		menu->addChild(new AutoLevelMenuItem(a, "Auto level"));
	}
};

Model *modelFauna = createModel<Fauna, FaunaWidget>("Fauna");