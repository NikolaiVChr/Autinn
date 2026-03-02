#include "Autinn.hpp"

struct Converge : Module {
    enum ParamIds {
        NUM_PARAMS
    };
    enum InputIds {
        ROOT_INPUT,
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

        configInput(ROOT_INPUT, "Root 1V/Oct");
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
        float rootPitch = inputs[ROOT_INPUT].getVoltage();
        
        // Swarm character
        constexpr float clusterWidth = 2.0f; // Spreads voices +/- 2 octaves
        constexpr float driftAmount = 0.1f;  // Each voice wanders +/- 0.1 octaves

        constexpr float safeFloor = -4.0f; // 16.35 Hz
        constexpr float safeCeiling = 6.0f; // 16.7K Hz
        float lowestPossiblePitch = rootPitch - clusterWidth - driftAmount;
        float highestPossiblePitch = rootPitch + clusterWidth + driftAmount;
        if (lowestPossiblePitch < safeFloor) {
            rootPitch += std::ceil(safeFloor - lowestPossiblePitch);
        }
        else if (highestPossiblePitch > safeCeiling) {
            rootPitch -= std::ceil(highestPossiblePitch - safeCeiling);
        }

        // 0.0 = Root swarm, 1.0 = Target chord
        const float convRaw = inputs[CONVERGE_CV].getVoltage() / 10.0f;
        const float conv = clamp(convRaw, 0.0f, 1.0f);
        
        // Easing: fast from 0, slows down near 1
        const float easeConv = 1.0f - std::pow(1.0f - conv, 3.0f);

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

            // Interpolate: 0 = Swarm, 1 = Target
            const float currentPitch = swarmPitch * (1.0f - easeConv) + targetPitch * easeConv;

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
        addInput(createInputCentered<InPortAutinn>(Vec(centerX, 160.0f), module, Converge::ROOT_INPUT));
        addInput(createInputCentered<InPortAutinn>(Vec(centerX, 220.0f), module, Converge::CHORD_INPUT));

        addOutput(createOutputCentered<OutPortAutinn>(Vec(centerX, 300.0f+HALF_PORT), module, Converge::POLY_OUTPUT));
    }
};

Model* modelAu = createModel<Converge, ConvergeWidget>("Au");