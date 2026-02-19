#include "Autinn.hpp"
#include "Autinn-dsp.hpp"
#include <cmath>
#include <cstdio>

static constexpr int BUFFER_SIZE = 1 << 22;// 2^20 (5.4 seconds at 192khz) - 2^22 (22 seconds at 192khz)
static constexpr int BUFFER_MASK = BUFFER_SIZE - 1;
static constexpr float DIVS_VERT = 8.0f;// total vert divs (audio scope std)
static constexpr float DIVS_HORIZ = 20.0f;// total horiz divs (approx effective 1:1)
static constexpr float DIVS_VERT_INV = 1.0f/DIVS_VERT;
static constexpr float DIVS_HORIZ_INV = 1.0f/DIVS_HORIZ;
static constexpr float TRIG_AUTO_MIN_TIMEOUT = 0.04f;    // seconds
static constexpr float TRIG_AUTO_MAX_TIMEOUT = 0.2f;    // seconds
static constexpr double AUTO_TIME_PERIOD_MAX = 10.0; // seconds
static constexpr double AUTO_TIME_PERIOD_MIN = 0.000025; // seconds, 40kHz
constexpr int TRIG_SOURCE_EXT = 4;
static constexpr float TRIG_HYSTERESIS = 0.1f; // will be multiplied by V/Div except for ext. trigger
static constexpr float AUTO_TIME_KNOB_OFF = 50.0f;
static constexpr float BLINK_HZ = 2.0f;
constexpr int TRIG_MODE_AUTO = 0; // wait TRIG_AUTO_MIN_TIMEOUT then trigger even if no trigger found
constexpr int TRIG_MODE_NORM = 1; // wait forever for trigger to be found
constexpr int TRIG_MODE_SOLO = 2; // freeze when finding trigger
constexpr int TRIG_MODE_XY   = 3; // Lissajous
constexpr bool TRIG_EDGE_RISE = true;
constexpr bool TRIG_EDGE_FALL = false;
constexpr int STATS_OFF = 0;
constexpr int STATS_ONE = 1;
constexpr int STATS_ALL = 2;
static constexpr float SCALE_DEFAULT = 2.0f;
constexpr int SCALE_DEFAULT_KNOB = 3;
static constexpr float SCALE_KNOB_MIN = -1.0f; // OFF
static constexpr float SCALE_KNOB_MAX = 11.0f; // 5 mV/Div
static constexpr float TIME_KNOB_MIN = -5.0f; //   from  10µs
static constexpr float TIME_KNOB_MAX = 0.0f; //      to   1s
static constexpr float HOLDOFF_KNOB_MIN = -4.0f;// from 100µs
static constexpr float HOLDOFF_KNOB_MAX = 1.0f; //   to  10s
static constexpr float TRIG_FOUND_TIMER = 0.1f;

// display
static constexpr float WAVE_PX_OFFSET = 0.5f;
static constexpr float WAVE_START_PX = -1.0;
static constexpr float WAVEFORM_SAMPLES_PER_PX = 64.0f;
static constexpr float WAVEFORM_PX_PER_SAMPLE = 1.0f/WAVEFORM_SAMPLES_PER_PX;
static constexpr int XY_SAMPLE_DECIMATION = 6000;// 6000 points is enough to look like a smooth curve on a 1080p screen.
static constexpr int STATS_DECIMATION_THRESHOLD = 24000;//   scanning up to 16000 before we bother optimizing.
static constexpr float STROKE_WAVE = 0.8f;
static constexpr float PX_WAVE = 0.5f;
static constexpr float STROKE_SCANLINE = 1.0f;
static constexpr float STROKE_XY = 1.0f;
static constexpr float STROKE_TRIGGER = 0.8f;
static constexpr float STROKE_TRIGGER_ALPHA = 0.75f;
static constexpr float FONTSIZE_STATS = 12.0f;
static constexpr float FONTSIZE_TRIGGER = 12.0f;
static constexpr NVGlineCap LINEJOIN_WAVE_ZOOM_OUT = NVG_BEVEL;// NVG_ROUND, NVG_BEVEL, NVG_MITER
static constexpr NVGlineCap LINECAP_WAVE_ZOOM_OUT = NVG_BUTT;// NVG_BUTT, NVG_SQUARE, NVG_ROUND
static constexpr NVGlineCap LINEJOIN_WAVE_ZOOM_IN = NVG_BEVEL;// NVG_ROUND, NVG_BEVEL, NVG_MITER
static constexpr NVGlineCap LINECAP_WAVE_ZOOM_IN = NVG_BUTT;// NVG_BUTT, NVG_SQUARE, NVG_ROUND
static constexpr NVGlineCap LINEJOIN_XY = NVG_ROUND;// NVG_ROUND, NVG_BEVEL, NVG_MITER
static constexpr NVGlineCap LINECAP_XY = NVG_ROUND;// NVG_BUTT, NVG_SQUARE, NVG_ROUND
static constexpr int ALPHA_WAVE = 255;
static constexpr int ALPHA_XY = 200;
static const NVGcolor colorLabels = nvgRGBA(0, 0, 0, 160);  // black, transparent
static const NVGcolor color0 = nvgRGBA(255, 230, 50, ALPHA_WAVE);  // Yellow
static const NVGcolor color1 = nvgRGBA(255, 55, 55, ALPHA_WAVE);   // Red
static const NVGcolor color2 = nvgRGBA(50, 255, 50, ALPHA_WAVE);    // Green
static const NVGcolor color3 = nvgRGBA(50, 150, 255, ALPHA_WAVE);  // Blue
static const NVGcolor colorExt = nvgRGBA(255, 255, 255, 255);// white
static const NVGcolor colorScanLine = nvgRGBA(255, 255, 255, 90);// faint white
static const NVGcolor colorXY1 = nvgRGBA(100, 255, 200, ALPHA_XY);//cyan
static const NVGcolor colorXY2 = nvgRGBA(255, 100, 255, ALPHA_XY);//magenta
static const NVGcolor colorBaseline = nvgRGBA(255, 255, 255, 100);// faint white
static const NVGcolor colorCenterline = nvgRGBA(200, 200, 200, 100);//light gray

static std::vector<std::string> scales = {
	"OFF","20 V/Div","10 V/Div","5 V/Div","2 V/Div", "1 V/Div","0.5 V/Div",
	"0.2 V/Div","0.1 V/Div","50 mV/Div","20 mV/Div","10 mV/Div", "5 mV/Div"
};
static float getScale(int knob) {
	switch (knob) {
	case -1: return -1.0f;
	case 0: return 20.0f;
	case 1: return 10.0f;
	case 2: return  5.0f;
	case 3: return  2.0f;
	case 4: return  1.0f;
	case 5: return  0.5f;
	case 6: return  0.2f;
	case 7: return  0.1f;
	case 8: return  0.05f;
	case 9: return  0.02f;
	case 10: return  0.01f;
	case 11: return 0.005f;
	default: return SCALE_DEFAULT;
	}
}

struct Scope : Module {

	enum ParamIds {
		POS_A_PARAM,
		POS_B_PARAM,
		POS_C_PARAM,
		POS_D_PARAM,
		TIME_PARAM,
		HOLDOFF_PARAM,
		TRIG_LEVEL_PARAM,
		TRIG_SOURCE_PARAM,
		TRIG_MODE_PARAM,
		TRIG_EDGE_PARAM,
		FREEZE_PARAM,
		AUTO_TIME_PARAM,
		STATS_PARAM,
		SCALE_A_PARAM,
		SCALE_B_PARAM,
		SCALE_C_PARAM,
		SCALE_D_PARAM,
		ENUMS(CV_OR_AUDIO_PARAM, 4),
		DEBUG_1,
		DEBUG_2,
		NUM_PARAMS
	};
	enum InputIds {
		A_INPUT,
		B_INPUT,
		C_INPUT,
		D_INPUT,
		CV_TRIG_EXT_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		CV_TRIG_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(TRIG_SOURCE_LIGHT_RGB, 3),
		TRIG_MODE_AUTO_LIGHT,
		TRIG_MODE_NORM_LIGHT,
		TRIG_MODE_SOLO_LIGHT,
		TRIG_MODE_XY_LIGHT,
		TRIG_EDGE_FALL_LIGHT,
		TRIG_EDGE_RISE_LIGHT,
		ENUMS(FREEZE_LIGHT_RGB, 3),
		AUTO_TIME_LIGHT,
		TRIG_FOUND_LIGHT,
		ENUMS(CV_OR_AUDIO_LIGHT, 4),
		NUM_LIGHTS
	};

	dsp::SchmittTrigger trigSchmitt;
	dsp::BooleanTrigger trigPulse;
	dsp::BooleanTrigger srcBtnTrig;
	dsp::BooleanTrigger modeBtnTrig;
	dsp::BooleanTrigger edgeBtnTrig;
	dsp::BooleanTrigger freezeBtnTrig;
	dsp::BooleanTrigger autoTimeBtnTrig;
	dsp::BooleanTrigger statsBtnTrig;
	dsp::BooleanTrigger cvModeTrig[4];
	dsp::PulseGenerator trigOutPulse;

	// transient
	float buffer[4][BUFFER_SIZE] = {};
	std::atomic<int> writeIndex = {0};
	std::atomic<int> triggerIndex = {0}; // last valid trigger
	std::atomic<int> lastTriggerIndex = {0};
	std::atomic<bool> recording = {false}; // We found trigger, we are now filling enough data into buffer to fill display.
	std::atomic<bool> triggerValid = {false};       // triggerIndex is valid
	std::atomic<bool> prev_triggerValid = {false};       // lastTriggerIndex is valid
	float sampleRate = 44100.0f;
	std::atomic<bool> frozen = {false};
	bool freezePending = false;
	double period_s = 0.0; // time since last actual trigger event. Does not count up during freeze.
	std::atomic<int> samplesSinceTrigger = {0};  // samples since last trigger. Trigger as in, triggered outside holdoff durations.
	float holdoffTime_s = 0.0f;     // Remaining holdoff in seconds
	float autoTrigTimer_s = 0.0f;   // Time since we in AUTO saw a trigger
	int dspFrame = 1001; // 1000 of these and we do lights and controls.
	std::atomic<float> autoTimeFrequency_hz = {0.0f};
	float blinkPhase = 0.0f;
	float autoTimeKnob = AUTO_TIME_KNOB_OFF;
	float trigFoundTimer = 0.0f; // Remaining time for trigger light and stats TRIGGER to be shown.
	std::atomic<bool> bufferFilled = {false}; // We wrapped the buffers at least once, so they has no garbage.
	DCBlocker dcBlockers[4];
	float lastSampleTime = 1.0f/44100.0f;


	// persisted
	bool autoTimeMode = false;
	int trigSource = 0; // 0-3: Channel, 4: Ext
	std::atomic<int> trigMode = {TRIG_MODE_AUTO};
	bool trigEdge = TRIG_EDGE_RISE;
	bool showBaselines = false;
	bool showCenterline = false;
	bool showGrid = true;
	int showStats = STATS_ONE;
	int autoTimePeriods = 3;
	bool cvMode[4] = {false, false, false, false};
	bool acCoupled[4] = {false, false, false, false};

	// controls
	bool sourceBtn = false;
	bool trigModeKnob = false;
	bool trigEdgeBtn = false;
	bool autotimeBtn = false;
	bool freezeBtn = false;
	bool statsBtn = false;
	float offset[4]{};
	float scale[4]{};
	float thresholdKnob = 0.0f;
	float threshold2 = 0.1f;
	float holdoffKnob = -3.0f;


	Scope() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		// pos (we use DIVS_HORIZ, due to X-Y sideways offset)
		configParam(POS_A_PARAM, -DIVS_HORIZ*0.25f, DIVS_HORIZ*0.25f, 0.0, "Channel A Pos", " Div");
		configParam(POS_B_PARAM, -DIVS_HORIZ*0.25f, DIVS_HORIZ*0.25f, 0.0, "Channel B Pos", " Div");
		configParam(POS_C_PARAM, -DIVS_HORIZ*0.25f, DIVS_HORIZ*0.25f, 0.0, "Channel C Pos", " Div");
		configParam(POS_D_PARAM, -DIVS_HORIZ*0.25f, DIVS_HORIZ*0.25f, 0.0, "Channel D Pos", " Div");

		// scale
		configSwitch(SCALE_A_PARAM, (float)SCALE_KNOB_MIN, (float)SCALE_KNOB_MAX, (float)SCALE_DEFAULT_KNOB, "Channel A Scale", scales);
		configSwitch(SCALE_B_PARAM, (float)SCALE_KNOB_MIN, (float)SCALE_KNOB_MAX, (float)SCALE_DEFAULT_KNOB, "Channel B Scale", scales);
		configSwitch(SCALE_C_PARAM, (float)SCALE_KNOB_MIN, (float)SCALE_KNOB_MAX, (float)SCALE_DEFAULT_KNOB, "Channel C Scale", scales);
		configSwitch(SCALE_D_PARAM, (float)SCALE_KNOB_MIN, (float)SCALE_KNOB_MAX, (float)SCALE_DEFAULT_KNOB, "Channel D Scale", scales);

		// Time
		configParam(TIME_PARAM, TIME_KNOB_MIN, TIME_KNOB_MAX, -3.0f, "Time / Div", " s", 10.0f);
		configParam(HOLDOFF_PARAM, HOLDOFF_KNOB_MIN, HOLDOFF_KNOB_MAX, -3.0f, "Trigger holdoff", " s", 10.0f);

		// Trigger
		configParam(TRIG_LEVEL_PARAM, -10.0f, 10.0f, 0.0f, "Trigger threshold", " V");
		configButton(TRIG_SOURCE_PARAM, "Trigger source");
		configButton(TRIG_MODE_PARAM, "Trigger mode");
		configButton(TRIG_EDGE_PARAM, "Trigger edge");
		configButton(FREEZE_PARAM, "Freeze");
		configButton(AUTO_TIME_PARAM, "Auto time");
		configButton(STATS_PARAM, "Cycle stats");
		for (int i = 0; i < 4; i++) {
			configButton(CV_OR_AUDIO_PARAM+i, "Toggle CV or audio input");
		}

		// inputs
		configInput(A_INPUT, "Channel A");
		configInput(B_INPUT, "Channel B");
		configInput(C_INPUT, "Channel C");
		configInput(D_INPUT, "Channel D");
		configInput(CV_TRIG_EXT_INPUT, "Ext. trigger");

		// outputs
		configInput(CV_TRIG_OUTPUT, "Trigger (ignore freezes and holdoff)");

		// lights
		configLight(TRIG_SOURCE_LIGHT_RGB, "Trigger source");
		configLight(TRIG_MODE_AUTO_LIGHT, "Auto trigger");
		configLight(TRIG_MODE_NORM_LIGHT, "Norm trigger");
		configLight(TRIG_MODE_SOLO_LIGHT, "Single trigger");
		configLight(TRIG_MODE_XY_LIGHT, "X-Y (A-B and/or C-D)");
		configLight(TRIG_EDGE_RISE_LIGHT, "Trigger on rising edge");
		configLight(TRIG_EDGE_FALL_LIGHT, "Trigger on falling edge");
		configLight(FREEZE_LIGHT_RGB, "Freeze");
		configLight(AUTO_TIME_LIGHT, "Auto time");
		configLight(TRIG_FOUND_LIGHT, "Trigger found");
		for (int i = 0; i < 4; i++) {
			configLight(CV_OR_AUDIO_LIGHT+i, "Input is CV");
		}

		//configParam(DEBUG_1, 0.1f, 2.0f, STROKE_WAVE, "DEBUG STROKE", " px");
		//configParam(DEBUG_2, 100.0f, 255.0f, ALPHA_WAVE, "DEBUG ALPHA", " ");

		readControls();
		for (auto & chDcBlocker : dcBlockers) {
			chDcBlocker.cutoff_hz = 0.1f;
			chDcBlocker.setSampleTime(lastSampleTime);
		}
	}

	void onReset(const ResetEvent& e) override {

		for (int c = 0; c < 4; c++) {
			cvModeTrig[c].reset();
			dcBlockers[c].reset();
			cvMode[c] = false;
			acCoupled[c] = false;
		}
		trigSchmitt.reset();
		trigPulse.reset();
		srcBtnTrig.reset();
		modeBtnTrig.reset();
		edgeBtnTrig.reset();
		freezeBtnTrig.reset();
		autoTimeBtnTrig.reset();
		statsBtnTrig.reset();
		trigOutPulse.reset();

		triggerValid = false;
		prev_triggerValid = false;
		frozen = false;
		freezePending = false;
		period_s = 0.0;
		samplesSinceTrigger = 0;
		holdoffTime_s = 0.0f;
		autoTrigTimer_s = 0.0f;
		dspFrame = 1001;
		autoTimeFrequency_hz = 0.0f;
		blinkPhase = 0.0f;
		autoTimeKnob = AUTO_TIME_KNOB_OFF;
		trigFoundTimer = 0.0f;

		autoTimeMode = false;
		trigSource = 0; // 0-3: Channel, 4: Ext
		trigMode = TRIG_MODE_AUTO;
		trigEdge = TRIG_EDGE_RISE;
		showBaselines = false;
		showCenterline = false;
		showGrid = true;
		showStats = STATS_ONE;
		autoTimePeriods = 3;

		Module::onReset(e);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "trigSource", json_integer(trigSource));
		json_object_set_new(rootJ, "trigMode", json_integer(trigMode));
		json_object_set_new(rootJ, "trigEdge", json_boolean(trigEdge));
		json_object_set_new(rootJ, "autoTimeMode", json_boolean(autoTimeMode));
		json_object_set_new(rootJ, "showStats", json_integer(showStats));
		json_object_set_new(rootJ, "showGrid", json_boolean(showGrid));
		json_object_set_new(rootJ, "showCenterline", json_boolean(showCenterline));
		json_object_set_new(rootJ, "showBaselines", json_boolean(showBaselines));
		json_object_set_new(rootJ, "autoTimePeriods", json_integer(autoTimePeriods));

		json_t* acdcJ = json_array();
		for (int i = 0; i < 4; i++) {
			json_array_append_new(acdcJ, json_boolean(acCoupled[i]));
		}
		json_object_set_new(rootJ, "acCoupling", acdcJ);

		json_t* modesJ = json_array();
		for (int i = 0; i < 4; i++) {
			json_array_append_new(modesJ, json_boolean(cvMode[i]));
		}
		json_object_set_new(rootJ, "renderModes", modesJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* sJ = json_object_get(rootJ, "trigSource");
		if (sJ) trigSource = int(json_integer_value(sJ));

		json_t* mJ = json_object_get(rootJ, "trigMode");
		if (mJ) trigMode = int(json_integer_value(mJ));

		json_t* eJ = json_object_get(rootJ, "trigEdge");
		if (eJ) trigEdge = json_is_true(eJ);

		json_t* aJ = json_object_get(rootJ, "autoTimeMode");
		if (aJ) autoTimeMode = json_is_true(aJ);

		json_t* stJ = json_object_get(rootJ, "showStats");
		if (stJ) {
			bool b = json_is_true(stJ);
			int i = int(json_integer_value(stJ));
			if (i == 0 && b) showStats = STATS_ONE;//backwards compat
			else showStats = i;
		}

		json_t* gJ = json_object_get(rootJ, "showGrid");
		if (gJ) showGrid = json_is_true(gJ);

		json_t* cJ = json_object_get(rootJ, "showCenterline");
		if (cJ) showCenterline = json_is_true(cJ);

		json_t* bJ = json_object_get(rootJ, "showBaselines");
		if (bJ) showBaselines = json_is_true(bJ);

		json_t* pJ = json_object_get(rootJ, "autoTimePeriods");
		if (pJ) autoTimePeriods = int(json_integer_value(pJ));

		json_t* acJ = json_object_get(rootJ, "acCoupled");
		if (acJ) {
			bool b = json_is_true(acJ);
			acCoupled[0] = b;
			acCoupled[1] = b;
			acCoupled[2] = b;
			acCoupled[3] = b;
		} else {
			json_t* modesJ = json_object_get(rootJ, "acCoupling");
			if (modesJ) {
				for (int i = 0; i < 4; i++) {
					const json_t* ac2J = json_array_get(modesJ, i);
					if (ac2J) acCoupled[i] = json_is_true(ac2J);
				}
			}
		}

		json_t* modesJ = json_object_get(rootJ, "renderModes");
		if (modesJ) {
			for (int i = 0; i < 4; i++) {
				const json_t* cvJ = json_array_get(modesJ, i);
				if (cvJ) cvMode[i] = json_is_true(cvJ);
			}
		}
	}

	void process(const ProcessArgs& args) override {

		/*
		// We need the scope to flatline when no signal is connected,
		// and we need the lights to not be stuck. And trigger CV out to work if ext. trigger is hooked up.
		if (!inputs[A_INPUT].isConnected() && !inputs[B_INPUT].isConnected() && !inputs[C_INPUT].isConnected() && !inputs[D_INPUT].isConnected()) {
			return;
		}
		*/

		if (lastSampleTime != args.sampleTime) {
			for (auto & chDcBlocker : dcBlockers) {
				chDcBlocker.setSampleTime(args.sampleTime);
			}
		}
		lastSampleTime = args.sampleTime;

		sampleRate = args.sampleRate;

		float in[4] = {};

		if (!frozen) {
			period_s += args.sampleTime;


			for (int c = 0; c < 4; c++) {
				in[c] = inputs[A_INPUT + c].getVoltage();

				if (acCoupled[c]) {
					in[c] = dcBlockers[c].process(in[c]);
				}

				buffer[c][writeIndex] = in[c];
			}

			writeIndex = (writeIndex + 1) & BUFFER_MASK;
			if (writeIndex == 0) bufferFilled = true;
		} else {
			for (int c = 0; c < 4; c++) {
				in[c] = inputs[A_INPUT + c].getVoltage();
				if (acCoupled[c]) {
					in[c] = dcBlockers[c].process(in[c]);
				}
			}
		}

		const bool edgeFound = triggerDetect(args, in);

		if (!frozen) {
			triggerResponse(args, edgeFound);
		}

		blinkPhase += args.sampleTime * BLINK_HZ * 0.5f;
		if (blinkPhase >= 1.0f) blinkPhase -= 1.0f;

		dspFrame++;
		if (dspFrame > 1000) {
			dspFrame = 0;
			if (period_s > 3600.0) period_s = 0.0;
			updateLights();
			readControls();
			autoTime();
		}

		outputs[CV_TRIG_OUTPUT].setVoltage(trigOutPulse.process(args.sampleTime) ? 10.0f : 0.0f);
	}

	/*
	 * TODO:
	 *		Stats: Duty cycle %, period, pulse width, crest factor, rise time, fall time, overshoot, compare phase.
	 *
	 */

	bool triggerDetect(const ProcessArgs& args, const float* in) {
		// Get trigger signal
		float trigSig = 0.0f;
		float hysteresis = TRIG_HYSTERESIS; // Default for Ext (100mV)
		if (trigSource < TRIG_SOURCE_EXT) {
			trigSig = in[trigSource];
			const float vPerDiv = scale[trigSource];
			if (vPerDiv > -0.5f && vPerDiv < 1.0f) {
				// We only scale it down. No reason it should ever get larger than 0.1V.
				hysteresis *= vPerDiv;
			}
		} else {
			trigSig = inputs[CV_TRIG_EXT_INPUT].getVoltage();
		}

		float threshold = thresholdKnob;

		// Holdoff
		if (holdoffTime_s > 0.0f) {
			// its fine that this counts down while being frozen, as unfreezing will reset it anyways.
			holdoffTime_s -= args.sampleTime;
		}
		bool holdoff_active = holdoffTime_s > 0.0f;

		if (trigFoundTimer > 0.0f) {
			trigFoundTimer -= args.sampleTime;
		}

		// Schmitt-trigger processing
		// Invert input for falling edge
		// Hysteresis window: 0.1V x V/div
		float signal = trigSig;
		if (trigEdge == TRIG_EDGE_FALL) {
			signal = -signal;
			//hysteresis = -hysteresis;
			threshold2 = threshold-hysteresis;
			threshold = -threshold;
		} else {
			threshold2 = threshold+hysteresis;
		}
		bool schmittState = trigSchmitt.process(signal, threshold, threshold+hysteresis);

		bool edgeFound = trigPulse.process(schmittState);

		if (edgeFound) {
			if (!frozen) {
				if (!holdoff_active) {
					bool periodValid = period_s < AUTO_TIME_PERIOD_MAX && period_s > AUTO_TIME_PERIOD_MIN;
					if (periodValid) {
						autoTimeFrequency_hz = (float)(1.0 / period_s);
					} else {
						//autoTimeFrequency_hz = 0.0f;
					}
					trigFoundTimer = TRIG_FOUND_TIMER;
				}
				period_s = 0.0;
			}
			trigOutPulse.trigger();
		}
		return edgeFound;
	}

	void triggerResponse(const ProcessArgs& args, bool edgeFound) {

		float timePerDiv = getTimeDiv();
		// We want to record DIVS_HORIZ divisions after the trigger to fill the screen
		float totalScreenTime = DIVS_HORIZ * timePerDiv;
		int samplesToRecord = int(totalScreenTime * sampleRate);

		if (samplesToRecord > BUFFER_SIZE) samplesToRecord = BUFFER_SIZE;
		if (samplesToRecord < 32) samplesToRecord = 32;

		bool holdoff_active = holdoffTime_s > 0.0f;

		if (recording) {
			// Recording
			// We have triggered, now we fill the buffer for the rest of the display
			++samplesSinceTrigger;

			if (samplesSinceTrigger >= samplesToRecord) {
				// buffer full
				recording = false;

				// Set holdoff
				holdoffTime_s = holdoffKnob > 0.00011f?holdoffKnob:0.0f;

				if (trigMode == TRIG_MODE_SOLO || freezePending) {
					frozen = true;
					freezePending = false;
				}
			}
		} else {
			// scanning for trigger
			if (!holdoff_active) {
				if (edgeFound) {
					// switch to recording
					lastTriggerIndex.store(triggerIndex);
					prev_triggerValid.store(triggerValid);
					triggerIndex.store(writeIndex);
					triggerValid.store(true);
					samplesSinceTrigger.store(0);
					recording.store(true);
					autoTrigTimer_s = 0.0f;
				}

				if (trigMode == TRIG_MODE_AUTO || trigMode == TRIG_MODE_XY) {
					autoTrigTimer_s += args.sampleTime;
					// If no trigger for screen time, force update

					// 25Hz = 0.04s
					// TRIG_AUTO_MIN_TIMEOUT prevents the CPU from going hot on extremely fast time
					// TRIG_AUTO_MAX_TIMEOUT prevents user on very slow time to think the scope got stuck doing nothing.
					float timeout = clamp(totalScreenTime, TRIG_AUTO_MIN_TIMEOUT, TRIG_AUTO_MAX_TIMEOUT);

					if (autoTimeFrequency_hz > 0.01f) {
						float knownPeriod = 1.0f / autoTimeFrequency_hz;
						// If the known period is longer than the screen time, use the period as the timeout
						if (knownPeriod > timeout) {
							timeout = knownPeriod;
						}
					}

					// tiny buffer so we don't preempt a valid trigger that is just beyond the screen
					timeout *= 1.05f;

					//timeout = std::min(TRIG_AUTO_MAX_TIMEOUT, timeout);

					if (autoTrigTimer_s > timeout) {
						// Force rolling trigger
						lastTriggerIndex.store(triggerIndex);
						triggerIndex = (writeIndex - samplesToRecord) & BUFFER_MASK;//only used for freezing.
						recording = false;
						triggerValid = false;
						prev_triggerValid = false;
						samplesSinceTrigger = 0;
						autoTrigTimer_s = 0.0f;


						if (freezePending) {
							frozen = true;
							freezePending = false;
						}
					}
				}
			}
		}
	}

	void autoTime() {
		if (autoTimeMode && autoTimeFrequency_hz > 0.01f) {
			// Time since last trigger
			double period = 1.0/autoTimeFrequency_hz;

			// Calculate ideal time/div to show 3 periods
			// 3 periods fill 1 screen
			double targetTimePerDiv_s = (period * double(autoTimePeriods)) / DIVS_HORIZ;

			// clamp to prevent log(0)
			if (targetTimePerDiv_s < 1e-5) targetTimePerDiv_s = 1e-5;
			autoTimeKnob = std::log10((float)targetTimePerDiv_s);
		} else {
			autoTimeKnob = AUTO_TIME_KNOB_OFF;
		}
	}

	float getTimeDiv() {
		float timePerDiv;
		if (autoTimeKnob > AUTO_TIME_KNOB_OFF - 1.0f) {
			// auto time have not set a time/div, so we read knob
			timePerDiv = std::pow(10.f, params[TIME_PARAM].getValue());
		} else {
			timePerDiv = std::pow(10.f, autoTimeKnob);
		}
		return timePerDiv;
	}

	void readControls() {
		sourceBtn = (bool)params[TRIG_SOURCE_PARAM].getValue();
		trigModeKnob = (bool)params[TRIG_MODE_PARAM].getValue();
		trigEdgeBtn = (bool)params[TRIG_EDGE_PARAM].getValue();
		freezeBtn = (bool)params[FREEZE_PARAM].getValue();
		autotimeBtn = (bool)params[AUTO_TIME_PARAM].getValue();
		statsBtn = (bool)params[STATS_PARAM].getValue();
		thresholdKnob = params[TRIG_LEVEL_PARAM].getValue();
		holdoffKnob = std::pow(10.f,params[HOLDOFF_PARAM].getValue());
		for (int ch = 0; ch < 4; ch++) {
			offset[ch] = params[POS_A_PARAM + ch].getValue();
			float kn = params[SCALE_A_PARAM + ch].getValue();
			if (kn > -0.5f) {
				scale[ch] = getScale(int(std::round(kn)));
			} else {
				scale[ch] = -1.0f;
			}
			bool cvModeBtn = (bool)params[CV_OR_AUDIO_PARAM + ch].getValue();
			if (cvModeTrig[ch].process(cvModeBtn)) {
				cvMode[ch] = !cvMode[ch];
			}
		}

		// time knob we skip here

		if (srcBtnTrig.process(sourceBtn)) {
			trigSource = (trigSource + 1) % 5;
			autoTimeFrequency_hz = 0.0f;
			prev_triggerValid = false;
			triggerValid = false;
			holdoffTime_s = 0.0f;// stop holdoff when switching source.
			recording = false;
		}
		if (modeBtnTrig.process(trigModeKnob)) {
			trigMode = (trigMode + 1) % 4;
			if (trigMode == TRIG_MODE_XY) frozen = false;
			prev_triggerValid = false;
			triggerValid = false;
			holdoffTime_s = 0.0f;// stop holdoff when switching mode.
			recording = false;
		}
		if (edgeBtnTrig.process(trigEdgeBtn)) {
			trigEdge = !trigEdge;
		}
		if (autoTimeBtnTrig.process(autotimeBtn)) {
			autoTimeMode = !autoTimeMode;
		}
		if (trigMode == TRIG_MODE_XY) {
			/*
			autoTimeMode = false;
			lastTriggerIndex = 0;
			triggerIndex = 0;
			autoTimeFrequency_hz = 0.0f;
			recording = false;
			triggerValid = false;
			prev_triggerValid = false;
			*/
			freezePending = false;
		}
		if (freezeBtnTrig.process(freezeBtn)) {
			if (freezePending || frozen) {
				if (frozen) holdoffTime_s = 0.0f;
				frozen = false;
				freezePending = false;
			} else if (trigMode == TRIG_MODE_XY) {
				// In lissajous we freeze instantly
				freezePending = false;
				frozen = true;
			} else {
				freezePending = true;
			}
		}
		if (statsBtnTrig.process(statsBtn)) {
			showStats++;
			if (showStats > STATS_ALL) showStats = STATS_OFF;
		}
	}

	static float getRed(int ch) {
		switch (ch) {
			case 0: return 0.9f;
			case 1: return 1.0f;
			case 2: return 0.0f;//0.2f;
			case 3: return 0.0f;//0.2f;
			default: return 0.0f;
		}
	}

	static float getGreen(int ch) {
		switch (ch) {
			case 0: return 0.8f;
			case 1: return 0.0f;//0.2f;
			case 2: return 0.8f;
			case 3: return 0.0f;//0.6f;
			default: return 0.0f;
		}
	}

	static float getBlue(int ch) {
		switch (ch) {
			case 0: return 0.0f;//0.2f;
			case 1: return 0.0f;//0.2f;
			case 2: return 0.0f;//0.2f;
			case 3: return 1.0f;
			default: return 0.0f;
		}
	}

	void updateLights() {
		float trigFoundBrightness = 0.0f;
		if (trigFoundTimer > 0.0f) {
			trigFoundBrightness = 1.0f;
		} else {
			trigFoundBrightness = 0.0f;
		}
		lights[TRIG_FOUND_LIGHT].setBrightness(trigFoundBrightness);

		float blinkBrightness = 1.0f;
		if (!recording && !frozen) {
			if (holdoffTime_s > 0) {
				// holdoff timeout
				blinkBrightness = 0.4f + 0.6f * std::pow((std::sin(blinkPhase * 2.0f * float(M_PI)) + 1.0f) / 2.0f, 2.0f);
			} else {
				// scanning for trigger
				if (int(blinkPhase * 6.0f) % 2 == 0) blinkBrightness = 0.1f;
			}
		}

		for (int ch = 0; ch < 4; ch++) {
			lights[CV_OR_AUDIO_LIGHT + ch].setBrightness(float(cvMode[ch]));
		}

		bool lissajous = trigMode==TRIG_MODE_XY;

		lights[TRIG_SOURCE_LIGHT_RGB + 0].setBrightness(getRed(trigSource)*blinkBrightness);
		lights[TRIG_SOURCE_LIGHT_RGB + 1].setBrightness(getGreen(trigSource)*blinkBrightness);
		lights[TRIG_SOURCE_LIGHT_RGB + 2].setBrightness(getBlue(trigSource)*blinkBrightness);

		lights[TRIG_MODE_AUTO_LIGHT].setBrightness(trigMode==TRIG_MODE_AUTO ? 1.0f : 0.0f);
		lights[TRIG_MODE_NORM_LIGHT].setBrightness(trigMode==TRIG_MODE_NORM ? 1.0f : 0.0f);
		lights[TRIG_MODE_SOLO_LIGHT].setBrightness(trigMode==TRIG_MODE_SOLO ? 1.0f : 0.0f);
		lights[TRIG_MODE_XY_LIGHT].setBrightness(lissajous ? 1.0f : 0.0f);

		lights[TRIG_EDGE_RISE_LIGHT].setBrightness(trigEdge == TRIG_EDGE_RISE? 1.0f : 0.0f);
		lights[TRIG_EDGE_FALL_LIGHT].setBrightness(trigEdge == TRIG_EDGE_FALL? 1.0f : 0.0f);

		lights[AUTO_TIME_LIGHT].setBrightness(autoTimeMode ? 1.0f : 0.0f);

		lights[FREEZE_LIGHT_RGB+0].setBrightness(frozen || freezePending? 1.0f : 0.0f);
		lights[FREEZE_LIGHT_RGB+1].setBrightness(freezePending ? 1.0f : 0.0f);
		//lights[FREEZE_LIGHT_RGB+2].setBrightness(frozen ? 0.0f : 0.0f);
	}
};













static std::string fontPath;

struct ScopeDisplay : OpaqueWidget {
	Scope* module{};
	int frame = 0;

	float lastTrigLevel = -999.0f;
	float trigVisibilityTimer = 0.0f;

	mutable int cachedIteratorStep = 1;
	mutable double lastSamplesPerPixel = 0.0f;

	const float column0 = 0.0f;
	const float column1 = 0.975f*1.0f/6.0f;
	const float column2 = 0.975f*2.0f/6.0f;
	const float column3 = 0.975f*2.8f/6.0f;
	const float column4 = 0.975f*4.4f/6.0f;
	const float column5 = 0.975f*5.0f/6.0f;

	ScopeDisplay() = default;

	static void drawText(const DrawArgs& args, const std::vector<std::string>& text, const float fontSize, const float y, const std::vector<float>& x, const NVGcolor color) {
		if (text[0].empty()) return;

		nvgBeginPath(args.vg);
		nvgFillColor(args.vg, color);

		for (int i = 0; i < text.size() ; i++) {
			nvgText(args.vg, x[i], y, text[i].c_str(), nullptr);
		}
	}

	static void setupFont(const DrawArgs& args, const float fontSize) {
		if (fontPath.empty()) {
			fontPath = asset::system("res/fonts/ShareTechMono-Regular.ttf");
			//fontPath = asset::plugin(pluginInstance, "res/fonts/FragmentMono-Regular.ttf");
		}
		if (!fontPath.empty()) {
			std::shared_ptr<Font> font = APP->window->loadFont(fontPath);

			if (font) {
				nvgFontFaceId(args.vg, font->handle);
			}
		}
		nvgFontSize(args.vg, fontSize);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE );
		nvgTextLetterSpacing(args.vg, 0.0f);
		nvgFontBlur(args.vg, 0.0f);
	}

	static NVGcolor getColor(const int ch) {
		switch (ch) {
		case 0: return color0;
		case 1: return color1;
		case 2: return color2;
		case 3: return color3;
		default: return colorExt;
		}
	}

	void drawStaticWaveform(const DrawArgs& args) const {

		// Center line
		nvgBeginPath(args.vg);
		nvgStrokeColor(args.vg, colorBaseline);
		nvgStrokeWidth(args.vg, 1.0f);
		nvgMoveTo(args.vg, 0, box.size.y * 0.5f);
		nvgLineTo(args.vg, box.size.x, box.size.y * 0.5f);
		nvgStroke(args.vg);

		// Damped sine
		nvgBeginPath(args.vg);
		nvgStrokeColor(args.vg, color2);
		nvgStrokeWidth(args.vg, 2.0f); // Bold line
		nvgLineJoin(args.vg, NVG_ROUND);

		const float cy = box.size.y * 0.5f;
		const float w = box.size.x;
		const float amp = box.size.y * 0.4f;

		for (float x = 0; x <= w; x += 3.0f) {
			// normalize x
			float t = (x / w - 0.5f) * 2.0f;

			// High freq sin * gaussian
			float y = std::sin(t * 20.0f) * std::exp(-5.0f * t * t);

			float py = cy - (y * amp);

			if (x == 0) nvgMoveTo(args.vg, x, py);
			else nvgLineTo(args.vg, x, py);
		}
		nvgStroke(args.vg);
	}

	void drawWaveform(const DrawArgs& args, int ch) const {
		if (!module) return;
		if (!module->inputs[Scope::A_INPUT + ch].isConnected()) return;

		float scale = module->scale[ch];
		if (scale < -0.5f) return;
		float offset = module->offset[ch];
		float timePerDiv_s = module->getTimeDiv();

		const float width_px = box.size.x/PX_WAVE;
		const float totalTime_s = DIVS_HORIZ * timePerDiv_s;
		const float samplesToDraw = totalTime_s * module->sampleRate;

		if (samplesToDraw < 2.0f) return;

		int idxWrite = module->writeIndex.load();
		int idxTrigger = module->triggerIndex.load();
		int idxLastTrig = module->lastTriggerIndex.load();
		int sSinceTrig = module->samplesSinceTrigger.load();
		bool idxValid = module->triggerValid.load();
		bool idxLastValid = module->prev_triggerValid.load();
		bool recording = module->recording.load();
		//float hz = module->autoTimeFrequency_hz.load();
		bool frozen = module->frozen.load();
		int trigMode = module->trigMode.load();


		double samplesPerPixel = samplesToDraw / width_px;

		// We calculate the number of samples in a single cycle of 20kHz (limit of human hearing).
		// If a pixel covers less time than this, we draw smooth vector lines (preserves shape).
		// If a pixel covers more time, we switch to min/max bars.
		float aliasingThreshold = 0.5f * module->sampleRate / 20000.0f;

		bool zoomedOut = samplesPerPixel > aliasingThreshold && !module->cvMode[ch];

		nvgBeginPath(args.vg);
		const NVGcolor color = getColor(ch);
		nvgStrokeColor(args.vg, color);

		int iteratorStep = 1;
		aliasingThreshold = std::min(aliasingThreshold * 8.0f, WAVEFORM_SAMPLES_PER_PX);
		if (samplesPerPixel > aliasingThreshold) {
			iteratorStep = (int)(samplesPerPixel / aliasingThreshold);
			if (iteratorStep < 1) iteratorStep = 1;
		}

		int idxAnchor = idxTrigger;

		int drawLimit_px = int(width_px)+1;
		const bool holdoffActive = module->holdoffTime_s > 0.0f;
		if (recording) { //  || holdoffActive is not needed, as all data is new when holdoff is active
			// we only draw enough pixels to reach writeIndex from trigger
			double validPixels = (double)sSinceTrig / samplesPerPixel;
			drawLimit_px = (int)validPixels;

			if (drawLimit_px > int(width_px)+1) drawLimit_px = int(width_px)+1;
			if (drawLimit_px < 0) drawLimit_px = 0;
		} else if (!idxValid && !frozen && trigMode == TRIG_MODE_AUTO) {
			// Calculate where the screen starts relative to the write head
			idxAnchor = (idxWrite - int(samplesToDraw)) & BUFFER_MASK;
		}

		// update the quality setting?
		// If we are not recording (rolling/Scanning), we update continuously so the UI is responsive.
		// If we are recording, we only update at the very start of the sweep (first ~2 pixels).
		// This guarantees the entire scanline uses the same decimation factor.
		bool startOfScan = sSinceTrig < (samplesPerPixel * 2.0);
		bool shouldUpdate = !recording || startOfScan;

		// Only update the cached step if the zoom changed by > 1%
		// This filters out floating point jitter and keeps the peaks from flickering due to varying decimation.
		auto changeRatio = 1.0f;
		if (lastSamplesPerPixel > 0.0) {
			changeRatio = static_cast<float>(std::abs(samplesPerPixel - lastSamplesPerPixel) / lastSamplesPerPixel);
		}
		if (shouldUpdate || changeRatio > 0.001f || iteratorStep == 1) {
			// User moved the knob significantly, update decimation amount
			cachedIteratorStep = iteratorStep;
			lastSamplesPerPixel = samplesPerPixel;
		}

		// Use the stable cached value, to avoid varying decimation from display frame to frame make peaks flicker.
		// If we use samplesPerPixel here, the signal jitters. We must use the cached value.
		if (lastSamplesPerPixel > 0.0f) {
			samplesPerPixel = lastSamplesPerPixel;
		}
		iteratorStep = cachedIteratorStep;

		bool first = true;
		float lastY = 0.0f;

		if (zoomedOut) {
			nvgLineCap(args.vg, LINECAP_WAVE_ZOOM_OUT);
			nvgLineJoin(args.vg, LINEJOIN_WAVE_ZOOM_OUT);
			nvgStrokeWidth(args.vg, STROKE_WAVE);
			bool wasNewData = true;
			for (int curr_px = int(WAVE_START_PX); curr_px <= int(width_px)+1; curr_px += 1) {
				// left: new
				// right: old
				// extreme right: ahead of bufferhead
				bool isNewData = true;
				if (recording) {
					isNewData = curr_px <= drawLimit_px;
				}

				// If we drawing past writeIndex, then we draw old data from previous trigger.
				// else we draw from current trigger.
				const int startIdx = isNewData ? idxAnchor : idxLastTrig;

				if (!isNewData && !idxLastValid) {
					// We are asked to draw history, but we have no history yet.
					// Stop drawing.
					break;
				}

				int sampleOffset = (int)(curr_px * samplesPerPixel);
				if (sampleOffset >= BUFFER_SIZE) {
					// whole buffer does not fit on screen
					// we stop drawing.
					break;
				}
				int readIndex = (startIdx + sampleOffset) & BUFFER_MASK;

				if (!isNewData) {
					// Distance from new trigger to this readIndex
					int distFromNew = (readIndex - idxTrigger) & BUFFER_MASK;

					if (distFromNew >= 0 && distFromNew < sSinceTrig) {
						// distFromNew is small and positive, it means this old pixel
						// is wrapped in the buffer. As in, we have drawn the entire buffer
						// and if we continue, we will be repeating data.
						break;
					}
				}

				const int iterStart = (int)(curr_px * samplesPerPixel);
				int iterEnd = (int)((curr_px + 1) * samplesPerPixel);
				if (iterEnd <= iterStart) iterEnd = iterStart + 1;

				if (!recording && trigMode == TRIG_MODE_AUTO) {
					// rolling in AUTO
					int readIdx = (startIdx + iterEnd) & BUFFER_MASK;

					// Calculate distance from Read Head to Write Head
					int distToHead = (idxWrite - readIdx) & BUFFER_MASK;

					// If the distance is huge
					// it means readIdx is actually ahead of writeIndex.
					// If the distance is tiny, we are catching up to the head.

					// If we are too close to the write head from the wrong side
					// stop drawing.
					if (distToHead > BUFFER_SIZE - 4000) {
						break;
					}
				}

				float minV = 100.0f;
				float maxV = -100.0f;
				bool found = false;
				for (int readIndexOffset = iterStart; readIndexOffset < iterEnd; readIndexOffset += iteratorStep) {
					if (recording && isNewData && readIndexOffset >= sSinceTrig) {
						// we bumped into old data
						break;
					}
					int readIndexRaw = (startIdx + readIndexOffset) & BUFFER_MASK;
					const float v = module->buffer[ch][readIndexRaw];
					if (v < minV) minV = v;
					if (v > maxV) maxV = v;
					found = true;
				}

				if (found) {
					float yTop = volt2PxVert(maxV, offset, scale);
					float yBottom = volt2PxVert(minV, offset, scale);

					yTop = clamp(yTop, -10000.0f, box.size.y+10000.0f);
					yBottom = clamp(yBottom, -10000.0f, box.size.y+10000.0f);

					auto px = float(curr_px)*PX_WAVE+WAVE_PX_OFFSET;
					if (first|| (wasNewData && !isNewData)) {
						nvgMoveTo(args.vg, px, yTop);
						nvgLineTo(args.vg, px, yBottom);
						lastY = yBottom;
						first = false;
					} else {
						float distToTop = std::abs(lastY - yTop);
						float distToBottom = std::abs(lastY - yBottom);

						if (distToTop < distToBottom) {
							// closer to the top
							nvgLineTo(args.vg, px, yTop);
							nvgLineTo(args.vg, px, yBottom);
							lastY = yBottom;
						} else {
							// closer to the bottom.
							nvgLineTo(args.vg, px, yBottom);
							nvgLineTo(args.vg, px, yTop);
							lastY = yTop;
						}
					}
				}
				wasNewData = isNewData;
			}
		} else {
			// zoomed in
			nvgStrokeWidth(args.vg, STROKE_WAVE);
			nvgLineCap(args.vg, LINECAP_WAVE_ZOOM_IN);
			nvgLineJoin(args.vg, LINEJOIN_WAVE_ZOOM_IN);

			float stepWidth = 1.0f;
			if (samplesPerPixel < 1.0f) {
				// If 1 sample is 10 pixels wide, we step 10 pixels at a time
				stepWidth = 1.0f / (float)samplesPerPixel;
			}

			bool wasNewData = true;

			for (float curr_px = WAVE_START_PX*stepWidth; curr_px <= width_px + stepWidth; curr_px += stepWidth) {

				int sampleOffset = (int)std::round(curr_px * samplesPerPixel);
				if (sampleOffset >= BUFFER_SIZE) {
					// whole buffer does not fit on screen
					// we stop drawing.
					break;
				}

				// This eliminates scanline jitter.
				// left: new
				// right: old
				// extreme right: ahead of bufferhead
				bool isNewData = true;
				if (recording) {
					if (sampleOffset >= sSinceTrig) {
						isNewData = false;
					}
				}

				// If we drawing past writeIndex, then we draw old data from previous trigger.
				// else we draw from current trigger.
				const int startIdx = isNewData ? idxAnchor : idxLastTrig;

				if (!isNewData && !idxLastValid) {
					// We are asked to draw history, but we have no history yet.
					// Stop drawing.
					break;
				}

				int readIndex = (startIdx + sampleOffset) & BUFFER_MASK;

				if (!isNewData) {
					// We are drawing the old waveform (history).
					// However, the new waveform is actively writing into the buffer at 'idxTrigger'.
					// We must check if our read pointer has wrapped around and collided with the new data.

					// Distance from new trigger to this readIndex
					int distFromNew = (readIndex - idxTrigger) & BUFFER_MASK;

					if (distFromNew >= 0 && distFromNew < sSinceTrig) {
						// We have lapped the buffer. Stop drawing to prevent garbage/repeat data.
						// distFromNew is small and positive, it means this old pixel
						// is wrapped in the buffer. As in, we have drawn the entire buffer
						// and if we continue, we will be repeating data.

						// in other words: this memory no longer holds old data. It holds new data.
						// Stop drawing the old trace so we don't display the new trace twice.
						break;
					}
				}

				if (!recording && trigMode == TRIG_MODE_AUTO) {
					// rolling in AUTO

					// Calculate distance from Read Head to Write Head
					int distToHead = (idxWrite - readIndex) & BUFFER_MASK;

					// If the distance is huge
					// it means readIdx is actually ahead of writeIndex.
					// If the distance is tiny, we are catching up to the head.

					// If we are too close to the write head from the wrong side
					// stop drawing.
					if (distToHead > BUFFER_SIZE - 4000) {
						break;
					}
				}

				const float v = module->buffer[ch][readIndex];
				float y = volt2PxVert(v, offset, scale);

				// clamp unseen.
				y = clamp(y, -10000.0f, box.size.y+10000.0f);

				float px = float(curr_px)*PX_WAVE+WAVE_PX_OFFSET;
				if (first) {
					nvgMoveTo(args.vg, px, y);
					first = false;
				} else {
					if (wasNewData && !isNewData) {
						// transition from new to old data
						nvgMoveTo(args.vg, px, y);
					} else {
						nvgLineTo(args.vg, px, y);
					}
				}
				wasNewData = isNewData;
			}
		}
		nvgStroke(args.vg);
		if (recording && float(drawLimit_px) <= width_px && !holdoffActive) {
			// scanline
			nvgBeginPath(args.vg);
			nvgStrokeColor(args.vg, colorScanLine); // Faint white
			nvgStrokeWidth(args.vg, STROKE_SCANLINE);
			nvgMoveTo(args.vg, (float)drawLimit_px*PX_WAVE+WAVE_PX_OFFSET, 0);
			nvgLineTo(args.vg, (float)drawLimit_px*PX_WAVE+WAVE_PX_OFFSET, box.size.y);
			nvgStroke(args.vg);
		}
	}

	void drawXY(const DrawArgs& args) const {
		if (!module) return;

		bool AB = module->inputs[Scope::A_INPUT].isConnected() &&
		module->inputs[Scope::B_INPUT].isConnected() && module->scale[0] > -0.5f && module->scale[1] > -0.5f;
		bool CD = module->inputs[Scope::C_INPUT].isConnected() &&
		module->inputs[Scope::D_INPUT].isConnected() && module->scale[2] > -0.5f && module->scale[3] > -0.5f;

		// Check connections
		if (!AB && !CD) return;

		int idxWrite = module->writeIndex.load();
		int idxTrigger = module->triggerIndex.load();
		int idxLastTrig = module->lastTriggerIndex.load();
		bool idxValid = module->triggerValid.load();
		bool idxLastValid = module->prev_triggerValid.load();

		const float* signalX = module->buffer[0]; // Channel A
		const float* signalY = module->buffer[1]; // Channel B
		const float* signalX2 = module->buffer[2]; // Channel C
		const float* signalY2 = module->buffer[3]; // Channel D

		const float timePerDiv = module->getTimeDiv();

		const float totalTime = DIVS_HORIZ * timePerDiv;// arbitrarily selected width

		int samplesToDraw = (int)(totalTime * module->sampleRate);
		if (samplesToDraw > BUFFER_SIZE) samplesToDraw = BUFFER_SIZE;
		if (samplesToDraw < 2) samplesToDraw = 2;

		int step = 1;
		if (samplesToDraw > XY_SAMPLE_DECIMATION) {
			step = samplesToDraw / XY_SAMPLE_DECIMATION;
			if (step < 1) step = 1;
		}

		// start index
		// Calc how many samples exist between trigger and write index
		int samplesRecorded = (idxWrite - idxTrigger) & BUFFER_MASK;
		bool enoughNew = (samplesRecorded >= samplesToDraw);
		int samplesRecorded2 = (idxWrite - idxLastTrig) & BUFFER_MASK;
		bool enoughOld = (samplesRecorded2 >= samplesToDraw);
		/*
		const int startIdx = module->triggerValid && enough?module->triggerIndex
									:(module->prev_triggerValid?module->lastTriggerIndex
									:((module->writeIndex - samplesToDraw) & BUFFER_MASK));
		*/
		int startIdx;
		int endIdx;
		int countMax = samplesToDraw;
		if (idxValid && enoughNew) {
			// Draw newest data from trigger forward
			startIdx = idxTrigger;
			//endIdx = (startIdx + samplesToDraw) & BUFFER_MASK;
		} else if (idxLastValid && idxValid && enoughOld) {
			// Draw old data from prev trigger till new trigger
			startIdx = idxLastTrig;
			//endIdx = (startIdx + samplesToDraw) & BUFFER_MASK;
		} else {
			// Just draw latest data, ignoring triggers

			//startIdx = (module->writeIndex - samplesToDraw) & BUFFER_MASK;
			//endIdx = module->writeIndex;

			// If buffer hasn't wrapped yet, we only have 'writeIndex' amount of data.
			// Don't draw past what we have recorded.
			int available = module->bufferFilled.load() ? BUFFER_SIZE : idxWrite;

			if (countMax > available) countMax = available;

			startIdx = (idxWrite - countMax) & BUFFER_MASK;
		}
		/*
		 Is okay, but countMax is always samplesToDraw so I simplified it
		if (endIdx < startIdx) {
			countMax = endIdx + (BUFFER_MASK - startIdx);
		} else {
			countMax = endIdx - startIdx;
		}
		*/
		auto drawPair = [&](const float* sigX, const float* sigY, const int scaleIdxX, const int scaleIdxY, const NVGcolor color) {
			bool first = true;
			nvgBeginPath(args.vg);
			nvgStrokeWidth(args.vg, STROKE_XY);
			nvgLineCap(args.vg, LINECAP_XY);
			nvgLineJoin(args.vg, LINEJOIN_XY);
			nvgStrokeColor(args.vg, color);
			for (int i = 0; i < countMax; i += step) {
				int idx = (startIdx + i) & BUFFER_MASK;

				// voltages to screen px
				float volX = sigX[idx];
				float volY = sigY[idx];
				float pxX = volt2PxHoriz(volX, module->offset[scaleIdxX], module->scale[scaleIdxX]);
				float pxY = volt2PxVert(volY, module->offset[scaleIdxY], module->scale[scaleIdxY]);

				if (first) {
					nvgMoveTo(args.vg, pxX, pxY);
					first = false;
				} else {
					nvgLineTo(args.vg, pxX, pxY);
				}
			}
			nvgStroke(args.vg);
		};
		if (AB) drawPair(signalX, signalY, 0, 1, colorXY1);
		if (CD) drawPair(signalX2, signalY2, 2, 3, colorXY2);
	}

	float volt2PxVert(float voltage, float offset_divs, float vPerDiv) const {

		const float totalVolts = DIVS_VERT * vPerDiv;

		const float pxPerVolt = box.size.y / totalVolts;
		const float centerY = box.size.y * 0.5f;

		return centerY - voltage * pxPerVolt - box.size.y*DIVS_VERT_INV*offset_divs;
	}

	float volt2PxHoriz(float voltage, float offset_divs, float vPerDiv) const {

		const float totalVolts = DIVS_HORIZ * vPerDiv;

		const float pxPerVolt = box.size.x / totalVolts;
		const float centerX = box.size.x * 0.5f;

		return centerX + voltage * pxPerVolt + box.size.x*DIVS_HORIZ_INV*offset_divs;
	}

	void draw(const DrawArgs& args) override {
		drawGrid(args);
		//Widget::draw(args); // draw children
	}

	void drawLayer(const DrawArgs& args, const int layer) override {
		if (layer == 1) {
			if (module) {// check if in plugin-browser or in rack.
				nvgSave(args.vg);
				nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
				if (module->trigMode == TRIG_MODE_XY) {
					drawXY(args);
				} else {
					for (int c = 0; c < 4; c++) {
						drawWaveform(args, c);
					}
				}
				drawStats(args);
				drawTrigger(args);
				nvgRestore(args.vg);
			} else {
				drawStaticWaveform(args);
			}
		}
		frame++;
		if (frame > 60) frame = 0;

		/*
		 debug code for jitter of waveforms. Turns out autotime was the reason, it micro adjusted the freq
		 during recording.
		if (frame == 0 && module) {
			float width = box.size.x;
			float timeKnob = module->params[Scope::TIME_PARAM].getValue();

			float timePerDiv = module->getTimeDiv();
			float totalTime = 20.0f * timePerDiv;
			double spp = (totalTime * module->sampleRate) / width;

			// Use %.10f to see tiny microscopic drifts
			INFO("ID: %lld | Width: %.10f | Knob: %.10f | SPP: %.10f | Time/div: %.10f", module->getId(), width, timeKnob, spp, timePerDiv);
		}
		*/
	}

	void drawStats(const DrawArgs& args) const {

		if (!module || module->showStats == STATS_OFF) return;

		setupFont(args, FONTSIZE_STATS);

		const int chTrig = module->trigSource;
		if (chTrig >= 4 && module->showStats == STATS_ONE) return; // no stats for ext trigger

		int done = 0;

		for (int ch = 0; ch < TRIG_SOURCE_EXT; ch++) {
			if (!module->inputs[Scope::A_INPUT + ch].isConnected()) continue;
			if (module->showStats == STATS_ONE && ch != module->trigSource) continue;

			// limit
			const float timePerDiv_s = module->getTimeDiv();
			const float totalTime = DIVS_HORIZ * timePerDiv_s;
			int samplesToScan = (int)(totalTime * module->sampleRate);
			if (samplesToScan > BUFFER_SIZE) samplesToScan = BUFFER_SIZE;

			int idxWrite = module->writeIndex.load();
			int idxTrigger = module->triggerIndex.load();
			int idxLastTrig = module->lastTriggerIndex.load();
			bool idxValid = module->triggerValid.load();
			bool idxLastValid = module->prev_triggerValid.load();
			bool bufferFilled = module->bufferFilled.load();

			// start index
			// Calc how many samples exist between trigger and write index
			int samplesRecorded = (idxWrite - idxTrigger) & BUFFER_MASK;
			bool enough = idxValid && (samplesRecorded >= samplesToScan);
			// And between last trigger and write index
			int samplesRecordedOld = (idxWrite - idxLastTrig) & BUFFER_MASK;
			bool enoughOld = idxLastValid && (samplesRecordedOld >= samplesToScan);

			int startIndex;
			if (enough) {
				startIndex = idxTrigger;
			} else if (enoughOld) {
				startIndex = idxLastTrig;
			} else {
				// Scanning / Rolling
				int available = bufferFilled ? BUFFER_SIZE : idxWrite;

				if (samplesToScan > available) samplesToScan = available;

				if (samplesToScan <= 0) continue;

				startIndex = (idxWrite - samplesToScan) & BUFFER_MASK;
			}


			float minV = 100.0f;
			float maxV = -100.0f;
			double sum = 0.0;
			double sumSq = 0.0;
			int count = 0;

			int step = 1;
			if (samplesToScan > STATS_DECIMATION_THRESHOLD) step = (int)std::ceil(float(samplesToScan) / STATS_DECIMATION_THRESHOLD);

			for (int i = 0; i < samplesToScan; i += step) {
				const int idx = (startIndex + i) & BUFFER_MASK;
				const float v = module->buffer[ch][idx];
				if (v < minV) minV = v;
				if (v > maxV) maxV = v;
				sum += v;
				sumSq += v*v;
				count++;
			}

			float avg = (count > 0) ? (float)(sum / count) : 0.0f;
			float rms = (count > 0) ? (float)std::sqrt(sumSq / count) : 0.0f;

			// Text box
			float textBoxHeight = 16.0f;
			float margin = 10.0f;
			float textY = getTextY(done, textBoxHeight, 0.0f);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0, textY, box.size.x, textBoxHeight, 0.0f);
			nvgFillColor(args.vg, colorLabels);
			nvgFill(args.vg);

			// Text
			std::vector<std::string> text;

			std::string t1 = string::f("%c Min: %+6.2fV", 'A' + ch, minV);
			text.push_back(t1);

			std::string t2 = string::f("Max: %+6.2fV", maxV);
			text.push_back(t2);

			std::string t3 = string::f("PP: %5.2fV", maxV - minV);
			text.push_back(t3);

			std::string t4 = string::f("AVG: %+6.2f RMS: %5.2f", avg, rms);
			text.push_back(t4);

			std::vector<float> x;
			if (ch == module->trigSource) {
				std::string triggerStatus = module->frozen?"FROZEN"
						:(module->trigFoundTimer > 0.0f?"TRIGGER"
						:(module->holdoffTime_s > 0.0f?"HOLDOFF"
						:(module->recording?"TRIGGER"
						:"SCANNING")));
				text.emplace_back(triggerStatus);

				if (module->autoTimeFrequency_hz > 0.0f) {
					std::string t = string::f("Freq: %7.1f Hz", module->autoTimeFrequency_hz.load());
					text.push_back(t);
				} else {
					text.emplace_back("");
				}
				x = {column0, box.size.x*column1, box.size.x*column2, box.size.x*column3, box.size.x*column4, box.size.x*column5};
			} else {
				x = {column0, box.size.x*column1, box.size.x*column2, box.size.x*column3};
			}
			float y = getTextY(done, textBoxHeight, textBoxHeight*0.5f);

			drawText(args,text,12.0f,y,x,getColor(ch));

			done++;
		}
	}

	float getTextY(const int done, const float textBoxHeight, const float margin) const {
		const float height = box.size.y;
		switch (done) {
			case 0: return margin;
			case 1: return height - textBoxHeight + margin;
			case 2: return textBoxHeight + margin;
			case 3: return height - textBoxHeight * 2.0f + margin;
			default: return height * 0.5f;
		}
	}

	void drawTrigger(const DrawArgs& args) {
		if (!module) return;

		const float currentLevel = module->thresholdKnob;
		const float currentLevel2 = module->threshold2;

		if (std::abs(currentLevel - lastTrigLevel) > 0.001f) {
			// user is turning knob
			trigVisibilityTimer = 0.5f; // Show for 0.5s at 60fps
			lastTrigLevel = currentLevel;
		}

		if (trigVisibilityTimer > 0.0f) {
			// Decrement Timer (60fps)
			trigVisibilityTimer -= 0.016f;
		} else {
			return;
		}

		const int ch = module->trigSource;

		float scale = SCALE_DEFAULT;
		float offset = 0.0f;

		if (ch < TRIG_SOURCE_EXT) {
			scale = module->scale[ch];
			if (scale < -0.5f) return;
			offset = module->offset[ch];
		} else {
			// If ext trigger then no visuals
			return;
		}

		float y = volt2PxVert(currentLevel, offset, scale);
		float y2 = volt2PxVert(currentLevel2, offset, scale);

		y = clamp(y, 5.0f, box.size.y - 5.0f);
		y2 = clamp(y2, 5.0f, box.size.y - 5.0f);

		// Line
		auto color = getColor(ch);
		color.a *= STROKE_TRIGGER_ALPHA;
		float stroke = std::max(STROKE_TRIGGER, std::abs(y-y2));
		y = (y+y2) * 0.5f;
		drawDashedLine(args.vg, 0.0f, y, box.size.x, y, stroke, color);

		// Label
		nvgFontSize(args.vg, FONTSIZE_TRIGGER);
		nvgFillColor(args.vg, color);
		nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);

		char text[32];
		snprintf(text, sizeof(text), "Trig: %.2fV", currentLevel);
		nvgText(args.vg, box.size.x - 5, y - 2, text, nullptr);
	}

	static void drawDashedLine(NVGcontext* vg, float x1, float y1, float x2, float y2, float stroke, NVGcolor color) {
		float dashLen = 5.0f; // Length of the solid part
		float gapLen = 5.0f;  // Length of the empty part

		float dx = x2 - x1;
		float dy = y2 - y1;
		float len = std::hypot(dx, dy);
		float nx = dx / len;
		float ny = dy / len;

		nvgBeginPath(vg);
		nvgStrokeWidth(vg, stroke);
		nvgStrokeColor(vg, color);
		nvgLineCap(vg, NVG_BUTT);

		for (float i = 0; i < len; i += (dashLen + gapLen)) {
			float startX = x1 + nx * i;
			float startY = y1 + ny * i;

			float distRemaining = len - i;
			float currentDash = (distRemaining < dashLen) ? distRemaining : dashLen;

			float endX = startX + nx * currentDash;
			float endY = startY + ny * currentDash;

			nvgMoveTo(vg, startX, startY);
			nvgLineTo(vg, endX, endY);
		}
		nvgStroke(vg);
	}
	
	void drawGrid(const DrawArgs& args) const {
		// Background (draw even if in module-browser)
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGB(20, 20, 20));
		nvgFill(args.vg);

		// grid (draw even if in module-browser)
		if (!module || module->showGrid) {
			nvgBeginPath(args.vg);
			nvgStrokeColor(args.vg, nvgRGBA(60, 60, 60, 150));
			nvgStrokeWidth(args.vg, 1.0);
			// vert lines:
			for (int i = 1; i < int(DIVS_HORIZ); i++) {
				float x = (box.size.x * DIVS_HORIZ_INV) * float(i);
				nvgMoveTo(args.vg, x, 0);
				nvgLineTo(args.vg, x, box.size.y);
			}
			// horiz lines:
			for (int i = 1; i < int(DIVS_VERT); i++) {
				float y = (box.size.y * DIVS_VERT_INV) * float(i);
				nvgMoveTo(args.vg, 0, y);
				nvgLineTo(args.vg, box.size.x, y);
			}
			nvgStroke(args.vg);
		}

		if (!module) return;

		float yCenter = box.size.y / 2.0f;
		if (module->showBaselines) {
			nvgBeginPath(args.vg);
			nvgStrokeColor(args.vg, colorBaseline);
			nvgStrokeWidth(args.vg, 1.0);
			const float x1 = 0;
			const float x2 = box.size.x;
			for (int ch = 0; ch < 4; ch++) {
				if (module->inputs[Scope::A_INPUT+ch].isConnected() && module->scale[ch] > -0.5f) {
					const float offset = module->offset[ch];
					const float y = volt2PxVert(0.0f, offset, 1.0f);//VPerDiv must not be zero as it's denominator in division.
					if (y >= 0.0f && y <= box.size.y) {
						nvgMoveTo(args.vg, x1, y);
						nvgLineTo(args.vg, x2, y);
					}
				}
			}
			nvgStroke(args.vg);
		}
		if (module->showCenterline) {
			nvgBeginPath(args.vg);
			nvgStrokeColor(args.vg, colorCenterline);
			nvgStrokeWidth(args.vg, 1.0);
			float x1 = 0;
			float x2 = box.size.x;
			nvgMoveTo(args.vg, x1, yCenter);
			nvgLineTo(args.vg, x2, yCenter);
			nvgStroke(args.vg);
		}
	}
};





















struct ShowCenterItem : MenuItem {
	Scope* _module;

	ShowCenterItem(Scope* module, const char* label)
	: _module(module)
	{
		this->text = label;
	}

	void onAction(const event::Action &e) override {
		_module->showCenterline = !_module->showCenterline;
	}

	void step() override {
		rightText = _module->showCenterline == true ? "✔" : "";
	}
};

struct ShowBaseItem : MenuItem {
	Scope* _module;

	ShowBaseItem(Scope* module, const char* label)
	: _module(module)
	{
		this->text = label;
	}

	void onAction(const event::Action &e) override {
		_module->showBaselines = !_module->showBaselines;
	}

	void step() override {
		rightText = _module->showBaselines == true ? "✔" : "";
	}
};

struct ShowGridItem : MenuItem {
	Scope* _module;

	ShowGridItem(Scope* module, const char* label)
	: _module(module)
	{
		this->text = label;
	}

	void onAction(const event::Action &e) override {
		_module->showGrid = !_module->showGrid;
	}

	void step() override {
		rightText = _module->showGrid == true ? "✔" : "";
	}
};

struct PeriodsMenuItem : MenuItem {
	Scope* _module;
	int _os;

	PeriodsMenuItem(Scope* module, const char* label, int os)
	: _module(module), _os(os)
	{
		this->text = label;
	}

	void onAction(const event::Action &e) override {
		_module->autoTimePeriods = _os;
	}

	void step() override {
		rightText = _module->autoTimePeriods == _os ? "✔" : "";
	}
};

struct ACItem : MenuItem {
	Scope* _module;
	int _os;

	ACItem(Scope* module, const char* label, int os)
	: _module(module), _os(os)
	{
		this->text = label;
	}

	void onAction(const event::Action& e) override {
		_module->acCoupled[_os] = !_module->acCoupled[_os];
	}

	void step() override {
		rightText = _module->acCoupled[_os] ? "✔" : "";
		MenuItem::step();
	}
};

struct RenderModeItem : MenuItem {
	Scope* module{};
	int channel{};

	void onAction(const event::Action& e) override {
		module->cvMode[channel] = !module->cvMode[channel];
	}

	void step() override {
		// Label shows current state
		rightText = module->cvMode[channel] ? "CV" : "Audio";
		MenuItem::step();
	}
};

struct ScopeWidget : ModuleWidget {
	explicit ScopeWidget(Scope* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/ScopeModule.svg")));

		if (box.size.x == 0) {
			// until I create a panel
			box.size = Vec(40 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
		}

		float panelWidth_px = box.size.x;
		float hp = RACK_GRID_WIDTH;
		float margin_mm = 5.0f;

		// Screws
		addChild(createWidget<ScrewStarAutinn>(Vec(hp, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * hp, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(hp, RACK_GRID_HEIGHT - hp)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * hp, RACK_GRID_HEIGHT - hp)));
		
		// Display
		float displayHeight_px = mm2px(80.0f);
		float margin = mm2px(margin_mm);

		auto* display = createWidget<ScopeDisplay>(Vec(std::round(margin), std::round(margin)));
		display->setSize(Vec(std::round(panelWidth_px - 2 * margin), std::round(displayHeight_px)));
		display->module = module;
		addChild(display);

		// Controls
		float controlTop = margin + displayHeight_px;
		// center for rows
		float yRow1 = controlTop + mm2px(10.0f); // Knobs
		float yRow2 = controlTop + mm2px(28.0f); // inputs / buttons

		// channels
		// evenly on the left side
		float xStart   = 2.0f * hp;
		float xSpacing = 4.0f * hp;

		for (int i = 0; i < 4; i++) {
			float x = xStart + (float(i) * xSpacing);
			addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(x, yRow1), module, Scope::POS_A_PARAM + i));
			addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(x, yRow2-(yRow2-yRow1)/4.0f), module, Scope::SCALE_A_PARAM + i));
			addInput(createInputCentered<InPortAutinn>(Vec(x+HALF_KNOB_SMALL, yRow2+HALF_KNOB_SMALL), module, Scope::A_INPUT + i));
			addParam(createParamCentered<RoundCVButtonSmallAutinn>(Vec(x-HALF_KNOB_SMALL, yRow2+HALF_KNOB_SMALL), module, Scope::CV_OR_AUDIO_PARAM +i));
			addChild(createLightCentered<SmallLight<WhiteLight>>(Vec(x-HALF_KNOB_SMALL + 6, yRow2+HALF_KNOB_SMALL + 12), module, Scope::CV_OR_AUDIO_LIGHT + i));
		}

		// Time
		float xTime = 19.0f * hp;
		float xHoldoff = 24.0f * hp;
		
		// Time
		//addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(xTime, yRow1), module, Scope::TIME_PARAM));
		auto timeKnob = createParamCentered<AutinnArcMidKnob>(Vec(xTime, yRow1), module, Scope::TIME_PARAM);
		timeKnob->setModulation(-2, [module](float cv, float val, float att) {
							return module->autoTimeKnob;
						});
		addParam(timeKnob);
		
		// holdoff
		addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(xHoldoff, yRow1), module, Scope::HOLDOFF_PARAM));
		
		// Trigger
		float xTrigLevel = 28.0f * hp;
		float xTrigBtns  = 33.0f * hp - mm2px(7.0f);
		float xExtTrig   = 38.0f * hp;

		// Trig level
		addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(xTrigLevel, yRow1), module, Scope::TRIG_LEVEL_PARAM));
		addChild(createLightCentered<SmallLight<YellowLight>>(Vec(xTrigLevel - HALF_KNOB_MED * 1.25f, yRow1 + HALF_KNOB_MED), module, Scope::TRIG_FOUND_LIGHT));

		// Freeze
		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xTrigLevel, yRow2), module, Scope::FREEZE_PARAM));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(xTrigLevel - 12, yRow2 + 12), module, Scope::FREEZE_LIGHT_RGB));

		// Auto time
		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xTime, yRow2), module, Scope::AUTO_TIME_PARAM));
		addChild(createLightCentered<SmallLight<GreenLight>>(Vec(xTime - 12, yRow2 + 12), module, Scope::AUTO_TIME_LIGHT));

		// Debug
		//addParam(createParamCentered<RoundSmallAutinnKnob>(Vec((xHoldoff+xTime)*0.5f, yRow2), module, Scope::DEBUG_1));
		//addParam(createParamCentered<RoundSmallAutinnKnob>(Vec((xHoldoff+xTrigLevel)*0.5f, yRow2), module, Scope::DEBUG_2));

		// Stats
		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xHoldoff, yRow2), module, Scope::STATS_PARAM));

		// Trig buttons (grid layout)
		// source, mode
		// edge, light
		float btnSpacingY = mm2px(18.0f/3.0f);// 12mm between them
		float btnLightOffsetX = mm2px(7.0f); // xTrigBtns to light center
		float lightSpacingY = btnLightOffsetX*0.5f;

		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xTrigBtns, yRow1), module, Scope::TRIG_SOURCE_PARAM));
		addChild(createLightCentered<LargeLight<RedGreenBlueLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow1), module, Scope::TRIG_SOURCE_LIGHT_RGB));

		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xTrigBtns, yRow1 + btnSpacingY*2.0f), module, Scope::TRIG_MODE_PARAM));
		addChild(createLightCentered<SmallLight<WhiteLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow1 + btnSpacingY*2.0f - lightSpacingY), module, Scope::TRIG_MODE_AUTO_LIGHT));
		addChild(createLightCentered<SmallLight<WhiteLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow1 + btnSpacingY*2.0f), module, Scope::TRIG_MODE_NORM_LIGHT));
		addChild(createLightCentered<SmallLight<WhiteLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow1 + btnSpacingY*2.0f + lightSpacingY), module, Scope::TRIG_MODE_SOLO_LIGHT));
		addChild(createLightCentered<SmallLight<WhiteLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow1 + btnSpacingY*2.0f + lightSpacingY*2.0f), module, Scope::TRIG_MODE_XY_LIGHT));

		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xTrigBtns, yRow2 + btnSpacingY), module, Scope::TRIG_EDGE_PARAM));
		addChild(createLightCentered<SmallLight<WhiteLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow2 + btnSpacingY), module, Scope::TRIG_EDGE_RISE_LIGHT));
		addChild(createLightCentered<SmallLight<WhiteLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow2 + lightSpacingY + btnSpacingY), module, Scope::TRIG_EDGE_FALL_LIGHT));

		/*
		INFO("Freeze %.0f, %.0f", px2mm(xTrigLevel), px2mm(yRow2));
		INFO("Time %.0f, %.0f", px2mm(xTime), px2mm(yRow1));
		INFO("Holdoff %.0f, %.0f", px2mm(xHoldoff), px2mm(yRow1));
		INFO("Threshold %.0f, %.0f", px2mm(xTrigLevel), px2mm(yRow1));
		INFO("Source %.0f, %.0f", px2mm(xTrigBtns), px2mm(yRow1));
		INFO("Auto %.0f, %.0f", px2mm(xTrigBtns + btnLightOffsetX), px2mm(yRow1 + btnSpacingY*2.0f - lightSpacingY));
		INFO("Norm %.0f, %.0f", px2mm(xTrigBtns + btnLightOffsetX), px2mm(yRow1 + btnSpacingY*2.0f));
		INFO("Single %.0f, %.0f", px2mm(xTrigBtns + btnLightOffsetX), px2mm(yRow1 + btnSpacingY*2.0f + lightSpacingY));
		INFO("Rise %.0f, %.0f", px2mm(xTrigBtns + btnLightOffsetX), px2mm(yRow2 + btnSpacingY));
		INFO("Fall %.0f, %.0f", px2mm(xTrigBtns + btnLightOffsetX), px2mm(yRow2 + lightSpacingY + btnSpacingY));
		*/

		// Ext trigger
		addInput(createInputCentered<InPortAutinn>(Vec(xExtTrig, yRow2), module, Scope::CV_TRIG_EXT_INPUT));

		// Trigger out
		addOutput(createOutputCentered<OutPortAutinn>(Vec(xExtTrig, yRow1), module, Scope::CV_TRIG_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		auto* a = dynamic_cast<Scope*>(module);
		assert(a);

		menu->addChild(new MenuSeparator());
		menu->addChild(new ShowGridItem(a, "Show grid"));
		menu->addChild(new ShowBaseItem(a, "Show baselines"));
		menu->addChild(new ShowCenterItem(a, "Show centerline"));
		menu->addChild(new MenuLabel());
		menu->addChild(new PeriodsMenuItem(a, "Auto-time periods  1", 1));
		menu->addChild(new PeriodsMenuItem(a, "Auto-time periods  3", 3));
		menu->addChild(new PeriodsMenuItem(a, "Auto-time periods 10", 10));
		menu->addChild(new PeriodsMenuItem(a, "Auto-time periods 25", 25));
		menu->addChild(new PeriodsMenuItem(a, "Auto-time periods 50", 50));
		menu->addChild(new MenuLabel());
		menu->addChild(new ACItem(a, "Ch A - AC Coupled (Block DC)", 0));
		menu->addChild(new ACItem(a, "Ch B - AC Coupled (Block DC)", 1));
		menu->addChild(new ACItem(a, "Ch C - AC Coupled (Block DC)", 2));
		menu->addChild(new ACItem(a, "Ch D - AC Coupled (Block DC)", 3));

		menu->addChild(new MenuLabel());
		/*
		for (int i = 0; i < 4; i++) {
			char label[32];
			snprintf(label, sizeof(label), "Channel %c Input", 'A' + i);
			auto* item = new RenderModeItem();
			item->text = label;
			item->module = a;
			item->channel = i;
			menu->addChild(item);
		}
		*/
	}
};

Model* modelScope = createModel<Scope, ScopeWidget>("Scope40");