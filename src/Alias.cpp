#include "Autinn.hpp"
#include "Autinn-dsp.hpp"
#include <cmath>

struct Alias : Module {
	enum ParamIds {
		START_BUTTON,
		NUM_PARAMS
	};
	enum InputIds {
		RETURN_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		TEST_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	enum State {
		READY,
		WORKING,
		FINISHED
	};

	State currentState = READY;
	dsp::SchmittTrigger startTrigger;
	
	float sweepPhase = 0.0f;
	float sweepFreq = 20.0f;
	
	// Graph Data
	float thdCurve[256]; 
	float score100Hz = -120.0f;
	float score1kHz = -120.0f;
	float score10kHz = -120.0f;

	Alias() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(START_BUTTON, "Start Sweep");
		configInput(RETURN_INPUT, "Audio Return");
		configOutput(TEST_OUTPUT, "Sine Test Output");

		for(int i = 0; i < 256; i++) thdCurve[i] = -120.0f;
	}

	void onReset(const ResetEvent& e) override {
		currentState = READY;
		sweepFreq = 20.0f;
		sweepPhase = 0.0f;
		score100Hz = score1kHz = score10kHz = -120.0f;
		for(int i = 0; i < 256; i++) thdCurve[i] = -120.0f;
		Module::onReset(e);
	}

	void process(const ProcessArgs &args) override {

		if (startTrigger.process(params[START_BUTTON].getValue())) {
			if (currentState != WORKING) {
				currentState = WORKING;
				sweepFreq = 20.0f;
				sweepPhase = 0.0f;
				score100Hz = score1kHz = score10kHz = -120.0f;
				for(int i = 0; i < 256; i++) thdCurve[i] = -120.0f;
			}
		}

		float out = 0.0f;

		if (currentState == WORKING) {
			// Generate pure sine (+/- 5V)
			out = std::sin(sweepPhase * 2.0f * float(M_PI)) * 5.0f; 
			
			sweepPhase += sweepFreq * args.sampleTime;
			if (sweepPhase >= 1.0f) sweepPhase -= 1.0f;

			// Exponentially sweep from 20Hz to 20kHz over 5 seconds
			float sweepMultiplier = std::pow(20000.0f / 20.0f, args.sampleTime / 5.0f);
			sweepFreq *= sweepMultiplier; 

			if (sweepFreq >= 20000.0f) {
				currentState = FINISHED;
				sweepFreq = 20.0f;
				out = 0.0f;
			} else {
				// TODO: DSP
				
				// Placeholder for testing the UI drawing:
				float currentThd = -100.0f + (sweepFreq / 20000.0f) * 80.0f; 

				// Data for the UI
				
				// Map 20Hz - 20kHz to our 0-255 array logarithmically
				float logMin = std::log10(20.0f);
				float logMax = std::log10(20000.0f);
				float currentLog = std::log10(sweepFreq);
				
				int pixelIndex = (currentLog - logMin) / (logMax - logMin) * 255.0f;
				pixelIndex = clamp(pixelIndex, 0, 255);
				thdCurve[pixelIndex] = currentThd;

				// benchmarks
				if (sweepFreq >= 100.0f && sweepFreq < 105.0f) score100Hz = currentThd;
				if (sweepFreq >= 997.0f && sweepFreq < 1005.0f) score1kHz = currentThd; // AES17
				if (sweepFreq >= 10000.0f && sweepFreq < 10100.0f) score10kHz = currentThd;
			}
		}

		outputs[TEST_OUTPUT].setVoltage(out);
	}
};

struct AliasDisplay : TransparentWidget {
	Alias* module;

	AliasDisplay() : module(nullptr) {
		box.size = Vec(130, 110);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !module) return;

		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/ShareTechMono-Regular.ttf"));
		
		if (font) {
			nvgFontSize(args.vg, 12);
			nvgFontFaceId(args.vg, font->handle);
			nvgFillColor(args.vg, nvgRGBA(0, 255, 0, 255));

			// Status
			std::string statusText = "STATUS: ";
			if (module->currentState == Alias::READY) statusText += "READY";
			else if (module->currentState == Alias::WORKING) statusText += "WORKING...";
			else if (module->currentState == Alias::FINISHED) statusText += "FINISHED";
			nvgText(args.vg, 0, 10, statusText.c_str(), nullptr);

			// Benchmarks
			if (module->currentState != Alias::READY) {
				nvgText(args.vg, 0, 25, string::f("100 Hz: %5.1f dB", module->score100Hz).c_str(), nullptr);
				nvgText(args.vg, 0, 40, string::f("1  kHz: %5.1f dB", module->score1kHz).c_str(), nullptr);
				nvgText(args.vg, 0, 55, string::f("10 kHz: %5.1f dB", module->score10kHz).c_str(), nullptr);
			}
		}

		// Line Graph
		if (module->currentState != Alias::READY) {
			float graphX = 0.0f;
			float graphY = 65.0f;
			float graphWidth = 130.0f;
			float graphHeight = 45.0f;

			// Draw graph background bounding box
			nvgBeginPath(args.vg);
			nvgRect(args.vg, graphX, graphY, graphWidth, graphHeight);
			nvgFillColor(args.vg, nvgRGBA(0x00, 0x22, 0x00, 0xFF));
			nvgFill(args.vg);

			// Draw the THD curve
			nvgBeginPath(args.vg);
			for (int i = 0; i < 256; i++) {
				float x = graphX + (i / 255.0f) * graphWidth;
				
				// Map -120dB (bottom) to 0dB (top)
				float normalizedY = (module->thdCurve[i] + 120.0f) / 120.0f; 
				normalizedY = clamp(normalizedY, 0.0f, 1.0f);
				
				float y = graphY + graphHeight - (normalizedY * graphHeight); 

				if (i == 0) nvgMoveTo(args.vg, x, y);
				else nvgLineTo(args.vg, x, y);
			}

			nvgStrokeColor(args.vg, nvgRGBA(0x44, 0xFF, 0x44, 0xFF)); 
			nvgStrokeWidth(args.vg, 1.5f);
			nvgStroke(args.vg);
		}
	}
};

struct AliasWidget : ModuleWidget {
	AliasWidget(Alias* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/AliasModule.svg")));

		// 10 HP Wide
		if (box.size.x == 0) {
			box.size = Vec(10 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
		}

		const float HP = RACK_GRID_WIDTH;
		const float centerX = box.size.x / 2.0f;

		addChild(createWidget<ScrewStarAutinn>(Vec(HP, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * HP, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(HP, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * HP, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Display Screen
		AliasDisplay* display = new AliasDisplay();
		display->box.pos = Vec(10.0f, 50.0f);
		display->module = module;
		addChild(display);

		// Controls & Ports
		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(centerX, 200.0f), module, Alias::START_BUTTON));
		
		addOutput(createOutputCentered<OutPortAutinn>(Vec(centerX - 25.0f, 260.0f), module, Alias::TEST_OUTPUT));
		addInput(createInputCentered<InPortAutinn>(Vec(centerX + 25.0f, 260.0f), module, Alias::RETURN_INPUT));
	}
};

Model* modelAlias = createModel<Alias, AliasWidget>("Alias");