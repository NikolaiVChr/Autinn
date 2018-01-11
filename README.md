# Autinn
Autinn VCV Rack plugin

For now only 1 module, and for Windows only.

## Transistor Lowpass Filter

A 4-pole transistor ladder lowpass filter.

Knob 1: Temperature of the circuit. 10-60 degrees celcius.

Knob 2: Resonance.

Knob 3: Cutoff frequency. 20-20KHz.

Knob 4: Cutoff CV influence.

Input 1: Cutoff CV.

Input 2: Audio signal.

Output 1: Audio output.

The response is non-linear, so if you don't like the sound, try varying the amplitude of the input signal to your liking.

Use the non-oversampled if you run engine sampling rate at higher than Audio-out rate. The oversampled module uses more CPU.

As you increase samplerate, you might have to increase block size also to make it not produce noise.
