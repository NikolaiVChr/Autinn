# Autinn VCV Rack plugin

## Bass

A bass synth emulating a classic hardware synth.

Send the tone into OSC input. Send the note trigger into GATE input. Send accent gate into ACCENT input, it should be either on or off. Changing accent gate in middle of a note wont affect anything.

Use accent knob to adjust how much accented notes are emphasized (higher volume and higher pitch).

Use env mod knob to adjust the range of the envelope that controls the filter sweep.

Attack are fixed and extremely short for non accent notes, both for the vca and filter sweep.

Use cutoff and resonance to adjust the filter. Resonance turned more clockwise will also increase attack time on filter sweep envelope.

The higher env mod is, the less accented notes will be pitch emphasized.

The decay of the volume envelope is fixed. But the accented filter sweep envelope is almost added to that.

Use decay knob to adjust the amount of filter sweep envelope decay. Filter sweep decay for accented notes are very short and cannot be adjusted with the decay knob.

If accented notes are sent in very quick succession the filter will open more up on each note to a certain degree.

I opted for no built in oscillator as people probably have their favorites. So its recommended to experiment with waveforms, slide, accent and frequencies. I mostly used Autinn Saw and Square to test with.

There is no sustain, so the gate is now really just a trigger.

## Saw

A special saw oscillator. Goes well with Bass module.

## Square

A special square oscillator. Goes well with Bass module.

## Flora

A lowpass filter emulating a 4-pole transistor ladder.

When input is unplugged or go silent at very high resonance setting it can self-oscillate, if resonance is dialed too low again, it will stop and the procedure have to be repeated to get it going again.

The linearity knob is to adjust for how strong the input signal is. The stronger it is the more you want to increase the knob, which in turn makes the filter act more linear. It just adjust the voltage going into the filter, and opposite for the signal going out. Best I can recommend is to experiment and find out what sounds the best. The default value should work good with +-5V audio input.

It attenuate around 24dB/octave. The cutoff attenuation is in the 12dB range.

## Jette

Oscillator. Control the fundamental and its harmonics amplitudes with the sliders.

The button selects waveform.

Approx up to -5.9V to 5.9V output.

## Oxcart

Oscillator with a frequency knob and a CV in.

It is a very aggresive sound.

Approx -1V to +5V output.

## DC

AC to DC module.

A typical use for this is to just measure DC offset of audio. Just connect the output to RJModules Display or Fundamentals Scope, and you get a readout of the value.
In theory you could also invert the polarity and add it to the audio, and then you have a DC blocker, this I haven't tested though.

The light will be green if DC is low, become more blue for negative DC and more red for positive DC.

Is meant for audio, not CV signals.

## Flopper

Takes the top part of the wave from input 1, and the lower part from input 2 to output 1.

Does the opposite for output 2.

The knob and CV determines which voltage the switch from top to bottom will take place.

## Amp

Simple Amplifier/attenuator. The yellow light will turn on when output signal amplitude peaks above +-10V.

Max 6dB amplification. Middle setting of knob is 0dB.

No clipping.

## Sjip

An oscillator with a very rounded waveform.

Approx. +-5V output.

## Aura

It uses ITD and IID to make stereo panning for headphones.

Use CV to control which pitch is the main freq in the input signal.

You have to press the apply button each time you change the source listening angle with the knob.

## Rails

Mono to fake stereo converter. It doesn't pan, the output power is still the same in both channels, but a delay in one channel.

## Dirt

Might remove it soon, as haven't found a any use for it and don't feel like improving it.

Just a very simple high-pass filter.

## Download

https://github.com/NikolaiVChr/Autinn/releases

## Changelog
0.5.21
* Autodafe compiled a mac binary, thank you!
* Fixed in Bass that accented notes were not attack softened when resonance was high.
* Increased the softening of accented notes at high resonance in Bass.
* Slightly increased range of cutoff knob in Bass.
* Changed the Bass filter to be pure 24dB.
* Added CV inputs to Bass module. So the module is a bit wider now.
* Removed description from Rack plugin module list. And sorted the list.
* Renamed Retri to Flora.
* Added Saw and Square oscillator modules.
* Removed sustain from Bass.
* Made Bass VCA envelope have linear attack, as its a declicker only anyway.
* Removed saturation at output, too expensive.
* Some more envelope changes to Bass.
* Removed clicky retrigger in Bass vca envelope.
* Made increased env mod decrease amount of extra pitch on accented notes in Bass.
* Declicked vca from filter envelope also.

0.5.1.20
* Added a bass synth

0.5.1.19
* Jette shape now works with randomize context menu.
* Flopper aliasing is now damped at lower sampling rates. I need to use better algorithm for anti-aliasing it, but won't be this release.
* Retri cutoff upper range limited, it did not behave well at the max positions anyway.
* Default position of linearity knob in Retri increased a bit to better match standard +-5V PP input.
* Upsampling in Retri is now better algorithm.
* Added DC, an AC to DC module.
* Removed high frequency DC offset (for real this time) from Oxcart by improving anti-aliasing. This also fixed amplitude changing with pitch.
* Made all oscillator lights blink speed dependent on frequency.
* Decreased the voltage of Oxcart to better conform with VCV standards.

0.5.1.18
* Updated graphics some more.
* Renamed Retri drive to linearity, is more accurate.

0.5.1.17
* Removed temperature knob and light from Retri. And rearranged knobs. Due to the way Rack indexes ports/knobs, this will probably mess up any patches with Retri, and it will have to be tuned/replugged again.
* Removed DC offset from Oxcart, except for a very tiny part.
* Made Jette anti-aliasing a little faster.
* Added Sjip oscillator. A little softer on the ears than a sine wave, IMO.
* Made initialize work on shape in Jette.
* Updated graphics a little.

0.5.1.16
* Removed the knob from Rails.
* Set default freq on Oxcart to be C instead of A. If this change will mess up your patch and it really matters to you, please complain in issues and I might change it back.
* Added Jette Oscillator module.
* Added Aura, another mono to stereo module.
* Added some color to the knobs.

0.5.1.15
* Optimized some code.

0.5.1.14
* Compiled binaries for Linux also.
* Small graphical change.

0.5.1.12
* Added knob to Rails.

0.5.1.11
* Added Rails mono to stereo module.

0.5.1.10
* The Retri cutoff frequency is now constantly auto-tuned to match desired cutoff as a function of sampling rate.
* The Retri resonance power is now autotuned to be approx constant (for resonance setting of less than 2/3) until higher frequencies.
* Retri can now self-oscillate at very high resonance setting.
* CPU consumption of Retri is now significant higher. Good sound costs..

0.5.1.9
* Changed graphics to make color less look like wires.
* Added Amp module.

0.5.1.8
* Realigned all graphics and added some color to the backgrounds.

0.5.1.7
* Fixed a bug in the Retri output algorithm.
* Added drive knob to Retri.
* Increased temperature range on Retri.

0.5.1.6
* Added Flopper.

0.5.1.5
* Added Dirt.

0.5.1.4
* Removed voltage knobs from Retri to avoid output being too attenuated.
* Reduced max resonance in Retri to 75% of what it was before.

0.5.1.3
* Anti-aliased the oscillator.

0.5.1.2
* Added a Oxcart oscillator.

0.5.1.1 
* Fixed filter saturation light.
* Introduced half step delay, to help on phase shifting.
* Added fine control knob of voltage.

0.5.1.0
* Made Retri filter.

## Feedback and suggestions

Post issues on the github page for that. :)
