# Autinn VCV Rack plugin

For now only modules for Windows.

## Retri

A 4-pole transistor ladder lowpass filter behavior.

Knobs from top to bottom:

Temperature of the circuit. 5-120 degrees celcius.

Resonance CV and Resonance. Notice that sometimes the resonance can diverge a bit from the cutoff frequency.

Cutoff CV and Cutoff frequency.

Drive (sort of).

When input is unplugged or go silent at very high resonance setting it can self-oscillate, if resonance is dialed too low again, it will stop and the procedure have to be repeated to get it going again.

It attenuate around 24dB/octave. The cutoff attenuation is in the 12dB range.

As you increase samplerate, you might have to increase block size also to make it not produce noise.

## Oxcart

Oscillator with a frequency knob and a CV in. Very simple.

Approx. +-5V output.

## Dirt

Just a very simple high-pass filter.

## Flopper

Takes the top part of the wave from input 1, and the lower part from input 2 to output 1.

Does the opposite for output 2. The knob and CV determines which voltage to switch from top to bottom.

Not anti-aliased, so sometimes good idea to follow it with a low-pass.

## Amp

Simple Amplifier/attenuator. The yellow light will turn on when output signal amplitude peaks above +-10V.
Max 6dB amplification. Middle setting of knob is 0dB. No clipping.

## Goals

Low CPU usage as much as possible.

Retri will never sound exactly like the real analog transistor ladder filter. But should sound nice and unique.

The graphics are placeholders as I am focusing on the sound for the time being.

## Download

For now it is compiled for Windows only: https://github.com/NikolaiVChr/Autinn/releases

## Changelog

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
