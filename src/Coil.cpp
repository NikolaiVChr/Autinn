#include "Autinn.hpp"
#include "Autinn-dsp.hpp"

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

#define FREQ_MAX 10000.0f
#define FREQ_MIN 100.0f
#define MIN_COILS 6
#define MAX_COILS 24

const float LOG2_FREQ_RANGE = float(std::log2(FREQ_MAX/FREQ_MIN));

// models a physical spring
struct SpringTank {
    // 2^17. Holds ~2.7s at 48kHz, or ~170ms at 768kHz.
    static constexpr int MAX_BUFFER_SIZE = 131072;
    float buffer[MAX_BUFFER_SIZE] = {};
    int writeHead = 0;

    dsp::RCFilter damper;
    AllPassFilter ap[MAX_COILS];
    
    // Physical variance (Randomness for stereo width)
    float tensionOffset = 0.f;
    float lengthOffset = 0.f;

    int curr_coils = 6;

    void init(const float t_off, const float l_off, const int coils) {
        tensionOffset = t_off;
        lengthOffset = l_off;
        curr_coils = coils;
    }

    void setCoils(const int c) {
        curr_coils = c;
    }

    void reset() {
        damper.reset();
        for (auto & coil : ap) {
            coil.reset();
        }
    }

    float process(float input, float feedbackAmt, float tension, float inertia, float dampFreq, float sampleRate) {
        
        // Determine delay length (Inertia)
        // Springs are usually 30ms to 70ms.
        // Apply small random variance for stereo width
        float targetDelay = inertia * (1.0f + lengthOffset);

        float delayOut = readBufferSmooth(targetDelay * sampleRate);

        // Apply dispersion (All-pass chain)
        // Modulating this changes the tightness and creates pitch shifts
        float t = clamp(tension + tensionOffset, 0.05f, 0.95f);

        float dispersed = delayOut;

        for (int i = 0; i < MAX_COILS && i < curr_coils; i++) {
            ap[i].setTension(t);
            dispersed = ap[i].process(dispersed);
        }

        // Apply Damping (Loss of high freq over time)
        damper.setCutoffFreq(dampFreq / sampleRate);
        damper.process(dispersed);
        float filtered = damper.lowpass();

        // Feedback Loop
        // Soft saturate the loop to allow self-oscillation without digital clipping
        float loopSignal = filtered * feedbackAmt;
        loopSignal = tanh_fast_high(loopSignal);

        // Write to Buffer
        // Add new input + feedback
        buffer[writeHead] = input + loopSignal;
        
        // Increment head
        writeHead++;
        if (writeHead >= MAX_BUFFER_SIZE) writeHead = 0;

        return filtered;
    }

    float readBufferSmooth(const float delaySamples) {
        float readPos = (float)writeHead - delaySamples;
        if (readPos < 0) readPos += MAX_BUFFER_SIZE;
        if (readPos >= MAX_BUFFER_SIZE) readPos -= MAX_BUFFER_SIZE;

        // Get the integer part and the fractional part
        int indexA = (int)readPos;

        float frac = readPos - indexA;

        // Prevents segfaults if float logic drifts
        indexA &= (MAX_BUFFER_SIZE - 1);
        int indexB = (indexA + 1) & (MAX_BUFFER_SIZE - 1);

        return interpolator(frac, buffer[indexA], buffer[indexB]);
    }
};

struct Coil : Module {
    enum ParamIds {
        DRIVE_PARAM,
        FEEDBACK_PARAM,
        MIX_PARAM,
        TENSION_PARAM,
        INERTIA_PARAM,
        DAMP_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        SIGNAL_LEFT_INPUT,
        SIGNAL_RIGHT_INPUT,
        PLUCK_INPUT,
        DRIVE_CV,
        FEEDBACK_CV,
        MIX_CV,
        TENSION_CV,
        INERTIA_CV,
        DAMP_CV,
        NUM_INPUTS
    };
    enum OutputIds {
        SIGNAL_LEFT_OUTPUT,
        SIGNAL_RIGHT_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        PLUCK_LIGHT,
        NUM_LIGHTS
    };

    SpringTank tankL;
    SpringTank tankR;
    dsp::SchmittTrigger pluckTrigger;
    
    // For the noise burst
    int pluckTimer = 0;
    int curr_coils = 9;
    
    Coil() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        
        configParam<Param3Digits>(DRIVE_PARAM, 0.f, 2.f, 1.f, "Drive");
        configParam<Param3Digits>(FEEDBACK_PARAM, 0.f, 1.2f, 0.85f, "Reflection"); // Goes > 1.0 for self oscillation
        configParam<Param3Digits>(MIX_PARAM, 0.f, 1.f, 1.f, "Mix");

        configParam<Param3Digits>(TENSION_PARAM, 0.1f, 0.95f, 0.4f, "Tension", "");
        configParam<Param3Digits>(INERTIA_PARAM, 20.f, 160.f, 50.f, "Inertia", "");
        configParam(DAMP_PARAM, 0.f, 1.f, 0.75f, "Damp", "", FREQ_MAX/FREQ_MIN, FREQ_MIN);

        configInput(DRIVE_CV, "Drive CV");
        configInput(FEEDBACK_CV, "Reflection CV");
        configInput(MIX_CV, "Mix CV");
        configInput(TENSION_CV, "Tension CV");
        configInput(INERTIA_CV, "Inertia CV");
        configInput(DAMP_CV, "1V/Oct Damp CV");

        configInput(SIGNAL_LEFT_INPUT, "Left");
        configInput(SIGNAL_RIGHT_INPUT, "Right");
        //configInput(PLUCK_INPUT, "Disturb (trigger)");

        configOutput(SIGNAL_LEFT_OUTPUT, "Left");
        configOutput(SIGNAL_RIGHT_OUTPUT, "Right");

        configBypass(SIGNAL_LEFT_INPUT, SIGNAL_LEFT_OUTPUT);
        configBypass(SIGNAL_RIGHT_INPUT, SIGNAL_RIGHT_OUTPUT);

        // Initialize tanks with slight variance for stereo width
        // Left: Standard
        // Right: 2% looser tension, 3% longer spring
        tankL.init(0.0f, 0.0f, curr_coils);
        tankR.init(-0.02f, 0.03f, curr_coils);
    }

    json_t *dataToJson() override {
        json_t *root = json_object();
        json_object_set_new(root, "coils", json_integer(curr_coils));
        return root;
    }

    void dataFromJson(json_t *rootJ) override {
        json_t *ext = json_object_get(rootJ, "coils");
        if (ext) {
            curr_coils = clamp((int)json_integer_value(ext), MIN_COILS ,MAX_COILS);
            tankL.setCoils(curr_coils);
            tankR.setCoils(curr_coils);
        }
    }

    void onReset(const ResetEvent& e) override {
        tankL.reset();
        tankR.reset();
        Module::onReset(e);
    }

    void onRandomize(const RandomizeEvent& e) override {
        Module::onRandomize(e);
        int menu = static_cast<int>(random::uniform() * 4.f);// 0,1,2 or 3
        if (menu == 0) {
            curr_coils = 6;
        } else if (menu == 1) {
            curr_coils = 9;
        } else if (menu == 2) {
            curr_coils = 12;
        } else if (menu == 3) {
            curr_coils = 24;
        }
        tankL.setCoils(curr_coils);
        tankR.setCoils(curr_coils);
    }

    static float toExp(float x) {
        // 0 to 1 to exp range
        return FREQ_MIN * std::exp2f( x*LOG2_FREQ_RANGE );
    }

    void process(const ProcessArgs& args) override {
        // VCV Rack audio rate is +-5V
        // VCV Rack CV is +-5V or 0V-10V

        if (!outputs[SIGNAL_RIGHT_OUTPUT].isConnected() && !outputs[SIGNAL_LEFT_OUTPUT].isConnected()) {
            return;
        }

        float drive = params[DRIVE_PARAM].getValue() + (inputs[DRIVE_CV].getVoltage() * 0.3f);
        drive = clamp(drive, 0.f, 3.f);

        float feedback = params[FEEDBACK_PARAM].getValue() + (inputs[FEEDBACK_CV].getVoltage() * 0.12f);
        feedback = clamp(feedback, 0.f, 1.2f);

        float mix = params[MIX_PARAM].getValue() + (inputs[MIX_CV].getVoltage() * 0.1f);
        mix = clamp(mix, 0.f, 1.f);

        float tension = params[TENSION_PARAM].getValue() + (inputs[TENSION_CV].getVoltage() * 0.1f);
        tension = clamp(tension, 0.05f, 0.95f);

        float inertiaMS = params[INERTIA_PARAM].getValue() + (inputs[INERTIA_CV].getVoltage() * 15.f);
        inertiaMS = clamp(inertiaMS, 10.f, 160.f); // Allow a wider range via CV
        float inertiaSeconds = inertiaMS / 1000.0f;

        float cv_damp =  std::exp2f(inputs[DAMP_CV].getVoltage());
        float dampFreq = clamp(this->toExp(params[DAMP_PARAM].getValue())*cv_damp, 20.f, 10000.f);

        // --- Audio Input Processing ---
        float inL = inputs[SIGNAL_LEFT_INPUT].isConnected() ? inputs[SIGNAL_LEFT_INPUT].getVoltage() : inputs[SIGNAL_RIGHT_INPUT].getVoltage();
        float inR = inputs[SIGNAL_RIGHT_INPUT].isConnected() ? inputs[SIGNAL_RIGHT_INPUT].getVoltage() : inputs[SIGNAL_LEFT_INPUT].getVoltage();

        // Apply Drive
        float inL_scaled = inL * drive * 0.2f;
        float inR_scaled = inR * drive * 0.2f;

        // --- Pluck Exciter Logic ---
        // Generates a 10ms burst of noise when triggered
        if (pluckTrigger.process(inputs[PLUCK_INPUT].getVoltage())) {
            pluckTimer = (int)(0.01f * args.sampleRate); // 10ms
        }

        float pluckSignal = 0.f;
        if (pluckTimer > 0) {
            // White Noise Burst
            pluckSignal = (random::uniform() * 2.f - 1.f);
            pluckTimer--;
            lights[PLUCK_LIGHT].setBrightness(1.f);
        } else {
            lights[PLUCK_LIGHT].setBrightnessSmooth(0.f, args.sampleTime);
        }

        // Add Pluck to input
        inL_scaled += pluckSignal;
        inR_scaled += pluckSignal; // Pluck excites both springs

        /*
        // --- Mechanical Coupling ---
        // Simulates energy transferring through the metal chassis.
        // 2.5% bleed is very subtle but glues the stereo image together.
        float crosstalk = 0.025f;
        inL_scaled += inR_scaled * crosstalk;
        inR_scaled += inL_scaled * crosstalk;

        Not 100% sure this is a good idea
        */

        // --- Process tank models ---
        float wetL = tankL.process(inL_scaled, feedback, tension, inertiaSeconds, dampFreq, args.sampleRate);
        float wetR = tankR.process(inR_scaled, feedback, tension, inertiaSeconds, dampFreq, args.sampleRate);

        wetL *= 5.0f;
        wetR *= 5.0f;

        // --- Output Mix ---
        float outL = inL * (1.f - mix) + wetL * mix;
        float outR = inR * (1.f - mix) + wetR * mix;

        outputs[SIGNAL_LEFT_OUTPUT].setVoltage(outL);
        outputs[SIGNAL_RIGHT_OUTPUT].setVoltage(outR);
    }
};


struct CoilsNumberMenuItem : MenuItem {
    Coil* _module;
    int _os;

    CoilsNumberMenuItem(Coil* module, const char* label, int os)
    : _module(module), _os(os)
    {
        this->text = label;
    }

    void onAction(const event::Action &e) override {
        _module->curr_coils = _os;
        _module->tankL.setCoils(_os);
        _module->tankR.setCoils(_os);
    }

    void step() override {
        rightText = _module->curr_coils == _os ? "✔" : "";
    }
};

struct CoilWidget : ModuleWidget {
    CoilWidget(Coil* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CoilModule.svg")));

        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float down = 50;
        float div3 = 10.0f * RACK_GRID_WIDTH * 0.25f;
        float hp = RACK_GRID_WIDTH*0.5f;

        // --- Knobs ---
        // Row 1: Exciter
        //addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(div3-hp, 40+down), module, Coil::DRIVE_PARAM));
        auto* driveKnob = createParamCentered<AutinnArcMidKnob>(Vec(div3-hp, 40+down), module, Coil::DRIVE_PARAM);
        driveKnob->setModulation(Coil::DRIVE_CV, [](float cv, float val, float att) {
                    return clamp(val + cv*0.3f, 0.0f, 3.0f);
                });
        addParam(driveKnob);
        //addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(div3*2, 40+down), module, Coil::FEEDBACK_PARAM));
        auto* feedKnob = createParamCentered<AutinnArcMidKnob>(Vec(div3*2, 40+down), module, Coil::FEEDBACK_PARAM);
        feedKnob->setModulation(Coil::FEEDBACK_CV, [](float cv, float val, float att) {
                    return clamp(val + cv*0.12f, 0.0f, 1.2f);
                });
        addParam(feedKnob);
        //addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(div3*3+hp, 40+down), module, Coil::MIX_PARAM));
        auto* mixKnob = createParamCentered<AutinnArcMidKnob>(Vec(div3*3+hp, 40+down), module, Coil::MIX_PARAM);
        mixKnob->setModulation(Coil::MIX_CV, [](float cv, float val, float att) {
                    return clamp(val + cv*0.1f, 0.0f, 1.0f);
                });
        addParam(mixKnob);

        // Row 2: Physics
        //addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(div3-hp, 100+down), module, Coil::TENSION_PARAM));
        auto* tensionKnob = createParamCentered<AutinnArcMidKnob>(Vec(div3-hp, 100+down), module, Coil::TENSION_PARAM);
        tensionKnob->setModulation(Coil::TENSION_CV, [](float cv, float val, float att) {
                    return clamp(val + cv*0.1f, 0.05f, 0.95f);
                });
        addParam(tensionKnob);
        //addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(div3*2, 100+down), module, Coil::INERTIA_PARAM));
        auto* inertiaKnob = createParamCentered<AutinnArcMidKnob>(Vec(div3*2, 100+down), module, Coil::INERTIA_PARAM);
        inertiaKnob->setModulation(Coil::INERTIA_CV, [](float cv, float val, float att) {
                    return clamp(val + cv*15.0f, 10.0f, 160.0f);
                });
        addParam(inertiaKnob);
        //addParam(createParamCentered<RoundSmallAutinnKnob>(Vec(div3*3+hp, 100+down), module, Coil::DAMP_PARAM));
        auto* dampKnob = createParamCentered<AutinnArcMidKnob>(Vec(div3*3+hp, 100+down), module, Coil::DAMP_PARAM);
        dampKnob->setModulation(Coil::DAMP_CV, [](float cv, float val, float att) {
                    // Calculate how many Octaves the knob covers
                    const float totalOctaves = std::log2f(FREQ_MAX / FREQ_MIN);
                    // 2. Scale CV so 1V = 1 Octave of knob travel
                    float cvNormalized = cv / totalOctaves;
                    // 3. Add to knob position (Linear Pitch Space)
                    return clamp(val + cvNormalized, -1.0f, 1.0f);// allow negative due to we allow CV to go down to 20hz.
                });
        addParam(dampKnob);

        // Row 3: CVs
        addInput(createInputCentered<InPortAutinn>(Vec(div3*1, 160+down), module, Coil::DRIVE_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(div3*2, 160+down), module, Coil::FEEDBACK_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(div3*3, 160+down), module, Coil::MIX_CV));
        
        addInput(createInputCentered<InPortAutinn>(Vec(div3*1, 195+down), module, Coil::TENSION_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(div3*2, 195+down), module, Coil::INERTIA_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(div3*3, 195+down), module, Coil::DAMP_CV));

        // Row 4: Audio IO & Pluck
        addInput(createInputCentered<InPortAutinn>(Vec(20, 330-RACK_GRID_WIDTH*1.5f), module, Coil::SIGNAL_LEFT_INPUT));
        addInput(createInputCentered<InPortAutinn>(Vec(55, 330-RACK_GRID_WIDTH*1.5f), module, Coil::SIGNAL_RIGHT_INPUT));
        
        //addInput(createInputCentered<PJ301MPort>(Vec(90, 250), module, Coil::PLUCK_INPUT));
        //addChild(createLightCentered<MediumLight<RedLight>>(Vec(115, 240), module, Coil::PLUCK_LIGHT)); // Light next to trigger

        addOutput(createOutputCentered<OutPortAutinn>(Vec(10.0f * RACK_GRID_WIDTH-55, 330-RACK_GRID_WIDTH*1.5f), module, Coil::SIGNAL_LEFT_OUTPUT));
        addOutput(createOutputCentered<OutPortAutinn>(Vec(10.0f * RACK_GRID_WIDTH-20, 330-RACK_GRID_WIDTH*1.5f), module, Coil::SIGNAL_RIGHT_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        Coil* a = dynamic_cast<Coil*>(module);
        assert(a);

        menu->addChild(new MenuLabel());
        menu->addChild(new CoilsNumberMenuItem(a, "Few coils", 6));
        menu->addChild(new CoilsNumberMenuItem(a, "Mid coils", 9));
        menu->addChild(new CoilsNumberMenuItem(a, "More coils", 12));
        menu->addChild(new CoilsNumberMenuItem(a, "Many coils", 24));
        menu->addChild(new MenuLabel());
    }
};


Model* modelCoil = createModel<Coil, CoilWidget>("Coil");