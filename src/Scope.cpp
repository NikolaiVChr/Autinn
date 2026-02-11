#include "Autinn.hpp"
#include <cmath>

static constexpr int BUFFER_SIZE = 1 << 20;// 2^20 (5.4 seconds at 192khz)
static constexpr int BUFFER_MASK = BUFFER_SIZE - 1;

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
		ENUMS(TRIG_MODE_LIGHT_RGB, 3),
		ENUMS(TRIG_EDGE_LIGHT_RGB, 3),
		FREEZE_LIGHT,
		NUM_LIGHTS
	};

	float buffer[4][BUFFER_SIZE] = {};
	int headIndex = 0;
	int triggerIndex = 0; // last valid trigger
	float sampleRate = 44100.0f;

	dsp::SchmittTrigger trigSchmitt;
	dsp::BooleanTrigger trigPulse;
	dsp::BooleanTrigger srcBtnTrig;
	dsp::BooleanTrigger modeBtnTrig;
	dsp::BooleanTrigger edgeBtnTrig;
	dsp::BooleanTrigger freezeBtnTrig;

#define TRIG_MODE_AUTO 0
#define TRIG_MODE_NORM 1
#define TRIG_MODE_SOLO 2
#define TRIG_EDGE_RISE true
#define TRIG_EDGE_FALL false

	int trigSource = 0; // 0-3: Channel, 4: Ext
	int trigMode = TRIG_MODE_AUTO;
	bool trigEdge = TRIG_EDGE_RISE;
	bool frozen = false;

	bool triggered = false;       // Have we found a trigger edge?
	int triggerCandidate = 0;     // Where did the trigger happen?
	int samplesSinceTrigger = 0;  // Counter for div * time/div wait
	float holdoffTime_s = 0.0f;     // Remaining holdoff in seconds
	float autoTrigTimer = 0.0f;   // Auto mode timeout

	int dspFrame = 1001;

	Scope() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		// pos
		configParam(POS_A_PARAM, -8.0, 8.0, 0.0, "Channel A Pos", " Div");
		configParam(POS_B_PARAM, -8.0, 8.0, 0.0, "Channel B Pos", " Div");
		configParam(POS_C_PARAM, -8.0, 8.0, 0.0, "Channel C Pos", " Div");
		configParam(POS_D_PARAM, -8.0, 8.0, 0.0, "Channel D Pos", " Div");

		// scale
		configParam(SCALE_A_PARAM, 0.1, 10.0, 0.1, "Channel A Scale", " V/Div");
		configParam(SCALE_B_PARAM, 0.1, 10.0, 0.1, "Channel B Scale", " V/Div");
		configParam(SCALE_C_PARAM, 0.1, 10.0, 0.1, "Channel C Scale", " V/Div");
		configParam(SCALE_D_PARAM, 0.1, 10.0, 0.1, "Channel D Scale", " V/Div");

		// Time
		configParam(TIME_PARAM, -5.0f, 1.0f, -3.0f, "Time / Div", " s", 10.0f);
		configParam(HOLDOFF_PARAM, -3.0f, 0.0f, -3.0f, "Trigger holdoff", " s", 10.0f);

		// Trigger
		configParam(TRIG_LEVEL_PARAM, -10.0f, 10.0f, 0.0f, "Trigger threshold", " V");
		configButton(TRIG_SOURCE_PARAM, "Trigger source");
		configButton(TRIG_MODE_PARAM, "Trigger mode");
		configButton(TRIG_EDGE_PARAM, "Trigger edge");
		configButton(FREEZE_PARAM, "Freeze");

		// inputs
		configInput(A_INPUT, "Channel A");
		configInput(B_INPUT, "Channel B");
		configInput(C_INPUT, "Channel C");
		configInput(D_INPUT, "Channel D");
		configInput(CV_TRIG_EXT_INPUT, "Ext. trigger");
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "trigSource", json_integer(trigSource));
		json_object_set_new(rootJ, "trigMode", json_integer(trigMode));
		json_object_set_new(rootJ, "trigEdge", json_boolean(trigEdge));
		//json_object_set_new(rootJ, "frozen", json_boolean(frozen));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* sJ = json_object_get(rootJ, "trigSource");
		if (sJ) trigSource = json_integer_value(sJ);

		json_t* mJ = json_object_get(rootJ, "trigMode");
		if (mJ) trigMode = json_integer_value(mJ);

		json_t* eJ = json_object_get(rootJ, "trigEdge");
		if (eJ) trigEdge = json_is_true(eJ);

		//json_t* fJ = json_object_get(rootJ, "frozen");
		//if (fJ) frozen = json_is_true(fJ);
	}

	void process(const ProcessArgs& args) override {
		sampleRate = args.sampleRate;

		if (srcBtnTrig.process((bool)params[TRIG_SOURCE_PARAM].getValue())) {
			trigSource = (trigSource + 1) % 5;
		}
		if (modeBtnTrig.process((bool)params[TRIG_MODE_PARAM].getValue())) {
			trigMode = (trigMode + 1) % 3;
		}
		if (edgeBtnTrig.process((bool)params[TRIG_EDGE_PARAM].getValue())) {
			trigEdge = !trigEdge;
		}
		if (freezeBtnTrig.process((bool)params[FREEZE_PARAM].getValue())) {
			frozen = !frozen;
		}

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
			updateLights();
			// TODO: update the other params to fields
		}
	}

	void triggerDetect(const ProcessArgs& args) {
		// Get trigger signal
		float trigSig = 0.0f;
		float hysteresis = 0.1f; // Default for Ext (100mV)
		if (trigSource < 4) {
			trigSig = inputs[A_INPUT + trigSource].getVoltage();
			float vPerDiv = params[SCALE_A_PARAM + trigSource].getValue();
			hysteresis = 0.1f * vPerDiv;
		} else {
			trigSig = inputs[CV_TRIG_EXT_INPUT].getVoltage();
		}

		float threshold = params[TRIG_LEVEL_PARAM].getValue();
		float timePerDiv = std::pow(10.f, params[TIME_PARAM].getValue());

		// We want to record 10 divisions after the trigger to fill the screen
		float totalScreenTime = 10.0f * timePerDiv;
		int samplesToRecord = int(totalScreenTime * sampleRate);

		// TODO: min samples
		if (samplesToRecord < 32) samplesToRecord = 32;

		// Holdoff
		if (holdoffTime_s > 0.0f) {
			holdoffTime_s -= args.sampleTime;
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
				holdoffTime_s = std::pow(10.f,params[HOLDOFF_PARAM].getValue());

				if (trigMode == TRIG_MODE_SOLO) {
					frozen = true;
				}
			}
		} else {
			// Waiting
			if (holdoffTime_s <= 0.0f) {
				// Schmitt-trigger processing
				// Invert input for falling edge
				float signal = trigEdge == TRIG_EDGE_FALL ? -trigSig : trigSig;
				float thr = trigEdge == TRIG_EDGE_FALL ? -threshold : threshold;

				// Hysteresis window 0.1V: Lower = Thr - 0.1, High = Thr.
				bool schmittState = trigSchmitt.process(signal, thr - hysteresis, thr);

				// Use trigPulse to detect the rising edge of the Schmitt state
				if (trigPulse.process(schmittState)) {
					triggered = true;
					//triggerCandidate = headIndex;
					triggerIndex = headIndex;
					samplesSinceTrigger = 0;
					autoTrigTimer = 0.0f;
				}
			}

			if (trigMode == TRIG_MODE_AUTO) {
				autoTrigTimer += args.sampleTime;
				// If no trigger for 0.5s (or > screen time), force update
				float timeout = 0.5f;
				if (timeout < totalScreenTime * 1.5f) timeout = totalScreenTime * 1.5f;

				if (autoTrigTimer > timeout) {
					// Force rolling trigger
					triggerIndex = (headIndex - samplesToRecord) & BUFFER_MASK;
					triggered = false;
					samplesSinceTrigger = 0;
					autoTrigTimer = 0.0f;
				}
			}
		}
	}

	void updateLights() {
		// Source: A=Red, B=Yel, C=Grn, D=Blu, Ext=White
		float sR=0, sG=0, sB=0;
		switch(trigSource) {
			case 0: sR=1; break; // A
			case 1: sR=1; sG=1; break; // B
			case 2: sG=1; break; // C
			case 3: sB=1; sG=0.5; break; // D
			case 4: sR=1; sG=1; sB=1; break; // Ext
			default: ;
		}
		lights[TRIG_SOURCE_LIGHT_RGB + 0].setBrightness(sR);
		lights[TRIG_SOURCE_LIGHT_RGB + 1].setBrightness(sG);
		lights[TRIG_SOURCE_LIGHT_RGB + 2].setBrightness(sB);

		// Mode: Auto=green, Norm=yellow, Single=red
		float mR=0, mG=0, mB=0;
		switch(trigMode) {
			case 0: mG=1; break; // Auto
			case 1: mR=1; mG=1; break; // Norm
			case 2: mR=1; break; // Single
			default: ;
		}
		lights[TRIG_MODE_LIGHT_RGB+0].setBrightness(mR);
		lights[TRIG_MODE_LIGHT_RGB+1].setBrightness(mG);
		lights[TRIG_MODE_LIGHT_RGB+2].setBrightness(mB);

		// Edge: Green=Rising, Red=Falling
		lights[TRIG_EDGE_LIGHT_RGB+0].setBrightness(trigEdge ? 1.0f : 0.0f);
		lights[TRIG_EDGE_LIGHT_RGB+1].setBrightness(trigEdge ? 0.0f : 1.0f);

		lights[FREEZE_LIGHT].setBrightness(frozen ? 1.0f : 0.0f);
	}
};


struct ScopeDisplay : TransparentWidget {
	Scope* module{};
	int frame = 0;
	// 8 divisions (audio scope std)
	const float numDivs = 8.0f;
	const float numDivs_inv = 1.0f/8.0f*2.0f;
	const float maxSamplesPerPx = 16.0f;
	const float maxPxPerSamples = 1.0f/maxSamplesPerPx;


	void drawWaveform(const DrawArgs& args, int ch) {
		if (!module) return;
		if (!module->inputs[Scope::A_INPUT + ch].isConnected()) return;

		float scale = module->params[Scope::SCALE_A_PARAM + ch].getValue();
		float offset = module->params[Scope::POS_A_PARAM + ch].getValue();
		float timePerDiv_s = std::pow(10.f, module->params[Scope::TIME_PARAM].getValue());

		NVGcolor color;
		switch (ch) {
			case 0: color = nvgRGBA(255, 50, 50, 230); break;   // Red
			case 1: color = nvgRGBA(255, 230, 50, 230); break;  // Yellow
			case 2: color = nvgRGBA(50, 255, 50, 230); break;   // Green
			case 3: color = nvgRGBA(50, 150, 255, 230); break;  // Blue
			default: color = nvgRGBA(255, 255, 255, 255); break;
		}

		const float width = box.size.x;
		// 10 horiz divs
		const float totalTime = 10.0f * timePerDiv_s;
		const float samplesToDraw = totalTime * module->sampleRate;

		if (samplesToDraw < 2.0f) return;

		// < 1.0: Zoomed in
		// > 1.0: Zoomed out
		const double samplesPerPixel = samplesToDraw / width;

		const int startIndex = module->triggerIndex;

		nvgBeginPath(args.vg);
		nvgStrokeColor(args.vg, color);
		nvgStrokeWidth(args.vg, 1.25f); // Slightly thicker line

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

		for (int x = 0; x < drawLimitPixel; x += 1.0f) {
			if (samplesPerPixel > 1.0) {
				// zoom out: Peaks
				const int iStart = (int)(x * samplesPerPixel);
				int iEnd = (int)((x + 1) * samplesPerPixel);
				if (iEnd <= iStart) iEnd = iStart + 1;

				float minV = 100.0f;
				float maxV = -100.0f;
				for (int i = iStart; i < iEnd; i += step) {
					const int idx = (startIndex + i) & BUFFER_MASK;
					const float v = module->buffer[ch][idx];
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
				const double idxOffset = x * samplesPerPixel;

				int bufferIndex = startIndex + (int)idxOffset;

				// Handle ring buffer wrap
				bufferIndex = bufferIndex & BUFFER_MASK;

				const float v = module->buffer[ch][bufferIndex];
				float y = volt2Px(v, offset, scale);

				// clamp unseen.
				y = clamp(y, -10000.0f, box.size.y+10000.0f);

				if (first) {
					nvgMoveTo(args.vg, float(x), y);
					first = false;
				} else {
					nvgLineTo(args.vg, float(x), y);
				}
			}
		}
		nvgStroke(args.vg);
	}

	float volt2Px(float voltage, float offset_divs, float vPerDiv) const {

		const float totalVolts = numDivs * vPerDiv;

		const float pxPerVolt = box.size.y / totalVolts;
		const float centerY = box.size.y * 0.5f;

		return centerY - voltage * pxPerVolt - box.size.y*numDivs_inv*offset_divs;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 0) drawGrid(args);
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
	
	void drawGrid(const DrawArgs& args) {
		// Background
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGB(20, 20, 20));
		nvgFill(args.vg);

		// tmp grid
		nvgBeginPath(args.vg);
		nvgStrokeColor(args.vg, nvgRGBA(60, 60, 60, 100));
		nvgStrokeWidth(args.vg, 1.0);
		for (int i = 1; i < 10; i++) {
			float x = (box.size.x / 10.0f) * float(i);
			nvgMoveTo(args.vg, x, 0);
			nvgLineTo(args.vg, x, box.size.y);
		}
		for (int i = 1; i < 8; i++) {
			float y = (box.size.y / 8.0f) * float(i);
			nvgMoveTo(args.vg, 0, y);
			nvgLineTo(args.vg, box.size.x, y);
		}
		nvgStroke(args.vg);
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
		
		// Freeze
		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xHoldoff, yRow2), module, Scope::FREEZE_PARAM));
		addChild(createLightCentered<SmallLight<RedLight>>(Vec(xHoldoff + 12, yRow2 + 12), module, Scope::FREEZE_LIGHT));


		// Trigger
		float xTrigLevel = 28.0f * hp;
		float xTrigBtns  = 33.0f * hp;
		float xExtTrig   = 38.0f * hp;

		// Trig level
		addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(xTrigLevel, yRow1), module, Scope::TRIG_LEVEL_PARAM));

		// Trig buttons (grid layout)
		// source, mode
		// edge, light
		float btnSpacingY = mm2px(18.0f/3.0f);// 12mm between them
		float btnLightOffsetX = mm2px(7.0f); // xTrigBtns to light center

		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xTrigBtns, yRow1 - btnSpacingY), module, Scope::TRIG_SOURCE_PARAM));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow1 - btnSpacingY), module, Scope::TRIG_SOURCE_LIGHT_RGB));

		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xTrigBtns, yRow1 + btnSpacingY), module, Scope::TRIG_MODE_PARAM));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow1 + btnSpacingY), module, Scope::TRIG_MODE_LIGHT_RGB));

		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xTrigBtns, yRow2), module, Scope::TRIG_EDGE_PARAM));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow2), module, Scope::TRIG_EDGE_LIGHT_RGB));

		// Ext trigger
		addInput(createInputCentered<InPortAutinn>(Vec(xExtTrig, yRow2), module, Scope::CV_TRIG_EXT_INPUT));
	}
};

Model* modelScope = createModel<Scope, ScopeWidget>("Scope40");