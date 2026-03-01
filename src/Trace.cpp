#include "Autinn.hpp"
#include <nanosvg.h>
#include <string>
#include <vector>
#include <algorithm>
#include <iomanip>

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

struct CubicBezier {
    rack::math::Vec p0, p1, p2, p3;
    float length;
};

inline rack::math::Vec evalBezier(const rack::math::Vec& p0, const rack::math::Vec& p1, const rack::math::Vec& p2, const rack::math::Vec& p3, float t) {
    const float u = 1.0f - t;
    const float tt = t * t;
    const float uu = u * u;
    const float uuu = uu * u;
    const float ttt = tt * t;

    return rack::math::Vec(
        uuu * p0.x + 3.0f * uu * t * p1.x + 3.0f * u * tt * p2.x + ttt * p3.x,
        uuu * p0.y + 3.0f * uu * t * p1.y + 3.0f * u * tt * p2.y + ttt * p3.y
    );
}

struct Trace : Module {
    enum ParamIds {
        PITCH_PARAM,
        SCALE_PARAM,
        ROTATE_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        CV_PITCH_INPUT,
        CV_SCALE_INPUT,
        CV_ROTATE_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        X_OUTPUT,
        Y_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        NUM_LIGHTS
    };

    static constexpr int MAX_SHAPES = 16;
    static constexpr int TABLE_SIZE = 2048;

    float xTable[MAX_SHAPES][TABLE_SIZE] = {};
    float yTable[MAX_SHAPES][TABLE_SIZE] = {};
    float phase[MAX_SHAPES] = {};
    int activeChannels = 1;
    std::string currentSvg = "";

    Trace() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(PITCH_PARAM, -4.0f, 2.0f, -3.0f, "Frequency", " Hz", 2.0f, dsp::FREQ_C4);
        configParam(SCALE_PARAM, 0.0f, 1.2f, 1.0f, "Scale");
        configParam(ROTATE_PARAM, -1.0f, 1.0f, 0.0f, "Rotate");

        configInput(CV_PITCH_INPUT, "1V/Oct CV");
        configInput(CV_SCALE_INPUT, "Scale CV");
        configInput(CV_ROTATE_INPUT, "5V/180deg Rotate CV");

        configOutput(X_OUTPUT, "X Axis");
        configOutput(Y_OUTPUT, "Y Axis");

        generateFallbackCircle();
    }

    void generateFallbackCircle() {
        activeChannels = 1;
        for (int i = 0; i < TABLE_SIZE; i++) {
            const float t = (float)i / TABLE_SIZE * 2.0f * (float)M_PI;
            xTable[0][i] = std::sin(t) * 5.0f;
            yTable[0][i] = std::cos(t) * 5.0f;
        }
    }

    void loadSvg(const std::string& svgData) {
        if (svgData.empty()) return;

        char* dataCopy = strdup(svgData.c_str());
        NSVGimage* image = nsvgParse(dataCopy, "px", 96.0f);

        if (!image) {
            free(dataCopy);
            return;
        }

        activeChannels = 0;
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;

        for (NSVGshape* shape = image->shapes; shape != nullptr && activeChannels < MAX_SHAPES; shape = shape->next) {

            // Collect all paths into a vector
            std::vector<NSVGpath*> sortedPaths;
            for (NSVGpath* path = shape->paths; path != nullptr; path = path->next) {
                sortedPaths.push_back(path);
            }

            // Sort paths from left to right
            std::sort(sortedPaths.begin(), sortedPaths.end(), [](const NSVGpath* a, const NSVGpath* b) {
                return a->bounds[0] < b->bounds[0];
            });

            std::vector<CubicBezier> segs;
            rack::math::Vec lastPt(0.f, 0.f), firstPt(0.f, 0.f);
            bool first = true;

            // Iterate over the sorted vector instead of the linked list
            for (NSVGpath* path : sortedPaths) {
                rack::math::Vec pathStart(path->pts[0], path->pts[1]);

                if (!first) {
                    rack::math::Vec ctrl1(lastPt.x + (pathStart.x - lastPt.x) * 0.33f, lastPt.y + (pathStart.y - lastPt.y) * 0.33f);
                    rack::math::Vec ctrl2(lastPt.x + (pathStart.x - lastPt.x) * 0.66f, lastPt.y + (pathStart.y - lastPt.y) * 0.66f);
                    segs.push_back({lastPt, ctrl1, ctrl2, pathStart, 0.0f});
                } else {
                    firstPt = pathStart;
                    first = false;
                }

                for (int i = 0; i < path->npts - 1; i += 3) {
                    const float* p = &path->pts[i * 2];
                    segs.push_back({
                        rack::math::Vec(p[0], p[1]), rack::math::Vec(p[2], p[3]),
                        rack::math::Vec(p[4], p[5]), rack::math::Vec(p[6], p[7]), 0.0f
                    });
                }
                lastPt = rack::math::Vec(path->pts[(path->npts - 1) * 2], path->pts[(path->npts - 1) * 2 + 1]);
            }

            if (segs.empty()) continue;

            // Close the loop back to the start
            rack::math::Vec ctrl1(lastPt.x + (firstPt.x - lastPt.x) * 0.33f, lastPt.y + (firstPt.y - lastPt.y) * 0.33f);
            rack::math::Vec ctrl2(lastPt.x + (firstPt.x - lastPt.x) * 0.66f, lastPt.y + (firstPt.y - lastPt.y) * 0.66f);
            segs.push_back({lastPt, ctrl1, ctrl2, firstPt, 0.0f});

            // Calc lengths and global bounds
            float totalLen = 0.0f;
            for (auto& seg : segs) {
                float len = 0.0f;
                rack::math::Vec prev = evalBezier(seg.p0, seg.p1, seg.p2, seg.p3, 0.0f);
                if (prev.x < minX) minX = prev.x; if (prev.x > maxX) maxX = prev.x;
                if (prev.y < minY) minY = prev.y; if (prev.y > maxY) maxY = prev.y;

                for (int i = 1; i <= 10; i++) {
                    rack::math::Vec curr = evalBezier(seg.p0, seg.p1, seg.p2, seg.p3, float(i) / 10.0f);
                    len += std::hypot(curr.x - prev.x, curr.y - prev.y);
                    if (curr.x < minX) minX = curr.x; if (curr.x > maxX) maxX = curr.x;
                    if (curr.y < minY) minY = curr.y; if (curr.y > maxY) maxY = curr.y;
                    prev = curr;
                }
                seg.length = len;
                totalLen += len;
            }

            // Sample into polyphonic channel
            int segIdx = 0;
            float lengthAccum = 0.0f;
            for (int i = 0; i < TABLE_SIZE; i++) {
                float targetLen = (float(i) / (float)TABLE_SIZE) * totalLen;
                while (segIdx < (int)segs.size() - 1 && lengthAccum + segs[segIdx].length < targetLen) {
                    lengthAccum += segs[segIdx].length;
                    segIdx++;
                }
                float t = (segs[segIdx].length > 0.0f) ? (targetLen - lengthAccum) / segs[segIdx].length : 0.0f;
                rack::math::Vec p = evalBezier(segs[segIdx].p0, segs[segIdx].p1, segs[segIdx].p2, segs[segIdx].p3, t);

                xTable[activeChannels][i] = p.x;
                yTable[activeChannels][i] = p.y;
            }
            activeChannels++;
        }

        currentSvg = createMinimalSvg(image);

        nsvgDelete(image);
        free(dataCopy);

        if (activeChannels == 0) {
            generateFallbackCircle();
            return;
        }

        // Global Normalization so shapes stay positioned relative to each other
        const float rangeX = maxX - minX;
        const float rangeY = maxY - minY;
        float maxRange = std::max(rangeX, rangeY);
        if (maxRange < 1e-4f) maxRange = 1.0f;
        const float centerX = (maxX + minX) / 2.0f;
        const float centerY = (maxY + minY) / 2.0f;

        for (int c = 0; c < activeChannels; c++) {
            for (int i = 0; i < TABLE_SIZE; i++) {
                xTable[c][i] = ((xTable[c][i] - centerX) / maxRange) * 10.0f;
                yTable[c][i] = -((yTable[c][i] - centerY) / maxRange) * 10.0f;
            }
        }
    }

    static std::string createMinimalSvg(NSVGimage* image) {
        std::stringstream ss;
        // 3 decimal places is plenty for scope coordinates
        ss << std::fixed << std::setprecision(3);
        ss << "<svg xmlns=\"http://www.w3.org/2000/svg\">";

        int active = 0;
        // Only save the shapes we actually render
        for (NSVGshape* shape = image->shapes; shape != nullptr && active < MAX_SHAPES; shape = shape->next) {
            ss << "<path d=\"";
            for (NSVGpath* path = shape->paths; path != nullptr; path = path->next) {
                // Move-To command (Start of path)
                ss << "M" << path->pts[0] << "," << path->pts[1] << " ";

                // Curve-To commands (Cubic Beziers)
                for (int i = 0; i < path->npts - 1; i += 3) {
                    float* p = &path->pts[i * 2];
                    ss << "C" << p[2] << "," << p[3] << " "
                       << p[4] << "," << p[5] << " "
                       << p[6] << "," << p[7] << " ";
                }
                if (path->closed) ss << "Z ";
            }
            ss << "\"/>";
            active++;
        }
        ss << "</svg>";
        return ss.str();
    }

    void process(const ProcessArgs& args) override {
        outputs[X_OUTPUT].setChannels(activeChannels);
        outputs[Y_OUTPUT].setChannels(activeChannels);

        float scaleKnob = params[SCALE_PARAM].getValue();
        float angleKnob = params[ROTATE_PARAM].getValue();

        for (int c = 0; c < activeChannels; c++) {
            const float cv = inputs[CV_PITCH_INPUT].getChannels() > c ? inputs[CV_PITCH_INPUT].getPolyVoltage(c) : inputs[CV_PITCH_INPUT].getVoltage();
            const float freq = dsp::FREQ_C4 * std::exp2f(params[PITCH_PARAM].getValue() + cv);

            phase[c] += freq * args.sampleTime;
            phase[c] -= std::floor(phase[c]);

            float rCV = inputs[CV_ROTATE_INPUT].getChannels() > c ? inputs[CV_ROTATE_INPUT].getPolyVoltage(c) : inputs[CV_ROTATE_INPUT].getVoltage();
            float angleRaw = angleKnob + rCV * 0.2f;
            float angleRads = -angleRaw * (float)M_PI;
            // Calculate trig once per sample
            float cosT = std::cos(angleRads);
            float sinT = std::sin(angleRads);

            float sCV = inputs[CV_SCALE_INPUT].getChannels() > c ? inputs[CV_SCALE_INPUT].getPolyVoltage(c) : inputs[CV_SCALE_INPUT].getVoltage();
            float scale = scaleKnob + sCV * 0.1f;
            scale = clamp(scale, 0.0f, 10.0f);

            const float floatIndex = phase[c] * TABLE_SIZE;
            const int index = (int)floatIndex;
            const float frac = floatIndex - (float)index;
            const int nextIndex = (index + 1) % TABLE_SIZE;

            const float xOut = xTable[c][index] + frac * (xTable[c][nextIndex] - xTable[c][index]);
            const float yOut = yTable[c][index] + frac * (yTable[c][nextIndex] - yTable[c][index]);

            float rotX = xOut * cosT - yOut * sinT;
            float rotY = xOut * sinT + yOut * cosT;

            outputs[X_OUTPUT].setVoltage(rotX * scale, c);
            outputs[Y_OUTPUT].setVoltage(rotY * scale, c);
        }
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "svg", json_string(currentSvg.c_str()));
        return root;
    }

    void dataFromJson(json_t* root) override {
        const json_t* svgJ = json_object_get(root, "svg");
        if (svgJ) {
            loadSvg(json_string_value(svgJ));
        }
    }
};

struct PasteSvgMenuItem : MenuItem {
    Trace* module{};
    void onAction(const event::Action& e) override {
        // Ask GLFW for the clipboard string using Rack's window pointer
        const char* clip = glfwGetClipboardString(APP->window->win);

        if (!clip) {
            //WARN("Clipboard is empty or inaccessible.");
            return;
        }

        std::string clipboard(clip);
        if (clipboard.find("<svg") != std::string::npos || clipboard.find("<path") != std::string::npos) {
            module->loadSvg(clipboard);
        } else {
            //WARN("Clipboard does not contain valid SVG data.");
        }
    }
};

struct ClearSvgMenuItem : MenuItem {
    Trace* module{};
    void onAction(const event::Action& e) override {
        module->currentSvg = "";
        module->generateFallbackCircle();
    }
};

struct TraceWidget : ModuleWidget {
    explicit TraceWidget(Trace* module) {
        setModule(module);
        
        setPanel(createPanel(asset::plugin(pluginInstance, "res/TraceModule.svg")));

        const float col1 = 3.f * RACK_GRID_WIDTH;
        const float col2 = 7.f * RACK_GRID_WIDTH;

        auto pitchKnob = createParamCentered<AutinnArcMidKnob>(Vec(col1, 100.0f), module, Trace::PITCH_PARAM);
        pitchKnob->setModulation(Trace::CV_PITCH_INPUT, [](float cv, float val, float att) {
            return val + cv;
        });
        addParam(pitchKnob);
        auto scaleKnob = createParamCentered<AutinnArcMidKnob>(Vec(col2, 100.0f), module, Trace::SCALE_PARAM);
        scaleKnob->setModulation(Trace::CV_SCALE_INPUT, [](float cv, float val, float att) {
            return clamp(val + (cv * 0.1f), 0.0f, 10.0f);
        });
        addParam(scaleKnob);

        auto rotKnob = createParamCentered<AutinnArcMidKnob>(Vec(col1, 220.0f), module, Trace::ROTATE_PARAM);
        rotKnob->setModulation(Trace::CV_ROTATE_INPUT, [](float cv, float val, float att) {
            return val + (cv * 0.2f);
        });
        addParam(rotKnob);

        addInput(createInputCentered<InPortAutinn>(Vec(col1, 160.0f), module, Trace::CV_PITCH_INPUT));
        addInput(createInputCentered<InPortAutinn>(Vec(col2, 160.0f), module, Trace::CV_SCALE_INPUT));
        addInput(createInputCentered<InPortAutinn>(Vec(col2, 220.0f), module, Trace::CV_ROTATE_INPUT));

        addOutput(createOutputCentered<OutPortAutinn>(Vec(col1, 300.0f+HALF_PORT), module, Trace::X_OUTPUT));
        addOutput(createOutputCentered<OutPortAutinn>(Vec(col2, 300.0f+HALF_PORT), module, Trace::Y_OUTPUT));
    }

    void appendContextMenu(Menu* menu) override {
        auto* module = dynamic_cast<Trace*>(this->module);
        assert(module);

        menu->addChild(new MenuSeparator());
        auto* pasteItem = createMenuItem<PasteSvgMenuItem>("Paste SVG from clipboard");
        pasteItem->module = module;
        menu->addChild(pasteItem);

        auto* clearItem = createMenuItem<ClearSvgMenuItem>("Clear SVG");
        clearItem->module = module;
        menu->addChild(clearItem);
    }
};

Model* modelTrace = createModel<Trace, TraceWidget>("Trace");