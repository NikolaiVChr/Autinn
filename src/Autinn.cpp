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


void init(rack::Plugin *plugin) {
	pluginInstance = plugin;
	// The "slug" is the unique identifier for your pluginInstance and must never change after release, so choose wisely.
	// It must only contain letters, numbers, and characters "-" and "_". No spaces.
	// To guarantee uniqueness, it is a good idea to prefix the slug by your name, alias, or company name if available, e.g. "MyCompany-MyPlugin".
	// The ZIP package must only contain one folder, with the name equal to the pluginInstance's slug.
//	p->slug = "Autinn";//TOSTRING(SLUG);
//	p->version = TOSTRING(VERSION);
//	p->website = "https://github.com/NikolaiVChr/Autinn";
//	p->manual = "https://github.com/NikolaiVChr/Autinn/wiki";

	// alphabetically order:

	plugin->addModel(modelAmp);
//	p->addModel(modelAura);
	plugin->addModel(modelDeadband);
	plugin->addModel(modelBass);
	plugin->addModel(modelCVConverter);
	plugin->addModel(modelDC);
	plugin->addModel(modelDigi);
//	p->addModel(modelDirt);
	plugin->addModel(modelFlopper);
	plugin->addModel(modelFlora);
	plugin->addModel(modelFauna);
	plugin->addModel(modelJette);
	plugin->addModel(modelBoomerang);
	plugin->addModel(modelOxcart);
//	p->addModel(modelRails);
	plugin->addModel(modelSaw);
	plugin->addModel(modelSjip);
	plugin->addModel(modelSquare);
	plugin->addModel(modelVibrato);
	plugin->addModel(modelVectorDriver);
	plugin->addModel(modelZod);
	plugin->addModel(modelTriBand);
	plugin->addModel(modelMixer6);
	plugin->addModel(modelNon);
	plugin->addModel(modelFil);
	plugin->addModel(modelNap);
	plugin->addModel(modelMelody);
	plugin->addModel(modelChord);
	plugin->addModel(modelKicker);
	plugin->addModel(modelSnare);
	plugin->addModel(modelCoil);
	plugin->addModel(modelGeiger);
	plugin->addModel(modelSaw2);
	plugin->addModel(modelScope);
	plugin->addModel(modelExcavi);
	plugin->addModel(modelAlias);
	plugin->addModel(modelTrace);
	plugin->addModel(modelAu);
	plugin->addModel(modelMyria);
}