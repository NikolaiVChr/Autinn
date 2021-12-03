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

#define VCV_TO_MOOG 0.18f         // 0.18 to convert from VCV to Moog audio rate voltages.
#define INPUT_TO_CAPACITOR 0.05f  // factor for input voltage to voltage over capacitor in first stage. 0.035 is when +-5V input, will match the g tuning, but a little higher for effect.
#define DRIVE_MAX 4.0f
#define FREQ_MIN 20.0f            // standard Moog minimum cutoff
#define FREQ_MAX 18000.0f         // beyond 18000 it does not react well and g is very out of tune at higher frequencies anyway.
#define RESONANCE_MAX 1.0f

static const int oversample2 = 2;
static const int oversample4 = 4;

struct Flora : Module {
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
		FLORA_INPUT,
		CUTOFF_INPUT,
		RESONANCE_INPUT,
		DRIVE_INPUT,
		FLORA_INPUT2,
		NUM_INPUTS
	};
	enum OutputIds {
		FLORA_OUTPUT,
		FLORA_OUTPUT2,
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

	float r = 0.0f;
	float Gres = 1.0f;
	float input_cutoff = 0.0f;
	float F_c = 0.0f;
	float F_s = 0.0f;
	float g = 0.0f; // tuning parameter
	int current_oversample = 2;
	float gComp = 0.0f;
	bool autoLevel = false;// This adjusts the output gain to compensate for drive.
	float F_c_prev = 0.0f;
	float F_s_prev = 0.0f;

	
	//LEFT
	
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
	
	dsp::Upsampler<oversample2, 10> upsampler2;
	dsp::Decimator<oversample2, 10> decimator2;
	dsp::Upsampler<oversample4, 10> upsampler4;
	dsp::Decimator<oversample4, 10> decimator4;

	// RIGHT
	
	float y_a_right = 0.0f;
	float y_b_right = 0.0f;
	float y_c_right = 0.0f;
	float y_d_right = 0.0f;
	float W_a_right = 0.0f;
	float W_b_right = 0.0f;
	float W_c_right = 0.0f;

	float y_a_prev_right = 0.0f;
	float y_b_prev_right = 0.0f;
	float y_c_prev_right = 0.0f;
	float y_d_prev_right = 0.0f;
	float y_d_prev_prev_right = 0.0f;

	float W_a_prev_right = 0.0f;
	float W_b_prev_right = 0.0f;
	float W_c_prev_right = 0.0f;
	
	dsp::Upsampler<oversample2, 10> upsampler2_right;
	dsp::Decimator<oversample2, 10> decimator2_right;
	dsp::Upsampler<oversample4, 10> upsampler4_right;
	dsp::Decimator<oversample4, 10> decimator4_right;

	

	Flora() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(Flora::CUTOFF_PARAM, 0.0f, 1.0f, 0.0f, "Cutoff"," Hz",FREQ_MAX/FREQ_MIN, FREQ_MIN);
		configParam(Flora::CUTOFF_INFL_PARAM, 0.0f, 1.0f, 0.0f, "Cutoff CV", "%", 0.0f, 100.0f);
		configParam(Flora::RESONANCE_PARAM, 0.0f, RESONANCE_MAX, 0.0f, "Resonance", "%", 0.0f, 100.0f);
		configParam(Flora::RESONANCE_INFL_PARAM, 0.0f, RESONANCE_MAX/5.0f, 0.0f, "Resonance CV", "%", 0.0f, 500.0f);
		configParam(Flora::DRIVE_INFL_PARAM, 0.0f, DRIVE_MAX/5.0f, 0.0f, "Drive CV", "%", 0.0f, 125.0f);
		configParam(Flora::DRIVE_PARAM, 0.0f, DRIVE_MAX, 1.00f, "Drive", " dB", -10, 20);
		configBypass(FLORA_INPUT, FLORA_OUTPUT);
		configBypass(FLORA_INPUT2, FLORA_OUTPUT2);

		configInput(CUTOFF_INPUT, "1V/Oct Cutoff CV");
		configInput(RESONANCE_INPUT, "Resonance CV");
		configInput(DRIVE_INPUT, "Drive CV");

		configInput(FLORA_INPUT, "Left");
		configInput(FLORA_INPUT2, "Right");
		configOutput(FLORA_OUTPUT, "Left");
		configOutput(FLORA_OUTPUT2, "Right");
	}

	void process(const ProcessArgs &args) override;
	void process_left(const ProcessArgs &args, int oversample_protected, float drive, float inv_drive);
	void process_right(const ProcessArgs &args, int oversample_protected, float drive, float inv_drive);
	float toExp(float x, float min, float max);
	
	json_t *dataToJson() override {
		json_t *root = json_object();
		//json_object_set_new(root, "Gcomp", json_righteal((double) gComp));
		json_object_set_new(root, "oversample", json_integer(current_oversample));
		json_object_set_new(root, "autoLevel", json_boolean(autoLevel));
		return root;
	}

	void dataFromJson(json_t *rootJ) override {
		//json_t *ext = json_object_get(rootJ, "Gcomp");
		//if (ext)
		//	gComp = (float) json_number_value(ext);
		json_t *ext2 = json_object_get(rootJ, "autoLevel");
		if (ext2)
			autoLevel = json_boolean_value(ext2);
		json_t *ext3 = json_object_get(rootJ, "oversample");
		if (ext3) {
			current_oversample = json_integer_value(ext3);
			if (current_oversample != 2 and current_oversample != 4) {
				current_oversample = 4;
			}
		}
	}

	void onReset(const ResetEvent& e) override {
		gComp = 0.0f;
		autoLevel = false;
		current_oversample = 2;
		Module::onReset(e);
	}
};

float Flora::toExp(float x, float min, float max) {
	// 0 to 1 to exp range
	return min * exp( x*log(max/min) );
}



void Flora::process(const ProcessArgs &args) {
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


	if (!outputs[FLORA_OUTPUT].isConnected()) {
		outputs[FLORA_OUTPUT].setVoltage(0.0f);
	}
	if (!outputs[FLORA_OUTPUT2].isConnected()) {
		outputs[FLORA_OUTPUT2].setVoltage(0.0f);
	}
	if (!outputs[FLORA_OUTPUT].isConnected() and !outputs[FLORA_OUTPUT2].isConnected()) {
		return;
	}
	int oversample_protected = current_oversample;// to be sure its not modified from another thread inside step.
	float drive = clamp(params[DRIVE_PARAM].getValue()+inputs[DRIVE_INPUT].getVoltage()*params[DRIVE_INFL_PARAM].getValue(),0.0f,DRIVE_MAX);
	
	r     = clamp(params[RESONANCE_PARAM].getValue()+(inputs[RESONANCE_INPUT].getVoltage()*params[RESONANCE_INFL_PARAM].getValue()), 0.0f, RESONANCE_MAX);
	input_cutoff =  powf(2.0f, inputs[CUTOFF_INPUT].getVoltage()*params[CUTOFF_INFL_PARAM].getValue());
	float F_c   = clamp(this->toExp(params[CUTOFF_PARAM].getValue(),FREQ_MIN,FREQ_MAX)*input_cutoff, FREQ_MIN, FREQ_MAX);
	F_s   = args.sampleRate*oversample_protected;

	if (F_c != F_c_prev || F_s != F_s_prev) {
		double w_c = double(2.0f*M_PI*F_c/F_s);// cutoff in radians per sample.
		//g = V_t * ( 0.9892f*w_c-0.4342f*w_c*w_c+0.1381f*w_c*w_c*w_c-0.0202f*w_c*w_c*w_c*w_c); // old auto tuned g for cutoff
		g = V_t * (0.0008116984 + 0.9724111*w_c - 0.5077766*w_c*w_c + 0.1534058*w_c*w_c*w_c);// new auto tuned g for cutoff  4th order: y = 0.00007055354 + 0.9960577*x - 0.6082669*x^2 + 0.286043*x^3 - 0.05393212*x^4
		//g = V_t * (1.0f - exp(-2.0f*M_PI*F_c/F_s));// old naive
		//Gres = 1.0029f+0.0526f*w_c-0.0926f*w_c*w_c+0.0218f*w_c*w_c*w_c;// old auto tuned resonance power for resonance <= 1.0 (0.0218->0.218)
		Gres = 1.037174 + 3.606925*w_c + 7.074555*w_c*w_c - 18.14674*w_c*w_c*w_c + 9.364587*w_c*w_c*w_c*w_c;
	}
	
	float inv_drive = VCV_TO_MOOG*INPUT_TO_CAPACITOR*((autoLevel && drive != 0.0f)?clamp(drive,0.10f,DRIVE_MAX):1.0f);
	
	if (outputs[FLORA_OUTPUT].isConnected()) {
		this->process_left(args, oversample_protected, drive, inv_drive);
	}
	
	if (outputs[FLORA_OUTPUT2].isConnected()) {
		this->process_right(args, oversample_protected, drive, inv_drive);
	}
	
	F_c_prev = F_c;
	F_s_prev = F_s;
}

void Flora::process_left(const ProcessArgs &args, int oversample_protected, float drive, float inv_drive) {
	float in = inputs[FLORA_INPUT].getVoltage()*drive*VCV_TO_MOOG*INPUT_TO_CAPACITOR;
	float inInter [oversample_protected];
	float outBuf  [oversample_protected];
	if (oversample_protected == oversample2) {
		upsampler2.process(in, inInter);
	} else {
		upsampler4.process(in, inInter);
	}

	for (int i = 0; i < oversample_protected; i++) {
		// x is the voltage over the capacitor in the first stage:
		float x   = inInter[i] - 2.0f*r*Gres*(y_d_prev+y_d_prev_prev);//unit and a half feedback delay to get phaseshift close to 180 deg at cutoff.
		// -inInter[i]*Gcomp to make passband gain not decrease too much when turning up resonance. This was disabled due to lowered resonance power too much.
		
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
	float out;
	if (oversample_protected == oversample2) {
		out = decimator2.process(outBuf);
	} else {
		out = decimator4.process(outBuf);
	}
	if(!std::isfinite(out)) {
		out = 0.0f;
	}
	outputs[FLORA_OUTPUT].setVoltage(out/inv_drive);
}

void Flora::process_right(const ProcessArgs &args, int oversample_protected, float drive, float inv_drive) {
	float in = inputs[FLORA_INPUT2].getVoltage()*drive*VCV_TO_MOOG*INPUT_TO_CAPACITOR;
	float inInter [oversample_protected];
	float outBuf  [oversample_protected];
	if (oversample_protected == oversample2) {
		upsampler2_right.process(in, inInter);
	} else {
		upsampler4_right.process(in, inInter);
	}

	for (int i = 0; i < oversample_protected; i++) {
		// x is the voltage over the capacitor in the first stage:
		float x   = inInter[i] - 2.0f*r*Gres*(y_d_prev_right+y_d_prev_prev_right);//unit and a half feedback delay to get phaseshift close to 180 deg at cutoff.
		// -inInter[i]*Gcomp to make passband gain not decrease too much when turning up resonance. This was disabled due to lowered resonance power too much.
		
		// 1st transistor stage:
		y_a_right = y_a_prev_right+g*(non_lin_func( x/V_t )-W_a_prev_right);
		W_a_right = non_lin_func( y_a_right/V_t );
		// 2nd transistor stage:
		y_b_right = y_b_prev_right+g*(W_a_right-W_b_prev_right);
		W_b_right = non_lin_func( y_b_right/V_t );
		// 3rd transistor stage:
		y_c_right = y_c_prev_right+g*(W_b_right-W_c_prev_right);
		W_c_right = non_lin_func( y_c_right/V_t );
		// 4th transistor stage:
		y_d_right = y_d_prev_right+g*(W_c_right-non_lin_func( y_d_prev_right/V_t ));

		// record stuff for next step
		y_d_prev_prev_right = y_d_prev_right;
		y_a_prev_right = y_a_right;
		y_b_prev_right = y_b_right;
		y_c_prev_right = y_c_right;
		y_d_prev_right = y_d_right;

		W_a_prev_right = W_a_right;
		W_b_prev_right = W_b_right;
		W_c_prev_right = W_c_right;
		
		outBuf[i] = y_d_right;
	}
	float out;
	if (oversample_protected == oversample2) {
		out = decimator2_right.process(outBuf);
	} else {
		out = decimator4_right.process(outBuf);
	}
	if(!std::isfinite(out)) {
		out = 0.0f;
	}
	outputs[FLORA_OUTPUT2].setVoltage(out/inv_drive);
}

/*struct EmphasizeMenuItem : MenuItem {
	Flora* _module;
	float _priority;

	EmphasizeMenuItem(Flora* module, const char* label, float priority)
	: _module(module), _priority(priority)
	{
		this->text = label;
	}

	void onAction(EventAction &e) override {
		_module->gComp = _priority;
	}

	void process(const ProcessArgs &args) override {
		rightText = _module->gComp == _priority ? "✔" : "";
	}
};*/

struct AutoLevelMenuItem : MenuItem {
	Flora* _module;

	AutoLevelMenuItem(Flora* module, const char* label)
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


struct OversampleFloraMenuItem : MenuItem {
	Flora* _module;
	int _os;

	OversampleFloraMenuItem(Flora* module, const char* label, int os)
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

struct FloraWidget : ModuleWidget {
	FloraWidget(Flora *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/RetriModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(75, RACK_GRID_HEIGHT-275-HALF_KNOB_MED), module, Flora::CUTOFF_PARAM));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(40, RACK_GRID_HEIGHT-275-HALF_KNOB_SMALL), module, Flora::CUTOFF_INFL_PARAM));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(75, RACK_GRID_HEIGHT-205-HALF_KNOB_MED), module, Flora::RESONANCE_PARAM));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(40, RACK_GRID_HEIGHT-205-HALF_KNOB_SMALL), module, Flora::RESONANCE_INFL_PARAM));

		addParam(createParam<RoundSmallAutinnKnob>(Vec(40, RACK_GRID_HEIGHT-135-HALF_KNOB_SMALL), module, Flora::DRIVE_INFL_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(75, RACK_GRID_HEIGHT-135-HALF_KNOB_MED), module, Flora::DRIVE_PARAM));

		addInput(createInput<InPortAutinn>(Vec(10, RACK_GRID_HEIGHT-275-HALF_PORT), module, Flora::CUTOFF_INPUT));
		addInput(createInput<InPortAutinn>(Vec(10, RACK_GRID_HEIGHT-205-HALF_PORT), module, Flora::RESONANCE_INPUT));
		addInput(createInput<InPortAutinn>(Vec(10, RACK_GRID_HEIGHT-135-HALF_PORT), module, Flora::DRIVE_INPUT));

		addInput(createInput<InPortAutinn>(Vec(9 * RACK_GRID_WIDTH*0.15-HALF_PORT, 300), module, Flora::FLORA_INPUT));
		addInput(createInput<InPortAutinn>(Vec(9 * RACK_GRID_WIDTH*0.35-HALF_PORT, 300), module, Flora::FLORA_INPUT2));
		addOutput(createOutput<OutPortAutinn>(Vec(9 * RACK_GRID_WIDTH*0.60-HALF_PORT, 300), module, Flora::FLORA_OUTPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(9 * RACK_GRID_WIDTH*0.85-HALF_PORT, 300), module, Flora::FLORA_OUTPUT2));
		//addOutput(createOutput<OutPortAutinn>(Vec(9 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Flora::OUTPUT_W));
	}
	
	void appendContextMenu(Menu* menu) override {
		Flora* a = dynamic_cast<Flora*>(module);
		assert(a);

		//menu->addChild(new MenuLabel());
		//menu->addChild(new EmphasizeMenuItem(a, "Passband gain comp.",  1.0f));
		//menu->addChild(new EmphasizeMenuItem(a, "Medium compensation",  0.5f));
		//menu->addChild(new EmphasizeMenuItem(a, "No compensation", 0.0f));
		
		menu->addChild(new MenuLabel());
		menu->addChild(new OversampleFloraMenuItem(a, "Oversample x2", 2));
		menu->addChild(new OversampleFloraMenuItem(a, "Oversample x4", 4));
		menu->addChild(new MenuLabel());
		menu->addChild(new AutoLevelMenuItem(a, "Auto level"));
	}
};

Model *modelFlora = createModel<Flora, FloraWidget>("Retri");