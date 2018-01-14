# Autinn
Autinn VCV Rack plugin

For now only modules for Windows.

## Retri

A 4-pole transistor ladder lowpass filter.

Knobs from top to bottom:

Temperature of the circuit. 10-60 degrees celcius.

Resonance CV and Resonance. Notice that sometimes the resonance can diverge from the cutoff frequency.

Cutoff CV and Cutoff frequency. Up to 20KHz.

As you increase samplerate, you might have to increase block size also to make it not produce noise.

## Oxcart

Oscillator with a frequency knob and a CV in. Very simple.

Approx. +-5V output.

## Dirt

Just a very simple high-pass filter.

## Flopper

Takes the top part of the wave from input 1, and the lower part from input 2 to output 1.

Does the opposite for output 2. The knob and CV determines which level determines where to switch from top to bottom.

Not anti-aliased, so sometimes good idea to follow it with a low-pass.

## Goals

Low CPU usage.

Retri will never sound exactly like the real analog transistor ladder filter. But should sound nice and unique.

The graphics are placeholders as I am focusing on the sound for the time being.

## Download

For now it is compiled for Windows only: https://github.com/NikolaiVChr/Autinn/releases

## Changelog

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
