#include "Autinn.hpp"
#include <cmath>

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

#define NOT_PLAYING -24
#define NUM_CHORDS 14
#define NUM_FINGERS 4

struct Chord : Module {
	enum ParamIds {
		//DIAL_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ROOT_INPUT,
		TRIGGER_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		CHORD_OUTPUT,
		FINGER_PLAYING_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		/*LOW_LIGHT,
		MID_LIGHT,
		HIGH_LIGHT,*/
		NUM_LIGHTS
	};

	Chord() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configBypass(ROOT_INPUT, CHORD_OUTPUT);
		configInput(ROOT_INPUT, "Root 1V/Oct");
		configInput(TRIGGER_INPUT, "Switch Chord Trigger.");
		configOutput(CHORD_OUTPUT, "Poly 1V/Oct chord");
		configOutput(FINGER_PLAYING_OUTPUT, "Poly CV. Active channels. Output from inactives is garbage.");

		/*configLight(HIGH_LIGHT, "Serious grinding going on.. ");
		configLight(MID_LIGHT, "Moderate filing.. ");
		configLight(LOW_LIGHT, "Hungry, feed me!  ");*/

		outputs[FINGER_PLAYING_OUTPUT].setChannels(NUM_FINGERS);
		outputs[CHORD_OUTPUT].setChannels(NUM_FINGERS);
	}

	int chords[NUM_CHORDS][4] = {
		{0,4,7,NOT_PLAYING},          // Major Triad
		{0,3,7,NOT_PLAYING},          // Minor Triad
		{0,4,8,NOT_PLAYING},          // Aug Triad
		{0,3,6,NOT_PLAYING},          // Dim Triad
		{0,7,NOT_PLAYING,NOT_PLAYING},// Power
		{0,8,NOT_PLAYING,NOT_PLAYING},// Aug Power
		{0,6,NOT_PLAYING,NOT_PLAYING},// Dim Power
		{0,-8,-5,NOT_PLAYING},        // Major Triad 1st inv
		{0,-9,-5,NOT_PLAYING},        // Minor Triad 1st inv
		{0,-8,-4,NOT_PLAYING},        // Aug Triad 1st inv
		{0,-9,-6,NOT_PLAYING},        // Dim Triad 1st inv
		{0,3,7,10},                   // Minor 7th
		//{0,4,7,11},                 // Major 7th (too dissonant)
		{0,4,7,10},                   // Dominant 7th
		{0,3,6, 9},                   // Diminished 7th
	};

	float semitone = 1.0f/12.0f;
	bool trig_prev = false;
	int chordIndex = 0;

	void process(const ProcessArgs &args) override;
};

void Chord::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[CHORD_OUTPUT].isConnected() || !outputs[ROOT_INPUT].isConnected()) {
		return;
	}

	bool trig = inputs[TRIGGER_INPUT].getVoltage() >= 1.0f;

	if (trig && !trig_prev) {
		chordIndex = (rand() % static_cast<int>(NUM_CHORDS));
	}

	float root = inputs[ROOT_INPUT].getVoltage();

	for (int finger = 0; finger < NUM_FINGERS; finger++) {
	    outputs[FINGER_PLAYING_OUTPUT].setVoltage(chords[chordIndex][finger] == NOT_PLAYING?0.0f:10.0f, finger);
	    outputs[CHORD_OUTPUT].setVoltage(root+semitone*chords[chordIndex][finger],finger);
	}

    trig_prev = trig;
}

struct ChordWidget : ModuleWidget {
	ChordWidget(Chord *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/RebelModule.svg")));

		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addOutput(createOutput<OutPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 125), module, Chord::FINGER_PLAYING_OUTPUT));
		addInput(createInput<InPortAutinn>(Vec(   5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 187), module, Chord::TRIGGER_INPUT));
		addInput(createInput<InPortAutinn>(Vec(   5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 248), module, Chord::ROOT_INPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(5 * RACK_GRID_WIDTH*0.5-HALF_PORT, 310), module, Chord::CHORD_OUTPUT));

		//addChild(createLight<MediumLight<RedLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 65), module, Chord::HIGH_LIGHT));
		//addChild(createLight<MediumLight<GreenLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 75), module, Chord::MID_LIGHT));
		//addChild(createLight<MediumLight<BlueLight>>(Vec(3 * RACK_GRID_WIDTH*0.5-9.378*0.5, 85), module, Chord::LOW_LIGHT));
	}
};

Model *modelChord = createModel<Chord, ChordWidget>("Chord");