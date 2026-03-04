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

	static constexpr int FFT_SIZE = 32768;
	static constexpr int STEPS = 256;
	static constexpr int numBins = FFT_SIZE / 2;
	static constexpr float START_HZ = 50.0f;
	static constexpr float END_HZ = 20000.0f;

	// Graph Data
	float ratioCurve[STEPS];
	float targetFrequencies[3] = {100.0f, 997.0f, 9973.0f};// 997 is a prime and does not share a common factor with 44.1k or 48k
	std::string benchmarkLabels[3] = {"100 Hz", " 1K Hz", "10K Hz"};
	float benchmarkScores[3] = {-210.0f, -210.0f, -210.0f};
	bool benchmarkRecorded[3] = {false, false, false};

	dsp::RealFFT fft;
	alignas(16) float windowArray[FFT_SIZE];
	alignas(16) float audioBuffer[FFT_SIZE];
	alignas(16) float fftOutput[FFT_SIZE];
	alignas(16) float power[numBins] = {};
	int bufferIndex = 0;

	const int MUTE_RADIUS_BINS = 5;// TODO: the 5th bin is -92dB of 0th bin. Have to mute it :(
	const int MINIMUM_FUNDAMENTAL_SEARCHRADIUS_BINS = 5;
	const int MINIMUM_HARMONICS_SEARCHRADIUS_BINS = 4;
	const float MAXIMUM_TOWARDS_NEXT_HARMONICS_SEARCH_FRACTION = 0.45f;
	const float HARMONICS_SEARCHRADIUS = 0.10f;
	const float FUNDAMENTAL_SEARCHRADIUS = 0.05f;

	// debug
	//volatile float debugValue = 0.0f;

	Alias() : fft(FFT_SIZE) { // Initialize the FFT size in the constructor initialization list
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SETTLE_KNOB, 0.01f, 2.5f, 0.05f, "Settle Time", " s");
		configButton(START_BUTTON, "Start Sweep");
		configButton(VCO_MODE_SWITCH, "Toggle Mode");
		configInput(RETURN_INPUT, "Audio Return");
		configOutput(TEST_OUTPUT, "Test Send");

		for(int i = 0; i < STEPS; i++) ratioCurve[i] = -210.0f;

		benchmarkRecorded[0] = benchmarkRecorded[1] = benchmarkRecorded[2] = false;

		// Blackman-Harris Window coefficients
		constexpr float a0 = 0.35875f;
		constexpr float a1 = 0.48829f;
		constexpr float a2 = 0.14128f;
		constexpr float a3 = 0.01168f;

		for (int i = 0; i < FFT_SIZE; i++) {
			const float phase = (float)i / (float)(FFT_SIZE - 1);
			windowArray[i] = a0
						   - a1 * std::cos(2.0f * (float)M_PI * phase)
						   + a2 * std::cos(4.0f * (float)M_PI * phase)
						   - a3 * std::cos(6.0f * (float)M_PI * phase);
		}

		/*
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
		*/
	}

	void onReset(const ResetEvent& e) override {
		currentState = READY;
		sweepFreq = getFreqForStep(0);
		sweepPhase = 0.0f;
		benchmarkScores[0] = benchmarkScores[1] = benchmarkScores[2] = -210.0f;
		benchmarkRecorded[0] = benchmarkRecorded[1] = benchmarkRecorded[2] = false;
		mode = false;
		for(int i = 0; i < STEPS; i++) ratioCurve[i] = -210.0f;
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

	/**
	 * Calculate the frequency for a specific pixel on the graph
	 *
	 */
	float getFreqForStep(const int step) const {
		const float sRate = (lastSampleRate > 0) ? lastSampleRate : 44100.0f;
		const float binRes = sRate / float(FFT_SIZE);

		// Align start exactly to the bin nearest to START_HZ Hz
		const float startFreq = std::round(START_HZ / binRes) * binRes;
		constexpr float endFreq = END_HZ;

		// Base ideal frequency from pure log sweep
		const float logP = step / float(STEPS - 1);
		float idealFreq = startFreq * std::pow(endFreq / startFreq, logP);

		// Pin to exact benchmark targets if this is the closest step
		for (int i = 0; i < 3; i++) {
			const float target = targetFrequencies[i];
			const float targetLogP = std::log(target / startFreq) / std::log(endFreq / startFreq);
			const int targetStep = int(std::round(targetLogP * (STEPS - 1)));

			if (step == targetStep) {
				idealFreq = target;
			}
		}

		// Snap to the nearest FFT bin (Synchronous Sampling)
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
			benchmarkRecorded[0] = benchmarkRecorded[1] = benchmarkRecorded[2] = false;
			for(int i = 0; i < STEPS; i++) ratioCurve[i] = -210.0f;
		}

		if (startTrigger.process(params[START_BUTTON].getValue())) {
			if (currentState == READY || currentState == FINISHED) {
				currentState = WAIT_ZERO_CROSS;
				currentStep = 0;
				sweepFreq = getFreqForStep(0);
				activeSettleTime = params[SETTLE_KNOB].getValue();
				mode = params[VCO_MODE_SWITCH].getValue() > 0.5f;
				sweepPhase = 0.0f;
				benchmarkScores[0] = benchmarkScores[1] = benchmarkScores[2] = -210.0f;
				benchmarkRecorded[0] = benchmarkRecorded[1] = benchmarkRecorded[2] = false;
				for(int i = 0; i < STEPS; i++) ratioCurve[i] = -210.0f;
			}
		}

		float out = 0.0f;

		if (currentState == READY || currentState == NOT_READY) {
			activeSettleTime = params[SETTLE_KNOB].getValue();
			mode = params[VCO_MODE_SWITCH].getValue() > 0.5f;
		} else if (currentState != READY && currentState != FINISHED) {

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
				crossedZero = true; // The wave wrapped around 0
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

				constexpr bool NAIVETEST = false;
				if (mode && NAIVETEST) {
					// make a naive saw for testing large amount of aliasing
					audioBuffer[bufferIndex] = (sweepPhase*2.0f)-1.0f;
				}

				bufferIndex++;

				// When buffer is full, do the fft
				if (bufferIndex >= FFT_SIZE) {
					// remove DC offset, this must be done before FFT

					// Calculate the DC of the raw samples
					float sum = 0.0f;
					for (int i = 0; i < FFT_SIZE; i++) sum += audioBuffer[i];
					const float dcOffset = sum / (float)FFT_SIZE;

					// Subtract DC and then apply window
					for (int i = 0; i < FFT_SIZE; i++) {
						audioBuffer[i] -= dcOffset;
						audioBuffer[i] *= windowArray[i];
					}
					fft.rfft(audioBuffer, fftOutput);

					power[0] = 0.0f;
					for (int k = 1; k < numBins; k++) {
						const float re = fftOutput[2 * k];
						const float im = fftOutput[2 * k + 1];
						power[k] = (re * re) + (im * im);
					}

					float signalPower = 0.0f;
					const float binResolution = args.sampleRate / FFT_SIZE;

					// Find the fundamental
					const int expectedFundBin = (int)std::round(sweepFreq / binResolution);
					const int searchWidth = std::max(MINIMUM_FUNDAMENTAL_SEARCHRADIUS_BINS, (int)(expectedFundBin * FUNDAMENTAL_SEARCHRADIUS)); // Search +/- 5% around expected pitch

					int actualFundBin = expectedFundBin;
					float maxMag = 0.0f;

					for (int bin = std::max(1, expectedFundBin - searchWidth); bin <= std::min(numBins - 1, expectedFundBin + searchWidth); bin++) {
						if (power[bin] > maxMag) {
							maxMag = power[bin];
							actualFundBin = bin;
						}
					}

					// This is the actual frequency the VCO is outputting
					float trueFundFreq = actualFundBin * binResolution;


					// Mute Fundamental and Harmonics
					for (int h = 1; (h * trueFundFreq) < (args.sampleRate / 2.0f); h++) {
						const float expectedHz = h * trueFundFreq;
						const int expectedBin = (int)std::round(expectedHz / binResolution);
						// Find the actual peak for this harmonic (h=1, 2, 3 etc.)
						// We look in a +-10% window to handle drifting VCOs
						int searchRadius = (int)std::round((expectedHz * HARMONICS_SEARCHRADIUS) / binResolution);
						// Don't look so far that we hit the next harmonic
						int maxSearch = (int)((trueFundFreq / binResolution) * MAXIMUM_TOWARDS_NEXT_HARMONICS_SEARCH_FRACTION);
						searchRadius = std::min(std::max(searchRadius, MINIMUM_HARMONICS_SEARCHRADIUS_BINS), maxSearch);// note: do not use clamp
						int peakBin = expectedBin;
						float maxMag2 = -1.0f;
						// Search the window for the loudest bin
						for (int i = expectedBin - searchRadius; i <= expectedBin + searchRadius; i++) {
							if (i > 0 && i < numBins) {
								if (power[i] > maxMag2) {
									maxMag2 = power[i];
									peakBin = i;
								}
							}
						}
						// Now scoop exactly 3 bins for the BH window power
						float currentHarmonicPower = 0.0f;

						for (int i = peakBin - MUTE_RADIUS_BINS; i <= peakBin + MUTE_RADIUS_BINS; i++) {
							if (i > 0 && i < numBins) {
								currentHarmonicPower += power[i];
								power[i] = 0.0f; // Mute this harmonic so only noise remains
							}
						}

						signalPower += currentHarmonicPower;

						// If this is the 1st harmonic, save the power and lock the frequency
						if (h == 1) {
							// This makes later harmonics (h=2, 3...) much more accurate
							trueFundFreq = peakBin * binResolution;
						}
					}

					// Calculate noise and alias
					float noisePower = 0.0f;
					for (int k = 1; k < numBins; k++) noisePower += power[k];

					float currentRatio = -210.0f;
					if (signalPower > 1e-5f && noisePower > 1e-20f) {
						// Signal to alias/noise ratio
						currentRatio = 10.0f * std::log10(noisePower / signalPower);
					}

					//INFO("Ratio: %.2f dB, Signal Power: %.2f, Noise Power: %.2f, Sweep Freq: %.1f Hz, Fund Freq: %.1f Hz",currentThd, signalPower, noisePower, sweepFreq, trueFundFreq);

					// Save the score
					ratioCurve[currentStep] = currentRatio;

					// Catch the Benchmarks (Check the current step's frequency)
					float startFreq = std::round(START_HZ / binResolution) * binResolution;

					for (int i = 0; i < 3; i++) {
						const float target = targetFrequencies[i];
						const float targetLogP = std::log(target / startFreq) / std::log(END_HZ / startFreq);
						const int targetStep = int(std::round(targetLogP * (STEPS - 1)));

						if (currentStep >= targetStep - 1 && currentStep <= targetStep + 1) {
							// If this is the first time entering the window, or if we found a worse dB
							if (!benchmarkRecorded[i] || currentRatio > benchmarkScores[i]) {
								benchmarkScores[i] = currentRatio;
							}
							if (currentStep == targetStep + 1) {
								benchmarkRecorded[i] = true;
							}
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
	float panelWidth = 128.0f;
	int frame = 0;

	const float DISPLAY_TOP_DB = 0.0f;
	const float DISPLAY_BOTTOM_DB = -144.0f;
	const float DISPLAY_STROKE = 0.5f;

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
			else {
				statusText += "WORKING";
				if (frame > 45) statusText += ".";
				if (frame > 30) statusText += ".";
				if (frame > 15) statusText += ".";
			}
			nvgText(args.vg, 0, 10, statusText.c_str(), nullptr);

			// Benchmarks
			if (module->currentState != Alias::READY && module->currentState != Alias::NOT_READY) {
				for (int i = 0; i < 3; i++) {
					std::string label = module->benchmarkLabels[i];

					if (!module->benchmarkRecorded[i]) {
						nvgText(args.vg, 0, 25 + (i * 12), string::f("%s    --- dB", label.c_str()).c_str(), nullptr);
					} else if (std::isinf(module->benchmarkScores[i])) {
						// Perfect score (Noise was 0.0, so log10 hit negative infinity)
						nvgText(args.vg, 0, 25 + (i * 12), string::f("%s   -inf dB", label.c_str()).c_str(), nullptr);
					} else {
						nvgText(args.vg, 0, 25 + (i * 12), string::f("%s %+6.1f dB", label.c_str(), module->benchmarkScores[i]).c_str(), nullptr);
					}
				}
			}
			//nvgText(args.vg, 0, 25 + (3 * 12), string::f("%+6.1f / %+6.1f", module->debugValue, module->sweepFreq).c_str(), nullptr);

			// mode
			nvgFillColor(args.vg, nvgRGBA(0, 255, 0, 255));
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP );
			std::string modeText = (module->mode > 0.5f) ? "VCO" : "FX";
			nvgText(args.vg, panelWidth, 55, modeText.c_str(), nullptr);

			// samplerate
			nvgFillColor(args.vg, nvgRGBA(0, 255, 0, 255));
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP );
			std::string rateText = string::f("%.1fK Hz  %.2gs", std::round(module->lastSampleRate * 0.01f) * 0.1f, module->activeSettleTime);
			nvgText(args.vg, 0, 55, rateText.c_str(), nullptr);
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
			const float sRate = (module->lastSampleRate > 0) ? module->lastSampleRate : 44100.0f;
			const float binRes = sRate / float(Alias::FFT_SIZE);
			const float startFreq = std::round(Alias::START_HZ / binRes) * binRes;

			nvgBeginPath(args.vg);
			for (int i = 0; i < 3; i++) {
				const float targetLogP = std::log(module->targetFrequencies[i] / startFreq) / std::log(Alias::END_HZ / startFreq);
				const float x = graphX + targetLogP * graphWidth;

				nvgMoveTo(args.vg, x, graphY);
				nvgLineTo(args.vg, x, graphY + graphHeight);
			}
			// draw horiz lines
			float sixty = (-60.f - DISPLAY_BOTTOM_DB) / (DISPLAY_TOP_DB - DISPLAY_BOTTOM_DB);
			sixty = graphY + graphHeight - (sixty * graphHeight);
			float hundredtwenty = (-120.f - DISPLAY_BOTTOM_DB) / (DISPLAY_TOP_DB - DISPLAY_BOTTOM_DB);
			hundredtwenty = graphY + graphHeight - (hundredtwenty * graphHeight);
			nvgMoveTo(args.vg, 0, sixty);
			nvgLineTo(args.vg, graphWidth, sixty);
			nvgMoveTo(args.vg, 0, hundredtwenty);
			nvgLineTo(args.vg, graphWidth, hundredtwenty);
			nvgStrokeColor(args.vg, nvgRGBA(0x00, 0x55, 0x00, 0xFF)); // Faint dark green
			nvgStrokeWidth(args.vg, DISPLAY_STROKE);
			nvgStroke(args.vg);

			// Draw the THD curve
			nvgSave(args.vg);
			nvgScissor(args.vg, graphX, graphY, graphWidth, graphHeight);
			nvgBeginPath(args.vg);
			for (int i = 0; i < Alias::STEPS; i++) {
				float x = graphX + (i / float(Alias::STEPS-1)) * graphWidth;
				
				// Map -144dB (bottom) to 0dB (top)
				float normalizedY = (module->ratioCurve[i] - DISPLAY_BOTTOM_DB) / (DISPLAY_TOP_DB - DISPLAY_BOTTOM_DB);
				normalizedY = clamp(normalizedY, -100.0f, 1.0f);// 1.0 is top
				
				float y = graphY + graphHeight - (normalizedY * graphHeight); 

				if (i == 0) nvgMoveTo(args.vg, x, y);
				else nvgLineTo(args.vg, x, y);
			}

			nvgStrokeColor(args.vg, nvgRGBA(0x44, 0xFF, 0x44, 0xFF)); 
			nvgStrokeWidth(args.vg, 0.8f);
			nvgStroke(args.vg);
			nvgRestore(args.vg);
		}
		frame++;
		if (frame >= 60) frame = 0;
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
		display->box.pos = Vec(11.0f, 50.0f);
		display->module = module;
		addChild(display);

		// Controls & Ports
		addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(centerX*0.5f, 250.0f), module, Alias::SETTLE_KNOB));
		addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(centerX, 200.0f), module, Alias::START_BUTTON));
		addParam(createParamCentered<RoundToggleButtonSmallAutinn>(Vec(centerX*1.5f, 250.0f), module, Alias::VCO_MODE_SWITCH));
		
		addOutput(createOutputCentered<OutPortAutinn>(Vec(centerX - 25.0f, 300.0f+HALF_PORT), module, Alias::TEST_OUTPUT));
		addInput(createInputCentered<InPortAutinn>(Vec(centerX + 25.0f, 300.0f+HALF_PORT), module, Alias::RETURN_INPUT));
	}
};

Model* modelAlias = createModel<Alias, AliasWidget>("Alias");