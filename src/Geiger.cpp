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
    dsp::BiquadFilter boxFilter[16];
    
    float lightDecay = 0.0f;
    int stepDivider = 33;
    float cached_probability_base = 0.0f; // From knob
    float filter_f = 0.0f;

    Geiger() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(RAD_PARAM, 0.0f, 1.0f, 0.0f, "Radiation", "%");
        
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
            float density = 0.2f + powf(knob, 4.0f) * 800.0f; // Events per second
            cached_probability_base = density * args.sampleTime;

            // Update Filter (Fixed characteristic of the "box")
            // 2.5kHz Bandpass with high Q gives that sharp "plastic" click sound.
            // Calculating this here saves expensive trig calls per sample.
            float freq = 2500.0f;
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
        float probability = 0.0f;

        for (int c = 0; c < channels; c++) {
            float impulse = 0.0f;

            // External Trigger (Deterministic click)
            if (triggers[c].process(inputs[TRIG_INPUT].getPolyVoltage(c))) {
                impulse = 1.0f;
            }

            // Radiation Logic (Stochastic click)
            // Combine Knob + CV
            float cv = inputs[RAD_CV_INPUT].getPolyVoltage(c) * 0.1f; // 0-10V -> 0-1.0
            // Allow negative CV to reduce knob setting
            float combined_prob = clamp(cached_probability_base + (cv * cv * 500.0f * args.sampleTime), 0.0f, 1.0f);
            
            // Poisson process: Random check per sample
            if (random::uniform() < combined_prob) {
                impulse = 1.0f;
            }

            // Sound Generation
            // Update filter parameters (safe to do frequently, but we use cached freq)
            // Ideally we only set this when sample rate changes, but it's cheap enough.
            boxFilter[c].setParameters(dsp::BiquadFilter::BANDPASS, filter_f, 2.5f, 1.0f);

            // Feed impulse (Dirac delta) into filter. 
            // The filter will "ring", creating the click sound.
            float out = boxFilter[c].process(impulse * 5.0f); // Boost impulse for volume

            // Output Stage
            // Hard clip to simulate the speaker distorting
            out = clamp(out * 2.0f, -5.0f, 5.0f);
            
            outputs[AUDIO_OUTPUT].setVoltage(out, c);

            // Light logic
            if (std::abs(out) > 0.5f) lightActive = true;
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
        addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(box.size.x/2, 100), module, Geiger::RAD_PARAM));

        // Light
        addChild(createLightCentered<MediumLight<GreenLight>>(Vec(box.size.x/2, 150), module, Geiger::ACT_LIGHT));

        // Inputs
        addInput(createInputCentered<InPortAutinn>(Vec(box.size.x/2, 220), module, Geiger::TRIG_INPUT));
        addInput(createInputCentered<InPortAutinn>(Vec(box.size.x/2, 270), module, Geiger::RAD_CV_INPUT));
        
        // Output
        addOutput(createOutputCentered<OutPortAutinn>(Vec(box.size.x/2, 330), module, Geiger::AUDIO_OUTPUT));
    }
};

Model *modelGeiger = createModel<Geiger, GeigerWidget>("Geiger");