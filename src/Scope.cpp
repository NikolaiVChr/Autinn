#include "Autinn.hpp"
#include <cmath>

static constexpr int BUFFER_SIZE = 1 << 22;// 2^20 (5.4 seconds at 192khz) - 2^22 (22 seconds at 192khz)
static constexpr int BUFFER_MASK = BUFFER_SIZE - 1;

// 8 divisions (audio scope std)
constexpr float numDivsVert = 8.0f;// total
constexpr float numDivsHoriz = 20.0f;
constexpr float numDivsVert_inv = 1.0f/numDivsVert;
constexpr float numDivsHoriz_inv = 1.0f/numDivsHoriz;

#define TRIG_AUTO_TIMEOUT 2.0f    // seconds
#define AUTO_TIME_PERIOD_MAX 10.0 // seconds
#define AUTO_TIME_PERIOD_MIN 0.000025 // seconds, 40kHz

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
		TRIG_EDGE_FALL_LIGHT,
		TRIG_EDGE_RISE_LIGHT,
		ENUMS(FREEZE_LIGHT_RGB, 3),
		AUTO_TIME_LIGHT,
		NUM_LIGHTS
	};

	float buffer[4][BUFFER_SIZE] = {};
	int headIndex = 0;
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
#define TRIG_EDGE_RISE true
#define TRIG_EDGE_FALL false

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
	float lastFrequency = 0.0f;

	// persisted
	bool autoTimeMode = false;
	int trigSource = 0; // 0-3: Channel, 4: Ext
	int trigMode = TRIG_MODE_AUTO;
	bool trigEdge = TRIG_EDGE_RISE;
	bool showBaselines = false;
	bool showCenterline = false;
	bool showGrid = true;
	bool showStats = true;

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
		configParam(TIME_PARAM, -5.0f, 1.0f, -3.0f, "Time / Div", " s", 10.0f);
		configParam(HOLDOFF_PARAM, -3.0f, 0.0f, -3.0f, "Trigger holdoff", " s", 10.0f);

		// Trigger
		configParam(TRIG_LEVEL_PARAM, -10.0f, 10.0f, 0.0f, "Trigger threshold", " V");
		configButton(TRIG_SOURCE_PARAM, "Trigger source");
		configButton(TRIG_MODE_PARAM, "Trigger mode");
		configButton(TRIG_EDGE_PARAM, "Trigger edge");
		configButton(FREEZE_PARAM, "Freeze");
		configButton(AUTO_TIME_PARAM, "Auto time");
		configButton(STATS_PARAM, "Stats");

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
		if (stJ) showStats = json_is_true(stJ);

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
				buffer[c][headIndex] = inputs[A_INPUT + c].getVoltage();
			}

			triggerDetect(args);

			headIndex = (headIndex + 1) & BUFFER_MASK;
		}

		dspFrame++;
		if (dspFrame > 1000) {
			dspFrame = 0;
			if (period_s > 3600.0) period_s = 0.0;
			updateLights();
			readControls();
		}
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
			trigMode = (trigMode + 1) % 3;
		}
		if (edgeBtnTrig.process(trigEdgeBtn)) {
			trigEdge = !trigEdge;
		}
		if (freezeBtnTrig.process(freezeBtn)) {
			if (freezePending || frozen) {
				frozen = false;
				freezePending = false;
			} else {
				freezePending = true;
			}
		}
		if (autoTimeBtnTrig.process(autotimeBtn)) {
			autoTimeMode = !autoTimeMode;
		}
		if (statsBtnTrig.process(statsBtn)) {
			showStats = !showStats;
		}
	}

	void triggerDetect(const ProcessArgs& args) {
		// Get trigger signal
		float trigSig = 0.0f;
		float hysteresis = 0.1f; // Default for Ext (100mV)
		if (trigSource < 4) {
			trigSig = inputs[A_INPUT + trigSource].getVoltage();
			float vPerDiv = scale[trigSource];
			hysteresis = 0.1f * vPerDiv;
		} else {
			trigSig = inputs[CV_TRIG_EXT_INPUT].getVoltage();
		}

		float threshold = thresholdKnob;
		float timePerDiv = std::pow(10.f, params[TIME_PARAM].getValue());

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
		float signal = trigEdge == TRIG_EDGE_FALL ? -trigSig : trigSig;
		float thr = trigEdge == TRIG_EDGE_FALL ? -threshold : threshold;

		// Hysteresis window 0.1V: Lower = Thr - 0.1, High = Thr.
		bool schmittState = trigSchmitt.process(signal, thr - hysteresis, thr);
		bool edgeFound = trigPulse.process(schmittState);

		if (edgeFound) {
			if (autoTimeMode && period_s < AUTO_TIME_PERIOD_MAX && period_s > AUTO_TIME_PERIOD_MIN) {
				// Time since last trigger
				double period = period_s;

				// Calculate ideal time/div to show 3 periods
				// 3.0 periods fill 1 screen
				double targetTimePerDiv_s = (period * 3.0) / numDivsHoriz;

				// clamp to prevent log(0)
				if (targetTimePerDiv_s < 1e-5) targetTimePerDiv_s = 1e-5;
				float newParamVal = std::log10((float)targetTimePerDiv_s);

				params[TIME_PARAM].setValue(newParamVal);
			}
			if (period_s > 0.000001) {
				lastFrequency = (float)(1.0 / period_s);
			} else {
				lastFrequency = 0.0f;
			}
			period_s = 0.0;
		}

		if (triggered) {
			// Recording
			// We have already triggered, now we fill the buffer for the rest of the screen
			samplesSinceTrigger++;

			if (samplesSinceTrigger >= samplesToRecord) {
				// buffer full, send to be drawn
				//triggerIndex = triggerCandidate;
				triggered = false;

				// Set holdoff (max 1 sec)
				holdoffTime_s = std::pow(10.f,holdoffKnob);

				if (trigMode == TRIG_MODE_SOLO || freezePending) {
					frozen = true;
					freezePending = false;
				}
			}
		} else {
			// Waiting
			if (holdoffTime_s <= 0.0f && edgeFound) {
				// switch to recording
				triggered = true;
				//triggerCandidate = headIndex;
				lastTriggerIndex = triggerIndex;
				triggerIndex = headIndex;
				samplesSinceTrigger = 0;
				autoTrigTimer = 0.0f;
			}

			if (trigMode == TRIG_MODE_AUTO) {
				autoTrigTimer += args.sampleTime;
				// If no trigger for 0.5s (or > screen time), force update
				float timeout = TRIG_AUTO_TIMEOUT;
				if (timeout < totalScreenTime * 1.5f) timeout = totalScreenTime * 1.5f;

				if (autoTrigTimer > timeout) {
					// Force rolling trigger
					//lastTriggerIndex = triggerIndex;//TODO: not sure if thsi line is smart or not.
					triggerIndex = (headIndex - samplesToRecord) & BUFFER_MASK;
					triggered = false;
					samplesSinceTrigger = 0;
					autoTrigTimer = 0.0f;

					if (freezePending) {
						frozen = true;
						freezePending = false;
					}
				}
			}
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
		lights[TRIG_SOURCE_LIGHT_RGB + 0].setBrightness(getRed(trigSource));
		lights[TRIG_SOURCE_LIGHT_RGB + 1].setBrightness(getGreen(trigSource));
		lights[TRIG_SOURCE_LIGHT_RGB + 2].setBrightness(getBlue(trigSource));

		lights[TRIG_MODE_AUTO_LIGHT].setBrightness(trigMode==TRIG_MODE_AUTO ? 1.0f : 0.0f);
		lights[TRIG_MODE_NORM_LIGHT].setBrightness(trigMode==TRIG_MODE_NORM ? 1.0f : 0.0f);
		lights[TRIG_MODE_SOLO_LIGHT].setBrightness(trigMode==TRIG_MODE_SOLO ? 1.0f : 0.0f);

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
		float timePerDiv_s = std::pow(10.f, module->params[Scope::TIME_PARAM].getValue());

		const NVGcolor color = getColor(ch);


		const float width = box.size.x;
		const float totalTime = numDivsHoriz * timePerDiv_s;
		const float samplesToDraw = totalTime * module->sampleRate;

		if (samplesToDraw < 2.0f) return;

		// < 1.0: Zoomed in
		// > 1.0: Zoomed out
		const double samplesPerPixel = samplesToDraw / width;

		nvgBeginPath(args.vg);
		nvgStrokeColor(args.vg, color);
		nvgStrokeWidth(args.vg, 1.25f); // Slightly thicker line
		nvgLineJoin(args.vg, NVG_BEVEL);// NVG_ROUND

		bool first = true;

		int step = 1;
		if (samplesPerPixel > maxSamplesPerPx) {
			step = (int)(samplesPerPixel * maxPxPerSamples);
			if (step < 1) step = 1;
		}

		int drawLimitPixel = int(width)+1;

		if (module->triggered) {
			// We are in the middle of a scan
			double validPixels = (double)module->samplesSinceTrigger / samplesPerPixel;
			drawLimitPixel = (int)validPixels;

			if (drawLimitPixel > int(width)+1) drawLimitPixel = int(width)+1;
			if (drawLimitPixel < 0) drawLimitPixel = 0;
		}

		for (int x = 0; x < int(width); x += 1.0f) {
			// left: new
			// right: old
			// extreme right: ahead of bufferhead
			bool isNewData = (x < drawLimitPixel);
			const int startIdx = isNewData ? module->triggerIndex : module->lastTriggerIndex;

			int sampleOffset = (int)(x * samplesPerPixel);
			if (sampleOffset >= BUFFER_SIZE) {
				break;
			}
			int readIndex = (startIdx + sampleOffset) & BUFFER_MASK;

			if (!isNewData) {
				// Distance from New Trigger to this Read Point
				int distFromNew = (readIndex - module->triggerIndex) & BUFFER_MASK;

				// If this distance is small and positive, it means this old pixel
				// is wrapped in the buffer.
				if (distFromNew >= 0 && distFromNew < module->samplesSinceTrigger) {
					break;
				}
			}

			if (samplesPerPixel > 1.0) {
				// zoom out: Peaks
				const int iStart = (int)(x * samplesPerPixel);
				int iEnd = (int)((x + 1) * samplesPerPixel);
				if (iEnd <= iStart) iEnd = iStart + 1;

				float minV = 100.0f;
				float maxV = -100.0f;
				for (int i = iStart; i < iEnd; i += step) {
					const float v = module->buffer[ch][readIndex];
					if (v < minV) minV = v;
					if (v > maxV) maxV = v;
				}

				float yTop = volt2Px(maxV, offset, scale);
				float yBottom = volt2Px(minV, offset, scale);

				yTop = clamp(yTop, -10000.0f, box.size.y+10000.0f);
				yBottom = clamp(yBottom, -10000.0f, box.size.y+10000.0f);

				if (first) {
					nvgMoveTo(args.vg, float(x), yTop);
					first = false;
				}
				nvgLineTo(args.vg, float(x), yTop);
				nvgLineTo(args.vg, float(x), yBottom);

			} else {
				// zoom in
				const float v = module->buffer[ch][readIndex];
				float y = volt2Px(v, offset, scale);

				// clamp unseen.
				y = clamp(y, -10000.0f, box.size.y+10000.0f);

				if (first) {
					nvgMoveTo(args.vg, float(x), y);
					first = false;
				} else {
					if (x == drawLimitPixel) {
						nvgMoveTo(args.vg, float(x), y);
					} else {
						nvgLineTo(args.vg, float(x), y);
					}
				}
			}
		}
		nvgStroke(args.vg);
		if (module->triggered && float(drawLimitPixel) <= width) {
			// scanline
			nvgBeginPath(args.vg);
			nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 90)); // Faint white
			nvgStrokeWidth(args.vg, 1.0f);
			nvgMoveTo(args.vg, (float)drawLimitPixel, 0);
			nvgLineTo(args.vg, (float)drawLimitPixel, box.size.y);
			nvgStroke(args.vg);
		}
	}

	float volt2Px(float voltage, float offset_divs, float vPerDiv) const {

		const float totalVolts = numDivsVert * vPerDiv;

		const float pxPerVolt = box.size.y / totalVolts;
		const float centerY = box.size.y * 0.5f;

		return centerY - voltage * pxPerVolt - box.size.y*numDivsVert_inv*offset_divs;
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
			for (int c = 0; c < 4; c++) {
				drawWaveform(args, c);
			}
			nvgRestore(args.vg);
		}
		frame++;
		if (frame > 60) frame = 0;
	}

	void drawStats(const DrawArgs& args) const {
		if (!module || !module->showStats) return;

		//if (!font) font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/autinn.ttf"));
		//if (!font) return;
		//nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 13.0f);

		const int ch = module->trigSource;
		if (ch >= 4) return; // no stats for ext trigger
		if (!module->inputs[Scope::A_INPUT + ch].isConnected()) return;

		// limit
		const int startIndex = module->triggerIndex;
		const float timePerDiv_s = std::pow(10.f, module->params[Scope::TIME_PARAM].getValue());
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
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, 20.0f, 0.0f);
		nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 128));
		nvgFill(args.vg);

		// Text
		nvgFillColor(args.vg, getColor(ch));
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		char text[128];
		if (module->lastFrequency > 0.0f) {
			snprintf(text, sizeof(text), "Ch %c  Min: %+.2f V   Max: %+.2f V   Vpp: %.2f V   Freq: %.1f Hz",
				'A' + ch,
				module->lastFrequency,
				minV,
				maxV,
				(maxV - minV));
		} else {
			snprintf(text, sizeof(text), "Ch %c  Min: %+.2f V   Max: %+.2f V   Vpp: %.2f V",
				'A' + ch,
				minV,
				maxV,
				(maxV - minV));
		}

		nvgText(args.vg, 10, 10, text, nullptr);
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

		if (ch < 4) {
			scale = module->scale[ch];
			offset = module->offset[ch];
		} else {
			// If ext trigger then no visuals
			return;
		}

		float y = volt2Px(currentLevel, offset, scale);
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

	void drawDashedLine(NVGcontext* vg, float x1, float y1, float x2, float y2, float stroke, NVGcolor color) const {
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
					const float y = volt2Px(0.0f, offset, scale);
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
		addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(xTime, yRow1), module, Scope::TIME_PARAM));
		
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
		Scope* a = dynamic_cast<Scope*>(module);
		assert(a);

		menu->addChild(new MenuLabel());
		menu->addChild(new ShowGridItem(a, "Show grid"));
		menu->addChild(new ShowBaseItem(a, "Show baselines"));
		menu->addChild(new ShowCenterItem(a, "Show centerline"));
		menu->addChild(new MenuLabel());
	}
};

Model* modelScope = createModel<Scope, ScopeWidget>("Scope40");