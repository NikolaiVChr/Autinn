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

static constexpr int OVERSAMPLE = 4;

struct Bunker : Module {
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
	PredictiveBLEP blepA[16];
	PredictiveBLEP blepB[16];
	float lastOutA[16] = {}; // 1-sample memory for TZFM
	DCBlocker hp1A[16], hp2A[16];
	DCBlocker hp1B[16], hp2B[16];
	DCBlocker dcBlockerA[16], dcBlockerB[16];
	float driftPhaseA1 = 0.0f;
	float driftPhaseA2 = 0.0f;
	float driftPhaseB1 = 0.0f;
	float driftPhaseB2 = 0.0f;
	float lastSampleTime = 1.0f/44100.0f;
	float last_age = 0.0f;
	float blinkTime = 0.0f;
	dsp::SchmittTrigger schmittButton;
	dsp::SchmittTrigger syncTrigger[16];
	bool hardSyncEnabled = false;
	std::vector<dsp::Decimator<OVERSAMPLE, 16>> decimatorA;
	std::vector<dsp::Decimator<OVERSAMPLE, 16>> decimatorB;

	Bunker() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<ShapeParamQuantity>(SHAPE_PARAM + 0, 0.0f, 3.0f, 2.0f, "Osc Master Shape");
		configParam<ShapeParamQuantity>(SHAPE_PARAM + 1, 0.0f, 3.0f, 1.0f, "Osc Slave Shape");
		configParam(PITCH_PARAM + 0, -4.0f, 6.0f, 0.0f, "Master Frequency", " Hz", 2.0f, dsp::FREQ_C4);
		configParam(PITCH_PARAM + 1, -4.0f, 6.0f, 0.0f, "Slave Frequency", " Hz", 2.0f, dsp::FREQ_C4);
		configParam<Param3Digits>(AGE_PARAM, 0.0f, 40.0f, 15.0f, "Age", " Years");
		configParam(GAIN_PARAM + 0, 0.0f, 1.0f, 1.0f, "Master gain", " dB", -10.0f, 20.f, .0f);
		configParam(GAIN_PARAM + 1, 0.0f, 1.0f, 1.0f, "Slave gain", " dB", -10.0f, 20.f, .0f);
		configParam(CROSS_MODULATION_PARAM, -1.0f, 1.0f, 0.0f, "Cross modulation");
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

		configLight(HARD_SYNC_LIGHT, "Flashing");

		decimatorA.resize(16);
		decimatorB.resize(16);

		for (int c = 0; c < 16; c++) {
			hp1A[c].cutoff_hz = 30.0f + 0.0f * 9.0f;
			hp2A[c].cutoff_hz = 0.5f + 0.0f * 4.0f;
			hp1B[c].cutoff_hz = hp1A[c].cutoff_hz;
			hp2B[c].cutoff_hz = hp2A[c].cutoff_hz;

			hp1A[c].setSampleTime(lastSampleTime/float(OVERSAMPLE));
			hp2A[c].setSampleTime(lastSampleTime/float(OVERSAMPLE));
			hp1B[c].setSampleTime(lastSampleTime/float(OVERSAMPLE));
			hp2B[c].setSampleTime(lastSampleTime/float(OVERSAMPLE));

			dcBlockerA[c].cutoff_hz = 2.0f;
			dcBlockerB[c].cutoff_hz = 2.0f;
			dcBlockerA[c].setSampleTime(lastSampleTime);
			dcBlockerB[c].setSampleTime(lastSampleTime);
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
			phaseA[c] = 0.0f;
			phaseB[c] = 0.0f;
			syncTrigger[c].reset();
		}
		blinkTime = 0.0f;
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

	void process(const ProcessArgs &args) override {

		// age
        const float ageKnob = params[AGE_PARAM].getValue();
        const float cv_age = inputs[CV_AGE_INPUT].getVoltage() * 10.0f;
        const float age = clamp(ageKnob + cv_age, 0.0f, 60.0f);

		float driftA = 0.0f;
		float driftB = 0.0f;

		if (age > 0.01f) {
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

			driftA = (sin_fast_high(driftPhaseA1) + sin_fast_high(driftPhaseA2)) * 0.0005f * age;
			driftB = (sin_fast_high(driftPhaseB1) + sin_fast_high(driftPhaseB2)) * 0.0005f * age;
		}

        const float shapeA_knob = params[SHAPE_PARAM + 0].getValue();
        const float shapeB_knob = params[SHAPE_PARAM + 1].getValue();
        const float pitchA_knob = params[PITCH_PARAM + 0].getValue() + driftA;
        const float pitchB_knob = params[PITCH_PARAM + 1].getValue() + driftB;
        const float gainA_knob = params[GAIN_PARAM + 0].getValue();
        const float gainB_knob = params[GAIN_PARAM + 1].getValue();
        const float fmDepth_knob = params[CROSS_MODULATION_PARAM].getValue();

		if (schmittButton.process(params[HARD_SYNC_TOGGLE_PARAM].getValue() + inputs[CV_HARD_SYNC_TOGGLE_INPUT].getVoltage())) {
			hardSyncEnabled = !hardSyncEnabled;
		}

        // Master input dictates the number of channels
        int channels = std::max(1, inputs[CV_PITCH_INPUT + 0].getChannels());
        outputs[BUZZ_OUTPUT].setChannels(channels);
        outputs[RING_MODULATION_OUTPUT].setChannels(channels);
        outputs[SOLO_OUTPUT + 0].setChannels(channels);
        outputs[SOLO_OUTPUT + 1].setChannels(channels);

		const float osSampleTime = args.sampleTime / (float)OVERSAMPLE;

		bool updateFilters = (std::abs(age - last_age) > 0.001f) || (args.sampleTime != lastSampleTime);

		if (updateFilters) {
			for (int c = 0; c < channels; c++) {
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
			}
		}

		MorphWeights wA, wB;
		wA.calculate(shapeA_knob);
		wB.calculate(shapeB_knob);

		const float makeupGain = 1.0f + (age * 0.1f);

        for (int c = 0; c < channels; c++) {

        	// gain
            float gA = gainA_knob;
            if (inputs[CV_GAIN_INPUT + 0].isConnected()) {
                gA *= clamp(inputs[CV_GAIN_INPUT + 0].getPolyVoltage(c) * 0.1f, 0.0f, 1.0f);
            }
            float gB = gainB_knob;
            if (inputs[CV_GAIN_INPUT + 1].isConnected()) {
                gB *= clamp(inputs[CV_GAIN_INPUT + 1].getPolyVoltage(c) * 0.1f, 0.0f, 1.0f);
            }

        	// pitch
            const float cvA = inputs[CV_PITCH_INPUT + 0].getPolyVoltage(c);
            const float freqA = dsp::FREQ_C4 * std::exp2f(pitchA_knob + cvA);
            const float cvB = inputs[CV_PITCH_INPUT + 1].isConnected() ?
                        inputs[CV_PITCH_INPUT + 1].getPolyVoltage(c) : cvA;
            const float baseFreqB = dsp::FREQ_C4 * std::exp2f(pitchB_knob + cvB);

        	// cross modulation (FM)
            float fmAmount = fmDepth_knob;
            if (inputs[CV_CROSS_MODULATION_INPUT].isConnected()) {
                fmAmount *= clamp(inputs[CV_CROSS_MODULATION_INPUT].getPolyVoltage(c) * 0.2f, -1.0f, 1.0f);
            }

        	// ext. sync
        	bool extSync = syncTrigger[c].process(inputs[CV_SYNC_INPUT].getPolyVoltage(c));

            float outBufA[OVERSAMPLE];
            float outBufB[OVERSAMPLE];

        	const float dtA = freqA * osSampleTime;

            for (int i = 0; i < OVERSAMPLE; i++) {

                const float oldPhaseA = phaseA[c];
            	if (extSync && i == 0) {
            		const float naiveBefore = calculateNaiveMorph(wA, phaseA[c]);
            		const float naiveAfter = calculateNaiveMorph(wA, 0.0f);
            		float jumpMag = naiveAfter - naiveBefore;

            		if (dtA < 0.0f) jumpMag = -jumpMag;// TZFM

            		blepA[c].jump(0.0f, jumpMag);// insert discontinuity from ext. sync
            		phaseA[c] = 0.0f;
            	}
                float outA = generateMorphingWaveform(wA, phaseA[c], dtA, blepA[c]);
                lastOutA[c] = outA; // Store for TZFM

                // TZFM
                const float currentFreqB = baseFreqB + (baseFreqB * (lastOutA[c] * fmAmount));
                const float dtB = currentFreqB * osSampleTime;

                float outB = 0.0f;

                // hard sync logic
                bool masterWrapped = (dtA > 0.0f && oldPhaseA + dtA >= 1.0f) ||
                                     (dtA < 0.0f && oldPhaseA + dtA < 0.0f);

                if (hardSyncEnabled && masterWrapped) {
                	// Master just finished a period and slave should be synced
                    const float overshoot = (dtA > 0.0f) ? (oldPhaseA + dtA - 1.0f) : (oldPhaseA + dtA);
                    const float fraction = overshoot / dtA; // Always +

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

            	if (age > 0.01f) {
            		outA = hp2A[c].process(hp1A[c].process(outA));
            		outB = hp2B[c].process(hp1B[c].process(outB));

            		outA = tanh_fast_high(outA * makeupGain);
            		outB = tanh_fast_high(outB * makeupGain);
            	}

                outBufA[i] = outA;
                outBufB[i] = outB;
            }

        	float finalOutA = decimatorA[c].process(outBufA) * gA;
        	float finalOutB = decimatorB[c].process(outBufB) * gB;

        	finalOutA = dcBlockerA[c].process(finalOutA);
        	finalOutB = dcBlockerB[c].process(finalOutB);

        	//TODO: Only calculate outB when OUT, RM OUT or SLAVE OUT is connected? ..waste of branching, since 99.9% of the time it will be in use.

            outputs[SOLO_OUTPUT + 0].setVoltage(finalOutA * 5.0f, c);
            outputs[SOLO_OUTPUT + 1].setVoltage(finalOutB * 5.0f, c);
            outputs[RING_MODULATION_OUTPUT].setVoltage((finalOutA * finalOutB) * 5.0f, c);
            outputs[BUZZ_OUTPUT].setVoltage((finalOutA + finalOutB) * 2.5f, c);
        }

		lights[HARD_SYNC_LIGHT].setBrightness(hardSyncEnabled ? 1.0f : 0.0f);

		lastSampleTime = args.sampleTime;
		last_age = age;
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
	    if (w.square > 0.0f) naive += w.square * (phase < 0.5f ? -1.0f : 1.0f);
	    return naive;
	}

	static inline float generateMorphingWaveform(const MorphWeights& w, float& phase, float dt, PredictiveBLEP& blep) {
	    const float dir = (dt >= 0.0f) ? 1.0f : -1.0f;
	    const float absDt = std::abs(dt);

	    const float jump0 = (w.saw * -2.0f + w.square * -2.0f) * dir;
	    const float jump5 = (w.square * 2.0f) * dir;
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

};

struct BunkerWidget : ModuleWidget {
    BunkerWidget(Bunker *module) {
        setModule(module);

    	// 20 HP
        setPanel(createPanel(asset::plugin(pluginInstance, "res/BunkerModule.svg")));

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
        const float xCenter = 10.f * HP; // Bunker column
        const float xMidR   = 13.f * HP; // Inner right
        const float xRight  = 16.f * HP; // Slave column

        constexpr float yRow1 = 70.0f;  // Shapes & FM
        constexpr float yRow2 = 130.0f; // Pitch & age
        constexpr float yRow3 = 190.0f; // Gains & sync
        constexpr float yRow4 = 240.0f; // Pitch/FM CV
        constexpr float yRow5 = 280.0f; // Gain/age CV
        constexpr float yRow6 = 330.0f; // Audio outputs

        // Row 1: Shapes & cross-modulation
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(xLeft, yRow1), module, Bunker::SHAPE_PARAM + 0));
    	auto modKnob = createParamCentered<AutinnArcMidKnob>(Vec(xCenter, yRow1), module, Bunker::CROSS_MODULATION_PARAM);
    	modKnob->setModulation(Bunker::CV_CROSS_MODULATION_INPUT, [](float cv, float val, float att) {
			return clamp(val * (cv * 0.2f), -1.0f, 1.0f);
		});
    	addParam(modKnob);
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(xRight, yRow1), module, Bunker::SHAPE_PARAM + 1));

        // Row 2: Pitches & age
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(xLeft, yRow2), module, Bunker::PITCH_PARAM + 0));
        auto ageKnob = createParamCentered<AutinnArcMidKnob>(Vec(xCenter, yRow2), module, Bunker::AGE_PARAM);
        ageKnob->setModulation(Bunker::CV_AGE_INPUT, [](float cv, float val, float att) {
            return clamp(val + (cv * 10.0f), 0.0f, 60.0f);
        });
        addParam(ageKnob);
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(xRight, yRow2), module, Bunker::PITCH_PARAM + 1));

        // Row 3: Gains, sync CV, & sync Button
    	auto gain1Knob = createParamCentered<AutinnArcSmallKnob>(Vec(xLeft, yRow3), module, Bunker::GAIN_PARAM + 0);
    	gain1Knob->setModulation(Bunker::CV_GAIN_INPUT+0, [](float cv, float val, float att) {
			return clamp(val * (cv * 0.1f), 0.0f, 1.0f);
		});
    	addParam(gain1Knob);
        addInput(createInputCentered<InPortAutinn>(Vec(xMidL, yRow3), module, Bunker::CV_SYNC_INPUT));
        addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xCenter, yRow3), module, Bunker::HARD_SYNC_TOGGLE_PARAM));
        addInput(createInputCentered<InPortAutinn>(Vec(xMidR, yRow3), module, Bunker::CV_HARD_SYNC_TOGGLE_INPUT));

    	auto gain2Knob = createParamCentered<AutinnArcSmallKnob>(Vec(xRight, yRow3), module, Bunker::GAIN_PARAM + 1);
    	gain2Knob->setModulation(Bunker::CV_GAIN_INPUT+1, [](float cv, float val, float att) {
			return clamp(val * (cv * 0.1f), 0.0f, 1.0f);
		});
    	addParam(gain2Knob);

        addChild(createLightCentered<MediumLight<YellowLight>>(Vec((xCenter+xMidR)*0.5f, yRow3), module, Bunker::HARD_SYNC_LIGHT));

        // Row 4: Pitch CV & cross-mod CV
        addInput(createInputCentered<InPortAutinn>(Vec(xLeft, yRow4), module, Bunker::CV_PITCH_INPUT + 0));
        addInput(createInputCentered<InPortAutinn>(Vec(xCenter, yRow4), module, Bunker::CV_CROSS_MODULATION_INPUT));
        addInput(createInputCentered<InPortAutinn>(Vec(xRight, yRow4), module, Bunker::CV_PITCH_INPUT + 1));

        // Row 5: Gain CV & age CV
        addInput(createInputCentered<InPortAutinn>(Vec(xLeft, yRow5), module, Bunker::CV_GAIN_INPUT + 0));
        addInput(createInputCentered<InPortAutinn>(Vec(xCenter, yRow5), module, Bunker::CV_AGE_INPUT));
        addInput(createInputCentered<InPortAutinn>(Vec(xRight, yRow5), module, Bunker::CV_GAIN_INPUT + 1));

        // Row 6: Outputs
        addOutput(createOutputCentered<OutPortAutinn>(Vec(xLeft, yRow6), module, Bunker::SOLO_OUTPUT + 0));
        addOutput(createOutputCentered<OutPortAutinn>(Vec(xMidL, yRow6), module, Bunker::RING_MODULATION_OUTPUT));
        addOutput(createOutputCentered<OutPortAutinn>(Vec(xMidR, yRow6), module, Bunker::BUZZ_OUTPUT));
        addOutput(createOutputCentered<OutPortAutinn>(Vec(xRight, yRow6), module, Bunker::SOLO_OUTPUT + 1));
    }
};

Model *modelBunker = createModel<Bunker, BunkerWidget>("Bunker");