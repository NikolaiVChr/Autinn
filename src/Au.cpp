#include "Autinn.hpp"

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

struct Converge : Module {
    enum ParamIds {
        NUM_PARAMS
    };
    enum InputIds {
        CV_SWARM_INPUT,
        CHORD_INPUT,
        CONVERGE_CV,
        NUM_INPUTS
    };
    enum OutputIds {
        POLY_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        NUM_LIGHTS
    };

    float walkValue[16] = {};
    float walkTarget[16] = {};

    Converge() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        configInput(CV_SWARM_INPUT, "Swarm 1V/Oct");
        configInput(CHORD_INPUT, "Poly Target Chord 1V/Oct");
        configInput(CONVERGE_CV, "Converge Envelope (0-10V)");
        configOutput(POLY_OUTPUT, "16-Channel Poly 1V/Oct");
        configBypass(CHORD_INPUT, POLY_OUTPUT);

        // Seed initial targets for the random walk
        for (int i = 0; i < 16; i++) {
            walkTarget[i] = (random::uniform() * 2.0f) - 1.0f;
        }
    }

    void process(const ProcessArgs& args) override {
        float rootPitch = inputs[CV_SWARM_INPUT].getVoltage();
        
        // Swarm character
        constexpr float clusterWidth = 0.5f; // Spreads voices +/- 2 octaves
        constexpr float driftAmount = 0.1f;  // Each voice wanders +/- 0.1 octaves

        constexpr float safeFloor = -4.0f; // 16.35 Hz
        constexpr float safeCeiling = 5.0f; // 16.7K Hz
        float lowestPossiblePitch = rootPitch - clusterWidth - driftAmount;
        float highestPossiblePitch = rootPitch + clusterWidth + driftAmount;
        if (lowestPossiblePitch < safeFloor) {
            rootPitch += std::ceil(safeFloor - lowestPossiblePitch);
        }
        else if (highestPossiblePitch > safeCeiling) {
            rootPitch -= std::ceil(highestPossiblePitch - safeCeiling);
        }

        float envVolts = inputs[CONVERGE_CV].getVoltage();

        // 0.0 = Root swarm, 1.0 = Target chord
        float convRaw = 1.0f;

        // Inverted behavior: 0V = Chord (Converged), 8V+ = Swarm (Chaos)
        if (envVolts >= 9.0f) {
            convRaw = 0.0f; // Perfect chaos for 8V-10V envelopes
        } else if (envVolts <= 0.1f) {
            convRaw = 1.0f; // Perfect lock (and kills ADSR exponential tails)
        } else {
            convRaw = 1.0f - (envVolts / 9.0f); // Smooth inversion
        }

        const float conv = clamp(convRaw, 0.0f, 1.0f);
        
        // Easing: fast from 0, slows down near 1
        // S-curve: Slow start, fast middle crossover, slow lock
        const float easeConv = conv * conv * (3.0f - 2.0f * conv);

        const int targetChannels = std::max(1, inputs[CHORD_INPUT].getChannels());

        outputs[POLY_OUTPUT].setChannels(16);

        for (int c = 0; c < 16; c++) {
            // Slow random walk
            // Constant-rate linear drift (moves 0.2 Volts per second)
            const float driftSlew = 0.2f * args.sampleTime;

            if (walkValue[c] < walkTarget[c]) {
                walkValue[c] += driftSlew;
                if (walkValue[c] >= walkTarget[c]) walkTarget[c] = (random::uniform() * 2.0f) - 1.0f;
            } else {
                walkValue[c] -= driftSlew;
                if (walkValue[c] <= walkTarget[c]) walkTarget[c] = (random::uniform() * 2.0f) - 1.0f;
            }

            // Swarm pitch
            const float baseOffset = ((c / 15.0f) * 2.0f - 1.0f) * clusterWidth;
            const float swarmPitch = rootPitch + baseOffset + (walkValue[c] * driftAmount);

            // Target chord (Inverted for crossover tearing effect)
            const int targetIdx = (targetChannels - 1) - (c % targetChannels);
            const float targetPitch = inputs[CHORD_INPUT].getPolyVoltage(targetIdx);

            // +/- 4 cents of spread to turn duplicate voltages into a thick supersaw
            const float microDetune = (c - 7.5f) * 0.0005f;
            int detuneBool = 0;

            // Interpolate: 0 = Swarm, 1 = Target
            const float currentPitch = swarmPitch * (1.0f - easeConv) + (targetPitch + microDetune * detuneBool) * easeConv;

            outputs[POLY_OUTPUT].setVoltage(currentPitch, c);
        }
    }
};

struct ConvergeWidget : ModuleWidget {
    explicit ConvergeWidget(Converge* module) {
        setModule(module);
        
        setPanel(createPanel(asset::plugin(pluginInstance, "res/AuModule.svg")));

        constexpr float centerX = 15.0f;

        addInput(createInputCentered<InPortAutinn>(Vec(centerX, 100.0f), module, Converge::CONVERGE_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(centerX, 160.0f), module, Converge::CV_SWARM_INPUT));
        addInput(createInputCentered<InPortAutinn>(Vec(centerX, 220.0f), module, Converge::CHORD_INPUT));

        addOutput(createOutputCentered<OutPortAutinn>(Vec(centerX, 300.0f+HALF_PORT), module, Converge::POLY_OUTPUT));
    }
};

Model* modelAu = createModel<Converge, ConvergeWidget>("Au");