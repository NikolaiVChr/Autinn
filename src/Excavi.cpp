#include "Autinn.hpp"
#include "Autinn-dsp.hpp"
#include <cmath>

/*

	Autinn VCV Rack Plugin
	Copyright (C) 2026  Nikolai V. Chr.

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

struct ShapeParamQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		const float val = getValue();

		if (val <= 0.02f) return "Sine";
		if (val >= 0.98f && val <= 1.02f) return "Triangle";
		if (val >= 1.98f && val <= 2.02f) return "Sawtooth";
		if (val >= 2.98f) return "Square";

		if (val < 1.0f) {
			int pct = (int)std::round(val * 100.0f);
			return string::f("Sine/Tri (%d%%)", pct);
		}
		if (val < 2.0f) {
			int pct = (int)std::round((val - 1.0f) * 100.0f);
			return string::f("Tri/Saw (%d%%)", pct);
		}
		int pct = (int)std::round((val - 2.0f) * 100.0f);
		return string::f("Saw/Sq (%d%%)", pct);
	}
};

struct Excavi : Module {
	enum ParamIds {
		ENUMS(PITCH_PARAM,2),
		AGE_PARAM,
		ENUMS(GAIN_PARAM,2),
		ENUMS(SHAPE_PARAM,2),
		HARD_SYNC_TOGGLE_PARAM,
		CROSS_MODULATION_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(CV_PITCH_INPUT,2),
		ENUMS(CV_GAIN_INPUT,2),
		CV_SYNC_INPUT,
		CV_AGE_INPUT,
		CV_HARD_SYNC_TOGGLE_INPUT,
		CV_CROSS_MODULATION_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		BUZZ_OUTPUT,
		RING_MODULATION_OUTPUT,
		ENUMS(SOLO_OUTPUT,2),
		NUM_OUTPUTS
	};
	enum LightIds {
		HARD_SYNC_LIGHT,
		NUM_LIGHTS
	};

	float phaseA[16] = {};
	float phaseB[16] = {};
	ReactiveBLEP blepA[16];
	ReactiveBLEP blepB[16];
	float lastOutA[16] = {}; // 1-sample memory for TZFM
	DCBlocker hp1A[16], hp2A[16];
	DCBlocker hp1B[16], hp2B[16];
	DCBlocker dcBlockerA[16], dcBlockerB[16];
	float roofLpfA[16] = {};
	float roofLpfB[16] = {};
	float driftPhaseA1 = 0.0f;
	float driftPhaseA2 = 0.0f;
	float driftPhaseB1 = 0.0f;
	float driftPhaseB2 = 0.0f;
	float lastSampleRate[16];
	float lastAge[16];

	std::vector<dsp::Decimator<4, 16>> decimatorA4;
	std::vector<dsp::Decimator<4, 16>> decimatorB4;
	std::vector<dsp::Decimator<2, 16>> decimatorA2;
	std::vector<dsp::Decimator<2, 16>> decimatorB2;

	static int getOversampleAmount(const float sampleRate) {
		if (sampleRate < 50000.0f) return 4;
		if (sampleRate < 100000.0f) return 2;
		return 1;
	}

	dsp::SchmittTrigger schmittButton;
	dsp::SchmittTrigger syncTrigger[16];
	bool hardSyncEnabled = false;

	Excavi() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<ShapeParamQuantity>(SHAPE_PARAM + 0, 0.0f, 3.0f, 2.0f, "Osc Master Shape");
		configParam<ShapeParamQuantity>(SHAPE_PARAM + 1, 0.0f, 3.0f, 1.0f, "Osc Slave Shape");
		configParam(PITCH_PARAM + 0, -4.0f, 6.0f, 0.0f, "Master Frequency", " Hz", 2.0f, dsp::FREQ_C4);
		configParam(PITCH_PARAM + 1, -4.0f, 6.0f, 0.0f, "Slave Frequency", " Hz", 2.0f, dsp::FREQ_C4);
		configParam<Param3Digits>(AGE_PARAM, 0.0f, 40.0f, 15.0f, "Age", " Years");
		configParam<Param3Digits>(GAIN_PARAM + 0, 0.0f, 1.0f, 1.0f, "Master gain", " dB", -10.0f, 20.f, .0f);
		configParam<Param3Digits>(GAIN_PARAM + 1, 0.0f, 1.0f, 1.0f, "Slave gain", " dB", -10.0f, 20.f, .0f);
		configParam<Param3Digits>(CROSS_MODULATION_PARAM, -1.0f, 1.0f, 0.0f, "Cross modulation");
		configButton(HARD_SYNC_TOGGLE_PARAM, "Toggle hard sync");

		configInput(CV_PITCH_INPUT+0, "Master 1V/Oct CV");
		configInput(CV_PITCH_INPUT+1, "Slave 1V/Oct CV");
		configInput(CV_GAIN_INPUT+0, "CV master gain");
		configInput(CV_GAIN_INPUT+1, "CV slave gain");
		configInput(CV_SYNC_INPUT, "CV sync");
		configInput(CV_HARD_SYNC_TOGGLE_INPUT, "CV hard sync toggle");
		configInput(CV_AGE_INPUT, "1V/decade age CV");
		configInput(CV_CROSS_MODULATION_INPUT, "Cross modulation CV");

		configOutput(BUZZ_OUTPUT, "Main audio");
		configOutput(RING_MODULATION_OUTPUT, "Ring modulation audio");
		configOutput(SOLO_OUTPUT+0, "Master audio");
		configOutput(SOLO_OUTPUT+1, "Slave audio");

		configLight(HARD_SYNC_LIGHT, "Hard sync");

		decimatorA4.resize(16); decimatorB4.resize(16);
		decimatorA2.resize(16); decimatorB2.resize(16);

		for (int c = 0; c < 16; c++) {
			hp1A[c].cutoff_hz = 30.0f + 0.0f * 9.0f;
			hp2A[c].cutoff_hz = 0.5f + 0.0f * 4.0f;
			hp1B[c].cutoff_hz = hp1A[c].cutoff_hz;
			hp2B[c].cutoff_hz = hp2A[c].cutoff_hz;

			dcBlockerA[c].cutoff_hz = 2.0f;
			dcBlockerB[c].cutoff_hz = 2.0f;

			lastAge[c] = -1.0f;
			lastSampleRate[c] = 0.0f;
		}
	}

	void onReset(const ResetEvent& e) override {
		for (int c = 0; c < 16; c++) {
			hp1A[c].reset();
			hp2A[c].reset();
			hp1B[c].reset();
			hp2B[c].reset();
			dcBlockerA[c].reset();
			dcBlockerB[c].reset();
			roofLpfA[c] = 0.0f;
			roofLpfB[c] = 0.0f;
			phaseA[c] = 0.0f;
			phaseB[c] = 0.0f;
			syncTrigger[c].reset();
		}
		schmittButton.reset();
		hardSyncEnabled = false;
		Module::onReset(e);
	}

	void onRandomize(const RandomizeEvent& e) override {
		Module::onRandomize(e);
	}

	json_t *dataToJson() override {
		json_t *root = json_object();
		json_object_set_new(root, "hardSyncEnabled", json_boolean(hardSyncEnabled));
		return root;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *hs = json_object_get(rootJ, "hardSyncEnabled");
		if (hs)
			hardSyncEnabled = json_boolean_value(hs);
	}

	struct MorphWeights {
		// 0:sin
		// 1:tri
		// 2:saw
		// 4:sqr

		float sine = 0.0f, tri = 0.0f, saw = 0.0f, square = 0.0f;

		void calculate(float shape) {
			sine = tri = saw = square = 0.0f;
			if (shape < 1.0f) {
				tri = shape; sine = 1.0f - tri;
			} else if (shape < 2.0f) {
				saw = shape - 1.0f; tri = 1.0f - saw;
			} else {
				square = shape - 2.0f; saw = 1.0f - square;
			}
		}
	};

	static inline float calculateNaiveMorph(const MorphWeights& w, const float phase) {
	    float naive = 0.0f;
	    if (w.sine > 0.0f) naive += w.sine * sin_fast_high(phase * 2.0f * float(M_PI));
	    if (w.tri > 0.0f) naive += w.tri * (phase < 0.5f ? -1.0f + 4.0f * phase : 3.0f - 4.0f * phase);
	    if (w.saw > 0.0f) naive += w.saw * (2.0f * phase - 1.0f);
	    if (w.square > 0.0f) naive += w.square * (phase < 0.5f ? -1.0f : 1.0f) * 0.7f;
		// note that 0.7 is makeup-gain since square sounds much louder at same max amplitude as the other waveforms.
	    return naive;
	}

	static inline float generateMorphingWaveform(const MorphWeights& w, float& phase, float dt, ReactiveBLEP& blep) {
	    const float dir = (dt >= 0.0f) ? 1.0f : -1.0f;
	    const float absDt = std::abs(dt);

		const float jump0 = (w.saw * -2.0f + w.square * -2.0f * 0.7f) * dir;
		const float jump5 = (w.square * 2.0f * 0.7f) * dir;
	    const float corner0 = (w.tri * 8.0f) * dir;
	    const float corner5 = (w.tri * -8.0f) * dir;

	    const float nextPhase = phase + dt;

	    if (dt >= 0.0f) {
	        if (nextPhase >= 1.0f) {
	            const float fraction = (nextPhase - 1.0f) / dt;
	            if (jump0 != 0.0f) blep.jump(fraction, jump0);// saw or sqr drop
	            if (corner0 != 0.0f) blep.corner(fraction, absDt, corner0);//triangle bottom
	            phase = nextPhase - 1.0f;
	        } else if (phase < 0.5f && nextPhase >= 0.5f) {
	            const float fraction = (nextPhase - 0.5f) / dt;
	            if (jump5 != 0.0f) blep.jump(fraction, jump5);// sqr rise
	            if (corner5 != 0.0f) blep.corner(fraction, absDt, corner5);//triangle top
	            phase = nextPhase;
	        } else { phase = nextPhase; }
	    } else {
	    	// TZFM (jump and corner's polarities are already flipped with the dir variable)
	        if (nextPhase < 0.0f) {
	            const float fraction = nextPhase / dt;
	            if (jump0 != 0.0f) blep.jump(fraction, jump0);// saw or sqr drop (inv)
	            if (corner0 != 0.0f) blep.corner(fraction, absDt, corner0);//triangle bottom (inv)
	            phase = 1.0f + nextPhase;
	        } else if (phase >= 0.5f && nextPhase < 0.5f) {
	            const float fraction = (nextPhase - 0.5f) / dt;
	            if (jump5 != 0.0f) blep.jump(fraction, jump5);// sqr rise (inv)
	            if (corner5 != 0.0f) blep.corner(fraction, absDt, corner5);//triangle top (inv)
	            phase = nextPhase;
	        } else {
		        phase = nextPhase;
	        }
	    }

	    return blep.process(calculateNaiveMorph(w, phase));
	}

	inline void processSubSample(int c, float dtA, float baseFreqB, float fmAmount, float osSampleTime, bool doExtSync,
								const MorphWeights& wA, const MorphWeights& wB, float makeupGain, float& outA, float& outB) {
		const float oldPhaseA = phaseA[c];

		if (doExtSync) {
			const float naiveBefore = calculateNaiveMorph(wA, phaseA[c]);
			const float naiveAfter = calculateNaiveMorph(wA, 0.0f);
			float jumpMag = naiveAfter - naiveBefore;
			if (dtA < 0.0f) jumpMag = -jumpMag;// TZFM
			blepA[c].jump(0.0f, jumpMag);// insert discontinuity from ext. sync
			phaseA[c] = 0.0f;
		}

		outA = generateMorphingWaveform(wA, phaseA[c], dtA, blepA[c]);
		lastOutA[c] = outA; // Store for TZFM

		// TZFM
		const float currentFreqB = baseFreqB + (baseFreqB * (lastOutA[c] * fmAmount));
		const float dtB = currentFreqB * osSampleTime;

		// hard sync logic
		const bool masterWrapped = (dtA > 0.0f && oldPhaseA + dtA >= 1.0f) ||
							 (dtA < 0.0f && oldPhaseA + dtA < 0.0f);

		if (hardSyncEnabled && masterWrapped) {
			// Master just finished a period and slave should be synced
			const float overshoot = (dtA > 0.0f) ? (oldPhaseA + dtA - 1.0f) : (oldPhaseA + dtA);
			const float fraction = overshoot / dtA;

			float phaseAtSync = phaseB[c] + dtB * (1.0f - fraction);
			phaseAtSync -= std::floor(phaseAtSync);
			if (phaseAtSync < 0.0f) phaseAtSync += 1.0f;

			const float naiveBefore = calculateNaiveMorph(wB, phaseAtSync);
			const float naiveAfter = calculateNaiveMorph(wB, 0.0f);
			float jumpMag = naiveAfter - naiveBefore;

			if (dtA < 0.0f) jumpMag = -jumpMag;

			blepB[c].jump(fraction, jumpMag);
			phaseB[c] = dtB * fraction;
			phaseB[c] -= std::floor(phaseB[c]);
			if (phaseB[c] < 0.0f) phaseB[c] += 1.0f;

			outB = blepB[c].process(calculateNaiveMorph(wB, phaseB[c]));

		} else {
			outB = generateMorphingWaveform(wB, phaseB[c], dtB, blepB[c]);
		}

		outA = hp2A[c].process(hp1A[c].process(outA));
		outB = hp2B[c].process(hp1B[c].process(outB));

		// roof filters (1-Pole LP to kill IMD before saturation)
		// 20.3 to 22.1khz cutoff, depending on rack samplerate
		roofLpfA[c] += 0.5f * (outA - roofLpfA[c]);
		roofLpfB[c] += 0.5f * (outB - roofLpfB[c]);

		outA = tanh_fast_high(roofLpfA[c] * makeupGain);
		outB = tanh_fast_high(roofLpfB[c] * makeupGain);
	}

	void process(const ProcessArgs &args) override {

		const int oversample = getOversampleAmount(args.sampleRate);
		const float osSampleTime = args.sampleTime / (float)oversample;

		if (schmittButton.process(params[HARD_SYNC_TOGGLE_PARAM].getValue() + inputs[CV_HARD_SYNC_TOGGLE_INPUT].getVoltage())) {
			hardSyncEnabled = !hardSyncEnabled;
		}

		if (!outputs[BUZZ_OUTPUT].isConnected() &&
			!outputs[RING_MODULATION_OUTPUT].isConnected() &&
			!outputs[SOLO_OUTPUT + 0].isConnected() &&
			!outputs[SOLO_OUTPUT + 1].isConnected()) {
			lights[HARD_SYNC_LIGHT].setBrightness(hardSyncEnabled ? 1.0f : 0.0f);
			return;
		}

		// each vco have their own capacitor and power fluctuations, so we do them independently.
		// 2 sines per vco make it sound random instead of vibrato.
		// We use primes numbers divided by 100, to avoid repeating pattern the brain can pick up on.

		// Wrap at 2*PI to prevent overflow safely
		driftPhaseA1 += 0.43f * args.sampleTime;
		if (driftPhaseA1 > 6.2831853f) driftPhaseA1 -= 6.2831853f;

		driftPhaseA2 += 0.71f * args.sampleTime;
		if (driftPhaseA2 > 6.2831853f) driftPhaseA2 -= 6.2831853f;

		driftPhaseB1 += 0.59f * args.sampleTime;
		if (driftPhaseB1 > 6.2831853f) driftPhaseB1 -= 6.2831853f;

		driftPhaseB2 += 0.83f * args.sampleTime;
		if (driftPhaseB2 > 6.2831853f) driftPhaseB2 -= 6.2831853f;

		float rawDriftA = (sin_fast_high(driftPhaseA1) + sin_fast_high(driftPhaseA2)) * 0.0005f;
		float rawDriftB = (sin_fast_high(driftPhaseB1) + sin_fast_high(driftPhaseB2)) * 0.0005f;

        const float shapeA_knob = params[SHAPE_PARAM + 0].getValue();
        const float shapeB_knob = params[SHAPE_PARAM + 1].getValue();
        const float pitchA_knob = params[PITCH_PARAM + 0].getValue();
        const float pitchB_knob = params[PITCH_PARAM + 1].getValue();
        const float gainA_knob = params[GAIN_PARAM + 0].getValue();
        const float gainB_knob = params[GAIN_PARAM + 1].getValue();
        const float fmDepth_knob = params[CROSS_MODULATION_PARAM].getValue();

		// Master input dictates the number of channels
        int channels = std::max(1, inputs[CV_PITCH_INPUT + 0].getChannels());
        outputs[BUZZ_OUTPUT].setChannels(channels);
        outputs[RING_MODULATION_OUTPUT].setChannels(channels);
        outputs[SOLO_OUTPUT + 0].setChannels(channels);
        outputs[SOLO_OUTPUT + 1].setChannels(channels);

		MorphWeights wA, wB;
		wA.calculate(shapeA_knob);
		wB.calculate(shapeB_knob);



        for (int c = 0; c < channels; c++) {

        	// age
        	float cv_age = inputs[CV_AGE_INPUT].getChannels() > c? inputs[CV_AGE_INPUT].getPolyVoltage(c):inputs[CV_AGE_INPUT].getVoltage();
        	float age = clamp(params[AGE_PARAM].getValue() + cv_age * 10.f, 0.0f, 60.0f);
        	const float makeupGain = 1.0f + (age * 0.1f);

        	float driftA = rawDriftA * age;
        	float driftB = rawDriftB * age;

        	if (std::abs(age - lastAge[c]) > 0.001f || args.sampleRate != lastSampleRate[c]) {
        		hp1A[c].cutoff_hz = 30.0f + age * 9.0f;
        		hp2A[c].cutoff_hz = 0.5f + age * 4.0f;
        		hp1B[c].cutoff_hz = hp1A[c].cutoff_hz;
        		hp2B[c].cutoff_hz = hp2A[c].cutoff_hz;

        		hp1A[c].setSampleTime(osSampleTime);
        		hp2A[c].setSampleTime(osSampleTime);
        		hp1B[c].setSampleTime(osSampleTime);
        		hp2B[c].setSampleTime(osSampleTime);

        		dcBlockerA[c].setSampleTime(args.sampleTime);
        		dcBlockerB[c].setSampleTime(args.sampleTime);

        		lastAge[c] = age;
        		lastSampleRate[c] = args.sampleRate;
        	}

        	// gain
            float gA = gainA_knob;
            if (inputs[CV_GAIN_INPUT + 0].isConnected()) {
                gA += inputs[CV_GAIN_INPUT + 0].getPolyVoltage(c) * 0.1f;
            }
        	gA = clamp(gA, 0.0f, 1.0f);
            float gB = gainB_knob;
            if (inputs[CV_GAIN_INPUT + 1].isConnected()) {
                gB += inputs[CV_GAIN_INPUT + 1].getPolyVoltage(c) * 0.1f;
            }
        	gB = clamp(gB, 0.0f, 1.0f);

        	// pitch
            const float cvA = inputs[CV_PITCH_INPUT + 0].getPolyVoltage(c);
            const float freqA = dsp::FREQ_C4 * std::exp2f(pitchA_knob + cvA + driftA);
            const float cvB = inputs[CV_PITCH_INPUT + 1].isConnected() ?
                        inputs[CV_PITCH_INPUT + 1].getPolyVoltage(c) : cvA;
            const float baseFreqB = dsp::FREQ_C4 * std::exp2f(pitchB_knob + cvB + driftB);

        	// cross modulation (FM)
            float fmAmount = fmDepth_knob;
            if (inputs[CV_CROSS_MODULATION_INPUT].isConnected()) {
            	fmAmount += inputs[CV_CROSS_MODULATION_INPUT].getPolyVoltage(c) * 0.2f;
            	fmAmount = clamp(fmAmount, -1.0f, 1.0f);
            }

        	// ext. sync
        	bool extSync = syncTrigger[c].process(inputs[CV_SYNC_INPUT].getPolyVoltage(c));

        	const float dtA = freqA * osSampleTime;

            float finalOutA = 0.0f;
        	float finalOutB = 0.0f;

        	if (oversample == 4) {
        		float outBufA[4], outBufB[4];
        		for (int i = 0; i < 4; i++) {
        			processSubSample(c, dtA, baseFreqB, fmAmount, osSampleTime, (extSync && i == 0), wA, wB, makeupGain, outBufA[i], outBufB[i]);
        		}
        		finalOutA = decimatorA4[c].process(outBufA) * gA;
        		finalOutB = decimatorB4[c].process(outBufB) * gB;
        	} else if (oversample == 2) {
        		float outBufA[2], outBufB[2];
        		for (int i = 0; i < 2; i++) {
        			processSubSample(c, dtA, baseFreqB, fmAmount, osSampleTime, (extSync && i == 0), wA, wB, makeupGain, outBufA[i], outBufB[i]);
        		}
        		finalOutA = decimatorA2[c].process(outBufA) * gA;
        		finalOutB = decimatorB2[c].process(outBufB) * gB;
        	} else {
        		// 1x (Bypass oversampling entirely at very high sample rates)
        		processSubSample(c, dtA, baseFreqB, fmAmount, osSampleTime, extSync, wA, wB, makeupGain, finalOutA, finalOutB);
        		finalOutA *= gA;
        		finalOutB *= gB;
        	}

        	finalOutA = dcBlockerA[c].process(finalOutA);
        	finalOutB = dcBlockerB[c].process(finalOutB);

        	//TODO: Only calculate outB when OUT, RM OUT or SLAVE OUT is connected? ..waste of branching, since 99.9% of the time it will be in use.

            outputs[SOLO_OUTPUT + 0].setVoltage(finalOutA * 5.0f, c);
            outputs[SOLO_OUTPUT + 1].setVoltage(finalOutB * 5.0f, c);
            outputs[RING_MODULATION_OUTPUT].setVoltage((finalOutA * finalOutB) * 5.0f, c);
            outputs[BUZZ_OUTPUT].setVoltage((finalOutA + finalOutB) * 2.5f, c);
        }

		lights[HARD_SYNC_LIGHT].setBrightness(hardSyncEnabled ? 1.0f : 0.0f);
    }
};

struct ExcaviWidget : ModuleWidget {
    ExcaviWidget(Excavi *module) {
        setModule(module);

    	// 20 HP
        setPanel(createPanel(asset::plugin(pluginInstance, "res/ExcaviModule.svg")));

        // VCV standard: 15px per HP. 20 HP = 300px wide.
        if (box.size.x == 0) {
            box.size = Vec(20 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
        }

    	const float HP = RACK_GRID_WIDTH;

        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        const float xLeft   = 4.f * HP;  // Master column
        const float xMidL   = 7.f * HP;  // Inner left
        const float xCenter = 10.f * HP; // Excavi column
        const float xMidR   = 13.f * HP; // Inner right
        const float xRight  = 16.f * HP; // Slave column

        const float yRow1 = 70.0f + 1.0f * HP;  // Shapes & FM
        const float yRow2 = 130.0f + 0.5f * HP; // Pitch & age
        const float yRow3 = 190.0f; // Gains & sync
        const float yRow4 = 240.0f; // Pitch/FM CV
        const float yRow5 = 280.0f; // Gain/age CV
        const float yRow6 = 330.0f; // Audio outputs

        // Row 1: Shapes & cross-modulation
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(xLeft, yRow1), module, Excavi::SHAPE_PARAM + 0));
    	auto modKnob = createParamCentered<AutinnArcMidKnob>(Vec(xCenter, yRow1), module, Excavi::CROSS_MODULATION_PARAM);
    	modKnob->setModulation(Excavi::CV_CROSS_MODULATION_INPUT, [](float cv, float val, float att) {
			return clamp(val + (cv * 0.2f), -1.0f, 1.0f);
		});
    	addParam(modKnob);
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(xRight, yRow1), module, Excavi::SHAPE_PARAM + 1));

        // Row 2: Pitches & age
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(xLeft, yRow2), module, Excavi::PITCH_PARAM + 0));
        auto ageKnob = createParamCentered<AutinnArcMidKnob>(Vec(xCenter, yRow2), module, Excavi::AGE_PARAM);
        ageKnob->setModulation(Excavi::CV_AGE_INPUT, [](float cv, float val, float att) {
            return clamp(val + (cv * 10.0f), 0.0f, 60.0f);
        });
        addParam(ageKnob);
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(xRight, yRow2), module, Excavi::PITCH_PARAM + 1));

        // Row 3: Gains, sync CV, & sync Button
    	auto gain1Knob = createParamCentered<AutinnArcSmallKnob>(Vec(xLeft, yRow3), module, Excavi::GAIN_PARAM + 0);
    	gain1Knob->setModulation(Excavi::CV_GAIN_INPUT+0, [](float cv, float val, float att) {
			return clamp(val + (cv * 0.1f), 0.0f, 1.0f);
		});
    	addParam(gain1Knob);
        addInput(createInputCentered<InPortAutinn>(Vec(xMidL, yRow3), module, Excavi::CV_SYNC_INPUT));
        addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xCenter, yRow3), module, Excavi::HARD_SYNC_TOGGLE_PARAM));
        addInput(createInputCentered<InPortAutinn>(Vec(xMidR, yRow3), module, Excavi::CV_HARD_SYNC_TOGGLE_INPUT));

    	auto gain2Knob = createParamCentered<AutinnArcSmallKnob>(Vec(xRight, yRow3), module, Excavi::GAIN_PARAM + 1);
    	gain2Knob->setModulation(Excavi::CV_GAIN_INPUT+1, [](float cv, float val, float att) {
			return clamp(val + (cv * 0.1f), 0.0f, 1.0f);
		});
    	addParam(gain2Knob);

        addChild(createLightCentered<MediumLight<YellowLight>>(Vec((xCenter+xMidR)*0.5f, yRow3), module, Excavi::HARD_SYNC_LIGHT));

        // Row 4: Pitch CV & cross-mod CV
        addInput(createInputCentered<InPortAutinn>(Vec(xLeft, yRow4), module, Excavi::CV_PITCH_INPUT + 0));
        addInput(createInputCentered<InPortAutinn>(Vec(xCenter, yRow4), module, Excavi::CV_CROSS_MODULATION_INPUT));
        addInput(createInputCentered<InPortAutinn>(Vec(xRight, yRow4), module, Excavi::CV_PITCH_INPUT + 1));

        // Row 5: Gain CV & age CV
        addInput(createInputCentered<InPortAutinn>(Vec(xLeft, yRow5), module, Excavi::CV_GAIN_INPUT + 0));
        addInput(createInputCentered<InPortAutinn>(Vec(xCenter, yRow5), module, Excavi::CV_AGE_INPUT));
        addInput(createInputCentered<InPortAutinn>(Vec(xRight, yRow5), module, Excavi::CV_GAIN_INPUT + 1));

        // Row 6: Outputs
        addOutput(createOutputCentered<OutPortAutinn>(Vec(xLeft, yRow6), module, Excavi::SOLO_OUTPUT + 0));
        addOutput(createOutputCentered<OutPortAutinn>(Vec(xMidL, yRow6), module, Excavi::RING_MODULATION_OUTPUT));
        addOutput(createOutputCentered<OutPortAutinn>(Vec(xMidR, yRow6), module, Excavi::BUZZ_OUTPUT));
        addOutput(createOutputCentered<OutPortAutinn>(Vec(xRight, yRow6), module, Excavi::SOLO_OUTPUT + 1));
    }
};

Model *modelExcavi = createModel<Excavi, ExcaviWidget>("Excavi");