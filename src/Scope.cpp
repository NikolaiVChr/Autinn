#include "Autinn.hpp"

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
		TRIG_SOURCE_LIGHT_RGB, TRIG_SOURCE_LIGHT_G, TRIG_SOURCE_LIGHT_B,
		TRIG_MODE_LIGHT_RGB,   TRIG_MODE_LIGHT_G,   TRIG_MODE_LIGHT_B,
		TRIG_EDGE_LIGHT_G,
		FREEZE_LIGHT,
		NUM_LIGHTS
	};

	Scope() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		// pos
		configParam(POS_A_PARAM, -10.0, 10.0, 0.0, "Channel A Pos", " V");
		configParam(POS_B_PARAM, -10.0, 10.0, 0.0, "Channel B Pos", " V");
		configParam(POS_C_PARAM, -10.0, 10.0, 0.0, "Channel C Pos", " V");
		configParam(POS_D_PARAM, -10.0, 10.0, 0.0, "Channel D Pos", " V");

		// Time
		configParam(TIME_PARAM, -6.0, 6.0, 0.0, "Time / Div"); 
		configParam(HOLDOFF_PARAM, 0.0, 1.0, 0.0, "Trigger holdoff", "%");

		// Trigger
		configParam(TRIG_LEVEL_PARAM, -10.0, 10.0, 0.0, "Trigger threshold", " V");
		configParam(TRIG_SOURCE_PARAM, 0.0, 1.0, 0.0, "Trigger source");
		configParam(TRIG_MODE_PARAM, 0.0, 1.0, 0.0, "Trigger mode");
		configParam(TRIG_EDGE_PARAM, 0.0, 1.0, 0.0, "Trigger edge");
		configParam(FREEZE_PARAM, 0.0, 1.0, 0.0, "Freeze");

		// inputs
		configInput(A_INPUT, "Channel A");
		configInput(B_INPUT, "Channel B");
		configInput(C_INPUT, "Channel C");
		configInput(D_INPUT, "Channel D");
		configInput(CV_TRIG_EXT_INPUT, "Ext. trigger");
	}

	void process(const ProcessArgs& args) override {
		// later
	}
};


struct ScopeDisplay : TransparentWidget {
	Scope* module{};
	
	void draw(const DrawArgs& args) override {
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
			addInput(createInputCentered<InPortAutinn>(Vec(x, yRow2), module, Scope::A_INPUT + i));
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
		float btnSpacingY = mm2px(8.0f);
		float btnLightOffsetX = mm2px(7.0f); // xTrigBtns to light center

		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xTrigBtns, yRow1 - btnSpacingY), module, Scope::TRIG_SOURCE_PARAM));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow1 - btnSpacingY), module, Scope::TRIG_SOURCE_LIGHT_RGB));

		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xTrigBtns, yRow1 + btnSpacingY), module, Scope::TRIG_MODE_PARAM));
		addChild(createLightCentered<SmallLight<RedGreenBlueLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow1 + btnSpacingY), module, Scope::TRIG_MODE_LIGHT_RGB));

		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xTrigBtns, yRow2), module, Scope::TRIG_EDGE_PARAM));
		addChild(createLightCentered<SmallLight<GreenLight>>(Vec(xTrigBtns + btnLightOffsetX, yRow2), module, Scope::TRIG_EDGE_LIGHT_G));

		// Ext trigger
		addInput(createInputCentered<InPortAutinn>(Vec(xExtTrig, yRow2), module, Scope::CV_TRIG_EXT_INPUT));
	}
};

Model* modelScope = createModel<Scope, ScopeWidget>("Scope40");