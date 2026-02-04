#include "Autinn.hpp"
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

struct Geiger : Module {
    enum ParamIds {
        RAD_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        TRIG_INPUT,
        RAD_CV_INPUT,
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

    dsp::SchmittTrigger triggers[16];
    dsp::BiquadFilter speakerFilter[16];

    int pulseTimer[16] = {};

    float lightDecay = 0.0f;
    int stepDivider = 33;
    float cached_probability_base = 0.0f; // From knob
    float filter_f = 0.0f;

    // 1 mR/h ~= 1300 CPM (SBM-20 tube) -> ~21.66 Hz
    const float HZ_PER_MRH = 21.66f;

    // Display Scaling
    // 0.01 mR/h (Background) to 50.0 mR/h
    const float DISP_BASE = 5000.0f;
    const float DISP_MULT = 0.01f;
    const float DISP_OFFSET = 0.0f;

    Geiger() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(RAD_PARAM, 0.0f, 1.0f, 0.0f, "Radiation", "mR/h", DISP_BASE, DISP_MULT, DISP_OFFSET);
        
        configInput(TRIG_INPUT, "Manual Click Trigger");
        configInput(RAD_CV_INPUT, "Radiation Level CV");
        configOutput(AUDIO_OUTPUT, "Audio Out");
    }

    void process(const ProcessArgs &args) override {
        if (!outputs[AUDIO_OUTPUT].isConnected()) return;

        int channels = std::max(1, inputs[TRIG_INPUT].getChannels());
        channels = std::max(channels, inputs[RAD_CV_INPUT].getChannels());
        outputs[AUDIO_OUTPUT].setChannels(channels);

        if (stepDivider++ >= 32) {
            stepDivider = 0;
            
            // Calculate Base Probability from Knob
            // We want an exponential curve: 
            // 0.0 -> ~0.2 Hz (Cosmic background)
            // 1.0 -> ~500 Hz (Chernobyl buzz)
            float knob = params[RAD_PARAM].getValue();
            float mRh = DISP_MULT * std::pow(DISP_BASE, knob) + DISP_OFFSET;

            // 2. Convert mR/h to Hz (Clicks per second) for the physics engine
            float densityHz = mRh * HZ_PER_MRH;
            cached_probability_base = densityHz * args.sampleTime;

            // Update Filter (Fixed characteristic of the "box")
            // 2.5kHz Bandpass with high Q gives that sharp "plastic" click sound.
            // Calculating this here saves expensive trig calls per sample.
            float freq = 1000.0f;
            float q = 2.5f;
            // Note: We set parameters per channel later if we want variation, 
            // but for a uniform machine, calculating coefficients once is efficient.
            // However, BiquadFilter struct stores state, not coeffs. 
            // We will just calculate normalized freq here.
            filter_f = freq * args.sampleTime;
            // If sample rate changes, we should update.

            // dsp::BiquadFilter::setParameters is relatively fast but let's do it safely.
        }

        bool lightActive = false;

        for (int c = 0; c < channels; c++) {
            bool triggered = false;

            // Triggers
            if (triggers[c].process(inputs[TRIG_INPUT].getChannels() > c?inputs[TRIG_INPUT].getPolyVoltage(c):inputs[TRIG_INPUT].getVoltage())) {
                triggered = true;
            }

            // Stochastic Probability
            float cv = inputs[RAD_CV_INPUT].getChannels() > c?inputs[RAD_CV_INPUT].getPolyVoltage(c):inputs[RAD_CV_INPUT].getVoltage() * 0.1f;
            float mRh = DISP_MULT * std::pow(DISP_BASE, cv) + DISP_OFFSET;

            // 2. Convert mR/h to Hz (Clicks per second) for the physics engine
            float densityHz = mRh * HZ_PER_MRH;
            float cv_probability_base = densityHz * args.sampleTime;
            float combined_prob = clamp(cached_probability_base + cv_probability_base, 0.0f, 1.0f);

            if (random::uniform() < combined_prob) {
                triggered = true;
            }

            // Discharge Model
            // If triggered, we start a short current pulse.
            // 5 samples at 44.1kHz is ~0.1ms. This adds "weight" to the click.
            if (triggered) {
                pulseTimer[c] = 5;
            }

            float raw_pulse = 0.0f;
            if (pulseTimer[c] > 0) {
                pulseTimer[c]--;
                // A square pulse has harmonics that cut through
                raw_pulse = 1.0f;
            }

            // Speaker Physics
            // Update filter: Lowpass allows the low-end "pop" to pass through.
            // Q = 2.0 simulates the speaker cone ringing slightly after the hit.
            speakerFilter[c].setParameters(dsp::BiquadFilter::LOWPASS, filter_f, 2.0f, 1.0f);

            // Drive the speaker hard (x10 gain)
            float out = speakerFilter[c].process(raw_pulse * 10.0f);

            // Output Transformer/Speaker Saturation
            // This compresses the loud click, making it sound "solid" rather than "spikey".
            out = non_lin_func(out); // Soft clip

            // Hard clamp for safety
            out = clamp(out * 2.0f, -5.0f, 5.0f);

            outputs[AUDIO_OUTPUT].setVoltage(out, c);

            if (std::abs(out) > 0.1f) lightActive = true;
        }

        // Light decay animation
        if (lightActive) lightDecay = 1.0f;
        lightDecay *= (1.0f - args.sampleTime * 15.0f); // Fast decay
        lights[ACT_LIGHT].value = lightDecay;
    }
};

struct GeigerWidget : ModuleWidget {
    GeigerWidget(Geiger *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/RadModule.svg")));

        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Knob
        addParam(createParam<RoundMediumAutinnKnob>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_KNOB_MED, 150), module, Geiger::RAD_PARAM));

        // Light
        addChild(createLight<MediumLight<GreenLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Geiger::ACT_LIGHT));

        // Inputs
        addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 200), module, Geiger::TRIG_INPUT));
        addInput(createInput<InPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 115), module, Geiger::RAD_CV_INPUT));

        // Output
        addOutput(createOutput<OutPortAutinn>(Vec(3 * RACK_GRID_WIDTH*0.5-HALF_PORT, 300), module, Geiger::AUDIO_OUTPUT));
    }
};

Model *modelGeiger = createModel<Geiger, GeigerWidget>("Geiger");