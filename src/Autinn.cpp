#include "Autinn.hpp"

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

// The pluginInstance-wide instance of the Plugin class
Plugin *pluginInstance;

float non_lin_func(float parm) {
	// 7 divisions in continued fraction series expansion
	if (parm > 4.97f) {
		return 1.0f;
	}
	if (parm < -4.97f) {
		return -1.0f;
	}
	double x2 = double(parm) * double(parm);
	double a = double(parm) * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2)));
	double b = 135135.0 + x2 * (62370.0 + x2 * (3150.0 + x2 * 28.0));
	return a / b;
}

float non_lin_func2(float parm) {
	return 2.0f * (exp(parm)-exp(-parm));
}

float slew(float input, float input_prev, float maxChangePerSec, float dt) {
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

void init(rack::Plugin *p) {
	pluginInstance = p;
	// The "slug" is the unique identifier for your pluginInstance and must never change after release, so choose wisely.
	// It must only contain letters, numbers, and characters "-" and "_". No spaces.
	// To guarantee uniqueness, it is a good idea to prefix the slug by your name, alias, or company name if available, e.g. "MyCompany-MyPlugin".
	// The ZIP package must only contain one folder, with the name equal to the pluginInstance's slug.
//	p->slug = "Autinn";//TOSTRING(SLUG);
//	p->version = TOSTRING(VERSION);
//	p->website = "https://github.com/NikolaiVChr/Autinn";
//	p->manual = "https://github.com/NikolaiVChr/Autinn/wiki";

	// alphabetically order:

	p->addModel(modelAmp);
//	p->addModel(modelAura);
	p->addModel(modelDeadband);
	p->addModel(modelBass);
	p->addModel(modelCVConverter);
	p->addModel(modelDC);
	p->addModel(modelDigi);
//	p->addModel(modelDirt);
	p->addModel(modelFlopper);
	p->addModel(modelFlora);
	p->addModel(modelJette);
	p->addModel(modelBoomerang);
	p->addModel(modelOxcart);
//	p->addModel(modelRails);
	p->addModel(modelSaw);
	p->addModel(modelSjip);
	p->addModel(modelSquare);
	p->addModel(modelVibrato);
	p->addModel(modelVectorDriver);
	p->addModel(modelZod);
	p->addModel(modelTriBand);
	p->addModel(modelMixer6);
	p->addModel(modelNon);
	p->addModel(modelFil);
	p->addModel(modelNap);
	p->addModel(modelMelody);
	p->addModel(modelChord);
}