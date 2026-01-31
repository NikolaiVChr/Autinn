#include "Autinn.hpp"
#include <cmath>
#include <algorithm>

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

    static const int MAX_CHANNELS = 16;

    // Polyphonic State
    float phase[MAX_CHANNELS] = {};
    float ampEnv[MAX_CHANNELS] = {};
    float pitchEnv[MAX_CHANNELS] = {};
    dsp::SchmittTrigger triggers[MAX_CHANNELS];
    float lightDecay = 0.0f;

    Kicker() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(FREQ_PARAM, 30.0f, 200.0f, 50.0f, "Tune", " Hz");
        configParam(DECAY_PARAM, 0.1f, 2.0f, 0.5f, "Decay", " s");
        configParam(SWEEP_PARAM, 0.0f, 1.0f, 0.5f, "Sweep", "%");
        configParam(CLICK_PARAM, 0.0f, 1.0f, 0.3f, "Click", "%");
        configParam(DRIVE_PARAM, 0.0f, 5.0f, 0.0f, "Drive", "%"); // 0 to 5x gain

        configInput(TRIG_INPUT, "Trigger");
        configInput(VOCT_INPUT, "V/Oct");
        configOutput(AUDIO_OUTPUT, "Audio");
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
    
    // envelope decay by 60dB (factor of 0.001) over 'decayTime' seconds.
    // Coefficient = exp(-6.9 / (decayTime * SampleRate))
    float decayParam = params[DECAY_PARAM].getValue();
    float decayCoeff = 1.0f - (6.9f * dt / decayParam);
    decayCoeff = clamp(decayCoeff, 0.999f, 0.99999f); // Safety limits

    // Pitch envelope creates the "Thump" - needs to be faster than amp envelope
    float pitchDecayCoeff = 1.0f - (20.0f * dt); // Fixed fast decay for punch

    bool active = false;

    for (int c = 0; c < channels; c++) {
        // Trigger Logic
        float trigVoltage = inputs[TRIG_INPUT].getPolyVoltage(c);
        if (triggers[c].process(trigVoltage)) {
            ampEnv[c] = 1.0f;
            pitchEnv[c] = 1.0f;
            phase[c] = 0.0f; // Reset phase for consistent punch
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
        float pitchMod = sweepDepth * pitchEnv[c];
        float freq = baseFreq * powf(2.0f, voct) + pitchMod;
        
        float deltaPhase = freq * dt;
        phase[c] += deltaPhase;
        if (phase[c] >= 1.0f) phase[c] -= 1.0f;

        // Sound Generation

        // Main Body (Sine)
        float body = sin(phase[c] * 2.0f * M_PI);
        
        // Click (Short burst of noise or high pitch sine at start)
        // Simple trick: Add a tiny bit of squared envelope to the start
        float click = (std::rand() % 2000 / 1000.0f - 1.0f) * pitchEnv[c] * clickLevel;

        // Mix & Saturate
        float signal = (body + click) * ampEnv[c] * drive;
        
        // Fast Tanh approximation for Analog feel
        float x = signal;
        if (x < -3.0f) x = -1.0f;
        else if (x > 3.0f) x = 1.0f;
        else x = x * (27.0f + x * x) / (27.0f + 9.0f * x * x);

        outputs[AUDIO_OUTPUT].setVoltage(x * 5.0f, c);
    }

    // Blink light if any drum triggered
    if (active) lightDecay = 1.0f;
    lightDecay *= 0.95f;
    lights[ACT_LIGHT].value = lightDecay;
}

struct KickerWidget : ModuleWidget {
    KickerWidget(Kicker *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/BassModule.svg"))); // You need to make this SVG

        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addParam(createParam<RoundMediumAutinnKnob>(Vec(10, 50), module, Kicker::FREQ_PARAM));
        addParam(createParam<RoundMediumAutinnKnob>(Vec(50, 50), module, Kicker::DECAY_PARAM));
        addParam(createParam<RoundSmallAutinnKnob>(Vec(15, 110), module, Kicker::SWEEP_PARAM));
        addParam(createParam<RoundSmallAutinnKnob>(Vec(55, 110), module, Kicker::CLICK_PARAM));
        addParam(createParam<RoundSmallAutinnKnob>(Vec(35, 160), module, Kicker::DRIVE_PARAM));

        addInput(createInput<InPortAutinn>(Vec(10, 300), module, Kicker::TRIG_INPUT));
        addInput(createInput<InPortAutinn>(Vec(50, 300), module, Kicker::VOCT_INPUT));
        addOutput(createOutput<OutPortAutinn>(Vec(90, 300), module, Kicker::AUDIO_OUTPUT));

        addChild(createLight<SmallLight<GreenLight>>(Vec(42, 280), module, Kicker::ACT_LIGHT));
    }
};

Model *modelKicker = createModel<Kicker, KickerWidget>("Kicker");