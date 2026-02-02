#include "Autinn.hpp"


#define FREQ_MAX 10000.0f
#define FREQ_MIN 100.0f
#define LOG_FREQ_RANGE float(log(FREQ_MAX/FREQ_MIN))

// 1st Order All-Pass Filter for Dispersion
struct AllPassFilter {
    float x1 = 0.f; // Previous input
    float y1 = 0.f; // Previous output
    float c = 0.f;  // Coefficient (Tension)

    void setTension(float tension) {
        // Map tension to a useful coefficient range (-0.9 to 0.9)
        // For springs, 0.1 to 0.8 is usually the sweet spot.
        c = tension;
    }

    float process(float x) {
        // y[n] = c * x[n] + x[n-1] - c * y[n-1]
        float y = -c * x + x1 + c * y1;
        // Denormal protection
        if (std::abs(y) < 1e-15f) y = 0.f;
        
        x1 = x;
        y1 = y;
        return y;
    }
};

// A helper struct to model one physical spring
struct SpringTank {
    static constexpr int MAX_BUFFER_SIZE = 96000; // ~2 seconds buffer
    float buffer[MAX_BUFFER_SIZE] = {};
    int writeHead = 0;
    
    // Components
    dsp::RCFilter damper;
    AllPassFilter ap1, ap2, ap3, ap4, ap5, ap6; // 6 stages of dispersion
    
    // Physical Variance (Randomness for Stereo Width)
    float tensionOffset = 0.f;
    float lengthOffset = 0.f; 

    void init(float t_off, float l_off) {
        tensionOffset = t_off;
        lengthOffset = l_off;
    }

    float process(float input, float feedbackAmt, float tension, float inertia, float dampFreq, float sampleRate) {
        
        // Determine Delay Length (Inertia)
        // Springs are usually 30ms to 70ms.
        // Apply small random variance for stereo width
        float targetDelay = inertia * (1.0f + lengthOffset);
        int delaySamples = (int)(targetDelay * sampleRate);
        if (delaySamples >= MAX_BUFFER_SIZE) delaySamples = MAX_BUFFER_SIZE - 1;
        if (delaySamples < 10) delaySamples = 10;

        // Read from Delay Line
        int readHead = writeHead - delaySamples;
        if (readHead < 0) readHead += MAX_BUFFER_SIZE;
        float delayOut = buffer[readHead];

        // Apply Dispersion (All-Pass Chain)
        // Modulating this changes the "tightness" and creates pitch shifts
        float t = clamp(tension + tensionOffset, 0.05f, 0.9f); 
        ap1.setTension(t);
        ap2.setTension(t);
        ap3.setTension(t);
        ap4.setTension(t);
        ap5.setTension(t);
        ap6.setTension(t);

        float dispersed = ap1.process(delayOut);
        dispersed = ap2.process(dispersed);
        dispersed = ap3.process(dispersed);
        dispersed = ap4.process(dispersed);
        dispersed = ap5.process(dispersed);
        dispersed = ap6.process(dispersed);

        // Apply Damping (Loss of high freq over time)
        damper.setCutoffFreq(dampFreq / sampleRate);
        damper.process(dispersed);
        float filtered = damper.lowpass();

        // Feedback Loop
        // Soft saturate the loop to allow self-oscillation without digital clipping
        float loopSignal = filtered * feedbackAmt;
        loopSignal = non_lin_func(loopSignal);

        // Write to Buffer
        // Add new input + feedback
        buffer[writeHead] = input + loopSignal;
        
        // Increment head
        writeHead++;
        if (writeHead >= MAX_BUFFER_SIZE) writeHead = 0;

        return filtered;
    }
};

// --- Module Definition ---

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
    
    Coil() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        
        configParam(DRIVE_PARAM, 0.f, 2.f, 1.f, "Drive");
        configParam(FEEDBACK_PARAM, 0.f, 1.2f, 0.7f, "Reflection"); // Goes > 1.0 for self oscillation
        configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix");

        configParam(TENSION_PARAM, 0.1f, 0.95f, 0.4f, "Tension", "");
        configParam(INERTIA_PARAM, 20.f, 80.f, 50.f, "Inertia", "");
        configParam(DAMP_PARAM, 0.f, 1.f, 0.75f, "Damp", "", FREQ_MAX/FREQ_MIN, FREQ_MIN);

        configInput(DRIVE_CV, "Drive CV");
        configInput(FEEDBACK_CV, "Reflection CV");
        configInput(MIX_CV, "Mix CV");
        configInput(TENSION_CV, "Tension CV");
        configInput(INERTIA_CV, "Inertia CV");
        configInput(DAMP_CV, "Damp CV");

        configInput(SIGNAL_LEFT_INPUT, "Left");
        configInput(SIGNAL_RIGHT_INPUT, "Right");
        //configInput(PLUCK_INPUT, "Disturb (trigger)");

        configOutput(SIGNAL_LEFT_OUTPUT, "Left");
        configOutput(SIGNAL_RIGHT_OUTPUT, "Right");

        // Initialize tanks with slight variance for stereo width
        // Left: Standard
        // Right: 2% looser tension, 3% longer spring
        tankL.init(0.0f, 0.0f);
        tankR.init(-0.02f, 0.03f);
    }

    static float toExp(float x) {
        // 0 to 1 to exp range
        return FREQ_MIN * exp( x*LOG_FREQ_RANGE );
    }

    void process(const ProcessArgs& args) override {
        if (outputs[SIGNAL_RIGHT_OUTPUT].isConnected() || !outputs[SIGNAL_RIGHT_OUTPUT].isConnected()) {
            return;
        }

        float drive = params[DRIVE_PARAM].getValue() + (inputs[DRIVE_CV].getVoltage() / 5.f);
        drive = clamp(drive, 0.f, 3.f);

        float feedback = params[FEEDBACK_PARAM].getValue() + (inputs[FEEDBACK_CV].getVoltage() / 5.f);
        feedback = clamp(feedback, 0.f, 1.5f);

        float mix = params[MIX_PARAM].getValue() + (inputs[MIX_CV].getVoltage() / 5.f);
        mix = clamp(mix, 0.f, 1.f);

        float tension = params[TENSION_PARAM].getValue() + (inputs[TENSION_CV].getVoltage() / 10.f);
        tension = clamp(tension, 0.05f, 0.9f);

        float inertiaMS = params[INERTIA_PARAM].getValue() + (inputs[INERTIA_CV].getVoltage() * 10.f);
        inertiaMS = clamp(inertiaMS, 10.f, 150.f); // Allow wider range via CV
        float inertiaSeconds = inertiaMS / 1000.0f;

        float cv_damp =  powf(2.0f, inputs[DAMP_CV].getVoltage());
        float dampFreq = clamp(this->toExp(params[DAMP_PARAM].getValue())*cv_damp, 20.f, 20000.f);

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


struct CoilWidget : ModuleWidget {
    CoilWidget(Coil* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CoilModule.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        float down = 120;

        // --- Knobs ---
        // Row 1: Exciter
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(30, 40+down), module, Coil::DRIVE_PARAM));
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(75, 40+down), module, Coil::FEEDBACK_PARAM));
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(120, 40+down), module, Coil::MIX_PARAM));

        // Row 2: Physics
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(30, 100+down), module, Coil::TENSION_PARAM));
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(75, 100+down), module, Coil::INERTIA_PARAM));
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(120, 100+down), module, Coil::DAMP_PARAM));

        // Row 3: CVs
        addInput(createInputCentered<InPortAutinn>(Vec(20, 160+down), module, Coil::DRIVE_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(55, 160+down), module, Coil::FEEDBACK_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(90, 160+down), module, Coil::MIX_CV));
        
        addInput(createInputCentered<InPortAutinn>(Vec(20, 195+down), module, Coil::TENSION_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(55, 195+down), module, Coil::INERTIA_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(90, 195+down), module, Coil::DAMP_CV));

        // Row 4: Audio IO & Pluck
        addInput(createInputCentered<InPortAutinn>(Vec(20, 310), module, Coil::SIGNAL_LEFT_INPUT));
        addInput(createInputCentered<InPortAutinn>(Vec(55, 310), module, Coil::SIGNAL_RIGHT_INPUT));
        
        //addInput(createInputCentered<PJ301MPort>(Vec(90, 250), module, Coil::PLUCK_INPUT));
        //addChild(createLightCentered<MediumLight<RedLight>>(Vec(115, 240), module, Coil::PLUCK_LIGHT)); // Light next to trigger

        addOutput(createOutputCentered<OutPortAutinn>(Vec(141.2-55, 310), module, Coil::SIGNAL_LEFT_OUTPUT));
        addOutput(createOutputCentered<OutPortAutinn>(Vec(141.2-20, 310), module, Coil::SIGNAL_RIGHT_OUTPUT));
    }
};


Model* modelCoil = createModel<Coil, CoilWidget>("Coil");