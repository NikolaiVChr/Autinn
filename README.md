# Autinn VCV Rack plugin

## Retri

A 4-pole transistor ladder lowpass filter behavior.

Notice that sometimes the resonance can diverge a bit from the cutoff frequency.

When input is unplugged or go silent at very high resonance setting it can self-oscillate, if resonance is dialed too low again, it will stop and the procedure have to be repeated to get it going again.

The linearity knob is to adjust for how strong the input signal is. The stronger it is the more you want to increase the knob, which in turn makes the filter act more linear. It just adjust the voltage going into the filter, and opposite for the signal going out. Best I can recommend is to experiment and find out what sounds the best.

It attenuate around 24dB/octave. The cutoff attenuation is in the 12dB range.

## Jette

Oscillator. Control the fundamental and its harmonics amplitudes with the sliders.

The button selects waveform.

## Oxcart

Oscillator with a frequency knob and a CV in.

## Flopper

Takes the top part of the wave from input 1, and the lower part from input 2 to output 1.

Does the opposite for output 2.

The knob and CV determines which voltage the switch from top to bottom will take place.

Be careful with higher frequencies as it is not anti-aliased.

## Amp

Simple Amplifier/attenuator. The yellow light will turn on when output signal amplitude peaks above +-10V.

Max 6dB amplification. Middle setting of knob is 0dB.

No clipping.

## Sjip

An oscillator.

Approx. +-5V output.

## Aura

It uses ITD and IID to make stereo panning for headphones.

Use CV to control which pitch is the main freq in the input signal.

You have to press the apply button each time you change the source listening angle with the knob.

## Rails

Mono to stereo converter. It doesn't pan, the output power is still the same in both channels.

## Dirt

Might remove it soon, as haven't found a any use for it and don't feel like improving it.

Just a very simple high-pass filter.

## Download

For now it is compiled for Windows and Linux only: https://github.com/NikolaiVChr/Autinn/releases

## Changelog

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
