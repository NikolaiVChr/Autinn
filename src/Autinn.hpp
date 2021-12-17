#include <rack.hpp>

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

static const float HALF_KNOB_MED   = 38*0.5;
static const float HALF_KNOB_SMALL = 28*0.5;
static const float HALF_KNOB_TINY  = 18*0.5;
static const float HALF_PORT       = 31.58*0.5;
static const float HALF_BUTTON     = 30*0.5;
static const float HALF_BUTTON_SMALL = 15*0.5;
static const float HALF_SLIDER     = 15*0.5;
static const float HALF_LIGHT_TINY   = mm2px(1.0)*0.5f;
static const float HALF_LIGHT_SMALL  = mm2px(2.0)*0.5f;// Was 6.4252f in Rack 1
static const float HALF_LIGHT_MEDIUM = mm2px(3.0)*0.5f;
static const float HALF_LIGHT_LARGE  = mm2px(5.0)*0.5f;

struct AutinnSlider : app::SvgSlider {
	AutinnSlider() {
		maxHandlePos = Vec(0,   0);
		minHandlePos = Vec(0, 270);
		background->svg = APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SliderAutinn.svg"));
		//background->wrap();
		background->box.pos = Vec(0, 0);
		background->box.size = Vec(15,300);
		box.size = background->box.size;
		handle->svg = APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SliderHandleAutinn.svg"));
		handle->box.size = Vec(15,30);
		handle->box.pos = Vec(0,15);
		//handle->wrap();
	}
};

struct AutinnSliderShort : app::SvgSlider {
	AutinnSliderShort() {
		maxHandlePos = Vec(0,   0);
		minHandlePos = Vec(0, 70);
		background->svg = APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SliderShortAutinn.svg"));
		//background->wrap();
		background->box.pos = Vec(0, 0);
		background->box.size = Vec(15,100);
		box.size = background->box.size;
		handle->svg = APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/SliderHandleAutinn.svg"));
		handle->box.size = Vec(15,30);
		handle->box.pos = Vec(0,15);
		//handle->wrap();
	}
};

struct RoundMediumAutinnKnob : RoundKnob {
	RoundMediumAutinnKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/KnobLargeAutinn.svg")));
		//box.size = Vec(38, 38);
	}
};

struct RoundSmallAutinnKnob : RoundKnob {
	RoundSmallAutinnKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/KnobSmallAutinn.svg")));
		//box.size = Vec(28, 28);
	}
};

struct RoundSmallAutinnSnapKnob : RoundKnob {
	RoundSmallAutinnSnapKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/KnobSmallAutinn.svg")));
		snap = true;
		//box.size = Vec(28, 28);
	}
};

struct RoundSmallTyrkAutinnKnob : RoundKnob {
	RoundSmallTyrkAutinnKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/KnobSmallAutinnTyrk.svg")));
		//box.size = Vec(28, 28);
	}
};

struct RoundSmallPinkAutinnKnob : RoundKnob {
	RoundSmallPinkAutinnKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/KnobSmallAutinnPink.svg")));
		//box.size = Vec(28, 28);
	}
};

struct RoundSmallYelAutinnKnob : RoundKnob {
	RoundSmallYelAutinnKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/KnobSmallAutinnYel.svg")));
		//box.size = Vec(28, 28);
	}
};

struct RoundTinyAutinnKnob : RoundKnob {
	RoundTinyAutinnKnob() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/RoundTinyAutinn.svg")));
		//box.size = Vec(18, 18);
	}
}; 

struct ScrewStarAutinn : SVGScrew {
	ScrewStarAutinn() {
		//setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/ScrewStarAutinn.svg")));
		sw->svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/ScrewStarAutinn.svg"));
		sw->wrap();
		box.size = sw->box.size;
	}
};

struct OutPortAutinn : SVGPort {
	OutPortAutinn() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/OutPortAutinn.svg")));
		shadow->opacity = 0.0;
		//background->svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/OutPortAutinn.svg"));
		//background->wrap();
		//box.size = background->box.size;
	}
};

struct InPortAutinn : SVGPort {
	InPortAutinn() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/InPortAutinn.svg")));
		shadow->opacity = 0.0;
		//background->svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/InPortAutinn.svg"));
		//background->wrap();
		//box.size = background->box.size;
	}
};

struct RoundButtonAutinn : app::SvgSwitch {
	RoundButtonAutinn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/RoundButtonAutinn.svg")));//up
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/RoundButtonAutinnDown.svg")));//depressed state
	}
};

struct RoundButtonSmallAutinn : app::SvgSwitch {
	RoundButtonSmallAutinn() {
		momentary = true;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/RoundButtonSmallAutinn.svg")));//up
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ComponentLibrary/RoundButtonSmallAutinnDown.svg")));//depressed state
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


float non_lin_func(float parm);
float non_lin_func2(float parm);//sinh
float slew(float input, float input_prev, float maxChangePerSec, float dt);

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