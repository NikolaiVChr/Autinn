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

struct Snare : Module {
    enum ParamIds {
        FREQ_PARAM,
        DECAY_PARAM,
        SWEEP_PARAM,
        SNAP_PARAM,
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

    static const int MAX_CHANNELS = 16;

    float phase[MAX_CHANNELS] = {};
    float ampEnv[MAX_CHANNELS] = {};
    float noiseEnv[MAX_CHANNELS] = {}; // Separate envelope for noise tail
    float pitchEnv[MAX_CHANNELS] = {};
    dsp::SchmittTrigger triggers[MAX_CHANNELS];
    float lightDecay = 0.0f;

    // Highpass filter state for the noise (Simple 1-pole)
    float noiseHp[MAX_CHANNELS] = {};
    dsp::BiquadFilter wireFilter[MAX_CHANNELS];
    uint32_t noiseState[16] = {};
    int stepDivider = 33;
    float noiseGain = 1.0f;


    Snare() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(FREQ_PARAM, 100.0f, 400.0f, 150.0f, "Tune", " Hz");
        configParam(DECAY_PARAM, 0.05f, 0.8f, 0.2f, "Decay", " s");
        configParam(SWEEP_PARAM, 0.0f, 1.0f, 0.2f, "Sweep", "%");
        configParam(SNAP_PARAM, 0.0f, 1.0f, 0.25f, "Snappy", "%");
        configParam(DRIVE_PARAM, 0.0f, 5.0f, 2.5f, "Drive", "%");

        configInput(TRIG_INPUT, "Trigger");
        configInput(VOCT_INPUT, "V/Oct");
        configOutput(AUDIO_OUTPUT, "Audio");

        for (int i = 0; i < 16; i++) {
            // Give every channel a different starting seed
            noiseState[i] = 0x12345678 + (i * 0xdeadbeef);
        }
    }

    void process(const ProcessArgs &args) override;
};

void Snare::process(const ProcessArgs &args) {
    if (!outputs[AUDIO_OUTPUT].isConnected()) return;

    int channels = std::max(1, inputs[TRIG_INPUT].getChannels());
    outputs[AUDIO_OUTPUT].setChannels(channels);

    if (stepDivider++ >= 32) {
        stepDivider = 0;

        // As rate goes up, we boost the noise to maintain constant Power Density
        noiseGain = std::sqrt(args.sampleRate / 44100.0f);
    }

    float dt = args.sampleTime;
    float baseFreq = params[FREQ_PARAM].getValue();
    float sweepDepth = params[SWEEP_PARAM].getValue() * 200.0f; 
    float snapLevel = params[SNAP_PARAM].getValue();
    float drive = 1.0f + params[DRIVE_PARAM].getValue();
    
    // Decay Coefficients
    float decayParam = params[DECAY_PARAM].getValue();
    
    // Body decays slightly faster than noise
    float bodyCoeff = 1.0f - (10.0f * dt / decayParam); 
    // Noise tail
    float noiseCoeff = 1.0f - (5.0f * dt / decayParam);
    
    bodyCoeff = clamp(bodyCoeff, 0.0f, 1.0f);
    noiseCoeff = clamp(noiseCoeff, 0.0f, 1.0f);

    float pitchDecayCoeff = 1.0f - (25.0f * dt); // Very fast pitch drop

    /*
    // Simple HPF Coefficient (Cutoff ~800Hz)
    float rc = 1.0f / (2.0f * M_PI * 800.0f);
    float alpha = rc / (rc + dt);
    */

    bool active = false;

    for (int c = 0; c < channels; c++) {
        // Trigger
        if (triggers[c].process(inputs[TRIG_INPUT].getPolyVoltage(c))) {
            ampEnv[c] = 1.0f;
            noiseEnv[c] = 1.0f;
            pitchEnv[c] = 1.0f;
            phase[c] = 0.0f; 
            active = true;

            // Configure the Filter (You can do this in the trigger logic to save CPU)
            // Use BANDPASS to isolate the 'sizzle' or PEAK to just boost it.
            float wireFreq = 2500.0f;
            float wireQ = 1.0f;     // A Q of 1.0 is broad and natural
            wireFilter[c].setParameters(wireFilter[c].BANDPASS, wireFreq / args.sampleRate, wireQ, 1.0f);
        }

        // Envelopes
        ampEnv[c] *= bodyCoeff;
        noiseEnv[c] *= noiseCoeff;
        pitchEnv[c] *= pitchDecayCoeff;

        if (ampEnv[c] < 0.001f) ampEnv[c] = 0.0f;
        if (noiseEnv[c] < 0.001f) noiseEnv[c] = 0.0f;

        // Tonal Body (Triangle/Sine mix for body)
        float voct = inputs[VOCT_INPUT].getPolyVoltage(c);
        float pitchMod = sweepDepth * pitchEnv[c];
        float freq = baseFreq * std::exp2f(voct) + pitchMod;
        
        float deltaPhase = freq * dt;
        phase[c] += deltaPhase;
        if (phase[c] >= 1.0f) phase[c] -= 1.0f;

        float sine = sin(phase[c] * 2.0f * M_PI);
        // Add a bit of harmonics (Triangle-ish)
        float body = sine + 0.2f * sin(phase[c] * 6.0f * M_PI);

        // Snappy Layer (White Noise -> Highpass)
        //float white = (float)std::rand() / RAND_MAX * 2.0f - 1.0f;

        uint32_t& s = noiseState[c];
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        // Using a float cast on the raw bits for a fast [-1.0, 1.0] range
        float white = (int32_t(s) >> 8) * (1.0f / 8388608.0f);
        white *= noiseGain;

        // Process the noise through the filter
        float hpfNoise = wireFilter[c].process(white);

        if (!std::isfinite(hpfNoise)) {
            hpfNoise = white;
            wireFilter[c].reset();
        }

        float snare = hpfNoise * noiseEnv[c] * snapLevel;

        // Mix
        float mix = (body * ampEnv[c]) + snare;
        float signal = mix * drive;

        // Saturation
        float x = signal;
        if (x < -3.0f) x = -1.0f;
        else if (x > 3.0f) x = 1.0f;
        else x = x * (27.0f + x * x) / (27.0f + 9.0f * x * x);

        outputs[AUDIO_OUTPUT].setVoltage(x * 5.0f, c);
    }

    if (active) lightDecay = 1.0f;
    float lightLambda = 1.0f - (args.sampleTime / 0.1f);
    lightDecay *= std::max(0.0f, lightLambda);
    lights[ACT_LIGHT].value = lightDecay > 0.35f?1.0f:0.0f;
}

struct SnareWidget : ModuleWidget {
    SnareWidget(Snare *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/SnareModule.svg")));

        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float down = 20;
        float up = 4 * RACK_GRID_WIDTH;

        // Row 1 (Large knobs)
        addParam(createParam<RoundMediumAutinnKnob>(Vec(34 - HALF_KNOB_MED, 60 + down - RACK_GRID_WIDTH/2), module, Snare::FREQ_PARAM));
        addParam(createParam<RoundMediumAutinnKnob>(Vec(101 - HALF_KNOB_MED, 60 + down - RACK_GRID_WIDTH/2), module, Snare::DECAY_PARAM));

        // Row 2 (Small knobs)
        addParam(createParam<RoundSmallAutinnKnob>(Vec(34 - HALF_KNOB_SMALL, 120 + down), module, Snare::SWEEP_PARAM));
        addParam(createParam<RoundSmallAutinnKnob>(Vec(101 - HALF_KNOB_SMALL, 120 + down), module, Snare::SNAP_PARAM));

        // Row 3 (Drive - centered)
        addParam(createParam<RoundSmallAutinnKnob>(Vec(67.5 - HALF_KNOB_SMALL, 175 + down), module, Snare::DRIVE_PARAM));

        // Light (Next to drive)
        addChild(createLight<SmallLight<GreenLight>>(Vec(85, 182 + down), module, Snare::ACT_LIGHT));

        // Ports
        addInput(createInput<InPortAutinn>(Vec(23 - HALF_PORT, 320 + down - up), module, Snare::TRIG_INPUT));
        addInput(createInput<InPortAutinn>(Vec(67.5 - HALF_PORT, 320 + down - up), module, Snare::VOCT_INPUT));
        addOutput(createOutput<OutPortAutinn>(Vec(112 - HALF_PORT, 320 + down - up), module, Snare::AUDIO_OUTPUT));
    }
};

Model *modelSnare = createModel<Snare, SnareWidget>("Lurer");