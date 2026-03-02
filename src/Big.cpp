#include "Autinn.hpp"
#include <vector>
#include <string>

struct Big : Module {
    enum ParamIds {
        TYPE_PARAM,
        SPREAD_PARAM,
        INV_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        ROOT_INPUT,
        TYPE_CV,
        SPREAD_CV,
        INV_CV,
        NUM_INPUTS
    };
    enum OutputIds {
        POLY_OUTPUT,
        NUM_OUTPUTS
    };

    struct ChordDef {
        std::string name;
        std::vector<float> intervals;
    };

    std::vector<ChordDef> chordTable = {
        {"Power",      {0, 7, 12}},
        {"Major",      {0, 4, 7}},
        {"Minor",      {0, 3, 7}},
        {"Penta",      {0, 2, 4, 7, 9}},
        {"Maj9",       {0, 4, 7, 11, 14}},
        {"Min9",       {0, 3, 7, 10, 14}},
        {"MinMaj9",    {0, 3, 7, 11, 14}},
        {"Mu Major",   {0, 2, 4, 7}},
        {"Lydian+",    {0, 4, 6, 7, 11}},
        {"Sus2/4",     {0, 2, 5, 7}},
        {"Quartal",    {0, 5, 10, 15}},
        {"Hendrix",    {0, 4, 7, 10, 15}},
        {"Dream",      {0, 5, 7, 11, 14, 19}},
        {"Aug7",       {0, 4, 8, 10}},
        {"Whole",      {0, 2, 4, 6, 8, 10}},
        {"Diminish",   {0, 3, 6, 9}},
        {"Stravin",    {0, 1, 4, 7, 9}},
        {"Cluster",    {0, 1, 2, 3, 4, 5}},
        {"Ghost",      {0, 8, 13, 20}},
        {"The End",    {0, 2, 5, 8, 11, 14}}
    };

    int currentType = 0;
    float outputPitches[16] = {};

    Big() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);
        configSwitch(TYPE_PARAM, 0.f, 19.f, 0.f, "Chord Type",{"Power","Major","Minor","Penta","Maj9","Min9","MinMaj9","Mu Major","Lydian+","Sus2/4","Quartal","Hendrix","Dream","Aug7","Whole","Diminish","Stravin","Cluster","Ghost","The End"});
        configParam(SPREAD_PARAM, 0.f, 1.f, 0.2f, "Spread");
        configParam(INV_PARAM, 0.f, 15.f, 0.f, "Inversion");

        configInput(ROOT_INPUT, "Root 1V/Oct");
        configInput(TYPE_CV, "Type CV");
        configInput(SPREAD_CV, "Spread CV");
        configInput(INV_CV, "Inversion CV");
        configOutput(POLY_OUTPUT, "16-Channel Poly Out");
    }

    void process(const ProcessArgs& args) override {
        float root = inputs[ROOT_INPUT].getVoltage();
        
        float typeRaw = params[TYPE_PARAM].getValue() + inputs[TYPE_CV].getVoltage()*2.0f;
        currentType = clamp((int)typeRaw, 0, 19);

        float spread = clamp(params[SPREAD_PARAM].getValue() + inputs[SPREAD_CV].getVoltage() / 10.f, 0.f, 1.f);
        int inversion = (int)(params[INV_PARAM].getValue() + inputs[INV_CV].getVoltage()*8.0f/5.0f) % 16;

        const auto& scale = chordTable[currentType].intervals;
        int scaleSize = (int)scale.size();

        outputs[POLY_OUTPUT].setChannels(16);

        for (int i = 0; i < 16; i++) {
            int idx = (i + inversion) % 16;
            int noteInScale = idx % scaleSize;
            int octaveWrap = idx / scaleSize;

            float interval = scale[noteInScale] / 12.f;
            float octaveOffset = (float)octaveWrap * (1.0f + (spread * 3.0f));

            outputPitches[i] = root + interval + octaveOffset;
            outputs[POLY_OUTPUT].setVoltage(outputPitches[i], i);
        }
    }
};

struct BigDisplay : TransparentWidget {
    Big* module;
    std::shared_ptr<Font> font;

    BigDisplay() : module(nullptr) {
        font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
    }

    void draw(const DrawArgs& args) override {
        // LCD background
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 5.0f);
        nvgFillColor(args.vg, nvgRGBA(10, 10, 10, 255));
        nvgFill(args.vg);

        if (!module) return;

        // Draw Chord Name
        if (font) {
            nvgFontSize(args.vg, 14.0f);
            nvgFontFaceId(args.vg, font->handle);
            nvgFillColor(args.vg, nvgRGB(0, 255, 255)); // Cyan
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
            nvgText(args.vg, box.size.x / 2.f, 18.f, module->chordTable[module->currentType].name.c_str(), nullptr);
        }

        // 16-voice Heatmap
        for (int i = 0; i < 16; i++) {
            // Map -4V to 6V range to the display width
            float p = module->outputPitches[i];
            float x = ((p + 4.f) / 10.f) * (box.size.x-4.0f);
            x = clamp(x+2.0f, 2.f, box.size.x - 2.f);

            nvgBeginPath(args.vg);
            nvgRect(args.vg, x - 1.f, 25.f, 2.f, 10.f);
            nvgFillColor(args.vg, nvgRGBA(0, 255, 255, 100 + (i * 8)));
            nvgFill(args.vg);
        }
    }
};

struct BigWidget : ModuleWidget {
    BigWidget(Big* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/BigModule.svg")));

        // Display
        BigDisplay* display = createWidget<BigDisplay>(Vec(5, 50));
        display->box.size = Vec(110, 40);
        display->module = module;
        addChild(display);

        float centerX = 60.f;

        // Knobs
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(centerX, 130), module, Big::TYPE_PARAM));
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(40, 190), module, Big::SPREAD_PARAM));
        addParam(createParamCentered<RoundMediumAutinnKnob>(Vec(80, 190), module, Big::INV_PARAM));

        // CV
        addInput(createInputCentered<InPortAutinn>(Vec(30, 240), module, Big::TYPE_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(60, 240), module, Big::SPREAD_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(90, 240), module, Big::INV_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(centerX, 300+HALF_PORT), module, Big::ROOT_INPUT));

        // Output
        addOutput(createOutputCentered<OutPortAutinn>(Vec(centerX, 330), module, Big::POLY_OUTPUT));
    }
};

Model* modelBig = createModel<Big, BigWidget>("Big");