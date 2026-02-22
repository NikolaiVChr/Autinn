#include "Autinn.hpp"
#include <cmath>

#include <vector>
#include <algorithm> // copy(), assign()
#include <iterator> // back_inserter()


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
		/// @brief Root note selection (0-11, C to B).
		TONIC_PARAM,
		/// @brief Musical mode selection (Major, Dorian, etc.).
		MODE_PARAM,
		/// @brief Manual trigger button to generate a new melody immediately.
		BUTTON_GENERATE_PARAM,
		/// @brief Target length for the generated phrase (4-32 steps).
		PHRASE_PARAM,
		/// @brief Note length/articulation (Staccato to Legato).
		GAP_PARAM,
		/// @brief Probability (0-100%) of a note being accented.
		ACCENT_PARAM,
		/// @brief Probability (0-100%) of a note sliding (glide).
		GLIDE_PARAM,
		/// @brief Number of beats to rest after a phrase finishes.
		REST_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		/// @brief External clock input (advances the sequence).
		CLOCK_INPUT,
		/// @brief Trigger input to generate a new melody (same as button).
		GENERATE_INPUT,
		/// @brief CV control for Tonic (added to parameter).
		CV_TONIC_INPUT,
		/// @brief CV control for Mode selection.
		CV_MODE_INPUT,
		/// @brief CV control for Phrase Length.
		CV_PHRASE_INPUT,
		/// @brief CV control for Gap/Articulation.
		CV_GAP_INPUT,
		/// @brief CV control for Accent probability.
		CV_ACCENT_INPUT,
		/// @brief CV control for Glide probability.
		CV_GLIDE_INPUT,
		/// @brief CV control for Rest amount.
		CV_REST_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		/// @brief 1V/Octave Pitch output.
		FREQ_OUTPUT,
		/// @brief Gate output (high while note is playing).
		GATE_OUTPUT,
		/// @brief Accent output (10V trigger/gate for accented notes).
		ACCENT_OUTPUT,
		/// @brief Trigger output when a *newly generated* phrase starts playing.
		NEW_PHRASE_OUTPUT,
		/// @brief Trigger output at the start of *every* phrase loop.
		START_PHRASE_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	/// @brief Buffer for the current phrase notes.
	std::vector<int> phrase[16] = {};

	/// @brief Buffer for the next phrase notes.
	///
	/// This vector is populated by the generator thread. When the current
	/// phrase finishes, these values are swapped into the active #phrase vector.
	std::vector<int> nextPhrase[16] = {};

	/// @brief Current phrase note durations, measured in clock steps.
	std::vector<int> phraseDurations[16] = {};
	/// @brief Next phrase note durations, measured in clock steps.
	std::vector<int> nextPhraseDurations[16] = {};
	/// @brief Current phrase note accent bools.
	std::vector<bool> phraseAccents[16] = {};
	/// @brief Next phrase note accent bools.
	std::vector<bool> nextPhraseAccents[16] = {};
	/// @brief Current phrase note glide bools.
	std::vector<bool> phraseGlides[16] = {};
	/// @brief If gliding mean keep gate open instead of making a rest before the sliding note
	bool gateOnWhenGliding = false;
	/// @brief When true, the note before the glide is always 1-clock duration.
	bool only1step = false;
	/// @brief Next phrase note glide bools.
	std::vector<bool> nextPhraseGlides[16] = {};
	/// @brief When true, then after generating a new phrase it will be started immediately.
	bool immediate = false;
	/// @brief When true, then generated new phrase this step.
	bool generated[16] = {};
	/// @brief Current phrase index in #phrase where we are currently playing
	int phrase_index[16] = {};
	/// @brief Schmitt trigger state for detecting the rising edge of the external clock input.
	//bool clockExt_prev[16] = {};
	dsp::SchmittTrigger clockTrigger[16];
	/// @brief Process steps counter tracking 'time' since the last clock pulse.
	/// Used to calculate the linear interpolation for Glide effects.
	long int frameCount[16] = {};
	/// @brief The total process steps duration of the previous clock cycle.
	/// Used as a reference to ensure Glide timing scales relative to the BPM.
	long int frameCount_last[16] = {};
	/// @brief Counter for how many clock ticks have passed during the current note.
	/// Compares against #phraseDurations to determine when to advance to the next note.
	int passedClocks[16] = {};
	/// @brief Current phrase gate ON times, expressed in fractions of a notes total clock ticks.
	float gap[16] = {};
	/// @brief Next phrase gaps (gate ON times, expressed in fractions of a notes total clock ticks).
	float nextGap[16] = {};
	/// @brief Current rest after phrase finished state countdown, expressed in clock ticks.
	int resting[16] = {};
	/// @brief Rest after the current phrase, expressed in clock ticks.
	int rest_amount[16] = {};
	/// @brief Pulse generator for the "Start Phrase" trigger output.
	/// Generates a 1ms pulse whenever the sequence loops back to index 0.
	dsp::PulseGenerator startPulse[16];
	/// @brief Pulse generator for the "New Phrase" trigger output.
	/// Generates a 1ms pulse only when a *newly generated* phrase begins playing.
	dsp::PulseGenerator newPhrasePulse[16];
	/// @brief State tracking for the "Generate" button/input to detect rising edges.
	/// Prevents the generator from triggering continuously while the button is held.
	//bool generate_prev = false;
	dsp::SchmittTrigger generateTriggerCV[16];
	dsp::BooleanTrigger generateTriggerButton;
	/// @brief Low-priority counter for throttling expensive parameter updates.
	/// Parameter `attenuvert` calculations only run when this reaches 512.
	long int stepCounter = 0;

	void initialize_melodies() {
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

			nextGap[c] = GAP_NORMAL;
			rest_amount[c] = 2; // Default rest
			phrase_index[c] = 0;
			passedClocks[c] = 0;
			resting[c] = 0;
			frameCount[c] = 0;
			frameCount_last[c] = 0;
			//clockExt_prev[c] = false;
			clockTrigger[c].reset();

			// Generate initial 'next' state
			this->generateMelody(c);
			switch_to_next_phrase(c);
		}
	}

	Melody() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(Melody::TONIC_PARAM, TONIC_MIN, TONIC_MAX, 62, "Tonic", {"C", "C#", "D", "D#","E","F","F#","G","G#","A","A#","B"});
		configSwitch(Melody::MODE_PARAM, 0, NUMBER_OF_MODES-1, 4, "Mode", { "I: Major", "II: Dorian", "III: Phrygian", "IV: Lydian", "V: Mixolydian",
															"VI: Minor", "VII: Locrian", "Double Harmonic Major", "Double Harmonic Minor",
															"Hexatonic Blues", "Bebop Dominant", "Major Pentatonic", "Chromatic"});
		configSwitch(Melody::GAP_PARAM, 0, 3, 2.50f, "Expression", {"Staccatissimo", "Staccato", "Normal", "Legato"});
		getParamQuantity(Melody::GAP_PARAM)->snapEnabled = false;
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

		initialize_melodies();
	}

#define JSON_POLY "voices"
#define JSON_REST_AMOUNT "rest"
#define JSON_SEQ "sequence"
#define JSON_DURAS "durations"
#define JSON_ACCENTS "accents"
#define JSON_GLIDES "glides"
#define JSON_GAP "gap"
//#define JSON_IDX_PHRASE "phrase_index"
//#define JSON_RESTING "resting"


	json_t *dataToJson() override {
	    json_t *root = json_object();

		json_object_set_new(root, "gateOnWhenGliding", json_boolean(gateOnWhenGliding));
		json_object_set_new(root, "only1stepBeforeGlide", json_boolean(only1step));
		json_object_set_new(root, "immediate", json_boolean(immediate));

		json_t *voicesJ = json_array();

		int active_voices = inputs[CLOCK_INPUT].getChannels();
		if (active_voices < 1) active_voices = 1;
		if (active_voices > 16) active_voices = 16;

		for (int c = 0; c < active_voices; c++) {
			json_t *voiceRoot = json_object();

			//json_object_set_new(voiceRoot, JSON_IDX_PHRASE, json_integer(phrase_index[c]));
			//json_object_set_new(voiceRoot, JSON_RESTING, json_integer(resting[c]));

			json_object_set_new(voiceRoot, JSON_REST_AMOUNT, json_integer(rest_amount[c]));
			json_object_set_new(voiceRoot, JSON_GAP, json_real(gap[c]));

			json_t *phraseJ = json_array();
			for (int val : phrase[c]) json_array_append_new(phraseJ, json_integer(val));
			json_object_set_new(voiceRoot, JSON_SEQ, phraseJ);

			json_t *durJ = json_array();
			for (int val : phraseDurations[c]) json_array_append_new(durJ, json_integer(val));
			json_object_set_new(voiceRoot, JSON_DURAS, durJ);

			json_t *accJ = json_array();
			for (bool val : phraseAccents[c]) json_array_append_new(accJ, json_integer((int)val));
			json_object_set_new(voiceRoot, JSON_ACCENTS, accJ);

			json_t *glideJ = json_array();
			for (bool val : phraseGlides[c]) json_array_append_new(glideJ, json_integer((int)val));
			json_object_set_new(voiceRoot, JSON_GLIDES, glideJ);

			json_array_append_new(voicesJ, voiceRoot);
		}

		json_object_set_new(root, JSON_POLY, voicesJ);

	    return root;
	}

	void dataFromJson(json_t *root) override
	{
		json_t *voicesJ = json_object_get(root, JSON_POLY);

		json_t *keepOpen = json_object_get(root, "gateOnWhenGliding");
		if (keepOpen)
			gateOnWhenGliding = json_boolean_value(keepOpen);
		else
			gateOnWhenGliding = false;

		json_t *only1 = json_object_get(root, "only1stepBeforeGlide");
		if (only1)
			only1step = json_boolean_value(only1);
		else
			only1step = false;

		json_t *imme = json_object_get(root, "immediate");
		if (imme)
			immediate = json_boolean_value(imme);
		else
			immediate = false;

		if (voicesJ) {
			for (int c = 0; c < 16; c++) {
                json_t *voiceRoot = json_array_get(voicesJ, c);
                if (!voiceRoot) continue;

                json_t *curr;
				/*
                curr = json_object_get(voiceRoot, JSON_IDX_PHRASE);
                if (curr) phrase_index[c] = json_integer_value(curr);

                curr = json_object_get(voiceRoot, JSON_RESTING);
                if (curr) resting[c] = json_integer_value(curr);
				*/
                curr = json_object_get(voiceRoot, JSON_REST_AMOUNT);
                if (curr) rest_amount[c] = json_integer_value(curr);

                curr = json_object_get(voiceRoot, JSON_GAP);
                if (curr) gap[c] = json_real_value(curr);

                // Load Vectors
                json_t *arr;

                arr = json_object_get(voiceRoot, JSON_SEQ);
                if (arr) {
                    phrase[c].clear();
                    size_t len = json_array_size(arr);
                    for (size_t i = 0; i < len; i++) phrase[c].push_back(json_integer_value(json_array_get(arr, i)));
                }

                arr = json_object_get(voiceRoot, JSON_DURAS);
                if (arr) {
                    phraseDurations[c].clear();
                    size_t len = json_array_size(arr);
                    for (size_t i = 0; i < len; i++) phraseDurations[c].push_back(json_integer_value(json_array_get(arr, i)));
                }

                arr = json_object_get(voiceRoot, JSON_ACCENTS);
                if (arr) {
                    phraseAccents[c].clear();
                    size_t len = json_array_size(arr);
                    for (size_t i = 0; i < len; i++) phraseAccents[c].push_back((bool)json_integer_value(json_array_get(arr, i)));
                }

                arr = json_object_get(voiceRoot, JSON_GLIDES);
                if (arr) {
                    phraseGlides[c].clear();
                    size_t len = json_array_size(arr);
                    for (size_t i = 0; i < len; i++) phraseGlides[c].push_back((bool)json_integer_value(json_array_get(arr, i)));
                }

				if (phraseDurations[c].size() != phrase[c].size() || phraseAccents[c].size() != phrase[c].size()
					|| phraseGlides[c].size() != phrase[c].size() || phrase[c].size() < PHRASE_LENGTH_MIN) {
					// Illegal Json, we generate new phrase instead
					this->generateMelody(c);
				} else {
					resting[c] = 0;
					nextPhrase[c].clear();// else it will switch to constructor generated one, right after loading json.
					nextPhraseDurations[c].clear();
					nextPhraseAccents[c].clear();
					nextPhraseGlides[c].clear();
					nextGap[c] = 0.0f;
				}
				phrase_index[c] = 0;
				passedClocks[c] = 0;
				frameCount[c] = 0;
            }
		} else {
			json_t *sequence_json_array = json_object_get(root, JSON_SEQ);
			if(sequence_json_array) {
				phrase[0].resize(0);
				size_t i;
				json_t *json_int;

				json_array_foreach(sequence_json_array, i, json_int) {
					phrase[0].push_back(json_integer_value(json_int));
				}
			}

			json_t *durations_json_array = json_object_get(root, JSON_DURAS);
			if(durations_json_array) {
				phraseDurations[0].resize(0);
				size_t i;
				json_t *json_int;

				json_array_foreach(durations_json_array, i, json_int) {
					phraseDurations[0].push_back(json_integer_value(json_int));
				}
			}

			json_t *accents_json_array = json_object_get(root, JSON_ACCENTS);
			if(accents_json_array) {
				phraseAccents[0].resize(0);
				size_t i;
				json_t *json_bool;

				json_array_foreach(accents_json_array, i, json_bool) {
					phraseAccents[0].push_back(json_boolean_value(json_bool));
				}
			}

			json_t *glides_json_array = json_object_get(root, JSON_GLIDES);
			if(glides_json_array) {
				phraseGlides[0].resize(0);
				size_t i;
				json_t *json_bool;

				json_array_foreach(glides_json_array, i, json_bool) {
					phraseGlides[0].push_back(json_boolean_value(json_bool));
				}
			}

			json_t *ext = json_object_get(root, JSON_GAP);
			if (ext) {
				gap[0] = float(json_real_value(ext));
			}

			json_t *ext2 = json_object_get(root, JSON_REST_AMOUNT);
			if (ext2) {
				rest_amount[0] = json_integer_value(ext2);
			}

			if (phraseDurations[0].size() != phrase[0].size() || phraseAccents[0].size() != phrase[0].size()
				|| phraseGlides[0].size() != phrase[0].size() || phrase[0].size() < PHRASE_LENGTH_MIN) {
				// Illegal Json, we generate new phrase instead
				this->generateMelody(0);
			} else {
				nextPhrase[0].resize(0);// else it will switch to constructor generated one, right after loading json.
				nextPhraseDurations[0].resize(0);
				nextPhraseAccents[0].resize(0);
				nextPhraseGlides[0].resize(0);
				nextGap[0] = gap[0];
			}
			phrase_index[0] = 0;
		}
	}

	/// @brief MIDI note number for C4 (Middle C), used as the 0V reference.
	const int c4 = 60;
	
	/// @brief Look-up table for scale intervals.
	/// Format: modes[mode_index][step_interval].
	/// Values represent semitones between scale degrees
	std::vector<int> modes[NUMBER_OF_MODES] = { {2,2,1,2,2,2,1},  //   I Major
												{2,1,2,2,2,1,2},  //  II Dorian
												{1,2,2,2,1,2,2},  // III Phrygian
												{2,2,2,1,2,2,1},  //  IV Lydian
												{2,2,1,2,2,1,2},  //   V Mixolydian
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
	static int getSemiNoteOffset (int steps, int referenceIndex, const std::vector<int>& mode);
	//int getModeIndex (int note, int reference, int referenceIndex, std::vector<int> mode);
	void generateMelody (int c);
	void attenuvertInt(int CV, int KNOB, int min_result, int max_result);
	void attenuvertFloat(int CV, int KNOB, float min_result, float max_result);

	void onReset(const ResetEvent& e) override {
		immediate = false;
		Module::onReset(e);
		initialize_melodies();
	}

	void onRandomize(const RandomizeEvent& e) override {
		// Later might think of something needed here
		Module::onRandomize(e);
	}

	void switch_to_next_phrase(int c);
	void process(const ProcessArgs &args) override;
};

void Melody::switch_to_next_phrase(int c) {
	//start = 10.0f;
	startPulse[c].trigger(); // 1ms pulse
	phrase_index[c] = 0;
	bool newPhraseStarted = false;
	if(!nextPhrase[c].empty()) {
		//newStart = 10.0f;
		newPhrasePulse[c].trigger(); // 1ms pulse
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

		nextPhrase[c].resize(0);
		gap[c] = nextGap[c];
		newPhraseStarted = true;
	}
	if (rest_amount[c] > 0 && !(newPhraseStarted && immediate)) {
		// now we start the rest stage. We set the countdown:
		resting[c] = rest_amount[c];
	} else if (newPhraseStarted && immediate) {
		resting[c] = 0;
	}
}

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
		this->attenuvertInt(CV_TONIC_INPUT, TONIC_PARAM, TONIC_MIN, TONIC_MAX);
		this->attenuvertInt(CV_MODE_INPUT, MODE_PARAM, 0, NUMBER_OF_MODES-1);
		this->attenuvertInt(CV_PHRASE_INPUT, PHRASE_PARAM, PHRASE_LENGTH_MIN, PHRASE_LENGTH_MAX);
		this->attenuvertInt(CV_ACCENT_INPUT, ACCENT_PARAM, 0, 100);
		this->attenuvertInt(CV_GLIDE_INPUT, GLIDE_PARAM, 0, 100);
		this->attenuvertInt(CV_REST_INPUT, REST_PARAM, 0, REST_MAX);
		this->attenuvertFloat(CV_GAP_INPUT, GAP_PARAM, 0, 3);
	}

	int genInCh = inputs[GENERATE_INPUT].getChannels();
	bool generateButton = generateTriggerButton.process(params[BUTTON_GENERATE_PARAM].getValue() > 0.5f);
	for(int c = 0; c < active_voices; c++) {
		bool generateCV = false;
		if (genInCh > c) {
			generateCV = generateTriggerCV[c].process(inputs[GENERATE_INPUT].getPolyVoltage(c));
		} else if (c > 0) {
			// bit of a hack..
			generateCV = generated[0];
		}
		if (generateCV || generateButton) {
			this->generateMelody(c);
			generated[c] = true;
		} else {
			generated[c] = false;
		}
	}

	bool clockPulse0 = false;
	for (int c = 0; c < active_voices; c++) {
		bool clockPulse;
		if (c > 0) {
			if (clock_channels > c)
				clockPulse = clockTrigger[c].process(inputs[CLOCK_INPUT].getPolyVoltage(c));
			else
				clockPulse = clockPulse0;
		} else {
			clockPulse = clockTrigger[c].process(inputs[CLOCK_INPUT].getPolyVoltage(0));
			clockPulse0 = clockPulse;
		}

		if (generated[c] && immediate) {
			// we pretend the generation was accompanied by a clock pulse
			passedClocks[c] = 0;// reset clock index
			resting[c] = 0;// skip resting
			switch_to_next_phrase(c);// this will set phrase_index to 0
			frameCount_last[c] = frameCount[c];
			frameCount[c] = 0;
		} else if (clockPulse) {
			if (resting[c] == 0) {
				// we are not in rest inbetween phrases
				passedClocks[c]++;// increment clock index
				if (passedClocks[c] >= phraseDurations[c][phrase_index[c]]) {
					passedClocks[c] = 0;// reset clock index
					phrase_index[c]++;// increment note index
				}
			} else {
				resting[c]--;
			}
			if (phrase_index[c] > phrase[c].size() - 1) {
				// this will set index to 0 and resting to rest_amount
				switch_to_next_phrase(c);
			}
			frameCount_last[c] = frameCount[c];
			frameCount[c] = 0;
		} else {
			frameCount[c]++;
			if (frameCount[c] > 10000000) {
				frameCount[c] = 0;
			}
		}

		bool startSignal = startPulse[c].process(args.sampleTime);
		bool startNewSignal = newPhrasePulse[c].process(args.sampleTime);

		outputs[START_PHRASE_OUTPUT].setVoltage(startSignal ? 10.0f : 0.0f, c);
		outputs[NEW_PHRASE_OUTPUT].setVoltage(startNewSignal ? 10.0f : 0.0f, c);


		bool gliding = phraseGlides[c][phrase_index[c]];//slide in present.
		float out = this->note2vPoct(phrase[c][phrase_index[c]]);
		if (gliding && resting[c] == 0 && passedClocks[c] == 0) {
			// gliding

			int prev_idx = phrase_index[c] - 1;
			if (prev_idx < 0) prev_idx = phrase[c].size() - 1;
			float out_prev = this->note2vPoct(phrase[c][prev_idx]);

			// glides are constant time (60ms),
			// but constrained by the step length so they don't overrun into the next step if the tempo is crazy fast.
			auto clockFrames = float(frameCount_last[c]);
			float maxGlideFrames = GLIDE_MAXIMUM / args.sampleTime;
			float glideFrames;
			// Use the full note duration as the upper limit
			glideFrames = std::min(clockFrames, maxGlideFrames);

			if (glideFrames > 0.0f) {
				// 60ms glide at start of note if prev note was marked as glide:
				outputs[FREQ_OUTPUT].setVoltage(clampSafe(rescale(float(frameCount[c]), 0, glideFrames, out_prev, out), out_prev, out), c);
			} else {
				// prevent divide by zero
				outputs[FREQ_OUTPUT].setVoltage(out, c);
			}
		} else {
			// not gliding
			if (resting[c] == 0) {
				// Only if not between phrase do we set voltage, so that previous voltage can be allowed to 'decay' if envelope is put on output.
				outputs[FREQ_OUTPUT].setVoltage(out, c);
			}
		}
		outputs[ACCENT_OUTPUT].setVoltage(float(phraseAccents[c][phrase_index[c]])*10.0f, c);

		bool isNextGliding = phrase_index[c]==phrase[c].size()-1?false:phraseGlides[c][phrase_index[c]+1];
		bool isGap = (frameCount[c] > frameCount_last[c]*gap[c] && passedClocks[c] >= phraseDurations[c][phrase_index[c]]-1);
		bool keepGateOpenForGlide = isNextGliding && gateOnWhenGliding;
		if (resting[c] > 0 || (isGap && !keepGateOpenForGlide)) {
			// We drop the gate only if we are in the Gap time, and we are not gliding to the next note (unless closeGateWhenGliding==true).
			outputs[GATE_OUTPUT].setVoltage(0.0f, c);
		} else {
			outputs[GATE_OUTPUT].setVoltage(10.0f, c);
		}
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
	int tonic = int(std::round(params[TONIC_PARAM].getValue()));
	std::vector<int> mode = modes[int(std::round(params[MODE_PARAM].getValue()))];
	int next_phrase_length = int(std::round(params[PHRASE_PARAM].getValue()));
	int minOffset = -2;
	int maxOffset =  4;
	nextPhrase[c].clear(); // Keeps capacity, just sets size to 0
	int startStep = 0;

	// figure out start note
	float startRoll = rack::random::uniform();
	if (next_phrase_length > 12) {
		if (startRoll > 0.85f)      startStep = 6; // 15% chance: Start on 7th (Leading Tone)
		else if (startRoll > 0.60f) startStep = 4; // 25% chance: Start on 5th (Dominant)
		else if (startRoll > 0.45f) startStep = 2; // 15% chance: Start on 3rd (Mediant)
		else                        startStep = 0; // 45% chance: Start on Root (Tonic)
	} else if (next_phrase_length > 8) {
		if (startRoll > 0.70f) startStep = 4;      // 30% chance: Start on 5th (Dominant)
		else if (startRoll > 0.55f) startStep = 2; // 15% chance: Start on 3rd (Mediant)
		else                        startStep = 0; // 55% chance: Start on Root (Tonic)
	} else {
		if (startRoll > 0.55f) startStep = 2; // 45% chance: Start on 3rd (Mediant)
		else                   startStep = 0; // 55% chance: Start on Root (Tonic)
	}

	int startSemi = getSemiNoteOffset(startStep, 0, mode);
	int startNote = tonic + startSemi;
	nextPhrase[c].push_back(startNote);
	int lastNote = startNote;
	int lastIndex = startStep % mode.size();
	int distanceToTonic = startStep;
	int closure = next_phrase_length >= PHRASE_LENGTH_THAT_DEMANDS_RESOLUTION?-1:0;
	int stepsTillEstablish = 12 + (int)(rack::random::uniform() * 5.0f); // 12 to 16
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
			if (stepsTillEstablish == 1) stepsTillEstablish = 12 + (int)(rack::random::uniform() * 5.0f);
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
		int range = (maxiRand - (miniRand)) + 1; // 9
		int rawOffset = miniRand + (int)(rack::random::uniform() * range);
		int noteOffset = clamp(rawOffset, minClamp, maxClamp);
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
	float chance = int(std::round(params[ACCENT_PARAM].getValue()));
	nextPhraseAccents[c].clear();
	float chance_g = int(std::round(params[GLIDE_PARAM].getValue()));
	nextPhraseGlides[c].clear();
	for (int i = 0; i < next_phrase_length; i++) {
		nextPhraseDurations[c].push_back(1 + (int)(rack::random::uniform() * 2)); // 1 or 2
		nextPhraseAccents[c].push_back((rack::random::uniform() * 100.0f) < chance);
		bool glide = (i==0?false:(rack::random::uniform() * 100.0f) < chance_g);//the first note does never glide
		nextPhraseGlides[c].push_back(glide);
		if (only1step && glide) {
			nextPhraseDurations[c][i-1] = 1;
		}
	}

	// Rest
	rest_amount[c] = int(std::round(params[REST_PARAM].getValue()));

	// Gaps
	nextGap[c] = rescale(params[GAP_PARAM].getValue(), 0, 3, GAP_STACCATISSIMO, GAP_LEGATO);
}

void Melody::attenuvertInt(int CV, int KNOB, int min_result, int max_result) {
	if (inputs[CV].isConnected()) {
		int total_steps = max_result - min_result + 1;
		float input_scaled = rescale(inputs[CV].getVoltage(), -5.0f, 5.0f, 0, 1);
		//    This creates perfectly even distributions for every integer.
		int step_index = (int)std::floor(input_scaled * total_steps);
		step_index = clamp(step_index, 0, total_steps - 1);
		int result = min_result + step_index;
		if (std::abs(params[KNOB].getValue() - (float)result) > 0.1f) {
	        params[KNOB].setValue((float)result);
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

int Melody::getSemiNoteOffset (int steps, int referenceIndex, const std::vector<int>& mode) {
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

float Melody::note2vPoct (const int note) {
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

	struct GlideGateItem : MenuItem {
		Melody* _module;

		GlideGateItem(Melody* module, const char* label)
		: _module(module) {
			this->text = label;
		}

		void onAction(const event::Action& e) override {
			_module->gateOnWhenGliding = !_module->gateOnWhenGliding;
		}
		void step() override {
			rightText = (_module->gateOnWhenGliding) ? "✔" : "";
			MenuItem::step();
		}
	};

	struct GlideOldItem : MenuItem {
		Melody* _module;

		GlideOldItem(Melody* module, const char* label)
		: _module(module) {
			this->text = label;
		}

		void onAction(const event::Action& e) override {
			_module->only1step = !_module->only1step;
		}
		void step() override {
			rightText = (_module->only1step) ? "✔" : "";
			MenuItem::step();
		}
	};

	struct ImmediateItem : MenuItem {
		Melody* _module;

		ImmediateItem(Melody* module, const char* label)
		: _module(module) {
			this->text = label;
		}

		void onAction(const event::Action& e) override {
			_module->immediate = !_module->immediate;
		}
		void step() override {
			rightText = (_module->immediate) ? "✔" : "";
			MenuItem::step();
		}
	};

	void appendContextMenu(Menu* menu) override {
		auto* a = dynamic_cast<Melody*>(module);
		assert(a);

		menu->addChild(new MenuLabel());
		menu->addChild(new GlideGateItem(a, "Glides keeps gate open"));
		menu->addChild(new GlideOldItem(a, "Only 1-step before glides"));
		menu->addChild(new ImmediateItem(a, "Generate is immediately"));
	}
};

Model *modelMelody = createModel<Melody, MelodyWidget>("Melody");
