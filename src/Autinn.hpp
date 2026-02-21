#pragma once
#include <rack.hpp>
#include <functional> // Required for std::function

/*

    Autinn VCV Rack Plugin
    Copyright (C) 2021  Nikolai V. Chr.

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

using namespace rack;


extern Plugin *pluginInstance;

static constexpr float HALF_KNOB_MED   = 38*0.5;
static constexpr float HALF_KNOB_SMALL = 28*0.5;
static constexpr float HALF_KNOB_TINY  = 18*0.5;
static constexpr float HALF_PORT       = 31.58*0.5;
static constexpr float HALF_BUTTON     = 30*0.5;
static constexpr float HALF_BUTTON_SMALL = 15*0.5;
static constexpr float HALF_SLIDER     = 15*0.5;
static const float HALF_LIGHT_TINY   = mm2px(1.0f)*0.5f;
static const float HALF_LIGHT_SMALL  = mm2px(2.0f)*0.5f;// Was 6.4252f in Rack 1
static const float HALF_LIGHT_MEDIUM = mm2px(3.0f)*0.5f;
static const float HALF_LIGHT_LARGE  = mm2px(5.0f)*0.5f;

inline float px2mm(float px) {
	return px / (SVG_DPI / MM_PER_IN);
}

inline Vec px2mm(math::Vec px) {
	return px.div(SVG_DPI / MM_PER_IN);
}

struct AutinnSlider : app::SvgSlider {
	AutinnSlider() {
		if (!pluginInstance) return;
		maxHandlePos = Vec(0,   0);
		minHandlePos = Vec(0, 270);
		setBackgroundSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/SliderAutinn.svg")));
		//background->wrap();
		background->box.pos = Vec(0, 0);
		background->box.size = Vec(15,300);
		box.size = background->box.size;
		setHandleSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/SliderHandleAutinn.svg")));
		handle->box.size = Vec(15,30);
		handle->box.pos = Vec(0,15);
		//handle->wrap();
	}
};

struct AutinnSliderShort : app::SvgSlider {
	AutinnSliderShort() {
		if (!pluginInstance) return;
		maxHandlePos = Vec(0,   0);
		minHandlePos = Vec(0, 70);
		setBackgroundSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/SliderShortAutinn.svg")));
		//background->wrap();
		background->box.pos = Vec(0, 0);
		background->box.size = Vec(15,100);
		box.size = background->box.size;
		setHandleSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/SliderHandleAutinn.svg")));
		handle->box.size = Vec(15,30);
		handle->box.pos = Vec(0,15);
		//handle->wrap();
	}
};

struct RoundMediumAutinnKnob : RoundKnob {
	RoundMediumAutinnKnob() {
		if (pluginInstance) {
			setSvg(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/KnobLargeAutinn.svg")));
			//box.size = Vec(38, 38);
		}
	}
};

struct RoundSmallAutinnKnob : RoundKnob {
	RoundSmallAutinnKnob() {
		if (!pluginInstance) return;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/KnobSmallAutinn.svg")));
		//box.size = Vec(28, 28);
	}
};

struct RoundSmallAutinnSnapKnob : RoundKnob {
	RoundSmallAutinnSnapKnob() {
		if (!pluginInstance) return;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/KnobSmallAutinn.svg")));
		snap = true;
		//box.size = Vec(28, 28);
	}
};

struct RoundSmallTyrkAutinnKnob : RoundKnob {
	RoundSmallTyrkAutinnKnob() {
		if (!pluginInstance) return;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/KnobSmallAutinnTyrk.svg")));
		//box.size = Vec(28, 28);
	}
};

struct RoundSmallPinkAutinnKnob : RoundKnob {
	RoundSmallPinkAutinnKnob() {
		if (!pluginInstance) return;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/KnobSmallAutinnPink.svg")));
		//box.size = Vec(28, 28);
	}
};

struct RoundSmallYelAutinnKnob : RoundKnob {
	RoundSmallYelAutinnKnob() {
		if (!pluginInstance) return;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/KnobSmallAutinnYel.svg")));
		//box.size = Vec(28, 28);
	}
};
/*
struct RoundTinyAutinnKnob : RoundKnob {
	RoundTinyAutinnKnob() {
		if (!pluginInstance) return;
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/RoundTinyAutinn.svg")));
		//box.size = Vec(18, 18);
	}
};
*/

template <typename TBase>
struct AutinnArcKnob : TBase {
    int inputId = -1;
	int attenId = -1;
	float radiusOffset = 0.0f;
	float stroke = 1.0f;
	bool green = false;

    // This function defines how to calculate the Arc position
    // Arguments: (Current CV Voltage, Current Knob Value, Attenuverter Value)
    // Returns: The value where the Arc should end.
    std::function<float(float cv, float knobVal, float att)> calcModulation;

    AutinnArcKnob() {
        //minAngle = -0.83f * M_PI;
        //maxAngle =  0.83f * M_PI;

        // Default Behavior: Linear 1:1 (Knob + CV)
        calcModulation = [](float cv, float val, float att) { return val + cv; };
    }

    void setModulation(const int input, const std::function<float(float, float, float)>& customMath = nullptr, const int attenParam = -1) {
        inputId = input;
    	attenId = attenParam;
        if (customMath) {
            calcModulation = customMath;
        }
    }

    void drawLayer(const Widget::DrawArgs& args, int layer) override {
        TBase::drawLayer(args, layer);

    	if (this->getParamQuantity() == nullptr) {
    		return;
    	}
		bool CV = true;
    	if (inputId >= -1) {
    		if (!this->module || inputId < 0) return;

    		if (!this->module->inputs[inputId].isConnected()) {
    			return; // Don't draw the arc if there's no CV
    		}
    	} else {
    		CV = false;
    	}

        if (layer == 1) {
            float minVal = this->getParamQuantity()->getMinValue();
            float maxVal = this->getParamQuantity()->getMaxValue();
            float currentVal = this->getParamQuantity()->getValue();

            float cv = CV?this->module->inputs[inputId].getVoltage():0.0f;
        	float attenVal = (attenId != -1) ? this->module->params[attenId].getValue() : 1.0f;

        	// Calculate raw value first (Unclamped)
        	float rawModVal = calcModulation(cv, currentVal, attenVal);

        	if (!CV && rawModVal > maxVal+1.0f) {
        		return;
        	}

        	// Clamp it for the visual arc (so it stays on the knob)
        	float modVal = clamp(rawModVal, minVal, maxVal);

        	// Shift angles by -90 degrees so 0 aligns with 12 o'clock
        	float angleCurrent = rescale(currentVal, minVal, maxVal, this->minAngle, this->maxAngle) - M_PI / 2.0f;
        	float angleMod = rescale(modVal, minVal, maxVal, this->minAngle, this->maxAngle) - M_PI / 2.0f;

        	if (std::abs(angleCurrent - angleMod) > 0.001f) {
        		nvgBeginPath(args.vg);
        		float r = this->box.size.x * 0.5f + radiusOffset;
        		nvgArc(args.vg, this->box.size.x/2.0f, this->box.size.y/2.0f, r, angleCurrent, angleMod, (angleMod > angleCurrent) ? NVG_CW : NVG_CCW);
        		nvgStrokeWidth(args.vg, stroke);

        		if (rawModVal > maxVal || rawModVal < minVal) {
        			// Greater magnitude than knob limits -> Orange
        			nvgStrokeColor(args.vg, nvgRGBA(255, 110, 0, 255));
        		} else {
        			// Normal Color -> Gold (Matches Logo) / Green
        			if (green) {
        				nvgStrokeColor(args.vg, nvgRGBA(0, 255, 240, 255));
        			} else {
        				nvgStrokeColor(args.vg, nvgRGBA(255, 230, 100, 240));
        			}
        		}

        		nvgStroke(args.vg);
        	}
        }
    }
};

struct AutinnArcMidKnob : AutinnArcKnob<RoundMediumAutinnKnob> {
	AutinnArcMidKnob() { radiusOffset = -1.0f; stroke = 1.5f; green = false;}
};
struct AutinnArcSmallKnob : AutinnArcKnob<RoundSmallAutinnKnob> {
	AutinnArcSmallKnob() { radiusOffset = -2.8f; stroke = 1.2f; green = true;}
};

struct ScrewStarAutinn : ThemedSvgScrew {
	ScrewStarAutinn() {
		//if (!pluginInstance) return;
		//setSvg(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/ScrewStarAutinn.svg")));
		if (pluginInstance) {
			//INFO("ScrewStarAutinn: Loading string..");
			std::shared_ptr<Svg> s = Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/ScrewStarAutinn.svg"));
			if (s) {
				//INFO("ScrewStarAutinn: Loading SVG..");
				setSvg(s,s);
				//INFO("ScrewStarAutinn: Loaded SVG successfully");
			} else {
				//INFO("ScrewStarAutinn: Loaded SVG unsuccessfully");
			}
		}
		//setSvg(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/ScrewStarAutinn.svg")));
		/*
		sw->svg = Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/ScrewStarAutinn.svg"));
		sw->wrap();
		box.size = sw->box.size;
		*/
	}
};

struct OutPortAutinn : ThemedSvgPort {
	OutPortAutinn() {
		if (pluginInstance) {
			std::shared_ptr<Svg> svg = Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/OutPortAutinn.svg"));
			setSvg(svg,svg);
			shadow->opacity = 0.0;
			//background->svg = Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/OutPortAutinn.svg"));
			//background->wrap();
			//box.size = background->box.size;
		}
	}
};

struct InPortAutinn : ThemedSvgPort {
	InPortAutinn() {
		if (pluginInstance) {
			std::shared_ptr<Svg> svg = Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/InPortAutinn.svg"));
			setSvg(svg,svg);
			shadow->opacity = 0.0;
			//background->svg = Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/InPortAutinn.svg"));
			//background->wrap();
			//box.size = background->box.size;
		}
	}
};

struct RoundButtonAutinn : app::SvgSwitch {
	RoundButtonAutinn() {
		if (!pluginInstance) return;
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/RoundButtonAutinn.svg")));//up
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/RoundButtonAutinnDown.svg")));//depressed state
	}
};

struct RoundButtonSmallAutinn : app::SvgSwitch {
	RoundButtonSmallAutinn() {
		if (!pluginInstance) return;
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/RoundButtonSmallAutinn.svg")));//up
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/RoundButtonSmallAutinnDown.svg")));//depressed state
	}
};

struct RoundCVButtonSmallAutinn : app::SvgSwitch {
	RoundCVButtonSmallAutinn() {
		if (!pluginInstance) return;
		momentary = true;
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/RoundCVButtonSmallAutinn.svg")));//up
		addFrame(Svg::load(asset::plugin(pluginInstance, "res/ComponentLibrary/RoundCVButtonSmallAutinnDown.svg")));//depressed state
	}
};

/* px
medium knob       38.0
small knob        28.0
ports             31.58
RACK_GRID_WIDTH   15.0
RACK_GRID_HEIGHT 380.0
light small        6.4252
light medium       9.3780
light large       12.2835
light tiny         3.2126

px=mm*75.0/25.4=mm*2.95
**/

inline float tanh_fast_high(float x) {
	// 7 divisions in Lambert's continued fraction series expansion
	x = clamp(x, -4.97f, 4.97f);
	const float x2 = x * x;
	const float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
	const float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
	return a / b;
}

inline float tanh_fast_low(const float x) {
	// Padé-style approximant
	const float x_safe = clamp(x, -3.0f, 3.0f);
	const float x2 = x_safe * x_safe;
	return x_safe * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/**
 *
 * @param x between -1.0 and 1.0 only
 * @return
 */
inline float sinh_fast_low(const float x) {
	// 3rd-order Taylor series
	return x + (x * x * x) * 0.16666f;
}

/**
 *
 * @param x between -2.0 and 2.0 only
 * @return
 */
inline float sinh_fast_high(const float x) {
	// 5th-order Taylor series
	const float p2 = x * x;
	return x * (1.0f + p2 * (0.16666667f + p2 * 0.00833333f));
}

inline float sin_fast_low(const float radians) {
	// Multiply by 1 / (2 * pi) for speed
	float phase = radians * 0.159154943f;

	// Wrap phase to 0.0 - 1.0
	phase -= std::floor(phase);

	// Shift to -1.0 to +1.0
	const float x = phase * 2.0f - 1.0f;

	// Fast parabolic approximation
	const float sine = -4.0f * x * (1.0f - std::abs(x));

	return sine;
}

inline float sin_fast_high(const float radians) {
	// Multiply by 1 / (2 * pi) for speed
	float phase = radians * 0.159154943f;

	// Wrap phase to 0.0 - 1.0
	phase -= std::floor(phase);

	// Shift to -1.0 to +1.0
	const float x = phase * 2.0f - 1.0f;

	// Fast parabolic approximation
	float sine = -4.0f * x * (1.0f - std::abs(x));

	// Curve smoothing (drops THD from 4% to 0.2%)
	sine = 0.225f * (sine * std::abs(sine) - sine) + sine;

	return sine;
}

inline float interpolator(float mix, float a, float b) {
	// same as: a * (1.0f - mix) + b * mix;
	return a + mix * (b - a);
}

struct Param3Digits : ParamQuantity {
	int getDisplayPrecision() override {
		return 3;
	}
};

struct Param4Digits : ParamQuantity {
	int getDisplayPrecision() override {
		return 4;
	}
};

inline float slew(const float input, float input_prev, const float maxChangePerSec, const float dt) {
	float delta = input - input_prev;

	if(maxChangePerSec*dt < delta) {
		delta = maxChangePerSec*dt;
	}
	if(-maxChangePerSec*dt > delta) {
		delta = -maxChangePerSec*dt;
	}
	input_prev += delta;

	return input_prev;
}

////////////////////
// module widgets
////////////////////

extern Model *modelJette;
extern Model *modelFlora;
extern Model *modelOxcart;
extern Model *modelDeadband;
extern Model *modelDigi;
//extern Model *modelDirt;   // deprecated
extern Model *modelFlopper;
extern Model *modelAmp;
//extern Model *modelRails; //deprecated
//extern Model *modelAura; //deprecated
extern Model *modelDC;
extern Model *modelSjip;
extern Model *modelBass;
extern Model *modelSquare;
extern Model *modelSaw;
extern Model *modelBoomerang;
extern Model *modelVibrato;
extern Model *modelVectorDriver; //deprecated
extern Model *modelCVConverter;
extern Model *modelZod;
extern Model *modelTriBand;
extern Model *modelMixer6;
extern Model *modelNon;
extern Model *modelFil;
extern Model *modelNap;
extern Model *modelMelody;
extern Model *modelChord;
extern Model *modelKicker;
extern Model *modelSnare;
extern Model *modelCoil;
extern Model *modelGeiger;
extern Model *modelSaw2;
extern Model *modelScope;
extern Model *modelExcavi;