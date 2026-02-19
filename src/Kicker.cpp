#include "Autinn.hpp"
#include <cmath>
#include <algorithm>

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

struct Kicker : Module {
    enum ParamIds {
        FREQ_PARAM,
        VOL_DECAY_PARAM,
        SWEEP_PARAM, // Pitch Envelope Depth
        CLICK_PARAM, // Transient Level
        DRIVE_PARAM,
        PITCH_DECAY_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        TRIG_INPUT,
        VOCT_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        AUDIO_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        ACT_LIGHT,
        NUM_LIGHTS
    };

    static constexpr int MAX_CHANNELS = 16;

    // Polyphonic State
    float phase[MAX_CHANNELS] = {};
    float ampEnv[MAX_CHANNELS] = {};
    float pitchEnv[MAX_CHANNELS] = {};
    dsp::SchmittTrigger triggers[MAX_CHANNELS];
    float lightDecay = 0.0f;
    uint32_t noiseState[16] = {};
    float lastClickFilter[16] = {};
    bool activeState[MAX_CHANNELS] = {};

    int stepDivider = 33;
    float noiseGain = 1.0f;
    float baseFreq = 60.0f;
    float sweepDepthOct = 0.0f;
    float clickLevel = 0.0f;
    float drive = 1.0f;
    float clickAlpha = 0.0f;
    float vcaDecayCoeff = 0.0f;
    float pitchDecayCoeff = 0.0f;
    float envBlend = 0.0f;

    Kicker() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(FREQ_PARAM, 30.0f, 200.0f, dsp::FREQ_C4/4.0f, "Tune", " Hz");
        configParam<Param3Digits>(VOL_DECAY_PARAM, 0.1f, 0.8f, 0.2f, "Vol Decay", " s");
        configParam<Param4Digits>(SWEEP_PARAM, 0.0f, 1.4f, 0.35f, "Sweep", "%",0, 100);
        configParam<Param4Digits>(CLICK_PARAM, 0.0f, 1.00f, 0.5f, "Click", "%",0, 100);
        configParam<Param3Digits>(DRIVE_PARAM, 0.0f, 5.0f, 1.0f, "Drive", ""); // 0 to 5x gain
        configParam<Param3Digits>(PITCH_DECAY_PARAM, 0.005f, 0.1f, 0.035f, "Pitch Decay", " ms",0,1000);

        configInput(TRIG_INPUT, "Trigger");
        configInput(VOCT_INPUT, "V/Oct");
        configOutput(AUDIO_OUTPUT, "Audio");

        for (int i = 0; i < 16; i++) {
            noiseState[i] = 0x12345678 + (i * 0xdeadbeef);
        }
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        for (int c = 0; c < MAX_CHANNELS; c++) {
            phase[c] = 0.0f;
            ampEnv[c] = 0.0f;
            pitchEnv[c] = 0.0f;
            activeState[c] = false;
            outputs[AUDIO_OUTPUT].setVoltage(0.0f, c);
        }
    }

    void process(const ProcessArgs &args) override;
};

void Kicker::process(const ProcessArgs &args) {
    if (!outputs[AUDIO_OUTPUT].isConnected()) return;

    int channels = std::max(1, inputs[TRIG_INPUT].getChannels());
    outputs[AUDIO_OUTPUT].setChannels(channels);

    float dt = args.sampleTime;

    if (stepDivider++ >= 32) {
        stepDivider = 0;

        // As rate goes up, we boost the noise to maintain constant Power Density
        noiseGain = std::sqrt(args.sampleRate / 44100.0f);

        baseFreq = params[FREQ_PARAM].getValue();
        sweepDepthOct = params[SWEEP_PARAM].getValue() * 5.0f;//since we allow up to 140%, we allow up to 7 octaves
        clickLevel = params[CLICK_PARAM].getValue();
        drive = 1.0f + params[DRIVE_PARAM].getValue();
        float decayValVca = params[VOL_DECAY_PARAM].getValue();
        float decayValPitch = params[PITCH_DECAY_PARAM].getValue();

        float clickCutoffFreq = 1750.0f;// 1000=wood/knock, 2500=synth click.
        clickAlpha = 1.0f - std::exp(-2.0f * M_PI * clickCutoffFreq * dt);

        // vca envelope decay
        vcaDecayCoeff = std::exp(-1.0f / (decayValVca * args.sampleRate));

        // Pitch envelope creates the thump - needs to be faster than amp envelope
        pitchDecayCoeff = std::exp(-1.0f / (decayValPitch * args.sampleRate));

        // Adaptive envelope blend
        envBlend = clamp((decayValVca - 0.4f) * 5.0f, 0.0f, 1.0f);
    }


    bool lightActive = false;

    for (int c = 0; c < channels; c++) {
        // Trigger Logic
        float trigVoltage = inputs[TRIG_INPUT].getPolyVoltage(c);
        if (triggers[c].process(trigVoltage)) {
            ampEnv[c] = 1.0f;
            pitchEnv[c] = 1.0f;
            phase[c] = 0.25f; // Reset phase for consistent punch
            activeState[c] = true;
            lightActive = true;
        }

        if (!activeState[c]) {
            outputs[AUDIO_OUTPUT].setVoltage(0.0f, c);
            continue;
        }

        // Envelopes
        ampEnv[c] *= vcaDecayCoeff;
        pitchEnv[c] *= pitchDecayCoeff;

        // If both envelopes are effectively zero, go to sleep.
        if (ampEnv[c] <= 0.001f && pitchEnv[c] <= 0.001f) {
            // Also prevents denormals
            ampEnv[c] = 0.0f;
            pitchEnv[c] = 0.0f;
            activeState[c] = false;
            outputs[AUDIO_OUTPUT].setVoltage(0.0f, c);
            continue;
        }

        // Oscillator
        // Pitch = Base + V/Oct + SweepEnvelope
        float voct = inputs[VOCT_INPUT].getPolyVoltage(c);
        float mod = pitchEnv[c] * pitchEnv[c] * sweepDepthOct;
        float freq = baseFreq * std::exp2f(voct + mod);


        float deltaPhase = freq * dt;
        phase[c] += deltaPhase;
        if (phase[c] >= 1.0f) phase[c] -= 1.0f;

        // Sound Generation

        // We use a sine for body with the freq from knob and apply the pitch sweep onto it plus the vca envelope.
        // The vca envelope is linear at high decay knob settings, and squared at low settings.
        // We make a click from filtered noise.
        // Then we apply some drive gain to it all.

        float body = sin(phase[c] * 2.0f * M_PI);
        
        // Click (Short burst of noise)
        float white = (int32_t(noiseState[c] = noiseState[c] * 1664525 + 1013904223) >> 8) * (1.0f / 8388608.0f);
        white *= noiseGain;

        //Adaptive Envelope
        // If decay is short (< 0.5s), square the env to make it "Dry/Tight".
        // If decay is long, keep it linear to preserve the "Boom".
        float linearEnv = ampEnv[c];              // Boomy (Linear)
        float squaredEnv = ampEnv[c] * ampEnv[c]; // Dry/Tight (Squared)

        // mix factor based on knob position
        // When Knob < 0.4, factor is 0.0 (Pure Squared/Dry)
        // When Knob > 0.6, factor is 1.0 (Pure Linear/Boomy)
        // Between 0.4 and 0.6, it blends smoothly.
        // So no clicking when turn the knob.
        float finalVcaEnv = squaredEnv + (linearEnv - squaredEnv) * envBlend;

        // click
        float filteredClick = lastClickFilter[c] + clickAlpha * (white - lastClickFilter[c]);
        lastClickFilter[c] = filteredClick;
        float highPass = white - filteredClick;// reverse the filter, cause to lazy to change the code
        float pEnv2 = pitchEnv[c] * pitchEnv[c];
        float clickEnv = pEnv2 * pEnv2;
        float click = highPass * clickEnv * clickLevel;

        // Mix & Saturate
        float signal = (body * finalVcaEnv + click) * drive;
        float x = tanh_fast_low(signal);

        outputs[AUDIO_OUTPUT].setVoltage(x * 5.0f, c);
    }

    // light if any drum triggered
    if (lightActive) lightDecay = 1.0f;
    float lightLambda = 1.0f - (args.sampleTime / 0.2f);
    lightDecay *= std::max(0.0f, lightLambda);
    lights[ACT_LIGHT].value = lightDecay;
}

struct KickerWidget : ModuleWidget {
    KickerWidget(Kicker *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/KickerModule.svg"))); // You need to make this SVG

        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float down = 20;
        float up = 4 * RACK_GRID_WIDTH;
        float fullX = this->getBox().size.x;

        // Row 1 (Large knobs)
        addParam(createParam<RoundMediumAutinnKnob>(Vec(34 - HALF_KNOB_MED, 60 + down - RACK_GRID_WIDTH/2), module, Kicker::FREQ_PARAM));
        addParam(createParam<RoundMediumAutinnKnob>(Vec(101 - HALF_KNOB_MED, 60 + down - RACK_GRID_WIDTH/2), module, Kicker::VOL_DECAY_PARAM));

        // Row 2 (Small knobs)
        addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(fullX * 0.25f, 120 + down + HALF_KNOB_SMALL), module, Kicker::SWEEP_PARAM));
        addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(fullX * 0.75f, 120 + down + HALF_KNOB_SMALL), module, Kicker::CLICK_PARAM));

        // Row 3 (Small knobs)
        addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(fullX * 0.25f, 175 + down + HALF_KNOB_SMALL), module, Kicker::PITCH_DECAY_PARAM));
        addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(fullX * 0.75f, 175 + down + HALF_KNOB_SMALL), module, Kicker::DRIVE_PARAM));

        // Light
        addChild(createLight<SmallLight<GreenLight>>(Vec(fullX * 0.5f, 182 + down), module, Kicker::ACT_LIGHT));

        // Ports (Evenly distributed: x = 23, 67.5, 112)
        addInput(createInput<InPortAutinn>(Vec(23 - HALF_PORT, 320 + down - up), module, Kicker::TRIG_INPUT));
        addInput(createInput<InPortAutinn>(Vec(67.5 - HALF_PORT, 320 + down - up), module, Kicker::VOCT_INPUT));
        addOutput(createOutput<OutPortAutinn>(Vec(112 - HALF_PORT, 320 + down - up), module, Kicker::AUDIO_OUTPUT));
    }
};

Model *modelKicker = createModel<Kicker, KickerWidget>("Kicker");