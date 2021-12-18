#include "Autinn.hpp"
#include <cmath>

#include <vector>
#include <algorithm> // copy(), assign()
#include <iterator> // back_inserter()
#include <sys/time.h>
#include <random>


//#include <iostream>
//#include <string>

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

#define PHRASE_LENGTH_MIN 4
#define PHRASE_LENGTH_DEFAULT 8
#define PHRASE_LENGTH_MAX 32
#define PHRASE_LENGTH_THAT_DEMANDS_RESOLUTION 6
#define PHRASE_LENGTH_THAT_DEMANDS_CADENCE 10
#define GAP_STACCATISSIMO 0.40f
#define GAP_STACCATO 0.60f
#define GAP_NORMAL 0.87f
#define GAP_LEGATO 0.99f
#define GLIDE_MAXIMUM 0.060f
#define NUMBER_OF_MODES 12
#define REST_MAX 10
#define TONIC_MIN 60
#define TONIC_MAX 71

struct Melody : Module {
	enum ParamIds {
		TONIC_PARAM,
		MODE_PARAM,
		BUTTON_GENERATE_PARAM,
		PHRASE_PARAM,
		GAP_PARAM,
		ACCENT_PARAM,
		GLIDE_PARAM,
		REST_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT,
		GENERATE_INPUT,
		CV_TONIC_INPUT,
		CV_MODE_INPUT,
		CV_PHRASE_INPUT,
		CV_GAP_INPUT,
		CV_ACCENT_INPUT,
		CV_GLIDE_INPUT,
		CV_REST_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		FREQ_OUTPUT,
		GATE_OUTPUT,
		ACCENT_OUTPUT,
		NEW_PHRASE_OUTPUT,
		START_PHRASE_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	Melody() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(Melody::TONIC_PARAM, TONIC_MIN, TONIC_MAX, 62, "Tonic", {"C", "C#", "D", "D#","E","F","F#","G","G#","A","A#","B"});
		configSwitch(Melody::MODE_PARAM, 0, NUMBER_OF_MODES-1, 4, "Mode", { "I: Major", "II: Dorian", "III: Phrygian", "IV: Lydian", "V: Mixolydian",
															"VI: Minor", "VII: Locrian", "Double Harmonic Major", "Double Harmonic Minor",
															"Hexatonic Blues", "Bebop Dominant", "Major Pentatonic"});
		configSwitch(Melody::GAP_PARAM, 0, 3, 2.50f, "Expression", {"Staccatissimo", "Staccato", "Normal", "Legato"});
		configButton(Melody::BUTTON_GENERATE_PARAM, "Generate new phrase from settings (will start when current phrase ends)");
		configParam(Melody::GLIDE_PARAM, 0, 100, 0, "Each note's chance of glide"," %", 0.0f, 1.0f);
		configParam(Melody::ACCENT_PARAM, 0, 100, 0, "Each note's chance of accent"," %", 0.0f, 1.0f);
		configParam(Melody::PHRASE_PARAM, PHRASE_LENGTH_MIN, PHRASE_LENGTH_MAX, PHRASE_LENGTH_DEFAULT, "Phrase length");
		configParam(Melody::REST_PARAM, 0, REST_MAX, 2, "Clock cycle rests before starting next phrase");
		configInput(CLOCK_INPUT, "Ext. Clock");
		configInput(GENERATE_INPUT, "Trigger Generate");
		configOutput(FREQ_OUTPUT, "1V/Oct");
		configOutput(GATE_OUTPUT, "Gate");
		configOutput(ACCENT_OUTPUT, "Accent");
		configBypass(CLOCK_INPUT, GATE_OUTPUT);
		configOutput(NEW_PHRASE_OUTPUT, "Starting new phrase");
		configOutput(START_PHRASE_OUTPUT, "Starting phrase");
		configInput(CV_MODE_INPUT, "Mode CV ±5V");
		configInput(CV_TONIC_INPUT, "Tonic CV ±5V");
		configInput(CV_PHRASE_INPUT, "Length CV ±5V");
		configInput(CV_GLIDE_INPUT, "Glides CV ±5V");
		configInput(CV_ACCENT_INPUT, "Accents CV ±5V");
		configInput(CV_GAP_INPUT, "Expression CV ±5V");
		configInput(CV_REST_INPUT, "Rest CV ±5V");

		int init_phrase[6] = {60,62,67,65,62,60};
		phrase.assign(init_phrase,init_phrase+6); 
		int init_phrase_dura[6] = {2,2,2,2,2,2};
		phraseDurations.assign(init_phrase_dura,init_phrase_dura+6);
		int init_phrase_acc[6] = {false,false,false,false,false,false};
		phraseAccents.assign(init_phrase_acc,init_phrase_acc+6);
		int init_phrase_glide[6] = {false,false,false,false,false,false};
		phraseGlides.assign(init_phrase_glide,init_phrase_glide+6);

		generator = std::mt19937_64(device());

		this->generateMelody();
	}

	json_t *dataToJson() override {
	    json_t *root = json_object();

	    json_t *sequence_json_array = json_array();
	    for(int note : phrase) {
	        json_array_append_new(sequence_json_array, json_integer(note));
	    }
	    json_object_set(root, "sequence", sequence_json_array);
	    json_decref(sequence_json_array);

	    json_t *durations_json_array = json_array();
	    for(int dura : phraseDurations) {
	        json_array_append_new(durations_json_array, json_integer(dura));
	    }
	    json_object_set(root, "durations", durations_json_array);
	    json_decref(durations_json_array);

	    json_t *accents_json_array = json_array();
	    for(bool acc : phraseAccents) {
	        json_array_append_new(accents_json_array, json_boolean(acc));
	    }
	    json_object_set(root, "accents", accents_json_array);
	    json_decref(accents_json_array);

	    json_t *glides_json_array = json_array();
	    for(bool glide : phraseGlides) {
	        json_array_append_new(glides_json_array, json_boolean(glide));
	    }
	    json_object_set(root, "glides", glides_json_array);
	    json_decref(glides_json_array);

	    json_object_set_new(root, "gap", json_real(gap));

	    json_object_set_new(root, "rest", json_integer(rest_amount));

	    return root;
	}

	void dataFromJson(json_t *root) override
	{
	    json_t *sequence_json_array = json_object_get(root, "sequence");
	    if(sequence_json_array) {
			phrase.resize(0);
			size_t i;
			json_t *json_int;

			json_array_foreach(sequence_json_array, i, json_int) {
			    phrase.push_back(json_integer_value(json_int));
			}
	    }

	    json_t *durations_json_array = json_object_get(root, "durations");
	    if(durations_json_array) {
			phraseDurations.resize(0);
			size_t i;
			json_t *json_int;

			json_array_foreach(durations_json_array, i, json_int) {
			    phraseDurations.push_back(json_integer_value(json_int));
			}
	    }

	    json_t *accents_json_array = json_object_get(root, "accents");
	    if(accents_json_array) {
			phraseAccents.resize(0);
			size_t i;
			json_t *json_bool;

			json_array_foreach(accents_json_array, i, json_bool) {
			    phraseAccents.push_back(json_boolean_value(json_bool));
			}
	    }

	    json_t *glides_json_array = json_object_get(root, "glides");
	    if(glides_json_array) {
			phraseGlides.resize(0);
			size_t i;
			json_t *json_bool;

			json_array_foreach(glides_json_array, i, json_bool) {
			    phraseGlides.push_back(json_boolean_value(json_bool));
			}
	    }

	    json_t *ext = json_object_get(root, "gap");
		if (ext) {
			gap = float(json_real_value(ext));
		}

		json_t *ext2 = json_object_get(root, "rest");
		if (ext2) {
			rest_amount = json_integer_value(ext2);
		}

		if (phraseDurations.size() != phrase.size() || phraseAccents.size() != phrase.size() || phraseGlides.size() != phrase.size() || phrase.size() < PHRASE_LENGTH_MIN) {
			// Illegal Json, we generate new phrase instead
	    	this->generateMelody();
	    	phrase_length = fmin(phrase.size(), phraseDurations.size());// fmin to prevent index being bigger than any of the vectors.
	    } else {
	    	phrase_length = phrase.size();
	    	nextPhrase.resize(0);// else it will switch to constructor generated one, right after loading json.
	    	nextPhraseDurations.resize(0);
	    	nextPhraseAccents.resize(0);
	    	nextPhraseGlides.resize(0);
	    }
	    phrase_index = 0;
	}

	int c4 = 60;
	
	// Intervals [modes][intervals]
	std::vector<int> modes[NUMBER_OF_MODES] = { {2,2,1,2,2,2,1},  //   I Major
												{2,1,2,2,2,1,2},  //  II Dorian
												{1,2,2,2,1,2,2},  // III Phrygian
												{2,2,2,1,2,2,1},  //  IV Lydian
												{2,1,2,2,2,1,2},  //   V Mixolydian
												{2,1,2,2,1,2,2},  //  VI Minor
												{1,2,2,1,2,2,2},  // VII Locrian
												{1,3,1,2,1,3,1},  //   Double Harmonic Major
												{2,1,3,1,1,3,1},  //   Double Harmonic Minor
												{3,2,1,1,3,2},    //   Hexatonic Blues
												{2,2,1,2,2,1,1,1},//   Bebop dominant
												{2,2,3,2,3}       //   Major Pentatonic
											  };

	int phrase_length = 6;
	int next_phrase_length = 0;

	std::vector<int> phrase;
	std::vector<int> nextPhrase;
	std::vector<int> phraseDurations;
	std::vector<int> nextPhraseDurations;
	std::vector<bool> phraseAccents;
	std::vector<bool> nextPhraseAccents;
	std::vector<bool> phraseGlides;
	std::vector<bool> nextPhraseGlides;
	
	int phrase_index = 0;
	bool clockExt_prev = false;
	long int clockCount = 0;
	long int clockCount_last = 0;
	int passedClocks = 0;
	float gap = GAP_NORMAL;
	float nextGap = gap;
	bool generate_prev = false;
	int resting = 0;
	int rest_amount = 0;
	long int stepCounter = 0;


	std::random_device device;
	std::mt19937_64 generator;	

	//float note2freq (int note);
	//float freq2vPoct (float freq);
	float note2vPoct (int note);
	int getSemiNoteOffset (int steps, int referenceIndex, std::vector<int> mode);
	//int getModeIndex (int note, int reference, int referenceIndex, std::vector<int> mode);
	void generateMelody ();
	//int attenuvertInt(int CV, int KNOB, float min_result, float max_result);
	void attenuvert(int CV, int KNOB, float min_result, float max_result);
	void attenuvertFloat(int CV, int KNOB, float min_result, float max_result);

	void onReset(const ResetEvent& e) override {
		// Later might think of something needed here
		Module::onReset(e);
	}

	void onRandomize(const RandomizeEvent& e) override {
		// Later might think of something needed here
		Module::onRandomize(e);
	}

	void process(const ProcessArgs &args) override;
};

void Melody::process(const ProcessArgs &args) {
	// VCV Rack audio rate is +-5V
	// VCV Rack CV is +-5V or 0V-10V

	if (!outputs[FREQ_OUTPUT].isConnected()) {
		return;
	}
	stepCounter++;
	if (stepCounter > 512) {
		stepCounter = 0;
	}

	
	bool clockExt = inputs[CLOCK_INPUT].getVoltage() >= 1.0f;
	bool generate = params[BUTTON_GENERATE_PARAM].getValue() >= 1.0f || inputs[GENERATE_INPUT].getVoltage() >= 1.0f;
	float start = 0.0f;
	float newStart = 0.0f;

	if (clockExt && !clockExt_prev) {
		if (resting == 0) {
			if (passedClocks >= phraseDurations[phrase_index]-1) {
				passedClocks = 0;
				phrase_index++;
			} else {
				passedClocks++;
			}
		} else {
			resting--;
		}
		if (phrase_index > phrase_length - 1) {
			start = 10.0f;
			phrase_index = 0;
			if(nextPhrase.size() > 0) {
				newStart = 10.0f;
				// Switching to next phrase
				phrase.resize(0);
				std::copy(nextPhrase.begin(), nextPhrase.end(), std::back_inserter(phrase));
				phraseDurations.resize(0);
				std::copy(nextPhraseDurations.begin(), nextPhraseDurations.end(), std::back_inserter(phraseDurations));
				phraseAccents.resize(0);
				std::copy(nextPhraseAccents.begin(), nextPhraseAccents.end(), std::back_inserter(phraseAccents));
				phraseGlides.resize(0);
				std::copy(nextPhraseGlides.begin(), nextPhraseGlides.end(), std::back_inserter(phraseGlides));
				phrase_length = next_phrase_length;
				nextPhrase.resize(0);
				gap = nextGap;
			}
			if (rest_amount > 0) {
				resting = rest_amount;
			}
		}
		clockCount_last = clockCount;
		clockCount = 0;
		
		outputs[START_PHRASE_OUTPUT].setVoltage(start);
		outputs[NEW_PHRASE_OUTPUT].setVoltage(newStart);
	} else {
		clockCount++;
		if (clockCount > 10000000) {
			clockCount = 0;
		}
	}

	if (stepCounter == 512) {
		this->attenuvert(CV_TONIC_INPUT, TONIC_PARAM, TONIC_MIN, TONIC_MAX+0.99f);// Add almost 1 to give it a chance of being selected
		this->attenuvert(CV_MODE_INPUT, MODE_PARAM, 0, NUMBER_OF_MODES-0.001f);
		this->attenuvert(CV_PHRASE_INPUT, PHRASE_PARAM, PHRASE_LENGTH_MIN, PHRASE_LENGTH_MAX+0.99f);
		this->attenuvert(CV_ACCENT_INPUT, ACCENT_PARAM, 0, 100.99f);
		this->attenuvert(CV_GLIDE_INPUT, GLIDE_PARAM, 0, 100.99f);
		this->attenuvert(CV_REST_INPUT, REST_PARAM, 0, REST_MAX+0.99f);
		this->attenuvertFloat(CV_GAP_INPUT, GAP_PARAM, 0, 3);
	}

	if (generate && !generate_prev) {
		this->generateMelody();
	}

	float out = this->note2vPoct(phrase[phrase_index]);
	if (!phraseGlides[phrase_index] || passedClocks > 0) {
		if (resting == 0) {
			// Only if not between phrase do we set voltage, so that previous voltage can be allowed to 'decay' if envelope is put on output.
			outputs[FREQ_OUTPUT].setVoltage(out);
		}
	} else {
		int phrase_index_prev = phrase_index - 1;
		if (phrase_index_prev < 0) phrase_index_prev = phrase_length - 1;
		float out_prev = this->note2vPoct(phrase[phrase_index_prev]);
		outputs[FREQ_OUTPUT].setVoltage(clampSafe(rescale(clockCount, 0, fmin(float(double(clockCount_last)*gap), GLIDE_MAXIMUM/args.sampleTime), out_prev, out), out_prev, out));// 60ms glide at start of note
	}
	outputs[ACCENT_OUTPUT].setVoltage(float(phraseAccents[phrase_index])*10.0f);
	if (resting > 0 || (clockCount > clockCount_last*gap && passedClocks >= phraseDurations[phrase_index]-1)) {// Normal
		outputs[GATE_OUTPUT].setVoltage(0.0f);
	} else {
		outputs[GATE_OUTPUT].setVoltage(10.0f);
	}
	clockExt_prev = clockExt;
	generate_prev = generate;
}

void Melody::generateMelody () {
	/*
	 +Polarity determines steepness
	 +Even chance of up or down with constraint for hitting cadence
	 rests in phrase?
	 +rests between phrases
	 +note durations (no option)
	 +note gaps option
	 +slides option
	 +establish tonic
	 +resolution except on very short phrases
	 more constraints?
	 octave select? (VCV Octave does the job just fine)
	 more CVs? (which ones?)
	 more scales/modes?
	*/



	// Melody
	int tonic = int(params[TONIC_PARAM].getValue());
	std::vector<int> mode = modes[int(params[MODE_PARAM].getValue())];
	next_phrase_length = int(params[PHRASE_PARAM].getValue());
	int minOffset = -2;
	int maxOffset =  4;
	nextPhrase.resize(0);
	nextPhrase.push_back(tonic);
	int lastNote = tonic;
	int lastIndex = 0;
	int distanceToTonic = 0;
	int closure = next_phrase_length >= PHRASE_LENGTH_THAT_DEMANDS_RESOLUTION?-1:0;
	std::uniform_int_distribution<int> uniform_dist_e(12, 16);
	int stepsTillEstablish = uniform_dist_e(generator);
	//int direction = 0;
	//std::cout << "    :::: \n";
	//std::cout << "    :::: \n";
	//std::cout << "    :::: \n";
	for (int i = 1; i < next_phrase_length+closure; i++) {
		if (distanceToTonic > 4) maxOffset = 2;
		if (distanceToTonic > 3) maxOffset = 3;
		else maxOffset = 4;
		int maxClamp;
		int minClamp;
		if (closure == 0) {
			// Skipping resolution
			minClamp =  minOffset;
			maxClamp =  maxOffset;
		} else if (i == next_phrase_length-2 && next_phrase_length >= PHRASE_LENGTH_THAT_DEMANDS_CADENCE) {
			// We are at cadence in larger phrase
			maxClamp = std::min(maxOffset, -distanceToTonic+1);
			minClamp = std::max(minOffset, -distanceToTonic-1);
		} else if (next_phrase_length < PHRASE_LENGTH_THAT_DEMANDS_CADENCE) {
			// Small phrase target resolution
			int stepsLeft  = next_phrase_length-i; // steps left including tonic step
			int howFarDown = stepsLeft * minOffset; // How far towards tonic can we get from now till tonic (negative number)
			int howFarUp   = stepsLeft * maxOffset;
			int maxUp   = minOffset-(distanceToTonic+howFarDown);
			int maxDown = maxOffset-(howFarUp+distanceToTonic);
			maxClamp = std::min(maxOffset, maxUp);
			minClamp = std::max(minOffset, maxDown);
		} else if (stepsTillEstablish < next_phrase_length-i-1) {
			// Longer phrase target establish
			int stepsLeft  = stepsTillEstablish; // steps left including tonic step
			if (stepsTillEstablish == 1) stepsTillEstablish = uniform_dist_e(generator);
			int howFarDown = stepsLeft * minOffset; // How far towards tonic can we get from now till tonic (negative number)
			int howFarUp   = stepsLeft * maxOffset;
			int maxUp   = minOffset-(distanceToTonic+howFarDown);
			int maxDown = maxOffset-(howFarUp+distanceToTonic);
			maxClamp = std::min(maxOffset, maxUp);
			minClamp = std::max(minOffset, maxDown);
		} else {
			// Longer phrase target cadence
			int stepsLeft  = next_phrase_length-i-1; // steps left including cadence step
			int howFarDown = stepsLeft * minOffset; // How far towards cadence can we get from now till cadence (negative number)
			int howFarUp   = stepsLeft * maxOffset;
			int maxUp   = minOffset-((distanceToTonic-1)+howFarDown);// note the asymmetry here, as we can approach from either side.
			int maxDown = maxOffset-(howFarUp+(distanceToTonic+1));
			maxClamp = std::min(maxOffset, maxUp);
			minClamp = std::max(minOffset, maxDown);
		}
		// Using only maxOffset to make sure initial equal chance of either way, but clamp will restrict that afterwards:
		int maxiRand =  maxOffset;
		int miniRand = -maxOffset;
		//maxiRand = std::min(miniRand+1, direction>2?-1:maxiRand);
		std::uniform_int_distribution<int> uniform_dist(miniRand, maxiRand);
		int noteOffset = clamp(uniform_dist(generator), minClamp, maxClamp);// min + (rand() % static_cast<int>(max - min + 1)) [including min and max]
		int note = lastNote + getSemiNoteOffset(noteOffset, lastIndex, mode);
		nextPhrase.push_back(note);
		distanceToTonic += noteOffset;
		lastNote = note;
		lastIndex += noteOffset;
		//direction += std::min(1, std::max(-1, noteOffset));
		if (lastIndex < 0) lastIndex += mode.size();
		if (lastIndex > int(mode.size())-1) lastIndex -= mode.size();
		stepsTillEstablish--;
	}
	if (closure == -1) {
		nextPhrase.push_back(tonic);
	}

	// Durations
	nextPhraseDurations.resize(0);
	for (int i = 0; i < next_phrase_length; i++) {
		nextPhraseDurations.push_back(1+(rand() % static_cast<int>(2+1)));
	}

	// Accents
	float chance = int(params[ACCENT_PARAM].getValue());
	nextPhraseAccents.resize(0);
	for (int i = 0; i < next_phrase_length; i++) {
		nextPhraseAccents.push_back((rand() % static_cast<int>(100+1)) < int(chance));
	}

	// Glides
	float chance_g = int(params[GLIDE_PARAM].getValue());
	nextPhraseGlides.resize(0);
	for (int i = 0; i < next_phrase_length; i++) {
		nextPhraseGlides.push_back((rand() % static_cast<int>(100+1)) < int(chance_g));
	}

	// Rest
	rest_amount = int(params[REST_PARAM].getValue());

	// Gaps
	nextGap = rescale(params[GAP_PARAM].getValue(), 0, 3, GAP_STACCATISSIMO, GAP_LEGATO);
	/*switch(int(params[GAP_PARAM].getValue())) {
		case 0:
			nextGap = GAP_STACCATO;
			break;
		case 1:
			nextGap = GAP_NORMAL;
			break;
		case 2:
			nextGap = GAP_LEGATO;
			break;
	}*/
}

/*
int Melody::attenuvertInt(int CV, int KNOB, float min_result, float max_result) {
	int result;
	if (inputs[CV].isConnected()) {
		result = clamp(rescale(inputs[CV].getVoltage(), -5.0f, 5.0f, min_result, max_result), min_result, max_result);
		params[KNOB].setValue(result);
	} else {
		result = int(params[KNOB].getValue());
	}
	return result;
}*/

void Melody::attenuvert(int CV, int KNOB, float min_result, float max_result) {
	if (inputs[CV].isConnected()) {
		int result = clamp(rescale(inputs[CV].getVoltage(), -5.0f, 5.0f, min_result, max_result), min_result, max_result);
		params[KNOB].setValue(result);
	}
}

void Melody::attenuvertFloat(int CV, int KNOB, float min_result, float max_result) {
	if (inputs[CV].isConnected()) {
		float result = clamp(rescale(inputs[CV].getVoltage(), -5.0f, 5.0f, min_result, max_result), min_result, max_result);
		params[KNOB].setValue(result);
	}
}

int Melody::getSemiNoteOffset (int steps, int referenceIndex, std::vector<int> mode) {
	int indexMax = mode.size()-1;
	int index = referenceIndex;
	int semiOffset = 0;
	if (steps == 0) {
		semiOffset = 0;
	} else if (steps > 0) {
		while (steps > 0) {
			semiOffset += mode[index];
			index++;
			if (index > indexMax) {
				index = 0;
			}			
			steps--;
		}
	} else if (steps < 0) {
		while (steps < 0) {
			index--;
			if (index < 0) {
				index = indexMax;
			}
			semiOffset -= mode[index];
			steps++;
		}
	}
	return semiOffset;
}
/*
int Melody::getModeIndex (int note, int reference, int referenceIndex, std::vector<int> mode) {
	// Return index in mode array for 'note'.
	// Not used atm.
	int indexMax = 6;
	int index = referenceIndex;
	int noted = reference;
	if (note > reference) {
		while (noted < note) {
			index++;
			if (index > indexMax) {
				index = 0;
			}
			noted += mode[index];
		}
	} else if (note < reference) {
		while (noted > note) {
			index--;
			if (index < 0) {
				index = indexMax;
			}
			noted -= mode[index];
		}
	}
	return index;
}

float Melody::note2freq (int note) {
	return pow(2.0f, float(note-c4) / 12.0f) * dsp::FREQ_C4;
}

float Melody::freq2vPoct (float freq) {
	return log2(freq / dsp::FREQ_C4);
}*/

float Melody::note2vPoct (int note) {
	return float(note-c4) / 12.0f;
}

struct MelodyWidget : ModuleWidget {
	MelodyWidget(Melody *module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/MelodyModule.svg")));

		// Screws
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStarAutinn>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Knobs
		addParam(createParam<RoundSmallAutinnSnapKnob>(Vec(16 * RACK_GRID_WIDTH*0.33-HALF_KNOB_SMALL, 85-HALF_KNOB_SMALL), module, Melody::TONIC_PARAM));
		addParam(createParam<RoundSmallAutinnSnapKnob>(Vec(16 * RACK_GRID_WIDTH*0.66-HALF_KNOB_SMALL, 85-HALF_KNOB_SMALL), module, Melody::MODE_PARAM));
		addParam(createParam<RoundSmallAutinnSnapKnob>(Vec(16 * RACK_GRID_WIDTH*0.33-HALF_KNOB_SMALL, 140-HALF_KNOB_SMALL), module, Melody::PHRASE_PARAM));
		addParam(createParam<RoundSmallAutinnKnob>(Vec(16 * RACK_GRID_WIDTH*0.66-HALF_KNOB_SMALL, 140-HALF_KNOB_SMALL), module, Melody::GAP_PARAM));
		addParam(createParam<RoundSmallAutinnSnapKnob>(Vec(16 * RACK_GRID_WIDTH*0.33-HALF_KNOB_SMALL, 190-HALF_KNOB_SMALL), module, Melody::ACCENT_PARAM));
		addParam(createParam<RoundSmallAutinnSnapKnob>(Vec(16 * RACK_GRID_WIDTH*0.66-HALF_KNOB_SMALL, 190-HALF_KNOB_SMALL), module, Melody::GLIDE_PARAM));
		addParam(createParam<RoundSmallAutinnSnapKnob>(Vec(16 * RACK_GRID_WIDTH*0.66-HALF_KNOB_SMALL, 240-HALF_KNOB_SMALL), module, Melody::REST_PARAM));

		// Button
		addParam(createParam<RoundButtonSmallAutinn>(Vec(16 * RACK_GRID_WIDTH*0.33-HALF_BUTTON_SMALL, 240-HALF_BUTTON_SMALL), module, Melody::BUTTON_GENERATE_PARAM));

		// CVs
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.15-HALF_PORT, 85-HALF_PORT), module, Melody::CV_TONIC_INPUT));
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.85-HALF_PORT, 85-HALF_PORT), module, Melody::CV_MODE_INPUT));
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.15-HALF_PORT, 140-HALF_PORT), module, Melody::CV_PHRASE_INPUT));
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.85-HALF_PORT, 140-HALF_PORT), module, Melody::CV_GAP_INPUT));
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.15-HALF_PORT, 190-HALF_PORT), module, Melody::CV_ACCENT_INPUT));
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.85-HALF_PORT, 190-HALF_PORT), module, Melody::CV_GLIDE_INPUT));
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.85-HALF_PORT, 240-HALF_PORT), module, Melody::CV_REST_INPUT));
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.15-HALF_PORT, 240-HALF_PORT), module, Melody::GENERATE_INPUT));

		// Ext. Clock
		addInput(createInput<InPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.15-HALF_PORT, 320), module, Melody::CLOCK_INPUT));		

		// Outputs
		addOutput(createOutput<OutPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.55-HALF_PORT, 270), module, Melody::START_PHRASE_OUTPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.75-HALF_PORT, 270), module, Melody::NEW_PHRASE_OUTPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.35-HALF_PORT, 320), module, Melody::FREQ_OUTPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.55-HALF_PORT, 320), module, Melody::ACCENT_OUTPUT));
		addOutput(createOutput<OutPortAutinn>(Vec(16 * RACK_GRID_WIDTH*0.75-HALF_PORT, 320), module, Melody::GATE_OUTPUT));
	}
};

Model *modelMelody = createModel<Melody, MelodyWidget>("Melody");