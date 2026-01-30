#include "Autinn.hpp"
#include <cmath>

#include <vector>
#include <algorithm> // copy(), assign()
#include <iterator> // back_inserter()
#include <sys/time.h>


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
#define NUMBER_OF_MODES 13
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

	std::vector<int> phrase[16];
	std::vector<int> nextPhrase[16];
	std::vector<int> phraseDurations[16];
	std::vector<int> nextPhraseDurations[16];
	std::vector<bool> phraseAccents[16];
	std::vector<bool> nextPhraseAccents[16];
	std::vector<bool> phraseGlides[16];
	std::vector<bool> nextPhraseGlides[16];

	int phrase_length[16];
	int next_phrase_length[16];
	int phrase_index[16] = {};

	bool clockExt_prev[16] = {};
	long int clockCount[16] = {};
	long int clockCount_last[16] = {};
	int passedClocks[16] = {};

	float gap[16];
	float nextGap[16];

	int resting[16] = {};
	int rest_amount[16] = {};

	bool generate_prev = false;
	long int stepCounter = 0;

	Melody() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(Melody::TONIC_PARAM, TONIC_MIN, TONIC_MAX, 62, "Tonic", {"C", "C#", "D", "D#","E","F","F#","G","G#","A","A#","B"});
		configSwitch(Melody::MODE_PARAM, 0, NUMBER_OF_MODES-1, 4, "Mode", { "I: Major", "II: Dorian", "III: Phrygian", "IV: Lydian", "V: Mixolydian",
															"VI: Minor", "VII: Locrian", "Double Harmonic Major", "Double Harmonic Minor",
															"Hexatonic Blues", "Bebop Dominant", "Major Pentatonic", "Chromatic"});
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
		int init_phrase_dura[6] = {2,2,2,2,2,2};
		int init_phrase_acc[6] = {false,false,false,false,false,false};
		int init_phrase_glide[6] = {false,false,false,false,false,false};

		for (int c = 0; c < 16; c++) {
			phrase[c].reserve(PHRASE_LENGTH_MAX);
			nextPhrase[c].reserve(PHRASE_LENGTH_MAX);
			phraseDurations[c].reserve(PHRASE_LENGTH_MAX);
			nextPhraseDurations[c].reserve(PHRASE_LENGTH_MAX);
			phraseAccents[c].reserve(PHRASE_LENGTH_MAX);
			nextPhraseAccents[c].reserve(PHRASE_LENGTH_MAX);
			phraseGlides[c].reserve(PHRASE_LENGTH_MAX);
			nextPhraseGlides[c].reserve(PHRASE_LENGTH_MAX);

			phrase[c].assign(init_phrase, init_phrase+6);
			phraseDurations[c].assign(init_phrase_dura, init_phrase_dura+6);
			phraseAccents[c].assign(init_phrase_acc, init_phrase_acc+6);
			phraseGlides[c].assign(init_phrase_glide, init_phrase_glide+6);

			phrase_length[c] = 6;
			gap[c] = GAP_NORMAL;
			nextGap[c] = GAP_NORMAL;
			rest_amount[c] = 2; // Default rest

			// Generate initial 'next' state
			this->generateMelody(c);
		}
	}

	json_t *dataToJson() override {
	    json_t *root = json_object();

		json_t *voicesJ = json_array();

		for (int c = 0; c < 16; c++) {
			json_t *voiceRoot = json_object();

			json_object_set_new(voiceRoot, "phrase_index", json_integer(phrase_index[c]));
			json_object_set_new(voiceRoot, "resting", json_integer(resting[c]));
			json_object_set_new(voiceRoot, "phrase_length", json_integer(phrase_length[c]));

			json_object_set_new(voiceRoot, "rest_amount", json_integer(rest_amount[c]));
			json_object_set_new(voiceRoot, "gap", json_real(gap[c]));

			json_t *phraseJ = json_array();
			for (int val : phrase[c]) json_array_append_new(phraseJ, json_integer(val));
			json_object_set_new(voiceRoot, "sequence", phraseJ);

			json_t *durJ = json_array();
			for (int val : phraseDurations[c]) json_array_append_new(durJ, json_integer(val));
			json_object_set_new(voiceRoot, "durations", durJ);

			json_t *accJ = json_array();
			for (bool val : phraseAccents[c]) json_array_append_new(accJ, json_integer((int)val));
			json_object_set_new(voiceRoot, "accents", accJ);

			json_t *glideJ = json_array();
			for (bool val : phraseGlides[c]) json_array_append_new(glideJ, json_integer((int)val));
			json_object_set_new(voiceRoot, "glides", glideJ);

			json_array_append_new(voicesJ, voiceRoot);
		}

	    return root;
	}

	void dataFromJson(json_t *root) override
	{
		// 1. Try to find the new Polyphonic format
		json_t *voicesJ = json_object_get(root, "voices");

		if (voicesJ) {
			for (int c = 0; c < 16; c++) {
                json_t *voiceRoot = json_array_get(voicesJ, c);
                if (!voiceRoot) continue;

                json_t *curr;

                curr = json_object_get(voiceRoot, "phrase_index");
                if (curr) phrase_index[c] = json_integer_value(curr);

                curr = json_object_get(voiceRoot, "resting");
                if (curr) resting[c] = json_integer_value(curr);

                curr = json_object_get(voiceRoot, "rest_amount");
                if (curr) rest_amount[c] = json_integer_value(curr);

                curr = json_object_get(voiceRoot, "phrase_length");
                if (curr) phrase_length[c] = json_integer_value(curr);

                curr = json_object_get(voiceRoot, "gap");
                if (curr) gap[c] = json_real_value(curr);

                // Load Vectors
                json_t *arr;

                arr = json_object_get(voiceRoot, "sequence");
                if (arr) {
                    phrase[c].clear();
                    size_t len = json_array_size(arr);
                    for (size_t i = 0; i < len; i++) phrase[c].push_back(json_integer_value(json_array_get(arr, i)));
                }

                arr = json_object_get(voiceRoot, "durations");
                if (arr) {
                    phraseDurations[c].clear();
                    size_t len = json_array_size(arr);
                    for (size_t i = 0; i < len; i++) phraseDurations[c].push_back(json_integer_value(json_array_get(arr, i)));
                }

                arr = json_object_get(voiceRoot, "accents");
                if (arr) {
                    phraseAccents[c].clear();
                    size_t len = json_array_size(arr);
                    for (size_t i = 0; i < len; i++) phraseAccents[c].push_back((bool)json_integer_value(json_array_get(arr, i)));
                }

                arr = json_object_get(voiceRoot, "glides");
                if (arr) {
                    phraseGlides[c].clear();
                    size_t len = json_array_size(arr);
                    for (size_t i = 0; i < len; i++) phraseGlides[c].push_back((bool)json_integer_value(json_array_get(arr, i)));
                }
            }
		} else {
			json_t *sequence_json_array = json_object_get(root, "sequence");
			if(sequence_json_array) {
				phrase[0].resize(0);
				size_t i;
				json_t *json_int;

				json_array_foreach(sequence_json_array, i, json_int) {
					phrase[0].push_back(json_integer_value(json_int));
				}
			}

			json_t *durations_json_array = json_object_get(root, "durations");
			if(durations_json_array) {
				phraseDurations[0].resize(0);
				size_t i;
				json_t *json_int;

				json_array_foreach(durations_json_array, i, json_int) {
					phraseDurations[0].push_back(json_integer_value(json_int));
				}
			}

			json_t *accents_json_array = json_object_get(root, "accents");
			if(accents_json_array) {
				phraseAccents[0].resize(0);
				size_t i;
				json_t *json_bool;

				json_array_foreach(accents_json_array, i, json_bool) {
					phraseAccents[0].push_back(json_boolean_value(json_bool));
				}
			}

			json_t *glides_json_array = json_object_get(root, "glides");
			if(glides_json_array) {
				phraseGlides[0].resize(0);
				size_t i;
				json_t *json_bool;

				json_array_foreach(glides_json_array, i, json_bool) {
					phraseGlides[0].push_back(json_boolean_value(json_bool));
				}
			}

			json_t *ext = json_object_get(root, "gap");
			if (ext) {
				gap[0] = float(json_real_value(ext));
			}

			json_t *ext2 = json_object_get(root, "rest");
			if (ext2) {
				rest_amount[0] = json_integer_value(ext2);
			}

			if (phraseDurations[0].size() != phrase[0].size() || phraseAccents[0].size() != phrase[0].size() || phraseGlides[0].size() != phrase[0].size() || phrase[0].size() < PHRASE_LENGTH_MIN) {
				// Illegal Json, we generate new phrase instead
				this->generateMelody(0);
				phrase_length[0] = fmin(phrase[0].size(), phraseDurations[0].size());// fmin to prevent index being bigger than any of the vectors.
			} else {
				phrase_length[0] = phrase[0].size();
				nextPhrase[0].resize(0);// else it will switch to constructor generated one, right after loading json.
				nextPhraseDurations[0].resize(0);
				nextPhraseAccents[0].resize(0);
				nextPhraseGlides[0].resize(0);
			}
			phrase_index[0] = 0;
		}
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
												{2,2,3,2,3},      //   Major Pentatonic
												{1,1,1,1,1,1,1,1,1,1,1,1} //   Chromatic
											  };

	//float note2freq (int note);
	//float freq2vPoct (float freq);
	float note2vPoct (int note);
	int getSemiNoteOffset (int steps, int referenceIndex, std::vector<int> mode);
	//int getModeIndex (int note, int reference, int referenceIndex, std::vector<int> mode);
	void generateMelody (int c);
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

	int active_voices = inputs[CLOCK_INPUT].getChannels();
	if (active_voices < 1) active_voices = 1;
	if (active_voices > 16) active_voices = 16;

	int clock_channels = inputs[CLOCK_INPUT].getChannels();

	outputs[FREQ_OUTPUT].setChannels(active_voices);
	outputs[GATE_OUTPUT].setChannels(active_voices);
	outputs[ACCENT_OUTPUT].setChannels(active_voices);
	outputs[START_PHRASE_OUTPUT].setChannels(active_voices);
	outputs[NEW_PHRASE_OUTPUT].setChannels(active_voices);

	stepCounter++;
	if (stepCounter > 512) {
		stepCounter = 0;
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

	bool generate = params[BUTTON_GENERATE_PARAM].getValue() >= 1.0f || inputs[GENERATE_INPUT].getVoltage() >= 1.0f;
	if (generate && !generate_prev) {
		for(int c = 0; c < active_voices; c++) {
			this->generateMelody(c);
		}
	}
	generate_prev = generate;

	for (int c = 0; c < active_voices; c++) {
		int clock_idx = (clock_channels > 1) ? c : 0;

		float clockVolt = inputs[CLOCK_INPUT].getPolyVoltage(clock_idx);
		bool clockExt = clockVolt >= 1.0f;
		float start = 0.0f;
		float newStart = 0.0f;

		if (clockExt && !clockExt_prev[c]) {
			if (resting[c] == 0) {
				if (passedClocks[c] >= phraseDurations[c][phrase_index[c]]-1) {
					passedClocks[c] = 0;
					phrase_index[c]++;
				} else {
					passedClocks[c]++;
				}
			} else {
				resting[c]--;
			}
			if (phrase_index[c] > phrase_length[c] - 1) {
				start = 10.0f;
				phrase_index[c] = 0;
				if(nextPhrase[c].size() > 0) {
					newStart = 10.0f;
					// Switching to next phrase
					// Safely copy data without triggering reallocation, so we can save json at same time this happens wihtout issues
					phrase[c].resize(nextPhrase[c].size());
					std::copy(nextPhrase[c].begin(), nextPhrase[c].end(), phrase[c].begin());

					phraseDurations[c].resize(nextPhraseDurations[c].size());
					std::copy(nextPhraseDurations[c].begin(), nextPhraseDurations[c].end(), phraseDurations[c].begin());

					phraseAccents[c].resize(nextPhraseAccents[c].size());
					std::copy(nextPhraseAccents[c].begin(), nextPhraseAccents[c].end(), phraseAccents[c].begin());

					phraseGlides[c].resize(nextPhraseGlides[c].size());
					std::copy(nextPhraseGlides[c].begin(), nextPhraseGlides[c].end(), phraseGlides[c].begin());

					phrase_length[c] = next_phrase_length[c];
					//nextPhrase.resize(0);
					gap[c] = nextGap[c];
				}
				if (rest_amount[c] > 0) {
					resting[c] = rest_amount[c];
				}
			}
			clockCount_last[c] = clockCount[c];
			clockCount[c] = 0;

			outputs[START_PHRASE_OUTPUT].setVoltage(start, c);
			outputs[NEW_PHRASE_OUTPUT].setVoltage(newStart, c);
		} else {
			clockCount[c]++;
			if (clockCount[c] > 10000000) {
				clockCount[c] = 0;
			}
		}

		float out = this->note2vPoct(phrase[c][phrase_index[c]]);
		if (!phraseGlides[c][phrase_index[c]] || passedClocks[c] > 0) {
			if (resting[c] == 0) {
				// Only if not between phrase do we set voltage, so that previous voltage can be allowed to 'decay' if envelope is put on output.
				outputs[FREQ_OUTPUT].setVoltage(out, c);
			}
		} else {
			int phrase_index_prev = phrase_index[c] - 1;
			if (phrase_index_prev < 0) phrase_index_prev = phrase_length[c] - 1;
			float out_prev = this->note2vPoct(phrase[c][phrase_index_prev]);
			float glideTime = fmin(float(double(clockCount_last[c])*gap[c]), GLIDE_MAXIMUM/args.sampleTime);
			if (glideTime > 0.0f) {
				outputs[FREQ_OUTPUT].setVoltage(clampSafe(rescale(clockCount[c], 0, glideTime, out_prev, out), out_prev, out), c);// 60ms glide at start of note
			} else {
				// prevent divide by zero
				outputs[FREQ_OUTPUT].setVoltage(out, c);
			}
		}
		outputs[ACCENT_OUTPUT].setVoltage(float(phraseAccents[c][phrase_index[c]])*10.0f, c);
		if (resting[c] > 0 || (clockCount[c] > clockCount_last[c]*gap[c] && passedClocks[c] >= phraseDurations[c][phrase_index[c]]-1)) {// Normal
			outputs[GATE_OUTPUT].setVoltage(0.0f, c);
		} else {
			outputs[GATE_OUTPUT].setVoltage(10.0f, c);
		}
		clockExt_prev[c] = clockExt;
	}
}

void Melody::generateMelody (int c) {
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
	next_phrase_length[c] = int(params[PHRASE_PARAM].getValue());
	int minOffset = -2;
	int maxOffset =  4;
	nextPhrase[c].clear(); // Keeps capacity, just sets size to 0
	nextPhrase[c].push_back(tonic);
	int lastNote = tonic;
	int lastIndex = 0;
	int distanceToTonic = 0;
	int closure = next_phrase_length[c] >= PHRASE_LENGTH_THAT_DEMANDS_RESOLUTION?-1:0;
	int stepsTillEstablish = 12 + (int)(rack::random::uniform() * 5.0f); // 12 to 16
	//int direction = 0;
	//std::cout << "    :::: \n";
	//std::cout << "    :::: \n";
	//std::cout << "    :::: \n";
	for (int i = 1; i < next_phrase_length[c]+closure; i++) {
		if (distanceToTonic > 4) maxOffset = 2;
		if (distanceToTonic > 3) maxOffset = 3;
		else maxOffset = 4;
		int maxClamp;
		int minClamp;
		if (closure == 0) {
			// Skipping resolution
			minClamp =  minOffset;
			maxClamp =  maxOffset;
		} else if (i == next_phrase_length[c]-2 && next_phrase_length[c] >= PHRASE_LENGTH_THAT_DEMANDS_CADENCE) {
			// We are at cadence in larger phrase
			maxClamp = std::min(maxOffset, -distanceToTonic+1);
			minClamp = std::max(minOffset, -distanceToTonic-1);
		} else if (next_phrase_length[c] < PHRASE_LENGTH_THAT_DEMANDS_CADENCE) {
			// Small phrase target resolution
			int stepsLeft  = next_phrase_length[c]-i; // steps left including tonic step
			int howFarDown = stepsLeft * minOffset; // How far towards tonic can we get from now till tonic (negative number)
			int howFarUp   = stepsLeft * maxOffset;
			int maxUp   = minOffset-(distanceToTonic+howFarDown);
			int maxDown = maxOffset-(howFarUp+distanceToTonic);
			maxClamp = std::min(maxOffset, maxUp);
			minClamp = std::max(minOffset, maxDown);
		} else if (stepsTillEstablish < next_phrase_length[c]-i-1) {
			// Longer phrase target establish
			int stepsLeft  = stepsTillEstablish; // steps left including tonic step
			if (stepsTillEstablish == 1) stepsTillEstablish = 12 + (int)(rack::random::uniform() * 5.0f);
			int howFarDown = stepsLeft * minOffset; // How far towards tonic can we get from now till tonic (negative number)
			int howFarUp   = stepsLeft * maxOffset;
			int maxUp   = minOffset-(distanceToTonic+howFarDown);
			int maxDown = maxOffset-(howFarUp+distanceToTonic);
			maxClamp = std::min(maxOffset, maxUp);
			minClamp = std::max(minOffset, maxDown);
		} else {
			// Longer phrase target cadence
			int stepsLeft  = next_phrase_length[c]-i-1; // steps left including cadence step
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
		int range = (maxiRand - (miniRand)) + 1; // 9
		int noteOffset = miniRand + (int)(rack::random::uniform() * range);
		int note = lastNote + getSemiNoteOffset(noteOffset, lastIndex, mode);
		nextPhrase[c].push_back(note);
		distanceToTonic += noteOffset;
		lastNote = note;
		lastIndex += noteOffset;
		//direction += std::min(1, std::max(-1, noteOffset));
		if (lastIndex < 0) lastIndex += mode.size();
		if (lastIndex > int(mode.size())-1) lastIndex -= mode.size();
		stepsTillEstablish--;
	}
	if (closure == -1) {
		nextPhrase[c].push_back(tonic);
	}

	nextPhraseDurations[c].clear();
	float chance = int(params[ACCENT_PARAM].getValue());
	nextPhraseAccents[c].clear();
	float chance_g = int(params[GLIDE_PARAM].getValue());
	nextPhraseGlides[c].clear();
	for (int i = 0; i < next_phrase_length[c]; i++) {
		nextPhraseDurations[c].push_back(1 + (int)(rack::random::uniform() * 2)); // 1 or 2
		nextPhraseAccents[c].push_back((rack::random::uniform() * 100.0f) < chance);
		nextPhraseGlides[c].push_back((rack::random::uniform() * 100.0f) < chance_g);
	}

	// Rest
	rest_amount[c] = int(params[REST_PARAM].getValue());

	// Gaps
	nextGap[c] = rescale(params[GAP_PARAM].getValue(), 0, 3, GAP_STACCATISSIMO, GAP_LEGATO);
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
		if (params[KNOB].getValue() != result) { 
	        params[KNOB].setValue(result); 
	    }
	} else {
		result = int(params[KNOB].getValue());
	}
	return result;
}*/

void Melody::attenuvert(int CV, int KNOB, float min_result, float max_result) {
	if (inputs[CV].isConnected()) {
		int result = clamp(rescale(inputs[CV].getVoltage(), -5.0f, 5.0f, min_result, max_result), min_result, max_result);
		if (params[KNOB].getValue() != result) { 
	        params[KNOB].setValue(result); 
	    }
	}
}

void Melody::attenuvertFloat(int CV, int KNOB, float min_result, float max_result) {
	if (inputs[CV].isConnected()) {
		float result = clamp(rescale(inputs[CV].getVoltage(), -5.0f, 5.0f, min_result, max_result), min_result, max_result);
		if (params[KNOB].getValue() != result) { 
	        params[KNOB].setValue(result); 
	    }
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
