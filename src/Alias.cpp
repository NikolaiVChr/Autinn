#include "Autinn.hpp"
#include "Autinn-dsp.hpp"
#include <cmath>

struct Alias : Module {
	enum ParamIds {
		START_BUTTON,
		SETTLE_KNOB,
		VCO_MODE_SWITCH,
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
		NOT_READY,
		READY,
		WORKING,
		WAIT_ZERO_CROSS,
		SETTLE,
		RECORD,
		FINISHED
	};

	State currentState = NOT_READY;
	dsp::SchmittTrigger startTrigger;
	
	float sweepPhase = 0.0f;
	float sweepFreq = 20.0f;
	
	int currentStep = 0;
	int settleCounter = 0;
	float lastSampleRate = 0.0f;
	float activeSettleTime = 0.05f;
	volatile bool mode = false;

	static constexpr int FFT_SIZE = 16384;
	static constexpr int STEPS = 256;

	// Graph Data
	float thdCurve[STEPS];
	float targetFrequencies[3] = {100.0f, 997.0f, 10000.0f};// 997 is a prime and does not share a common factor with 44.1khz
	std::string benchmarkLabels[3] = {"100 Hz", " 1K Hz", "10K Hz"};
	float benchmarkScores[3] = {-210.0f, -210.0f, -210.0f};

	dsp::RealFFT fft;
	alignas(16) float windowArray[FFT_SIZE];
	alignas(16) float audioBuffer[FFT_SIZE];
	alignas(16) float fftOutput[FFT_SIZE];
	int bufferIndex = 0;

	Alias() : fft(FFT_SIZE) { // Initialize the FFT size in the constructor initialization list
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SETTLE_KNOB, 0.01f, 2.5f, 0.05f, "Settle Time", " s");
		configButton(START_BUTTON, "Start Sweep");
		configButton(VCO_MODE_SWITCH, "Toggle Mode");
		configInput(RETURN_INPUT, "Audio Return");
		configOutput(TEST_OUTPUT, "Test Send");

		for(int i = 0; i < STEPS; i++) thdCurve[i] = -210.0f;

		/*
		// Pre-calculate the Blackman-Harris window
		const float a0 = 0.35875f;
		const float a1 = 0.48829f;
		const float a2 = 0.14128f;
		const float a3 = 0.01168f;

		for (int i = 0; i < FFT_SIZE; i++) {
			float phase = (float)i / (float)(FFT_SIZE - 1);
			windowArray[i] = a0
						   - a1 * std::cos(2.0f * (float)M_PI * phase)
						   + a2 * std::cos(4.0f * (float)M_PI * phase)
						   - a3 * std::cos(6.0f * (float)M_PI * phase);
		}
		*/

		// Flat Top Window coefficients
		float a0 = 0.21557895;
		float a1 = 0.41663158;
		float a2 = 0.27726315;
		float a3 = 0.08357894;
		float a4 = 0.00694736;

		for (int i = 0; i < FFT_SIZE; i++) {
			windowArray[i] = a0
				- a1 * cos(2.0 * M_PI * (double)i / double(FFT_SIZE))
				+ a2 * cos(4.0 * M_PI * (double)i / double(FFT_SIZE))
				- a3 * cos(6.0 * M_PI * (double)i / double(FFT_SIZE))
				+ a4 * cos(8.0 * M_PI * (double)i / double(FFT_SIZE));
		}
	}

	void onReset(const ResetEvent& e) override {
		currentState = READY;
		sweepFreq = 20.0f;
		sweepPhase = 0.0f;
		benchmarkScores[0] = benchmarkScores[1] = benchmarkScores[2] = -210.0f;
		mode = false;
		for(int i = 0; i < STEPS; i++) thdCurve[i] = -210.0f;
		Module::onReset(e);
	}

	json_t *dataToJson() override {
		json_t *root = json_object();
		json_object_set_new(root, "mode", json_boolean(mode));
		return root;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *ext = json_object_get(rootJ, "mode");
		if (ext) {
			mode = json_boolean_value(ext);
			params[VCO_MODE_SWITCH].setValue(mode?1.0f:0.0f);
		}
	}

	/*
	static float getFreqForStep(int step) {
		float logMin = std::log10(20.0f);
		float logMax = std::log10(20000.0f);
		float stepLog = logMin + (step / 255.0f) * (logMax - logMin);
		return std::pow(10.0f, stepLog);
	}
	*/

	/**
	 * Calculate the frequency for a specific pixel on the graph
	 *
	 */
	float getFreqForStep(int step) {
		// Calculate the ideal log frequency
		float logP = step / float(STEPS-1);
		float idealFreq = 20.0f * std::pow(20000.0f / 20.0f, logP);

		float sRate = (lastSampleRate > 0) ? lastSampleRate : 44100.0f;

		// Snap it to the nearest FFT bin (Synchronous Sampling)
		float binRes = sRate / float(FFT_SIZE);
		float binIndex = std::round(idealFreq / binRes);

		// Don't let it be bin 0 (DC)
		if (binIndex < 2) binIndex = 2;

		return binIndex * binRes;
	}

	void process(const ProcessArgs &args) override {

		bool sampleRateChanged = false;
		if (args.sampleRate != lastSampleRate) {
			lastSampleRate = args.sampleRate;
			sampleRateChanged = true;
		}

		bool isPatched = inputs[RETURN_INPUT].isConnected() && outputs[TEST_OUTPUT].isConnected();

		if (!isPatched || sampleRateChanged) {
			// If a cable is pulled, abort everything.
			currentState = NOT_READY;
		} else if (currentState == NOT_READY) {
			currentState = READY;
			benchmarkScores[0] = benchmarkScores[1] = benchmarkScores[2] = -210.0f;
			for(int i = 0; i < STEPS; i++) thdCurve[i] = -210.0f;
		}

		if (startTrigger.process(params[START_BUTTON].getValue())) {
			if (currentState == READY || currentState == FINISHED) {
				currentState = WAIT_ZERO_CROSS;
				currentStep = 0;
				sweepFreq = getFreqForStep(0);
				activeSettleTime = params[SETTLE_KNOB].getValue();
				sweepPhase = 0.0f;
				benchmarkScores[0] = benchmarkScores[1] = benchmarkScores[2] = -210.0f;
				for(int i = 0; i < STEPS; i++) thdCurve[i] = -210.0f;
			}
		}

		float out = 0.0f;

		mode = params[VCO_MODE_SWITCH].getValue() > 0.5f;

		if (currentState != READY && currentState != FINISHED) {

			if (mode) {
				// 1V/Octave
				out = std::log2(sweepFreq / FREQ_C4);
			} else {
				// Generate pure sine (+/- 5V)
				out = std::sin(sweepPhase * 2.0f * float(M_PI)) * 5.0f;
			}

			// Advance phase and check for zero-crossing
			sweepPhase += sweepFreq * args.sampleTime;
			bool crossedZero = false;
			if (sweepPhase >= 1.0f) {
				sweepPhase -= 1.0f;
				crossedZero = true; // The wave wrapped perfectly around 0!
			}

			// state
			if (currentState == WAIT_ZERO_CROSS) {
				if (crossedZero) {
					// Snap to the new frequency precisely at 0.0V to prevent clicks
					sweepFreq = getFreqForStep(currentStep);
					currentState = SETTLE;
					settleCounter = 0;
				}
			} else if (currentState == SETTLE) {
				settleCounter++;
				if ((settleCounter * args.sampleTime) >= activeSettleTime) {
					currentState = RECORD;
					bufferIndex = 0;
				}
			} else if (currentState == RECORD) {
				// Record the stable signal
				audioBuffer[bufferIndex] = inputs[RETURN_INPUT].getVoltage() * 0.2f;
				bufferIndex++;

				// When buffer is full, do the math!
				if (bufferIndex >= FFT_SIZE) {

					float dcOffset = 0.0f;
					for (int i = 0; i < FFT_SIZE; i++) dcOffset += audioBuffer[i];
					dcOffset /= (float)FFT_SIZE;
					for (int i = 0; i < FFT_SIZE; i++) audioBuffer[i] -= dcOffset;

					// Apply Window and FFT
					for (int i = 0; i < FFT_SIZE; i++) audioBuffer[i] *= windowArray[i];
					fft.rfft(audioBuffer, fftOutput);

					constexpr int numBins = FFT_SIZE / 2;
					float magnitudes[numBins];
					for (int k = 1; k < numBins; k++) {
						float re = fftOutput[2 * k];
						float im = fftOutput[2 * k + 1];
						magnitudes[k] = (re * re) + (im * im);
					}

					float signalPower = 0.0f;
					float binResolution = args.sampleRate / FFT_SIZE;


					// Mute Fundamental and Harmonics
					for (int h = 1; (h * sweepFreq) < (args.sampleRate / 2.0f); h++) {
						float targetFreq = h * sweepFreq;
						int centerBin = (int)std::round(targetFreq / binResolution);

						// Calculate +-5% musical width in bins
						float hzWidth = targetFreq * 0.05f;
						int dynamicNotch = (int)std::round(hzWidth / binResolution);

						// Calculate the maximum safe width so we don't eat the next harmonic
						// Harmonics are spaced apart by exactly `sweepFreq`
						int maxSafeNotch = (int)((sweepFreq / binResolution) * 0.45f);

						// Apply the limits! (Minimum 4 bins for the Blackman-Harris window)
						dynamicNotch = std::max(7, dynamicNotch);// 4 for blackman-harris, 7 for flattop
						dynamicNotch = std::min(dynamicNotch, maxSafeNotch);


						float currentHarmonicPower = 0.0f;
						for (int b = centerBin - dynamicNotch; b <= centerBin + dynamicNotch; b++) {
							if (b > 0 && b < numBins) {
								currentHarmonicPower += magnitudes[b];
								magnitudes[b] = 0.0f;
							}
						}

						if (h == 1) signalPower = currentHarmonicPower;
					}

					// Calculate Noise and THD
					float noisePower = 0.0f;
					for (int k = 1; k < numBins; k++) noisePower += magnitudes[k];

					float currentThd = -210.0f;
					if (signalPower > 1e-5f && noisePower > 1e-20f) {
						currentThd = 10.0f * std::log10(noisePower / signalPower);
					}

					// Save the score
					thdCurve[currentStep] = currentThd;

					// Catch the Benchmarks (Check the current step's frequency)
					for (int i = 0; i < 3; i++) {
						float target = targetFrequencies[i];

						// If we haven't recorded this benchmark yet, and we just crossed or hit it
						if (benchmarkScores[i] <= -200.0f && sweepFreq >= target) {
							benchmarkScores[i] = currentThd;
						}
					}

					// Advance to the next pixel
					currentStep++;
					if (currentStep >= STEPS) {
						currentState = FINISHED;
					} else {
						currentState = WAIT_ZERO_CROSS; // Prepare for the next pitch
					}
				}
			}
		}

		outputs[TEST_OUTPUT].setVoltage(out);
	}
};

struct AliasDisplay : TransparentWidget {
	Alias* module;

	float panelHeight = 110.0f;
	float panelWidth = 130.0f;

	AliasDisplay() : module(nullptr) {
		box.size = Vec(panelWidth, panelHeight);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !module) return;

		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, panelWidth, panelHeight);
		nvgFillColor(args.vg, nvgRGBA(0x00, 0x10, 0x00, 0xFF));
		nvgFill(args.vg);

		std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		
		if (font) {
			nvgFontSize(args.vg, 12);
			nvgFontFaceId(args.vg, font->handle);
			nvgFillColor(args.vg, nvgRGBA(0, 255, 0, 255));

			// Status
			std::string statusText = "STATUS: ";
			if (module->currentState == Alias::NOT_READY) statusText += "NOT READY";
			else if (module->currentState == Alias::READY) statusText += "READY";
			else if (module->currentState == Alias::FINISHED) statusText += "FINISHED";
			else statusText += "WORKING...";
			nvgText(args.vg, 0, 10, statusText.c_str(), nullptr);

			// Benchmarks
			if (module->currentState != Alias::READY && module->currentState != Alias::NOT_READY) {
				for (int i = 0; i < 3; i++) {
					std::string label = module->benchmarkLabels[i];

					if (module->benchmarkScores[i] <= -200.0f) {
						nvgText(args.vg, 0, 25 + (i * 12), string::f("%s    --- dB", label.c_str()).c_str(), nullptr);
					} else {
						nvgText(args.vg, 0, 25 + (i * 12), string::f("%s %-6.1f dB", label.c_str(), module->benchmarkScores[i]).c_str(), nullptr);
					}
				}
			}

			// mode
			nvgFillColor(args.vg, nvgRGBA(0, 255, 0, 255));
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP );
			std::string modeText = (module->mode > 0.5f) ? "VCO" : "FX";
			nvgText(args.vg, panelWidth, 55, modeText.c_str(), nullptr);
		}

		// Line Graph
		if (module->currentState != Alias::READY && module->currentState != Alias::NOT_READY) {

			float graphX = 0.0f;
			float graphY = 65.0f;
			float graphWidth = panelWidth;
			float graphHeight = 45.0f;

			// Draw graph background bounding box
			nvgBeginPath(args.vg);
			nvgRect(args.vg, graphX, graphY, graphWidth, graphHeight);
			nvgFillColor(args.vg, nvgRGBA(0x00, 0x22, 0x00, 0xFF));
			nvgFill(args.vg);

			// Draw vertical grid lines
			nvgBeginPath(args.vg);
			// 100 Hz = ~23.3% | 1 kHz = ~56.6% | 10 kHz = ~90.0%
			float x100 = graphX + 0.233f * graphWidth;
			float x1k  = graphX + 0.566f * graphWidth;
			float x10k = graphX + 0.900f * graphWidth;

			nvgMoveTo(args.vg, x100, graphY); nvgLineTo(args.vg, x100, graphY + graphHeight);
			nvgMoveTo(args.vg, x1k, graphY);  nvgLineTo(args.vg, x1k, graphY + graphHeight);
			nvgMoveTo(args.vg, x10k, graphY); nvgLineTo(args.vg, x10k, graphY + graphHeight);

			nvgStrokeColor(args.vg, nvgRGBA(0x00, 0x55, 0x00, 0xFF)); // Faint dark green
			nvgStrokeWidth(args.vg, 0.5f);
			nvgStroke(args.vg);

			// Draw the THD curve
			nvgSave(args.vg);
			nvgScissor(args.vg, graphX, graphY, graphWidth, graphHeight);
			nvgBeginPath(args.vg);
			for (int i = 0; i < module->STEPS; i++) {
				float x = graphX + (i / float(module->STEPS-1)) * graphWidth;
				
				// Map -120dB (bottom) to 0dB (top)
				float normalizedY = (module->thdCurve[i] + 120.0f) / 120.0f; 
				normalizedY = clamp(normalizedY, 0.0f, 1.0f);
				
				float y = graphY + graphHeight - (normalizedY * graphHeight); 

				if (i == 0) nvgMoveTo(args.vg, x, y);
				else nvgLineTo(args.vg, x, y);
			}

			nvgStrokeColor(args.vg, nvgRGBA(0x44, 0xFF, 0x44, 0xFF)); 
			nvgStrokeWidth(args.vg, 0.8f);
			nvgStroke(args.vg);
			nvgRestore(args.vg);
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
		addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(centerX, 250.0f), module, Alias::SETTLE_KNOB));
		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(centerX*0.5f, 200.0f), module, Alias::START_BUTTON));
		addParam(createParamCentered<RoundToggleButtonSmallAutinn>(Vec(centerX*1.5f, 200.0f), module, Alias::VCO_MODE_SWITCH));
		
		addOutput(createOutputCentered<OutPortAutinn>(Vec(centerX - 25.0f, 300.0f), module, Alias::TEST_OUTPUT));
		addInput(createInputCentered<InPortAutinn>(Vec(centerX + 25.0f, 300.0f), module, Alias::RETURN_INPUT));
	}
};

Model* modelAlias = createModel<Alias, AliasWidget>("Alias");