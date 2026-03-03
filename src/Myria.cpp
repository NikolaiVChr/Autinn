#include "Autinn.hpp"
#include <vector>
#include <string>
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

struct Myria : Module {
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
    bool scrambleChannels = false;
    int channelMap[16];

    float lastRootPitch = -1000.f;
    int lastType = -1;
    float lastSpread = -1000.f;
    int lastInversion = -1;
    bool lastScrambleChannels = false;

    struct PrecomputedChord {
        float pcs[12];
        int numPcs;
    };
    PrecomputedChord precomputedChords[20];

    Myria() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);
        configSwitch(TYPE_PARAM, 0.f, 19.f, 0.f, "Chord Type",{"Power","Major","Minor","Penta","Maj9","Min9","MinMaj9","Mu Major","Lydian+","Sus2/4","Quartal","Hendrix","Dream","Aug7","Whole","Diminish","Stravin","Cluster","Ghost","The End"});
        configParam<Param3Digits>(SPREAD_PARAM, 0.f, 1.f, 0.2f, "Spread");
        configParam(INV_PARAM, 0.f, 15.f, 0.f, "Inversion")->snapEnabled=true;

        configInput(ROOT_INPUT, "Root 1V/Oct");
        configInput(TYPE_CV, "Type CV");
        configInput(SPREAD_CV, "Spread CV");
        configInput(INV_CV, "Inversion CV");
        configOutput(POLY_OUTPUT, "16-Channel Poly Out");

        for (int i = 0; i < 16; i++) channelMap[i] = i;

        for (int c = 0; c < 20; c++) {
            std::vector<float> tempPcs;
            for (float interval : chordTable[c].intervals) {
                float pc = std::fmod(interval, 12.0f);
                bool duplicate = false;
                for (float existing : tempPcs) {
                    if (std::abs(existing - pc) < 0.1f) {
                        duplicate = true; break;
                    }
                }
                if (!duplicate) tempPcs.push_back(pc);
            }
            std::sort(tempPcs.begin(), tempPcs.end());
            precomputedChords[c].numPcs = tempPcs.size();
            for(int i = 0; i < tempPcs.size(); i++) {
                precomputedChords[c].pcs[i] = tempPcs[i];
            }
        }
    }
    void onReset(const ResetEvent& e) override {
        scrambleChannels = false;
        Module::onReset(e);
    }

    void reShuffle() {
        for (int i = 0; i < 16; i++) channelMap[i] = i;
        if (scrambleChannels) {
            for (int i = 15; i > 0; i--) {
                int j = (int)(random::uniform() * (i + 1));
                std::swap(channelMap[i], channelMap[j]);
            }
        }
    }

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "scrambleChannels", json_boolean(scrambleChannels));

        json_t* mapJ = json_array();
        for (int i = 0; i < 16; i++) {
            json_array_append_new(mapJ, json_integer(channelMap[i]));
        }
        json_object_set_new(rootJ, "channelMap", mapJ);
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* scrambleJ = json_object_get(rootJ, "scrambleChannels");
        if (scrambleJ) scrambleChannels = json_is_true(scrambleJ);

        json_t* mapJ = json_object_get(rootJ, "channelMap");
        if (mapJ) {
            for (int i = 0; i < 16; i++) {
                json_t* itemJ = json_array_get(mapJ, i);
                if (itemJ) channelMap[i] = json_integer_value(itemJ);
            }
        }
    }

    void process(const ProcessArgs& args) override {
        const float rootPitch = inputs[ROOT_INPUT].getVoltage();

        const float typeRaw = params[TYPE_PARAM].getValue() + inputs[TYPE_CV].getVoltage()*2.0f;
        const int newType = clamp((int)typeRaw, 0, 19);

        const float spread = clamp(params[SPREAD_PARAM].getValue() + inputs[SPREAD_CV].getVoltage() * 0.1f, 0.f, 1.f);
        const int inversion = (int)(std::round(params[INV_PARAM].getValue()) + inputs[INV_CV].getVoltage()*1.5f) % 16;

        if (std::abs(rootPitch - lastRootPitch) < 1e-5f &&
                newType == lastType &&
                std::abs(spread - lastSpread) < 1e-5f &&
                inversion == lastInversion &&
                scrambleChannels == lastScrambleChannels) {

            outputs[POLY_OUTPUT].setChannels(16);
            for (int i = 0; i < 16; i++) {
                outputs[POLY_OUTPUT].setVoltage(outputPitches[i], i);
            }
            return;
        }

        lastRootPitch = rootPitch;
        lastType = newType;
        lastSpread = spread;
        lastInversion = inversion;
        lastScrambleChannels = scrambleChannels;
        currentType = newType;

        const auto& chord = precomputedChords[currentType];

        // Use a fixed stack array instead of std::vector
        float pool[256];
        int M = 0;

        // Generate the massive pool. Because octaves and pcs are both ascending,
        // the pool is naturally sorted. No std::sort required.
        for (int oct = -8; oct <= 8; oct++) {
            for (int i = 0; i < chord.numPcs; i++) {
                const float p = rootPitch + (chord.pcs[i] / 12.0f) + oct;
                if (p >= -4.0f && p < 5.0f) {
                    pool[M++] = p;
                }
            }
        }

        if (M < 16) {
            while (M < 16) {
                pool[M] = pool[M - 1] + 1.0f;
                M++;
            }
        }

        // Find the index of the note closest to the input Root Pitch
        int centerIdx = 0;
        float minDist = 100.0f;
        for (int i = 0; i < M; i++) {
            const float dist = std::abs(pool[i] - rootPitch);
            if (dist < minDist) {
                minDist = dist;
                centerIdx = i;
            }
        }

        // Define 3 Spread Settings
        int span = 16;
        if (spread > 0.33f && spread <= 0.66f) {
            span = std::max(16, (int)(16 + (M - 16) / 2.0f));
        } else if (spread > 0.66f) {
            span = M;
        }

        // Calculate starting index
        float startIndex = centerIdx - (span - 1) / 2.0f + inversion;
        if (startIndex < 0.0f) startIndex = 0.0f;
        if (startIndex + span > M) startIndex = M - span;

        // Pick 16 evenly spaced notes
        float tempPitches[16];
        const float step = (span - 1) / 15.0f;

        for (int i = 0; i < 16; i++) {
            int idx = std::round(startIndex + i * step);
            idx = clamp(idx, 0, M - 1);
            tempPitches[i] = pool[idx];
        }

        outputs[POLY_OUTPUT].setChannels(16);
        for (int i = 0; i < 16; i++) {
            int outIdx = scrambleChannels ? channelMap[i] : i;
            outputPitches[outIdx] = tempPitches[i];
            outputs[POLY_OUTPUT].setVoltage(tempPitches[i], outIdx);
        }
    }
};

struct BigDisplay : TransparentWidget {
    Myria* module;

    BigDisplay() : module(nullptr) {
    }

    void draw(const DrawArgs& args) override {
        // LCD background
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 5.0f);
        nvgFillColor(args.vg, nvgRGBA(10, 10, 10, 255));
        nvgFill(args.vg);

        if (!module) return;

        const std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));

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
            // Map -4V to 5V range (9 octaves) to the display width
            const float p = module->outputPitches[i];
            float x = ((p + 4.f) / 9.f) * (box.size.x - 4.0f);
            x = clamp(x + 2.0f, 2.f, box.size.x - 2.f);

            nvgBeginPath(args.vg);
            nvgRect(args.vg, x - 1.f, 25.f, 2.f, 10.f);
            nvgFillColor(args.vg, nvgRGBA(0, 255, 255, 100 + (i * 8)));
            nvgFill(args.vg);
        }
    }
};

struct ScrambleItem : MenuItem {
    Myria* module;
    void onAction(const event::Action& e) override {
        module->scrambleChannels = !module->scrambleChannels;
        module->reShuffle();
    }
    void step() override {
        rightText = module->scrambleChannels ? "✔" : "";
        MenuItem::step();
    }
};

struct BigWidget : ModuleWidget {
    explicit BigWidget(Myria* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/BigModule.svg")));

        // Display
        BigDisplay* display = createWidget<BigDisplay>(Vec(5.f, 50.f));
        display->box.size = Vec(110.f, 40.f);
        display->module = module;
        addChild(display);

        constexpr float centerX = 60.f;

        // Knobs
        const auto typeKnob = createParamCentered<AutinnArcMidKnob>(Vec(centerX, 130.f), module, Myria::TYPE_PARAM);
        typeKnob->setModulation(Myria::TYPE_CV, [](const float cv,const  float val, float att) {
            return (float)clamp(int(val + cv*2.0f), 0, 19);
        });
        addParam(typeKnob);
        const auto spreadKnob = createParamCentered<AutinnArcMidKnob>(Vec(30.f, 190.f), module, Myria::SPREAD_PARAM);
        spreadKnob->setModulation(Myria::SPREAD_CV, [](const float cv, const float val, float att) {
            return clamp(val + (cv * 0.1f), 0.0f, 1.0f);
        });
        addParam(spreadKnob);
        const auto invKnob = createParamCentered<AutinnArcMidKnob>(Vec(90.f, 190.f), module, Myria::INV_PARAM);
        invKnob->setModulation(Myria::INV_CV, [](const float cv, const float val, float att) {
            return (int)(std::round(val) + cv*1.5f) % 16;
        });
        addParam(invKnob);

        // CV
        addInput(createInputCentered<InPortAutinn>(Vec(30.f, 240.f), module, Myria::TYPE_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(60.f, 240.f), module, Myria::SPREAD_CV));
        addInput(createInputCentered<InPortAutinn>(Vec(90.f, 240.f), module, Myria::INV_CV));

        addInput(createInputCentered<InPortAutinn>(Vec(30.f, 300.f+HALF_PORT), module, Myria::ROOT_INPUT));

        // Output
        addOutput(createOutputCentered<OutPortAutinn>(Vec(90.f, 300.f+HALF_PORT), module, Myria::POLY_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        Myria* module = dynamic_cast<Myria*>(this->module);
        if (!module) return;

        menu->addChild(new MenuEntry);
        ScrambleItem* item = new ScrambleItem;
        item->text = "Scramble Channel Routing";
        item->module = module;
        menu->addChild(item);
    }
};

Model* modelMyria = createModel<Myria, BigWidget>("Big");