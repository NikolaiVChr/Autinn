#include "Autinn.hpp"
#include <cmath>
#include <queue>

using std::queue;
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

#define HYSTERESIS_TIME_SEC               0.0003
//#define THRESHOLD_DEFAULT_NOISEGATE_DB  -70.0
//#define THRESHOLD_DEFAULT_EXPANDER_DB   -60.0
//#define THRESHOLD_DEFAULT_COMPRESSOR_DB  -6.0
#define THRESHOLD_DEFAULT_LIMITER_DB      7.5
#define THRESHOLD_LIMIT_LOW_DB          -15.0// a little less than 1 V
#define THRESHOLD_LIMIT_HIGH_DB           7.5//12.0V
//#define ATTACK_LOW_MS                     1.0//was 0.16
//#define ATTACK_HIGH_MS                 2600.0
#define ATTACK_LIMITER_LOW_MS             0.02
#define ATTACK_LIMITER_HIGH_MS           10.0
#define RELEASE_LOW_MS                    1.0
#define RELEASE_HIGH_MS                5000.0
//#define RMS_TIME_LOW_MS                   1.0
//#define RMS_TIME_HIGH_MS                350.0
//#define RMS_TIME_DEFAULT_MS             175.5
//#define COMPRESSOR_RATIO_MAX             10.0
//#define EXPANDER_RATIO_MIN                0.25
//#define KNEE_MAX_DB                      10.0
//#define KNEE_DEFAULT_DB                   5.0
#define MAKEUP_GAIN_MAX                  10.0//20dB
//#define SMOOTH_FILTER_POLE_SLEW         1000.0

struct Non : Module {
	enum ParamIds {
		//ATTACK_PARAM,
		RELEASE_PARAM,
		//RATIO_COMPRESSOR_PARAM,
		//RATIO_EXPANDER_PARAM,
		T_LIMITER_PARAM,
		//T_COMPRESSOR_PARAM,
		//T_EXPANDER_PARAM,
		//T_NOISEGATE_PARAM,
		//AVERAGE_TIME_PARAM,
		ATTACK_PEAK_PARAM,
		OUT_GAIN_PARAM,
		KNEE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		LEFT_INPUT,
		RIGHT_INPUT,
		/*N_INPUT,
		E_INPUT,
		C_INPUT,*/
		L_INPUT,
		SIDE_LEFT_INPUT,
		SIDE_RIGHT_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		LEFT_OUTPUT,
		RIGHT_OUTPUT,
		//DB,
		NUM_OUTPUTS
	};
	enum LightIds {
		//A,
		//B,
		C,
		//DD,
		E,
		ENUMS(VU_IN_LEFT_LIGHT, 15),
		ENUMS(VU_OUT_LEFT_LIGHT, 15),
		ENUMS(VU_IN_RIGHT_LIGHT, 15),
		ENUMS(VU_OUT_RIGHT_LIGHT, 15),
		NUM_LIGHTS
	};



	double g_prev = 1.0;
	double f_prev = 0.0;
	double peak_prev = 0.0;
	//double rms2_prev = 0.0;
	unsigned hysteresis = 0;
	bool attack = true;
	bool limiter = false;

	unsigned D = 2;
	queue <float> bufferL;
	queue <float> bufferR;

	// these are here to optimize so not to do expensive ops every step:
	//double ta = -150.0;
	double tap = -150.0;
	double tr = -150.0;
	//double ER = -150.0;
	//double CR = -150.0;
	//double taKnob = -150.0;
	double tapKnob = -150.0;
	double trKnob = -150.0;
	//double erKnob = -150.0;
	//double crKnob = -150.0;
	//double tavKnob = -150.0;
	double gainKnob = 0.0;
	double makeupGain = 1.0;
	double rate   = 0.0;
	double RT = 0.1;
	//double AT = 0.1;
	double ATp = 0.1;
	//double TAV = 0.03;
	//double k_prev = 1.0 - exp(-2.2 * 0.0000226 / 0.001 );// Just a starting value in aprox the range its going to be used in.


	// VU Meter stuff
	dsp::VuMeter2 vuMeterIn;
	dsp::VuMeter2 vuMeterIn2;
	dsp::VuMeter2 vuMeterOut;
	dsp::VuMeter2 vuMeterOut2;
	const float vuMaxDB = 22.5f;
	const float intervalDB = vuMaxDB/15.0f;
	unsigned short int step = 0;

	Non() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		//configParam(Non::T_NOISEGATE_PARAM,  THRESHOLD_LIMIT_LOW_DB, THRESHOLD_LIMIT_HIGH_DB, THRESHOLD_DEFAULT_NOISEGATE_DB, "Noisegate", " dB", 0.0f, 1.0f);
		//configParam(Non::T_EXPANDER_PARAM,   THRESHOLD_LIMIT_LOW_DB, THRESHOLD_LIMIT_HIGH_DB, THRESHOLD_DEFAULT_EXPANDER_DB, "Expander", " dB", 0.0f, 1.0f);
		//configParam(Non::T_COMPRESSOR_PARAM, THRESHOLD_LIMIT_LOW_DB, THRESHOLD_LIMIT_HIGH_DB, THRESHOLD_DEFAULT_COMPRESSOR_DB, "Compressor", " dB", 0.0f, 1.0f);
		configParam(Non::T_LIMITER_PARAM,    THRESHOLD_LIMIT_LOW_DB, THRESHOLD_LIMIT_HIGH_DB, THRESHOLD_DEFAULT_LIMITER_DB, "Limiter", " dB", 0.0f, 1.0f);
		//configParam(Non::AVERAGE_TIME_PARAM, RMS_TIME_LOW_MS, RMS_TIME_HIGH_MS, RMS_TIME_DEFAULT_MS, "Lookahead", " ms", 0.0f, 1.0f);
		//configParam(Non::ATTACK_PARAM, 0.0, 1.0, 0.5, "Attack", " ms", ATTACK_HIGH_MS / ATTACK_LOW_MS, ATTACK_LOW_MS);
		configParam(Non::RELEASE_PARAM, 0.0, 1.0, 0.5, "Release", " ms", RELEASE_HIGH_MS / RELEASE_LOW_MS, RELEASE_LOW_MS);
		configParam(Non::ATTACK_PEAK_PARAM, 0.0, 1.0, 0.5, "Limiter attack", " ms", ATTACK_LIMITER_HIGH_MS / ATTACK_LIMITER_LOW_MS, ATTACK_LIMITER_LOW_MS);
		//configParam(Non::RATIO_EXPANDER_PARAM, 1.0, 0.0, 1.0, "Expander ratio  1 ", "", 1.0f / EXPANDER_RATIO_MIN, EXPANDER_RATIO_MIN);
		//configParam(Non::RATIO_COMPRESSOR_PARAM, 0.0, 1.0, 0.0, "Compressor ratio", ":1", COMPRESSOR_RATIO_MAX / 1.0f, 1.0f);
		//configParam(Non::KNEE_PARAM, 0.0, KNEE_MAX_DB, KNEE_DEFAULT_DB, "Knee softness", " dB", 0.0f, 1.0f);
		configParam(Non::OUT_GAIN_PARAM, 0.0, 1.0, 0.0, "Makeup gain", " dB", 0.0f, 20.0f);

		//configLight(A, "Noise gate");
		//configLight(B, "Expander");
		configLight(C, "Unity");
		//configLight(DD, "Compressor");
		configLight(E, "Limiter");

		configInput(LEFT_INPUT, "Left audio");
		configInput(RIGHT_INPUT, "Right audio");
		configOutput(LEFT_OUTPUT, "Left audio");
		configOutput(RIGHT_OUTPUT, "Right audio");

		configInput(SIDE_LEFT_INPUT, "Sidechain audio left");
		configInput(SIDE_RIGHT_INPUT, "Sidechain audio right");
		//configInput(N_INPUT, "Noise gate threshold CV");
		//configInput(E_INPUT, "Expander threshold CV");
		//configInput(C_INPUT, "Compressor threshold CV");
		configInput(L_INPUT, "Limiter threshold CV");

		configBypass(LEFT_INPUT, LEFT_OUTPUT);
		configBypass(RIGHT_INPUT, RIGHT_OUTPUT);
	}

	double toDB(double volt);
	double toGain(double dB);
	double smooth(double k, double g_prev, double f);
	double peak(double x, double ATp, double RT);
	double peakW(double x, double ATp, double RT);
	//double rms(double x);
	//double staticCurve(double rms, double peak, double LT, double LS, double CS, double CT, double CR, 
	//				   double NT, double ET, double ES, double ER, double knee);
	double staticCurve(double peak, double LT, double LS);
	double toExp10(double x, double min, double max);

	void process(const ProcessArgs &args) override;
};

/*
	//dBMax = 7.6 dB = 12V
	//dBMin = -60 dB = 0.005V
	dBMax = 7.5 dB = 12V
	dB    = 0.0 dB =  5V
	dBMin = -70 dB =  0V (curve is asymptotic, but 0.0 is defined in CV input)

	LT = limiter threshold       PARAM dB
	CT = compressor threshold    PARAM dB
	ET = expander threshold      PARAM dB
	NT = noise gate threshold    PARAM dB
	AT = attack time parameter          
	ATp= peak attack time parameter
	RT = release time parameter
	TAV= RMS window time parameter
	TS = sampling interval       Rack controls ms
	ta = attacktime in ms        PARAM ms
	tr = releasetime in ms       PARAM ms
	t_M = RMS window time in ms  PARAM (5-130ms)
	T_A = time constant          Did I plan to use TS in seconds?
	CS = compressor slope
	ES = expander slope
	NS = noise slope
	R  = ratio                   PARAM (one for each)
	D  = lookahead time in samples
	X/Y=in/out-put               dB
	f  = control parameter
	k  = filter coefficient

	thresholds example (0dB = 5V):
	dB = 20*log10(voltage/5)
	V  = 10^(dB/20)*5
	LT = -10dB
	CT = -25dB
	ET = -45dB
	NT = -70dB

	ratio:
	R = X_dB(n)-CT/(Y_dB(n)-CT)

	slope:
	S = 1-1/R

	ratio:
	R = ∞, limiter,
	R > 1, compressor (CR: compressor ratio),
	0 <R< 1, expander (ER: expander ratio),
	R = 0, noise gate.

	level measurements:
	For |x(n)| > xPEAK(n - 1) :
	xPEAK(n) = (1 - ATp) * xPEAK(n - 1) + ATp * |x(n)|

	and for |x(n)| ≤ xPEAK(n - 1) :
	xPEAK(n) = (1 - RT) * xPEAK(n - 1)

	AT = 1 - exp( -2.2TS/(ta/1000))
	RT = 1 - exp( -2.2TS/(tr/1000))

	T_A = TS in ms
	TAV = 1 - exp(-2.2*T_A/(t_M/1000))

	xx_RMS(n) = (1-TAV)*xx_RMS(n-1)+TAV*x(n)^2

	smoothing: (aka attack/release time)
	g(n)=(1-k)*g(n-1)+k*f(n)         [k = AT or k=RT determined by hysteresis]


	TODO: upsample for RMS/peak detection. (hesitant)
	      optimize more?
	      Could some of the doubles be floats instead? I should have made more notes about my tests and considerations when I made it.
	      Since max samplerate in Rack has increased, D can cause:
	      		unsigned(768000Hz * 350 * 0.001) x bytes = 268800 x 4 = 1075200B [Its big, but doable]
		  Seperate lookahead time from RMS window time (5 to 130ms) and make lookahead fixed.
		  Auto makeupgain
		  Auto release



	      (old) design:

	                      Zod

	                   Thresholds
	      Noisegate Expander Compressor Limiter
	      -70dB to +7.5dB

	      Average    Attack   Release   Limiter-
	      								 Attack
	      1-350ms 0.16ms-2.6s 1ms-5s   0.02-10ms

	    L      Expansion Compression   Knee
	    C      off       off         hard-10dB
	    0
	    E                Out gain
	    N               -∞ to 8dB
*/

/*
	Non is a brute henchman of general Zod. Some day gotta make Ursa also to complete the Kryptonian trio.
	
	Changes from Zod:
		No transition from compressor, as there is non.
		Zod specific code is commented instead of removed to ease maintaining both at same time.
		Threshold only goes down to -15dB (0.89V)
		Attack knob goes lower, down to 0.02ms
		Lookahead is fixed duration.     Note to self: deprecate Zod, and make v2 version with this improvement.
		Better hysteresis code.          Note to self: deprecate Zod, and make v2 version with this improvement.
	TODO:
		More CV control?
*/


void Non::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[LEFT_OUTPUT].isConnected() && !outputs[RIGHT_OUTPUT].isConnected()) {
		return;
	}
	step++;

	double LT = params[T_LIMITER_PARAM].getValue();
	//double CT = params[T_COMPRESSOR_PARAM].getValue();
	//double ET = params[T_EXPANDER_PARAM].getValue();
	//double NT = params[T_NOISEGATE_PARAM].getValue();
	/*
	if (inputs[N_INPUT].isConnected()) {
		if (inputs[N_INPUT].getVoltage() == 0.0f) NT = -70.0;
		else NT = this->toDB(fabs(inputs[N_INPUT].getVoltage()));
		params[T_NOISEGATE_PARAM].setValue(NT);
	}
	if (inputs[E_INPUT].isConnected()) {
		if (inputs[E_INPUT].getVoltage() == 0.0f) ET = -70.0;
		else ET = this->toDB(fabs(inputs[E_INPUT].getVoltage()));
		params[T_EXPANDER_PARAM].setValue(ET);
	}
	if (inputs[C_INPUT].isConnected()) {
		if (inputs[C_INPUT].getVoltage() == 0.0f) CT = -70.0;
		else CT = this->toDB(fabs(inputs[C_INPUT].getVoltage()));
		params[T_COMPRESSOR_PARAM].setValue(CT);
	}*/
	if (inputs[L_INPUT].isConnected()) {
		if (inputs[L_INPUT].getVoltage() == 0.0f) LT = THRESHOLD_LIMIT_LOW_DB;
		else LT = this->toDB(fabs(inputs[L_INPUT].getVoltage()));
		params[T_LIMITER_PARAM].setValue(LT);
	}

	double TS = args.sampleTime * 1000.0; //ms

	//if (taKnob != params[ATTACK_PARAM].getValue() || tapKnob != params[ATTACK_PEAK_PARAM].getValue() || trKnob != params[RELEASE_PARAM].getValue() || erKnob != params[RATIO_EXPANDER_PARAM].getValue() || crKnob != params[RATIO_COMPRESSOR_PARAM].getValue() || rate != args.sampleRate || tavKnob != params[AVERAGE_TIME_PARAM].getValue() || gainKnob != params[OUT_GAIN_PARAM].getValue()) {
	if (tapKnob != params[ATTACK_PEAK_PARAM].getValue() || trKnob != params[RELEASE_PARAM].getValue() || rate != args.sampleRate || gainKnob != params[OUT_GAIN_PARAM].getValue()) {
		rate = args.sampleRate;
		//tavKnob = params[AVERAGE_TIME_PARAM].getValue();
		//D   = (unsigned)(rate * tavKnob * 0.001);
		
		//taKnob = params[ATTACK_PARAM].getValue();
		tapKnob = params[ATTACK_PEAK_PARAM].getValue();
		trKnob = params[RELEASE_PARAM].getValue();
		//erKnob = params[RATIO_EXPANDER_PARAM].getValue();
		//crKnob = params[RATIO_COMPRESSOR_PARAM].getValue();
		gainKnob = params[OUT_GAIN_PARAM].getValue();

		D   = (unsigned)(rate * 0.15 * 0.001);

		//ta = this->toExp10(taKnob,  ATTACK_LOW_MS, ATTACK_HIGH_MS);
		tap = this->toExp10(tapKnob, ATTACK_LIMITER_LOW_MS, ATTACK_LIMITER_HIGH_MS);
		tr = this->toExp10(trKnob,  RELEASE_LOW_MS, RELEASE_HIGH_MS);
		//ER = this->toExp10(erKnob,  EXPANDER_RATIO_MIN, 1.00);
		//CR = this->toExp10(crKnob,  1.00, COMPRESSOR_RATIO_MAX);
		makeupGain = this->toExp10(gainKnob, 1.00, MAKEUP_GAIN_MAX);

		RT  = 1.0 - exp(-2.2 * TS / tr );
		//AT  = 1.0 - exp(-2.2 * TS / ta );
		ATp = 1.0 - exp(-2.2 * TS / tap);

		//double t_M = TS * D;
		//TAV = 1.0 - exp(-2.2 * TS / t_M);
	}

	unsigned hyst_max = HYSTERESIS_TIME_SEC / args.sampleTime;
	//unsigned hyst_max_attack = tapKnob / args.sampleTime;

	// inputs:
	float left  = inputs[LEFT_INPUT].getVoltage();
	float right = inputs[RIGHT_INPUT].getVoltage();
	bufferL.push(left);
	bufferR.push(right);
	float pastL = bufferL.front();
	float pastR = bufferR.front();
	while (bufferL.size() > D) {
		bufferL.pop();
		bufferR.pop();
	}
	double stereo = left + right;

	if (inputs[SIDE_LEFT_INPUT].isConnected() || inputs[SIDE_RIGHT_INPUT].isConnected()) {
		stereo = inputs[SIDE_LEFT_INPUT].getVoltage() + inputs[SIDE_RIGHT_INPUT].getVoltage();
	}

	//double knee = params[KNEE_PARAM].getValue();//dB

	// some values:
	//double CS = 1.0 - 1.0 / CR;
	//double ES = 1.0 - 1.0 / ER;
	double LS = 1.0;

	// level measurement:
	//double peak = this->peakW(stereo, ATp, RT); // Slightly slower version
	double peak = this->peak(stereo, ATp, RT);
	//double rms  = this->rms(stereo);

	// static curve:
	double f = this->staticCurve(peak, LT, LS);

	// smoothing filter:
	double k = 0.0;
	if (f >= g_prev && attack) {
		// We are in attack and want release, hyst starts counting towards release
		hysteresis += 1;
	} else if (f >= g_prev && !attack) {
		// We are in release and want to release even further, hyst not activating
		hysteresis = 0;
	} else if (f < g_prev && !attack) {
		// We are in release and want attack, hyst starts counting towards attack
		hysteresis += 1;
	} else if (f < g_prev && attack) {
		// We are in attack and want to keep that, hyst not activating
		hysteresis = 0;
	}
	if (hysteresis > hyst_max && attack) {// _attack
		hysteresis = 0;
		attack = false;
	} else if (hysteresis > hyst_max && !attack) { // _release
		hysteresis = 0;
		attack = true;
	}
	if (attack) {
		//if (limiter) {
			k = ATp;
		/*} else {
			k = AT;
		}*/
	} else {
		k = RT;
	}
	//k = slew(k, k_prev, SMOOTH_FILTER_POLE_SLEW, args.sampleTime);

	double g = this->smooth(k, g_prev, f);

	// apply gain:
	float outL = pastL * g;
	float outR = pastR * g;
	if (!std::isfinite(outL) || !std::isfinite(outR)) {
		outL = 0.0;
		outR = 0.0;
		peak_prev = 1.0;
		peak = 1.0;
		//rms2_prev = 1.0;
		g_prev = 1.0;
		f_prev = 1.0;
		g = 1.0;
		f = 1.0;
	}
	outL *= makeupGain;
	outL = non_lin_func(outL / 12.0f) * 12.0f;
	outputs[LEFT_OUTPUT].setVoltage(outL);
	outR *= makeupGain;
	outR = non_lin_func(outR / 12.0f) * 12.0f;
	outputs[RIGHT_OUTPUT].setVoltage(outR);


	// VU meters
	vuMeterIn.process(args.sampleTime, pastL * 0.1f);
	vuMeterIn2.process(args.sampleTime, pastR * 0.1f);
	vuMeterOut.process(args.sampleTime, outL * 0.1f);
	vuMeterOut2.process(args.sampleTime, outR * 0.1f);
	//vuMeterOut.mode = dsp::VuMeter2::RMS;
	for (int v = 0; step == 512 && v < 15; v++) {
		lights[VU_IN_LEFT_LIGHT + 14 - v].setBrightness(vuMeterIn.getBrightness(-intervalDB * (v + 1), -intervalDB * v));
		lights[VU_IN_RIGHT_LIGHT + 14 - v].setBrightness(vuMeterIn2.getBrightness(-intervalDB * (v + 1), -intervalDB * v));
		lights[VU_OUT_LEFT_LIGHT + 14 - v].setBrightness(vuMeterOut.getBrightness(-intervalDB * (v + 1), -intervalDB * v));
		lights[VU_OUT_RIGHT_LIGHT + 14 - v].setBrightness(vuMeterOut2.getBrightness(-intervalDB * (v + 1), -intervalDB * v));
	}
	if (step == 512) {
		step = 0;
	}

	// set previous values for next step:
	peak_prev = peak;
	g_prev = g;
	f_prev = f;
	//k_prev = k;
}



double Non::toExp10(double x, double min, double max) {
	// 0 to 1 to exp range
	return min * pow(10.0, x * log10(max / min));
}

//double Non::staticCurve(double rms, double peak, double LT, double LS, double CS, double CT, double CR, 
//						double NT, double ET, double ES, double ER, double knee) {
double Non::staticCurve(double peak, double LT, double LS) {
	limiter = false;

	double peak_dB = this->toDB(peak);
	double G = 0.0;

	//lights[A].value  = 0.0;
	//lights[B].value  = 0.0;
	//lights[C].value  = 0.0;
	//lights[DD].value = 0.0;
	//lights[E].value  = 0.0;
	if (peak_dB > LT) {// hard knee:
		// limiter
		G = (peak_dB - LT) * (-LS);// - CS * (LT - CT);
		lights[E].value = 1.0;
		lights[C].value  = 0.0;
		limiter = true;
	} else {
		/*double x_dB = this->toDB(sqrt(rms));
		double CTknee = CT - knee * 0.5;
		double ETknee = ET - knee * 0.5;
		if (x_dB < NT) {// hard knee:
			// noise gate
			lights[A].value = 1.0;
			return 0.0;
		} else if (x_dB < ETknee) {
			// full expander
			G = (x_dB - ET) * (-ES);
			lights[B].value = 1.0;
		} else if (x_dB < ETknee + knee) {
			// semi expander
			G = -(1.0 / ER - 1.0) * pow(x_dB - ET - knee * 0.5, 2.0) / (2.0 * knee);
			lights[B].value = 0.5;
			lights[C].value = 0.5;
		} else if (x_dB < CTknee) {*/
			// neutral
			lights[C].value = 1.0;
			lights[E].value  = 0.0;
			G = 0.0;
		/*} else if (knee > 0.0 && x_dB < CTknee + knee) {
			// semi compressor
			lights[DD].value = 0.5;
			lights[C].value  = 0.5;
			G = (1.0 / CR - 1.0) * pow(x_dB - CT + knee * 0.5, 2.0) / (2.0 * knee);
		} else {
			// full compressor
			lights[DD].value = 1.0;
			G = (x_dB - CT) * (-CS);
		}*/
		// n | e | 1 | c | l
	}
	return toGain(G);
}

/*double Non::rms(double x) {
	double rms2 = (1.0 - TAV) * rms2_prev + TAV * x * x;
	rms2_prev = rms2;
	return rms2;
}*/

double Non::peak(double x, double ATp, double RT) {
	double xAbs = fabs(x);
	if (xAbs > peak_prev) {
		return (1.0 - ATp) * peak_prev + ATp * xAbs;
	}
	return (1.0 - RT) * peak_prev;
}

double Non::peakW(double x, double ATp, double RT) {
	double xAbs = fabs(x);
	double xPeak = fmax(0, xAbs - peak_prev) * ATp + peak_prev;
	if (xAbs > peak_prev) {
		RT = 0;
	}
	xPeak -= RT * peak_prev;
	return xPeak;
}

double Non::smooth(double k, double g_prev, double f) {
	return (1.0 - k) * g_prev + k * f;
}

double Non::toDB(double volt) {
	return 20.0 * log10(volt / 5.0);
}

double Non::toGain(double dB) {
	return pow(10.0, (dB / 20.0)); //I don't multiply with 5v here as its a ratio.
}

struct NonWidget : ModuleWidget {
	NonWidget(Non *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/NonModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		//addParam(createParam<RoundMediumAutinnKnob>(Vec(16 * RACK_GRID_WIDTH * 0.20 - HALF_KNOB_MED, 75), module, Non::T_NOISEGATE_PARAM));
		//addParam(createParam<RoundMediumAutinnKnob>(Vec(16 * RACK_GRID_WIDTH * 0.40 - HALF_KNOB_MED, 75), module, Non::T_EXPANDER_PARAM));
		//addParam(createParam<RoundMediumAutinnKnob>(Vec(16 * RACK_GRID_WIDTH * 0.60 - HALF_KNOB_MED, 75), module, Non::T_COMPRESSOR_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(10 * RACK_GRID_WIDTH * 0.25 - HALF_KNOB_MED, 75), module, Non::T_LIMITER_PARAM));

		//addParam(createParam<RoundMediumAutinnKnob>(Vec(10 * RACK_GRID_WIDTH * 0.20 - HALF_KNOB_MED, 135), module, Non::AVERAGE_TIME_PARAM));
		//addParam(createParam<RoundMediumAutinnKnob>(Vec(16 * RACK_GRID_WIDTH * 0.40 - HALF_KNOB_MED, 135), module, Non::ATTACK_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(10 * RACK_GRID_WIDTH * 0.75 - HALF_KNOB_MED, 135), module, Non::RELEASE_PARAM));
		addParam(createParam<RoundMediumAutinnKnob>(Vec(10 * RACK_GRID_WIDTH * 0.25 - HALF_KNOB_MED, 135), module, Non::ATTACK_PEAK_PARAM));

		//addParam(createParam<RoundMediumAutinnKnob>(Vec(16 * RACK_GRID_WIDTH * 0.235 - HALF_KNOB_MED, 200), module, Non::RATIO_EXPANDER_PARAM));
		//addParam(createParam<RoundMediumAutinnKnob>(Vec(16 * RACK_GRID_WIDTH * 0.48 - HALF_KNOB_MED, 200), module, Non::RATIO_COMPRESSOR_PARAM));
		//addParam(createParam<RoundMediumAutinnKnob>(Vec(16 * RACK_GRID_WIDTH * 0.725 - HALF_KNOB_MED, 200), module, Non::KNEE_PARAM));

		addParam(createParam<RoundMediumAutinnKnob>(Vec(10 * RACK_GRID_WIDTH * 0.5 - HALF_KNOB_MED, 195), module, Non::OUT_GAIN_PARAM));

		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH * 0.15 - HALF_PORT, 245), module, Non::LEFT_INPUT));
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH * 0.30 - HALF_PORT, 245), module, Non::RIGHT_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(16 * RACK_GRID_WIDTH * 0.15 - HALF_PORT, 275), module, Non::LEFT_OUTPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(16 * RACK_GRID_WIDTH * 0.30 - HALF_PORT, 275), module, Non::RIGHT_OUTPUT));
		//addOutput(createOutput<OutPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.75-HALF_PORT, 75), module, Non::DB));
		//addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH * 0.55 - HALF_PORT, 305), module, Non::N_INPUT));
		//addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH * 0.65 - HALF_PORT, 305), module, Non::E_INPUT));
		//addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH * 0.75 - HALF_PORT, 305), module, Non::C_INPUT));
		addInput(createInput<InPortAutinn>(Vec(10 * RACK_GRID_WIDTH * 0.50 - HALF_PORT, 75 - HALF_PORT + HALF_KNOB_MED), module, Non::L_INPUT));

		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH * 0.15 - HALF_PORT, 325), module, Non::SIDE_LEFT_INPUT));
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH * 0.30 - HALF_PORT, 325), module, Non::SIDE_RIGHT_INPUT));

		//addChild(createLight<SmallLight<YellowLight>>(Vec(16 * RACK_GRID_WIDTH * 0.10 - HALF_LIGHT_SMALL, 317.5), module, Non::A));
		//addChild(createLight<SmallLight<GreenLight>>(Vec(16 * RACK_GRID_WIDTH * 0.18 - HALF_LIGHT_SMALL, 317.5), module, Non::B));
		addChild(createLight<MediumLight<BlueLight>>(Vec(10 * RACK_GRID_WIDTH * 0.85 - HALF_LIGHT_MEDIUM,100), module, Non::C));
		//addChild(createLight<SmallLight<GreenLight>>(Vec(16 * RACK_GRID_WIDTH * 0.34 - HALF_LIGHT_SMALL, 317.5), module, Non::DD));
		addChild(createLight<MediumLight<RedLight>>(Vec(10 * RACK_GRID_WIDTH * 0.85 - HALF_LIGHT_MEDIUM,  75), module, Non::E));

		float light_x_pos = 0.475f;
		float light_y_pos = 260.0f;
		float light_column_dist = HALF_LIGHT_SMALL * 6.0f;
		float light_y_spacing = HALF_LIGHT_SMALL * 2.0f; //0.75dB
		for (int i = 0; i < 11; i++) {
			addChild(createLight<SmallLight<GreenLight>>(Vec(16 * RACK_GRID_WIDTH * light_x_pos - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Non::VU_IN_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<GreenLight>>(Vec(16 * RACK_GRID_WIDTH * light_x_pos - HALF_LIGHT_SMALL + HALF_LIGHT_SMALL * 2.0f, light_y_pos - light_y_spacing * i), module, Non::VU_IN_RIGHT_LIGHT + i));
			addChild(createLight<SmallLight<GreenLight>>(Vec(16 * RACK_GRID_WIDTH * light_x_pos - HALF_LIGHT_SMALL + light_column_dist, light_y_pos - light_y_spacing * i), module, Non::VU_OUT_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<GreenLight>>(Vec(16 * RACK_GRID_WIDTH * light_x_pos - HALF_LIGHT_SMALL + light_column_dist + HALF_LIGHT_SMALL * 2.0f, light_y_pos - light_y_spacing * i), module, Non::VU_OUT_RIGHT_LIGHT + i));
		}
		for (int i = 11; i < 14; i++) {
			addChild(createLight<SmallLight<YellowLight>>(Vec(16 * RACK_GRID_WIDTH * light_x_pos - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Non::VU_IN_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<YellowLight>>(Vec(16 * RACK_GRID_WIDTH * light_x_pos - HALF_LIGHT_SMALL + HALF_LIGHT_SMALL * 2.0f, light_y_pos - light_y_spacing * i), module, Non::VU_IN_RIGHT_LIGHT + i));
			addChild(createLight<SmallLight<YellowLight>>(Vec(16 * RACK_GRID_WIDTH * light_x_pos - HALF_LIGHT_SMALL + light_column_dist, light_y_pos - light_y_spacing * i), module, Non::VU_OUT_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<YellowLight>>(Vec(16 * RACK_GRID_WIDTH * light_x_pos - HALF_LIGHT_SMALL + light_column_dist + HALF_LIGHT_SMALL * 2.0f, light_y_pos - light_y_spacing * i), module, Non::VU_OUT_RIGHT_LIGHT + i));
		}
		for (int i = 14; i < 15; i++) {
			addChild(createLight<SmallLight<RedLight>>(Vec(16 * RACK_GRID_WIDTH * light_x_pos - HALF_LIGHT_SMALL, light_y_pos - light_y_spacing * i), module, Non::VU_IN_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<RedLight>>(Vec(16 * RACK_GRID_WIDTH * light_x_pos - HALF_LIGHT_SMALL + HALF_LIGHT_SMALL * 2.0f, light_y_pos - light_y_spacing * i), module, Non::VU_IN_RIGHT_LIGHT + i));
			addChild(createLight<SmallLight<RedLight>>(Vec(16 * RACK_GRID_WIDTH * light_x_pos - HALF_LIGHT_SMALL + light_column_dist, light_y_pos - light_y_spacing * i), module, Non::VU_OUT_LEFT_LIGHT + i));
			addChild(createLight<SmallLight<RedLight>>(Vec(16 * RACK_GRID_WIDTH * light_x_pos - HALF_LIGHT_SMALL + light_column_dist + HALF_LIGHT_SMALL * 2.0f, light_y_pos - light_y_spacing * i), module, Non::VU_OUT_RIGHT_LIGHT + i));
		}
	}
};

Model *modelNon = createModel<Non, NonWidget>("Non");
