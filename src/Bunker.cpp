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
	DCBlocker dcBlockerA[16];
	DCBlocker dcBlockerB[16];
	float driftTime = 0.0f;
	float lastSampleTime = 1.0f/44100.0f;
	float blinkTime = 0.0f;
	dsp::SchmittTrigger schmittButton;
	bool hardSyncEnabled = false;
	std::vector<dsp::Decimator<OVERSAMPLE, 8>> decimatorA;
	std::vector<dsp::Decimator<OVERSAMPLE, 8>> decimatorB;

	Bunker() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<ShapeParamQuantity>(SHAPE_PARAM + 0, 0.0f, 3.0f, 2.0f, "Osc Master Shape");
		configParam<ShapeParamQuantity>(SHAPE_PARAM + 1, 0.0f, 3.0f, 2.0f, "Osc Slave Shape");
		configParam(PITCH_PARAM + 0, -4.0f, 6.0f, 0.0f, "Master Frequency", " Hz", 2.0f, dsp::FREQ_C4);
		configParam(PITCH_PARAM + 1, -4.0f, 6.0f, 0.0f, "Slave Frequency", " Hz", 2.0f, dsp::FREQ_C4);
		configParam<Param3Digits>(AGE_PARAM, 0.0f, 40.0f, 15.0f, "Age", " Years");
		configParam(GAIN_PARAM + 0, 0.0f, 1.0f, 0.0f, "Master gain", " dB", 10.0f, 20.f, .0f);
		configParam(GAIN_PARAM + 1, 0.0f, 1.0f, 0.0f, "Slave gain", " dB", 10.0f, 20.f, .0f);
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
			// Correct DC drift without killing the sub-bass.
			dcBlockerA[c].cutoff_hz = 2.0f;
			dcBlockerA[c].setSampleTime(lastSampleTime);

			dcBlockerB[c].cutoff_hz = 2.0f;
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
		}
		blinkTime = 0.0f;
		schmittButton.reset();
		hardSyncEnabled = false;
		Module::onReset(e);
	}

	void onRandomize(const RandomizeEvent& e) override {
		Module::onRandomize(e);
	}

	void process(const ProcessArgs &args) override {
        driftTime += args.sampleTime;
        if (driftTime > 10000.0f) driftTime -= 10000.0f; // Prevent float overflow

        // Analog pitch drift (Age knob)
        float ageKnob = params[AGE_PARAM].getValue();
        float cv_age = inputs[CV_AGE_INPUT].getVoltage() * 10.0f;
        float age = clamp(ageKnob + cv_age, 0.0f, 60.0f);

        float driftA = 0.0f;
        float driftB = 0.0f;

        if (age > 0.01f) {
            // Osc A wanders using two slow prime frequencies
            driftA = (std::sin(driftTime * 0.43f) + std::sin(driftTime * 0.71f)) * 0.0005f * age;
            // Osc B wanders using two different frequencies so they drift apart
            driftB = (std::sin(driftTime * 0.59f) + std::sin(driftTime * 0.83f)) * 0.0005f * age;
        }

        float shapeA_knob = params[SHAPE_PARAM + 0].getValue();
        float shapeB_knob = params[SHAPE_PARAM + 1].getValue();
        float pitchA_knob = params[PITCH_PARAM + 0].getValue() + driftA;
        float pitchB_knob = params[PITCH_PARAM + 1].getValue() + driftB;
        float gainA_knob = params[GAIN_PARAM + 0].getValue();
        float gainB_knob = params[GAIN_PARAM + 1].getValue();
        float fmDepth_knob = params[CROSS_MODULATION_PARAM].getValue();

		if (schmittButton.process(params[HARD_SYNC_TOGGLE_PARAM].getValue() + inputs[CV_HARD_SYNC_TOGGLE_INPUT].getVoltage())) {
			hardSyncEnabled = !hardSyncEnabled;
		}

        // Master input dictates the number of channels
        int channels = std::max(1, inputs[CV_PITCH_INPUT + 0].getChannels());
        outputs[BUZZ_OUTPUT].setChannels(channels);
        outputs[RING_MODULATION_OUTPUT].setChannels(channels);
        outputs[SOLO_OUTPUT + 0].setChannels(channels);
        outputs[SOLO_OUTPUT + 1].setChannels(channels);

        for (int c = 0; c < channels; c++) {

            float gA = gainA_knob;
            if (inputs[CV_GAIN_INPUT + 0].isConnected()) {
                gA *= clamp(inputs[CV_GAIN_INPUT + 0].getPolyVoltage(c) / 10.0f, 0.0f, 1.0f);
            }

            float gB = gainB_knob;
            if (inputs[CV_GAIN_INPUT + 1].isConnected()) {
                gB *= clamp(inputs[CV_GAIN_INPUT + 1].getPolyVoltage(c) / 10.0f, 0.0f, 1.0f);
            }


            float cvA = inputs[CV_PITCH_INPUT + 0].getPolyVoltage(c);
            float freqA = dsp::FREQ_C4 * std::pow(2.0f, pitchA_knob + cvA);

            float cvB = inputs[CV_PITCH_INPUT + 1].isConnected() ?
                        inputs[CV_PITCH_INPUT + 1].getPolyVoltage(c) : cvA;
            float baseFreqB = dsp::FREQ_C4 * std::pow(2.0f, pitchB_knob + cvB);

            float fmAmount = fmDepth_knob;
            if (inputs[CV_CROSS_MODULATION_INPUT].isConnected()) {
                fmAmount *= clamp(inputs[CV_CROSS_MODULATION_INPUT].getPolyVoltage(c) / 5.0f, -1.0f, 1.0f);
            }


            float outBufA[OVERSAMPLE];
            float outBufB[OVERSAMPLE];

            for (int i = 0; i < OVERSAMPLE; i++) {
                float osSampleTime = args.sampleTime / (float)OVERSAMPLE;
                float dtA = freqA * osSampleTime;

                float oldPhaseA = phaseA[c];
                float outA = generateMorphingWaveform(shapeA_knob, phaseA[c], dtA, blepA[c]);
                lastOutA[c] = outA; // Store for TZFM

                // TZFM
                float currentFreqB = baseFreqB + (baseFreqB * (lastOutA[c] * fmAmount));
                float dtB = currentFreqB * osSampleTime;

                float outB = 0.0f;

                // hard sync logic
                bool masterWrapped = (dtA > 0.0f && oldPhaseA + dtA >= 1.0f) ||
                                     (dtA < 0.0f && oldPhaseA + dtA < 0.0f);

                if (hardSyncEnabled && masterWrapped) {
                    float overshoot = (dtA > 0.0f) ? (oldPhaseA + dtA - 1.0f) : (oldPhaseA + dtA);
                    float fraction = overshoot / dtA; // Always +

                    float phaseAtSync = phaseB[c] + dtB * (1.0f - fraction);
                    phaseAtSync -= std::floor(phaseAtSync);
                    if (phaseAtSync < 0.0f) phaseAtSync += 1.0f;

                    float naiveBefore = calculateNaiveMorph(shapeB_knob, phaseAtSync);
                    float naiveAfter = calculateNaiveMorph(shapeB_knob, 0.0f);
                    float jumpMag = naiveAfter - naiveBefore;

                    if (dtA < 0.0f) jumpMag = -jumpMag;

                    blepB[c].jump(fraction, jumpMag);
                    phaseB[c] = dtB * fraction;
                    phaseB[c] -= std::floor(phaseB[c]);
                    if (phaseB[c] < 0.0f) phaseB[c] += 1.0f;

                    float naiveB = calculateNaiveMorph(shapeB_knob, phaseB[c]);
                    outB = blepB[c].process(naiveB);
                } else {
                    outB = generateMorphingWaveform(shapeB_knob, phaseB[c], dtB, blepB[c]);
                }

                if (age > 0.01f) {
                    hp1A[c].cutoff_hz = 30.0f + age * 9.0f;
                    hp2A[c].cutoff_hz = 0.5f + age * 4.0f;
                    hp1B[c].cutoff_hz = 30.0f + age * 9.0f;
                    hp2B[c].cutoff_hz = 0.5f + age * 4.0f;

                    hp1A[c].setSampleTime(osSampleTime);
                    hp2A[c].setSampleTime(osSampleTime);
                    hp1B[c].setSampleTime(osSampleTime);
                    hp2B[c].setSampleTime(osSampleTime);

                    float makeupGain = 1.0f + (age * 0.1f);

                    outA = hp2A[c].process(hp1A[c].process(outA));
                    outA = tanh_fast_high(outA * makeupGain);

                    outB = hp2B[c].process(hp1B[c].process(outB));
                    outB = tanh_fast_high(outB * makeupGain);
                }

                outBufA[i] = outA;
                outBufB[i] = outB;
            }

        	float finalOutA = decimatorA[c].process(outBufA) * gA;
        	float finalOutB = decimatorB[c].process(outBufB) * gB;

        	finalOutA = dcBlockerA[c].process(finalOutA);
        	finalOutB = dcBlockerB[c].process(finalOutB);

            outputs[SOLO_OUTPUT + 0].setVoltage(finalOutA * 5.0f, c);
            outputs[SOLO_OUTPUT + 1].setVoltage(finalOutB * 5.0f, c);
            outputs[RING_MODULATION_OUTPUT].setVoltage((finalOutA * finalOutB) * 5.0f, c);
            outputs[BUZZ_OUTPUT].setVoltage((finalOutA + finalOutB) * 2.5f, c);
        }

		lights[HARD_SYNC_LIGHT].setBrightness(hardSyncEnabled ? 1.0f : 0.0f);
    }

	static float calculateNaiveMorph(float shape, float phase) {
		float wSine = 0.0f, wTri = 0.0f, wSaw = 0.0f, wSquare = 0.0f;
		if (shape < 1.0f) {
			wTri = shape;
			wSine = 1.0f - wTri;
		} else if (shape < 2.0f) {
			wSaw = shape - 1.0f;
			wTri = 1.0f - wSaw;
		} else {
			wSquare = shape - 2.0f;
			wSaw = 1.0f - wSquare;
		}

		float naive = 0.0f;
		if (wSine > 0.0f) naive += wSine * std::sin(phase * 2.0f * float(M_PI));
		if (wTri > 0.0f) naive += wTri * ((phase < 0.5f) ? (-1.0f + 4.0f * phase) : (3.0f - 4.0f * phase));
		if (wSaw > 0.0f) naive += wSaw * (2.0f * phase - 1.0f);
		if (wSquare > 0.0f) naive += wSquare * ((phase < 0.5f) ? -1.0f : 1.0f);

		return naive;
	}

	static float generateMorphingWaveform(float shape, float& phase, float dt, PredictiveBLEP& blep) {
	    // Weights
	    float wSine = 0.0f, wTri = 0.0f, wSaw = 0.0f, wSquare = 0.0f;
	    if (shape < 1.0f) {
		    wTri = shape; wSine = 1.0f - wTri;
	    } else if (shape < 2.0f) {
		    wSaw = shape - 1.0f; wTri = 1.0f - wSaw;
	    } else {
		    wSquare = shape - 2.0f; wSaw = 1.0f - wSquare;
	    }

	    // Polarities based on direction (TZFM)
	    float dir = (dt >= 0.0f) ? 1.0f : -1.0f;
	    float absDt = std::abs(dt);

	    // Base BLEP magnitudes multiplied by direction
	    float jump0 = (wSaw * -2.0f + wSquare * -2.0f) * dir;
	    float jump5 = (wSquare * 2.0f) * dir;
	    float corner0 = (wTri * 8.0f) * dir;
	    float corner5 = (wTri * -8.0f) * dir;

	    float nextPhase = phase + dt;

	    // Forward/backward phase crossings
	    if (dt >= 0.0f) {
	        // Forward
	        if (nextPhase >= 1.0f) {
	            float overshoot = nextPhase - 1.0f;
	            float fraction = overshoot / dt;
	            if (jump0 != 0.0f) blep.jump(fraction, jump0);
	            if (corner0 != 0.0f) blep.corner(fraction, absDt, corner0);
	            phase = overshoot;
	        } else if (phase < 0.5f && nextPhase >= 0.5f) {
	            float overshoot = nextPhase - 0.5f;
	            float fraction = overshoot / dt;
	            if (jump5 != 0.0f) blep.jump(fraction, jump5);
	            if (corner5 != 0.0f) blep.corner(fraction, absDt, corner5);
	            phase = nextPhase;
	        } else {
		        phase = nextPhase;
	        }
	    } else {
	        // Reverse (TZFM)
	        if (nextPhase < 0.0f) {
	            float overshoot = nextPhase; //  -0.1
	            float fraction = overshoot / dt; // is +
	            if (jump0 != 0.0f) blep.jump(fraction, jump0);
	            if (corner0 != 0.0f) blep.corner(fraction, absDt, corner0);
	            phase = 1.0f + overshoot; // Wrap backwards
	        } else if (phase >= 0.5f && nextPhase < 0.5f) {
	            float overshoot = nextPhase - 0.5f;
	            float fraction = overshoot / dt;
	            if (jump5 != 0.0f) blep.jump(fraction, jump5);
	            if (corner5 != 0.0f) blep.corner(fraction, absDt, corner5);
	            phase = nextPhase;
	        } else {
		        phase = nextPhase;
	        }
	    }

	    float naive = calculateNaiveMorph(shape, phase);
	    return blep.process(naive);
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

        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        const float xLeft   = 60.0f;  // Master column
        const float xMidL   = 105.0f; // Inner left
        const float xCenter = 150.0f; // Bunker column
        const float xMidR   = 195.0f; // Inner right
        const float xRight  = 240.0f; // Slave column

        const float yRow1 = 60.0f;  // Shapes & FM
        const float yRow2 = 130.0f; // Pitch & age
        const float yRow3 = 190.0f; // Gains & sync
        const float yRow4 = 240.0f; // Pitch/FM CV
        const float yRow5 = 280.0f; // Gain/age CV
        const float yRow6 = 330.0f; // Audio outputs

        // Row 1: Shapes & cross-modulation
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(xLeft, yRow1), module, Bunker::SHAPE_PARAM + 0));
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(xCenter, yRow1), module, Bunker::CROSS_MODULATION_PARAM));
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
        addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(xLeft, yRow3), module, Bunker::GAIN_PARAM + 0));

        // Packed tightly around the center sync button
        addInput(createInputCentered<InPortAutinn>(Vec(xMidL, yRow3), module, Bunker::CV_SYNC_INPUT));
        addParam(createParamCentered<RoundButtonSmallAutinn>(Vec(xCenter, yRow3), module, Bunker::HARD_SYNC_TOGGLE_PARAM));
        addInput(createInputCentered<InPortAutinn>(Vec(xMidR, yRow3), module, Bunker::CV_HARD_SYNC_TOGGLE_INPUT));

        addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(xRight, yRow3), module, Bunker::GAIN_PARAM + 1));

        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(xCenter, yRow3 + 20.0f), module, Bunker::HARD_SYNC_LIGHT));

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