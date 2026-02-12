#include "Autinn.hpp"
#include <cmath>

static constexpr int BUFFER_SIZE = 1 << 22;// 2^20 (5.4 seconds at 192khz) - 2^22 (22 seconds at 192khz)
static constexpr int BUFFER_MASK = BUFFER_SIZE - 1;

// 8 divisions (audio scope std)
constexpr float numDivsVert = 8.0f;// total vert divs
constexpr float numDivsHoriz = 20.0f;// total horiz divs
constexpr float numDivsVert_inv = 1.0f/numDivsVert;
constexpr float numDivsHoriz_inv = 1.0f/numDivsHoriz;

#define TRIG_AUTO_TIMEOUT 1.0f    // seconds
#define AUTO_TIME_PERIOD_MAX 10.0 // seconds
#define AUTO_TIME_PERIOD_MIN 0.000025 // seconds, 40kHz
#define TRIG_SOURCE_EXT 4
#define STATS_OFF 0
#define STATS_ONE 1
#define STATS_ALL 2

static std::vector<std::string> scales = {
	"10 V/Div","5 V/Div","2 V/Div", "1 V/Div","0.5 V/Div",
	"0.2 V/Div","0.1 V/Div","50 mV/Div","20 mV/Div","10 mV/Div", "5 mV/Div"
};
static float getScale(int knob) {
	switch (knob) {
	case 0: return 10.0f;
	case 1: return  5.0f;
	case 2: return  2.0f;
	case 3: return  1.0f;
	case 4: return  0.5f;
	case 5: return  0.2f;
	case 6: return  0.1f;
	case 7: return  0.05f;
	case 8: return  0.02f;
	case 9: return  0.01f;
	case 10: return 0.005f;
	default: return 0.5f;
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
		NUM_LIGHTS
	};

	float buffer[4][BUFFER_SIZE] = {};
	int writeIndex = 0;
	int triggerIndex = 0; // last valid trigger
	int lastTriggerIndex = 0;
	float sampleRate = 44100.0f;

	dsp::SchmittTrigger trigSchmitt;
	dsp::BooleanTrigger trigPulse;
	dsp::BooleanTrigger srcBtnTrig;
	dsp::BooleanTrigger modeBtnTrig;
	dsp::BooleanTrigger edgeBtnTrig;
	dsp::BooleanTrigger freezeBtnTrig;
	dsp::BooleanTrigger autoTimeBtnTrig;
	dsp::BooleanTrigger statsBtnTrig;

#define TRIG_MODE_AUTO 0 // wait TRIG_AUTO_TIMEOUT then trigger even if no trigger found
#define TRIG_MODE_NORM 1 // wait forever for trigger to be found
#define TRIG_MODE_SOLO 2 // freeze when finding trigger
#define TRIG_MODE_XY   3 // Lissajous
#define TRIG_EDGE_RISE true
#define TRIG_EDGE_FALL false
#define AUTO_TIME_KNOB_OFF 50.0f

	// transient
	bool frozen = false;
	bool freezePending = false;
	double period_s = 0.0;
	bool triggered = false;       // Have we found a trigger edge?
	//int triggerCandidate = 0;     // Where did the trigger happen?
	int samplesSinceTrigger = 0;  // Counter for div * time/div wait
	float holdoffTime_s = 0.0f;     // Remaining holdoff in seconds
	float autoTrigTimer = 0.0f;   // Auto mode timeout
	int dspFrame = 1001;
	float lastFrequency_hz = 0.0f;
	float blinkPhase = 0.0f;
	float autoTimeKnob = AUTO_TIME_KNOB_OFF;

	// persisted
	bool autoTimeMode = false;
	int trigSource = 0; // 0-3: Channel, 4: Ext
	int trigMode = TRIG_MODE_AUTO;
	bool trigEdge = TRIG_EDGE_RISE;
	bool showBaselines = false;
	bool showCenterline = false;
	bool showGrid = true;
	int showStats = STATS_ONE;

	// controls
	bool sourceBtn;
	bool trigModeKnob;
	bool trigEdgeBtn;
	bool autotimeBtn;
	bool freezeBtn;
	bool statsBtn;
	float offset[4];
	float scale[4];
	float thresholdKnob;
	float holdoffKnob;


	Scope() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		// pos
		configParam(POS_A_PARAM, -8.0, 8.0, 0.0, "Channel A Pos", " Div");
		configParam(POS_B_PARAM, -8.0, 8.0, 0.0, "Channel B Pos", " Div");
		configParam(POS_C_PARAM, -8.0, 8.0, 0.0, "Channel C Pos", " Div");
		configParam(POS_D_PARAM, -8.0, 8.0, 0.0, "Channel D Pos", " Div");

		// scale
		configSwitch(SCALE_A_PARAM, 0.0f, 10.0f, 4.0f, "Channel A Scale", scales);
		configSwitch(SCALE_B_PARAM, 0.0f, 10.0f, 4.0f, "Channel B Scale", scales);
		configSwitch(SCALE_C_PARAM, 0.0f, 10.0f, 4.0f, "Channel C Scale", scales);
		configSwitch(SCALE_D_PARAM, 0.0f, 10.0f, 4.0f, "Channel D Scale", scales);

		// Time
		configParam(TIME_PARAM, -5.0f, 0.0f, -3.0f, "Time / Div", " s", 10.0f);
		configParam(HOLDOFF_PARAM, -4.0f, 1.0f, -3.0f, "Trigger holdoff", " s", 10.0f);

		// Trigger
		configParam(TRIG_LEVEL_PARAM, -10.0f, 10.0f, 0.0f, "Trigger threshold", " V");
		configButton(TRIG_SOURCE_PARAM, "Trigger source");
		configButton(TRIG_MODE_PARAM, "Trigger mode");
		configButton(TRIG_EDGE_PARAM, "Trigger edge");
		configButton(FREEZE_PARAM, "Freeze");
		configButton(AUTO_TIME_PARAM, "Auto time");
		configButton(STATS_PARAM, "Stats for selected channel");

		// inputs
		configInput(A_INPUT, "Channel A");
		configInput(B_INPUT, "Channel B");
		configInput(C_INPUT, "Channel C");
		configInput(D_INPUT, "Channel D");
		configInput(CV_TRIG_EXT_INPUT, "Ext. trigger");

		// lights
		configLight(TRIG_SOURCE_LIGHT_RGB, "Trigger source");
		configLight(TRIG_MODE_AUTO_LIGHT, "Auto trigger");
		configLight(TRIG_MODE_NORM_LIGHT, "Norm trigger");
		configLight(TRIG_MODE_SOLO_LIGHT, "Single trigger");
		configLight(TRIG_MODE_XY_LIGHT, "X-Y (A & B)");
		configLight(TRIG_EDGE_RISE_LIGHT, "Trigger on rising edge");
		configLight(TRIG_EDGE_FALL_LIGHT, "Trigger on falling edge");
		configLight(FREEZE_LIGHT_RGB, "Freeze");
		configLight(AUTO_TIME_LIGHT, "Auto time");

		readControls();
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
			if (i == 0 && b) showStats = STATS_ONE;
			else showStats = i;
		}

		json_t* gJ = json_object_get(rootJ, "showGrid");
		if (gJ) showGrid = json_is_true(gJ);

		json_t* cJ = json_object_get(rootJ, "showCenterline");
		if (cJ) showCenterline = json_is_true(cJ);

		json_t* bJ = json_object_get(rootJ, "showBaselines");
		if (bJ) showBaselines = json_is_true(bJ);
	}

	void process(const ProcessArgs& args) override {
		sampleRate = args.sampleRate;

		period_s += args.sampleTime;

		if (!frozen) {
			for (int c = 0; c < 4; c++) {
				buffer[c][writeIndex] = inputs[A_INPUT + c].getVoltage();
			}

			triggerDetect(args);

			writeIndex = (writeIndex + 1) & BUFFER_MASK;
		}

		blinkPhase += args.sampleTime * 2.0f;
		if (blinkPhase >= 1.0f) blinkPhase -= 1.0f;

		dspFrame++;
		if (dspFrame > 1000) {
			dspFrame = 0;
			if (period_s > 3600.0) period_s = 0.0;
			updateLights();
			readControls();
			autoTime();
		}
	}

	/*
	 * TODO:
	 *		Consider zeroing a channel.
	 *		Update manual.
	 *		X-Y Mode (Lissajous)
	 *		AC/DC switch to remove DC
	 *		Stats for all channels (cycle options)
	 *
	 */

	void triggerDetect(const ProcessArgs& args) {
		// Get trigger signal
		float trigSig = 0.0f;
		float hysteresis = 0.1f; // Default for Ext (100mV)
		if (trigSource < TRIG_SOURCE_EXT) {
			trigSig = inputs[A_INPUT + trigSource].getVoltage();
			float vPerDiv = scale[trigSource];
			hysteresis *= vPerDiv;
		} else {
			trigSig = inputs[CV_TRIG_EXT_INPUT].getVoltage();
		}

		float threshold = thresholdKnob;
		float timePerDiv = getTimeDiv();
		// We want to record numDivsHoriz divisions after the trigger to fill the screen
		float totalScreenTime = numDivsHoriz * timePerDiv;
		int samplesToRecord = int(totalScreenTime * sampleRate);

		if (samplesToRecord > BUFFER_SIZE) samplesToRecord = BUFFER_SIZE;
		if (samplesToRecord < 32) samplesToRecord = 32;

		// Holdoff
		if (holdoffTime_s > 0.0f) {
			holdoffTime_s -= args.sampleTime;
		}

		// Schmitt-trigger processing
		// Invert input for falling edge
		// Hysteresis window 0.1V
		float signal = trigSig;
		if (trigEdge == TRIG_EDGE_FALL) {
			signal = -signal;
			hysteresis = -hysteresis;
			threshold = -threshold;
		}
		bool schmittState = trigSchmitt.process(signal, threshold, threshold+hysteresis);
		bool edgeFound = trigPulse.process(schmittState);

		if (edgeFound) {
			if (period_s < AUTO_TIME_PERIOD_MAX && period_s > AUTO_TIME_PERIOD_MIN) {
				lastFrequency_hz = (float)(1.0 / period_s);
			} else {
				lastFrequency_hz = 0.0f;
			}
			period_s = 0.0;
		}

		if (triggered) {
			// Recording
			// We have already triggered, now we fill the buffer for the rest of the screen
			samplesSinceTrigger++;

			if (samplesSinceTrigger >= samplesToRecord) {
				// buffer full
				triggered = false;

				// Set holdoff (max 1 sec)
				holdoffTime_s = holdoffKnob > 0.00011f?holdoffKnob:0.0f;

				if (trigMode == TRIG_MODE_SOLO || freezePending) {
					frozen = true;
					freezePending = false;
				}
			}
		} else {
			// Waiting
			if (holdoffTime_s <= 0.0f && edgeFound && trigMode != TRIG_MODE_XY) {
				// switch to recording
				triggered = true;
				//triggerCandidate = writeIndex;
				lastTriggerIndex = triggerIndex;
				triggerIndex = writeIndex;
				samplesSinceTrigger = 0;
				autoTrigTimer = 0.0f;
			}

			if (trigMode == TRIG_MODE_AUTO) {
				autoTrigTimer += args.sampleTime;
				// If no trigger for TRIG_AUTO_TIMEOUT (or > screen time), force update
				float timeout = TRIG_AUTO_TIMEOUT;
				if (timeout < totalScreenTime * 1.25f) timeout = totalScreenTime * 1.25f;

				if (autoTrigTimer > timeout) {
					// Force rolling trigger
					//lastTriggerIndex = triggerIndex;//TODO: not sure
					triggerIndex = (writeIndex - samplesToRecord) & BUFFER_MASK;
					triggered = false;
					samplesSinceTrigger = 0;
					autoTrigTimer = 0.0f;
					lastFrequency_hz = 0.0f;

					if (freezePending) {
						frozen = true;
						freezePending = false;
					}
				}
			}
		}
	}

	void autoTime () {
		if (autoTimeMode && lastFrequency_hz > 0.01f) {
			// Time since last trigger
			double period = 1.0/lastFrequency_hz;

			// Calculate ideal time/div to show 3 periods
			// 3 periods fill 1 screen
			double targetTimePerDiv_s = (period * 3.0) / numDivsHoriz;

			// clamp to prevent log(0)
			if (targetTimePerDiv_s < 1e-5) targetTimePerDiv_s = 1e-5;
			autoTimeKnob = std::log10((float)targetTimePerDiv_s);

			//params[TIME_PARAM].setValue(autoTimeKnob);//TODO:
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
			scale[ch] = getScale(int(std::round(params[SCALE_A_PARAM + ch].getValue())));
		}
		// time knob we skip here

		if (srcBtnTrig.process(sourceBtn)) {
			trigSource = (trigSource + 1) % 5;
		}
		if (modeBtnTrig.process(trigModeKnob)) {
			trigMode = (trigMode + 1) % 4;
			if (trigMode == TRIG_MODE_XY) frozen = false;
		}
		if (edgeBtnTrig.process(trigEdgeBtn)) {
			trigEdge = !trigEdge;
		}
		if (autoTimeBtnTrig.process(autotimeBtn)) {
			autoTimeMode = !autoTimeMode;
		}
		if (trigMode == TRIG_MODE_XY) {
			autoTimeMode = false;
			lastTriggerIndex = 0;
			triggerIndex = 0;
			lastFrequency_hz = 0.0f;
			triggered = false;
			freezePending = false;
		}
		if (freezeBtnTrig.process(freezeBtn)) {
			if (freezePending || frozen) {
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
			case 0: return 1.0f;
			case 1: return 0.9f;
			case 2: return 0.0f;//0.2f;
			case 3: return 0.0f;//0.2f;
			default: return 0.0f;
		}
	}

	static float getGreen(int ch) {
		switch (ch) {
			case 0: return 0.0f;//0.2f;
			case 1: return 0.8f;
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
		float blinkBrightness = 1.0f;

		if (!triggered && !frozen) {
			// scanning
			if (blinkPhase > 0.5f) blinkBrightness = 0.1f;
		}

		bool lissajous = trigMode==TRIG_MODE_XY;

		if (!lissajous) {
			lights[TRIG_SOURCE_LIGHT_RGB + 0].setBrightness(getRed(trigSource)*blinkBrightness);
			lights[TRIG_SOURCE_LIGHT_RGB + 1].setBrightness(getGreen(trigSource)*blinkBrightness);
			lights[TRIG_SOURCE_LIGHT_RGB + 2].setBrightness(getBlue(trigSource)*blinkBrightness);
		} else {
			lights[TRIG_SOURCE_LIGHT_RGB + 0].setBrightness(0.5f);
			lights[TRIG_SOURCE_LIGHT_RGB + 1].setBrightness(0.5f);
			lights[TRIG_SOURCE_LIGHT_RGB + 2].setBrightness(0.5f);
		}

		lights[TRIG_MODE_AUTO_LIGHT].setBrightness(trigMode==TRIG_MODE_AUTO ? 1.0f : 0.0f);
		lights[TRIG_MODE_NORM_LIGHT].setBrightness(trigMode==TRIG_MODE_NORM ? 1.0f : 0.0f);
		lights[TRIG_MODE_SOLO_LIGHT].setBrightness(trigMode==TRIG_MODE_SOLO ? 1.0f : 0.0f);
		lights[TRIG_MODE_XY_LIGHT].setBrightness(lissajous ? 1.0f : 0.0f);

		lights[TRIG_EDGE_RISE_LIGHT].setBrightness(trigEdge == TRIG_EDGE_RISE ? 1.0f : 0.0f);
		lights[TRIG_EDGE_FALL_LIGHT].setBrightness(trigEdge == TRIG_EDGE_FALL ? 1.0f : 0.0f);

		lights[AUTO_TIME_LIGHT].setBrightness(autoTimeMode ? 1.0f : 0.0f);

		lights[FREEZE_LIGHT_RGB+0].setBrightness(frozen || freezePending? 1.0f : 0.0f);
		lights[FREEZE_LIGHT_RGB+1].setBrightness(freezePending ? 1.0f : 0.0f);
		//lights[FREEZE_LIGHT_RGB+2].setBrightness(frozen ? 0.0f : 0.0f);
	}
};


struct ScopeDisplay : TransparentWidget {
	Scope* module{};
	int frame = 0;

	const float maxSamplesPerPx = 16.0f;
	const float maxPxPerSamples = 1.0f/maxSamplesPerPx;

	//std::shared_ptr<Font> font;
	float lastTrigLevel = -999.0f;
	float trigVisibilityTimer = 0.0f;

	NVGcolor color0 = nvgRGBA(255, 50, 50, 230);   // Red
	NVGcolor color1 = nvgRGBA(255, 230, 50, 230);  // Yellow
	NVGcolor color2 = nvgRGBA(50, 255, 50, 230);    // Green
	NVGcolor color3 = nvgRGBA(50, 150, 255, 230);  // Blue
	NVGcolor colorExt = nvgRGBA(255, 255, 255, 255);



	NVGcolor getColor(int ch) const {
		switch (ch) {
		case 0: return color0;
		case 1: return color1;
		case 2: return color2;
		case 3: return color3;
		default: return colorExt;
		}
	}

	void drawWaveform(const DrawArgs& args, int ch) const {
		if (!module) return;
		if (!module->inputs[Scope::A_INPUT + ch].isConnected()) return;

		float scale = module->scale[ch];
		float offset = module->offset[ch];
		float timePerDiv_s = module->getTimeDiv();

		const NVGcolor color = getColor(ch);


		const float width_px = box.size.x;
		const float totalTime_s = numDivsHoriz * timePerDiv_s;
		const float samplesToDraw = totalTime_s * module->sampleRate;

		if (samplesToDraw < 2.0f) return;

		// < 1.0: Zoomed in
		// > 1.0: Zoomed out
		const double samplesPerPixel = samplesToDraw / width_px;

		nvgBeginPath(args.vg);
		nvgStrokeColor(args.vg, color);
		nvgStrokeWidth(args.vg, 1.25f); // Slightly thicker line
		nvgLineJoin(args.vg, NVG_BEVEL);// NVG_ROUND

		int iteratorStep = 1;
		if (samplesPerPixel > maxSamplesPerPx) {
			iteratorStep = (int)(samplesPerPixel * maxPxPerSamples);
			if (iteratorStep < 1) iteratorStep = 1;
		}

		int drawLimitPixel = int(width_px)+1;

		if (module->triggered) {
			// we only draw enough pixels to reach writeIndex from trigger
			double validPixels = (double)module->samplesSinceTrigger / samplesPerPixel;
			drawLimitPixel = (int)validPixels;

			if (drawLimitPixel > int(width_px)+1) drawLimitPixel = int(width_px)+1;
			if (drawLimitPixel < 0) drawLimitPixel = 0;
		}

		bool first = true;

		for (int curr_px = 0; curr_px < int(width_px); curr_px += 1.0f) {
			// left: new
			// right: old
			// extreme right: ahead of bufferhead
			bool isNewData = curr_px < drawLimitPixel;

			// If we drawing past writeIndex, then we draw old data from previous trigger.
			// else we draw from current trigger.
			const int startIdx = isNewData ? module->triggerIndex : module->lastTriggerIndex;

			int sampleOffset = (int)(curr_px * samplesPerPixel);
			if (sampleOffset >= BUFFER_SIZE) {
				// whole buffer does not fit on screen
				// we stop drawing.
				break;
			}
			int readIndex = (startIdx + sampleOffset) & BUFFER_MASK;

			if (!isNewData) {
				// Distance from New Trigger to this Read Point
				int distFromNew = (readIndex - module->triggerIndex) & BUFFER_MASK;

				if (distFromNew >= 0 && distFromNew < module->samplesSinceTrigger) {
					// distFromNew is small and positive, it means this old pixel
					// is wrapped in the buffer.
					break;//TODO:
				}
			}

			if (samplesPerPixel > 1.0) {
				// zoom out: Peaks
				const int iterStart = (int)(curr_px * samplesPerPixel);
				int iterEnd = (int)((curr_px + 1) * samplesPerPixel);
				if (iterEnd <= iterStart) iterEnd = iterStart + 1;

				float minV = 100.0f;
				float maxV = -100.0f;
				for (int i = iterStart; i < iterEnd; i += iteratorStep) {
					const float v = module->buffer[ch][readIndex];
					if (v < minV) minV = v;
					if (v > maxV) maxV = v;
				}

				float yTop = volt2PxVert(maxV, offset, scale);
				float yBottom = volt2PxVert(minV, offset, scale);

				yTop = clamp(yTop, -10000.0f, box.size.y+10000.0f);
				yBottom = clamp(yBottom, -10000.0f, box.size.y+10000.0f);

				if (first) {
					nvgMoveTo(args.vg, float(curr_px), yTop);
					first = false;
				} else {
					nvgLineTo(args.vg, float(curr_px), yTop);
				}
				nvgLineTo(args.vg, float(curr_px), yBottom);

			} else {
				// zoom in
				const float v = module->buffer[ch][readIndex];
				float y = volt2PxVert(v, offset, scale);

				// clamp unseen.
				y = clamp(y, -10000.0f, box.size.y+10000.0f);

				if (first) {
					nvgMoveTo(args.vg, float(curr_px), y);
					first = false;
				} else {
					if (curr_px == drawLimitPixel) {
						// transition from new to old data
						nvgMoveTo(args.vg, float(curr_px), y);
					} else {
						nvgLineTo(args.vg, float(curr_px), y);
					}
				}
			}
		}
		nvgStroke(args.vg);
		if (module->triggered && float(drawLimitPixel) <= width_px) {
			// scanline
			nvgBeginPath(args.vg);
			nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 90)); // Faint white
			nvgStrokeWidth(args.vg, 1.0f);
			nvgMoveTo(args.vg, (float)drawLimitPixel, 0);
			nvgLineTo(args.vg, (float)drawLimitPixel, box.size.y);
			nvgStroke(args.vg);
		}
	}

	void drawXY(const DrawArgs& args) const {
		if (!module) return;

		const float* signalX = module->buffer[0]; // Channel A
		const float* signalY = module->buffer[1]; // Channel B

		// Check connections
		if (!module->inputs[Scope::A_INPUT].isConnected() ||
		!module->inputs[Scope::B_INPUT].isConnected()) return;

		nvgBeginPath(args.vg);
		nvgStrokeColor(args.vg, nvgRGBA(100, 255, 200, 200));
		nvgStrokeWidth(args.vg, 1.5f);

		const float timePerDiv = module->getTimeDiv();

		const float totalTime = numDivsHoriz * timePerDiv;// arbitrarily selected width

		int samplesToDraw = (int)(totalTime * module->sampleRate);
		if (samplesToDraw > BUFFER_SIZE) samplesToDraw = BUFFER_SIZE;
		if (samplesToDraw < 2) samplesToDraw = 2;

		int step = 1;
		if (samplesToDraw > 6000) {
			step = samplesToDraw / 6000;
			if (step < 1) step = 1;
		}

		int startIdx = (module->writeIndex - samplesToDraw) & BUFFER_MASK;
		if (startIdx < 0) startIdx += BUFFER_SIZE;

		bool first = true;

		for (int i = 0; i < samplesToDraw; i += step) {
			int idx = (startIdx + i) & BUFFER_MASK;

			// voltages to screen px
			float volX = signalX[idx];
			float volY = signalY[idx];

			float pxX = volt2PxHoriz(volX, module->scale[0]);// Ch A settings for X
			float pxY = volt2PxVert(volY, module->offset[1], module->scale[1]); // Ch B settings for Y

			if (first) {
				nvgMoveTo(args.vg, pxX, pxY);
				first = false;
			} else {
				nvgLineTo(args.vg, pxX, pxY);
			}
		}
		nvgStroke(args.vg);
	}

	float volt2PxVert(float voltage, float offset_divs, float vPerDiv) const {

		const float totalVolts = numDivsVert * vPerDiv;

		const float pxPerVolt = box.size.y / totalVolts;
		const float centerY = box.size.y * 0.5f;

		return centerY - voltage * pxPerVolt - box.size.y*numDivsVert_inv*offset_divs;
	}

	float volt2PxHoriz(float voltage, float vPerDiv) const {

		const float totalVolts = numDivsHoriz * vPerDiv;

		const float pxPerVolt = box.size.x / totalVolts;
		const float centerX = box.size.x * 0.5f;

		return centerX + voltage * pxPerVolt;
	}

	void draw(const DrawArgs& args) override {
		drawGrid(args);
		drawStats(args);
		drawTrigger(args);
	}

	void drawLayer(const DrawArgs& args, const int layer) override {
		if (module && layer == 1) {// check if in plugin-browser or in rack.
			nvgSave(args.vg);
			nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
			if (module->trigMode == TRIG_MODE_XY) {
				drawXY(args);
			} else {
				for (int c = 0; c < 4; c++) {
					drawWaveform(args, c);
				}
			}
			nvgRestore(args.vg);
		}
		frame++;
		if (frame > 60) frame = 0;
	}

	void drawStats(const DrawArgs& args) const {
		if (!module || module->showStats == STATS_OFF) return;

		//if (!font) font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/autinn.ttf"));
		//if (!font) return;
		//nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 13.0f);

		const int chTrig = module->trigSource;
		if (chTrig >= 4 && module->showStats == STATS_ONE) return; // no stats for ext trigger

		int done = 0;

		for (int ch = 0; ch < TRIG_SOURCE_EXT; ch++) {
			if (!module->inputs[Scope::A_INPUT + ch].isConnected()) continue;
			if (module->showStats == STATS_ONE && ch != module->trigSource) continue;
			// limit
			const int startIndex = module->triggerIndex;
			const float timePerDiv_s = module->getTimeDiv();
			const float totalTime = numDivsHoriz * timePerDiv_s;
			int samplesToScan = (int)(totalTime * module->sampleRate);
			if (samplesToScan > BUFFER_SIZE) samplesToScan = BUFFER_SIZE;

			float minV = 100.0f;
			float maxV = -100.0f;

			int step = 1;
			if (samplesToScan > 4000) step = samplesToScan / 2000;

			for (int i = 0; i < samplesToScan; i += step) {
				const int idx = (startIndex + i) & BUFFER_MASK;
				const float v = module->buffer[ch][idx];
				if (v < minV) minV = v;
				if (v > maxV) maxV = v;
			}

			// Text box
			float textBoxHeight = 20.0f;
			float textY = getTextY(done, textBoxHeight, 0.0f);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0, textY, box.size.x, textBoxHeight, 0.0f);
			nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 128));
			nvgFill(args.vg);

			// Text
			nvgFillColor(args.vg, getColor(ch));
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			char text[128];
			if (module->lastFrequency_hz > 0.0f && ch == module->trigSource) {
				snprintf(text, sizeof(text), "Ch %c  Min: %+.2f V   Max: %+.2f V   PP: %.2f V   Freq: %.1f Hz",
					'A' + ch,
					minV,
					maxV,
					(maxV - minV),
					module->lastFrequency_hz);
			} else {
				snprintf(text, sizeof(text), "Ch %c  Min: %+.2f V   Max: %+.2f V   PP: %.2f V",
					'A' + ch,
					minV,
					maxV,
					(maxV - minV));
			}

			nvgText(args.vg, 10, getTextY(done, textBoxHeight, 10.0f), text, nullptr);
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

		float scale = 2.0f; // Default 2V/Div
		float offset = 0.0f;

		if (ch < TRIG_SOURCE_EXT) {
			scale = module->scale[ch];
			offset = module->offset[ch];
		} else {
			// If ext trigger then no visuals
			return;
		}

		float y = volt2PxVert(currentLevel, offset, scale);
		if (y < 0) y = 0;
		if (y > box.size.y) y = box.size.y;

		// Line
		drawDashedLine(args.vg, 0, y, box.size.x, y, 1.0f, getColor(ch));
		/*
		nvgBeginPath(args.vg);
		nvgStrokeColor(args.vg, getColor(ch)); // Match Source Color
		nvgStrokeWidth(args.vg, 1.0f);
		nvgMoveTo(args.vg, 0, y);
		nvgLineTo(args.vg, box.size.x, y);
		nvgStroke(args.vg);
		*/

		// Label
		nvgFontSize(args.vg, 12.0f);
		nvgFillColor(args.vg, getColor(ch));
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
		if (!module) return;

		// Background
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGB(20, 20, 20));
		nvgFill(args.vg);

		// grid
		if (module->showGrid) {
			nvgBeginPath(args.vg);
			nvgStrokeColor(args.vg, nvgRGBA(60, 60, 60, 150));
			nvgStrokeWidth(args.vg, 1.0);
			// vert lines:
			for (int i = 1; i < int(numDivsHoriz); i++) {
				float x = (box.size.x * numDivsHoriz_inv) * float(i);
				nvgMoveTo(args.vg, x, 0);
				nvgLineTo(args.vg, x, box.size.y);
			}
			// horiz lines:
			for (int i = 1; i < int(numDivsVert); i++) {
				float y = (box.size.y * numDivsVert_inv) * float(i);
				nvgMoveTo(args.vg, 0, y);
				nvgLineTo(args.vg, box.size.x, y);
			}
			nvgStroke(args.vg);
		}
		float yCenter = box.size.y / 2.0f;
		if (module->showBaselines) {
			nvgBeginPath(args.vg);
			nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 100));
			nvgStrokeWidth(args.vg, 1.0);
			const float x1 = 0;
			const float x2 = box.size.x;
			for (int ch = 0; ch < 4; ch++) {
				if (module->inputs[Scope::A_INPUT+ch].isConnected()) {
					const float offset = module->offset[ch];
					const float scale = module->scale[ch];
					const float y = volt2PxVert(0.0f, offset, scale);
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
			nvgStrokeColor(args.vg, nvgRGBA(200, 200, 200, 100));
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

		auto* display = createWidget<ScopeDisplay>(Vec(margin, margin));
		display->box.size = Vec(panelWidth_px - 2 * margin, displayHeight_px);
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

		// Freeze
		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xTrigLevel, yRow2), module, Scope::FREEZE_PARAM));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(xTrigLevel + 12, yRow2 + 12), module, Scope::FREEZE_LIGHT_RGB));

		// Auto time
		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xTime, yRow2), module, Scope::AUTO_TIME_PARAM));
		addChild(createLightCentered<SmallLight<BlueLight>>(Vec(xTime + 12, yRow2 + 12), module, Scope::AUTO_TIME_LIGHT));

		// Auto time
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
		addChild(createLightCentered<SmallLight<BlueLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow2 + btnSpacingY), module, Scope::TRIG_EDGE_RISE_LIGHT));
		addChild(createLightCentered<SmallLight<GreenLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow2 + lightSpacingY + btnSpacingY), module, Scope::TRIG_EDGE_FALL_LIGHT));

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
	}

	void appendContextMenu(Menu* menu) override {
		auto* a = dynamic_cast<Scope*>(module);
		assert(a);

		menu->addChild(new MenuLabel());
		menu->addChild(new ShowGridItem(a, "Show grid"));
		menu->addChild(new ShowBaseItem(a, "Show baselines"));
		menu->addChild(new ShowCenterItem(a, "Show centerline"));
		menu->addChild(new MenuLabel());
	}
};

Model* modelScope = createModel<Scope, ScopeWidget>("Scope40");