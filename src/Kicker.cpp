#include "Autinn.hpp"
#include <cmath>
#include <algorithm>

/*

    Autinn VCV Rack Plugin
    Copyright (C) 2021  Nikolai V. Chr.

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
        DECAY_PARAM,
        SWEEP_PARAM, // Pitch Envelope Depth
        CLICK_PARAM, // Transient Level
        DRIVE_PARAM,
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

    Kicker() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(FREQ_PARAM, 30.0f, 200.0f, 60.0f, "Tune", " Hz");
        configParam(DECAY_PARAM, 0.1f, 0.8f, 0.4f, "Decay", " s");
        configParam(SWEEP_PARAM, 0.0f, 1.0f, 1.0f, "Sweep", "%");
        configParam(CLICK_PARAM, 0.0f, 1.0f, 0.5f, "Click", "%");
        configParam(DRIVE_PARAM, 0.0f, 5.0f, 2.5f, "Drive", "%"); // 0 to 5x gain

        configInput(TRIG_INPUT, "Trigger");
        configInput(VOCT_INPUT, "V/Oct");
        configOutput(AUDIO_OUTPUT, "Audio");

        for (int i = 0; i < 16; i++) {
            noiseState[i] = 0x12345678 + (i * 0xdeadbeef);
        }
    }

    void process(const ProcessArgs &args) override;
};

void Kicker::process(const ProcessArgs &args) {
    if (!outputs[AUDIO_OUTPUT].isConnected()) return;

    int channels = std::max(1, inputs[TRIG_INPUT].getChannels());
    outputs[AUDIO_OUTPUT].setChannels(channels);

    float dt = args.sampleTime;
    float baseFreq = params[FREQ_PARAM].getValue();
    float sweepDepth = params[SWEEP_PARAM].getValue() * 400.0f; // 0 to 400Hz drop
    float clickLevel = params[CLICK_PARAM].getValue();
    float drive = 1.0f + params[DRIVE_PARAM].getValue();

    // As rate goes up, we boost the noise to maintain constant Power Density
    float noiseGain = std::sqrt(args.sampleRate / 44100.0f);
    // We calculate a new coefficient clickAlpha that keeps the 2500Hz tone
    // regardless of the user's sample rate.
    float clickCutoffFreq = 2500.0f;
    float clickAlpha = 1.0f - std::exp(-2.0f * M_PI * clickCutoffFreq * dt);
    
    // envelope decay by 60dB (factor of 0.001) over 'decayTime' seconds.
    // Coefficient = exp(-6.9 / (decayTime * SampleRate))
    //float decayParam = params[DECAY_PARAM].getValue();
    //float decayCoeff = 1.0f - (6.9f * dt / decayParam);
    //decayCoeff = clamp(decayCoeff, 0.999f, 0.99999f); // Safety limits

    // Pitch envelope creates the "Thump" - needs to be faster than amp envelope
    //float pitchDecayCoeff = 1.0f - (20.0f * dt); // Fixed fast decay for punch

    float decayVal = std::max(params[DECAY_PARAM].getValue(), 0.01f);
    float decayCoeff = std::exp(-1.0f / (decayVal * args.sampleRate));
    //float pitchDecayCoeff = std::exp(-1.0f / (0.02f * args.sampleRate)); // 20ms fixed sweep
    float pitchTime = 0.005f + (decayVal * 0.015f);
    float pitchDecayCoeff = std::exp(-1.0f / (pitchTime * args.sampleRate));

    bool active = false;

    for (int c = 0; c < channels; c++) {
        // Trigger Logic
        float trigVoltage = inputs[TRIG_INPUT].getPolyVoltage(c);
        if (triggers[c].process(trigVoltage)) {
            ampEnv[c] = 1.0f;
            pitchEnv[c] = 1.0f;
            phase[c] = 0.25f; // Reset phase for consistent punch
            active = true;
        }

        // Envelopes
        ampEnv[c] *= decayCoeff;
        pitchEnv[c] *= pitchDecayCoeff;

        // Prevent denormals
        if (ampEnv[c] < 0.001f) ampEnv[c] = 0.0f;
        if (pitchEnv[c] < 0.001f) pitchEnv[c] = 0.0f;

        // Oscillator
        // Pitch = Base + V/Oct + SweepEnvelope
        float voct = inputs[VOCT_INPUT].getPolyVoltage(c);
        //float pitchMod = sweepDepth * pitchEnv[c];
        //float freq = baseFreq * powf(2.0f, voct) + pitchMod;
        //float freq = baseFreq * powf(2.0f, voct + (pitchEnv[c] * 3.0f * params[SWEEP_PARAM].getValue()));
        float mod = pitchEnv[c] * pitchEnv[c] * 5.0f * params[SWEEP_PARAM].getValue();
        float freq = baseFreq * powf(2.0f, voct + mod);


        float deltaPhase = freq * dt;
        phase[c] += deltaPhase;
        if (phase[c] >= 1.0f) phase[c] -= 1.0f;

        // Sound Generation

        // Main Body (Sine)
        float body = sin(phase[c] * 2.0f * M_PI);
        
        // Click (Short burst of noise or high pitch sine at start)
        // trick: Add a tiny bit of squared envelope to the start
        float white = (int32_t(noiseState[c] = noiseState[c] * 1664525 + 1013904223) >> 8) * (1.0f / 8388608.0f);
        white *= noiseGain;
        //float click = white * pitchEnv[c] * pitchEnv[c] * clickLevel;
        //float click = clamp(white * 5.0f, -1.0f, 1.0f) * pitchEnv[c] * pitchEnv[c] * clickLevel;
        //float click = (std::rand() % 2000 / 1000.0f - 1.0f) * pitchEnv[c] * clickLevel;

        //Adaptive Envelope
        // If decay is short (< 0.5s), square the env to make it "Dry/Tight".
        // If decay is long, keep it linear to preserve the "Boom".
        float linearEnv = ampEnv[c];              // Boomy (Linear)
        float squaredEnv = ampEnv[c] * ampEnv[c]; // Dry/Tight (Squared)

        // mix factor based on knob position
        // When Knob < 0.4, factor is 0.0 (Pure Squared/Dry)
        // When Knob > 0.6, factor is 1.0 (Pure Linear/Boomy)
        // Between 0.4 and 0.6, it blends smoothly.
        float blend = clamp((decayVal - 0.4f) * 5.0f, 0.0f, 1.0f);
        // no clicking when turn the knob.
        float finalEnv = squaredEnv + (linearEnv - squaredEnv) * blend;

        float filteredClick = lastClickFilter[c] + clickAlpha * (white - lastClickFilter[c]);
        lastClickFilter[c] = filteredClick;

        // Short envelope
        float click = filteredClick * (pitchEnv[c] * pitchEnv[c]) * clickLevel;

        //float click = white * (pitchEnv[c] * pitchEnv[c]) * clickLevel;

        // Apply adaptive envelope to body
        float signal = (body * finalEnv + click) * drive;

        // Mix & Saturate
        //float signal = (body + click) * ampEnv[c] * drive;
        //float signal = (body * ampEnv[c] + click) * ampEnv[c] * drive;

        // Fast Tanh approximation for Analog feel
        float x = signal;
        if (x < -3.0f) x = -1.0f;
        else if (x > 3.0f) x = 1.0f;
        else x = x * (27.0f + x * x) / (27.0f + 9.0f * x * x);

        outputs[AUDIO_OUTPUT].setVoltage(x * 5.0f, c);
    }

    // Blink light if any drum triggered
    if (active) lightDecay = 1.0f;
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

        // Row 1 (Large knobs)
        addParam(createParam<RoundMediumAutinnKnob>(Vec(34 - HALF_KNOB_MED, 60 + down - RACK_GRID_WIDTH/2), module, Kicker::FREQ_PARAM));
        addParam(createParam<RoundMediumAutinnKnob>(Vec(101 - HALF_KNOB_MED, 60 + down - RACK_GRID_WIDTH/2), module, Kicker::DECAY_PARAM));

        // Row 2 (Small knobs)
        addParam(createParam<RoundSmallAutinnKnob>(Vec(34 - HALF_KNOB_SMALL, 120 + down), module, Kicker::SWEEP_PARAM));
        addParam(createParam<RoundSmallAutinnKnob>(Vec(101 - HALF_KNOB_SMALL, 120 + down), module, Kicker::CLICK_PARAM));

        // Row 3 (Drive - centered)
        addParam(createParam<RoundSmallAutinnKnob>(Vec(67.5 - HALF_KNOB_SMALL, 175 + down), module, Kicker::DRIVE_PARAM));

        // Light (Next to drive)
        addChild(createLight<SmallLight<GreenLight>>(Vec(85, 182 + down), module, Kicker::ACT_LIGHT));

        // Ports (Evenly distributed: x = 23, 67.5, 112)
        addInput(createInput<InPortAutinn>(Vec(23 - HALF_PORT, 320 + down - up), module, Kicker::TRIG_INPUT));
        addInput(createInput<InPortAutinn>(Vec(67.5 - HALF_PORT, 320 + down - up), module, Kicker::VOCT_INPUT));
        addOutput(createOutput<OutPortAutinn>(Vec(112 - HALF_PORT, 320 + down - up), module, Kicker::AUDIO_OUTPUT));
    }
};

Model *modelKicker = createModel<Kicker, KickerWidget>("Kicker");