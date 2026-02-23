#include "Autinn.hpp"
#include "Autinn-dsp.hpp"
#include <cmath>

constexpr int notchWidth = 7; // Because freq is stable, we only need a tight 4-bin notch

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

	// Graph Data
	float thdCurve[256]; 
	float score100Hz = -120.0f;
	float score1kHz = -120.0f;
	float score10kHz = -120.0f;

	static constexpr int FFT_SIZE = 4096;
	dsp::RealFFT fft;
	float windowArray[FFT_SIZE];
	float audioBuffer[FFT_SIZE];
	float fftOutput[FFT_SIZE];
	int bufferIndex = 0;

	Alias() : fft(FFT_SIZE) { // Initialize the FFT size in the constructor initialization list
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(START_BUTTON, "Start Sweep");
		configInput(RETURN_INPUT, "Audio Return");
		configOutput(TEST_OUTPUT, "Sine Test Output");

		for(int i = 0; i < 256; i++) thdCurve[i] = -120.0f;

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
	}

	void onReset(const ResetEvent& e) override {
		currentState = READY;
		sweepFreq = 20.0f;
		sweepPhase = 0.0f;
		score100Hz = score1kHz = score10kHz = -120.0f;
		for(int i = 0; i < 256; i++) thdCurve[i] = -120.0f;
		Module::onReset(e);
	}

	// calculate the frequency for a specific pixel on the graph
	static float getFreqForStep(int step) {
		float logMin = std::log10(20.0f);
		float logMax = std::log10(20000.0f);
		float stepLog = logMin + (step / 255.0f) * (logMax - logMin);
		return std::pow(10.0f, stepLog);
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
			score100Hz = score1kHz = score10kHz = -120.0f;
			for(int i = 0; i < 256; i++) thdCurve[i] = -120.0f;
		}

		if (startTrigger.process(params[START_BUTTON].getValue())) {
			if (currentState == READY || currentState == FINISHED) {
				currentState = WAIT_ZERO_CROSS;
				currentStep = 0;
				sweepFreq = getFreqForStep(0);
				sweepPhase = 0.0f;
				score100Hz = score1kHz = score10kHz = -120.0f;
				for(int i = 0; i < 256; i++) thdCurve[i] = -120.0f;
			}
		}

		float out = 0.0f;

		if (currentState != READY && currentState != FINISHED) {
			// Generate pure sine (+/- 5V)
			out = std::sin(sweepPhase * 2.0f * float(M_PI)) * 5.0f;

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
				// Wait for 45 milliseconds
				settleCounter++;
				if ((settleCounter * args.sampleTime) >= 0.045f) {
					currentState = RECORD;
					bufferIndex = 0;
				}
			} else if (currentState == RECORD) {
				// Record the stable signal
				audioBuffer[bufferIndex] = inputs[RETURN_INPUT].getVoltage() * 0.2f;
				bufferIndex++;

				// When buffer is full, do the math!
				if (bufferIndex >= FFT_SIZE) {

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

						float currentHarmonicPower = 0.0f;
						for (int b = centerBin - notchWidth; b <= centerBin + notchWidth; b++) {
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

					float currentThd = -120.0f;
					if (signalPower > 1e-9f && noisePower > 1e-9f) {
						currentThd = 10.0f * std::log10(noisePower / signalPower);
					}

					// Save the score
					thdCurve[currentStep] = currentThd;

					// Catch the Benchmarks (Check the current step's frequency)
					if (sweepFreq >= 100.0f && score100Hz <= -120.0f) score100Hz = currentThd;
					if (sweepFreq >= 997.0f && score1kHz <= -120.0f) score1kHz = currentThd;
					if (sweepFreq >= 10000.0f && score10kHz <= -120.0f) score10kHz = currentThd;

					// Advance to the next pixel
					currentStep++;
					if (currentStep >= 256) {
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

	AliasDisplay() : module(nullptr) {
		box.size = Vec(130, 110);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !module) return;

		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, 130, 110);
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
			nvgStrokeWidth(args.vg, 0.8f);
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