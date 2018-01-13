# Autinn
Autinn VCV Rack plugin

For now only 1 module, and for Windows only.

## Retri Transistor Lowpass Filter

A 4-pole transistor ladder lowpass filter.

Knobs from top to bottom:

Temperature of the circuit. 10-60 degrees celcius.

Resonance CV and Resonance. Notice that sometimes the resonance can diverge from the cutoff frequency.

Cutoff CV and Cutoff frequency. Up to 20KHz.

Voltage fine and coarse. Experiment with this to get the sound to your liking, its just called voltage for lack of better description. I prefer to decrease it at least so much that the red light next to the output does not turn on.

As you increase samplerate, you might have to increase block size also to make it not produce noise.

## Goals

Low CPU usage.

It will never sound exactly like the real analog transistor ladder filter. But should sound nice and unique.

The graphics are placeholders as I am focusing on the sound for the time being.

## Download

For now it is compiled for Windows only: https://github.com/NikolaiVChr/Autinn/releases

## Feedback and suggestions

Post issues on the github page for that. :)
