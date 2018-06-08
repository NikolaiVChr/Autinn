# Autinn VCV Rack plugin

## Bass

A bass synth emulating a classic hardware synth.

![Autinn Bass](https://s19.postimg.cc/gsv4h6jtf/bass.png)

Send the tone into OSC input. Send the note gate/trigger into GATE input, and click the white button to select gate or trigger. Send accent gate into ACCENT input, it should be either on or off. Changing accent gate in middle of a note wont affect anything.

Use accent knob to adjust how much accented notes are emphasized (higher volume and higher pitch if in fast succesion).

Use env mod knob to adjust the range of the envelope that controls the filter sweep.

Attack are fixed and extremely short for non accent notes, both for the vca and filter sweep.

Use cutoff and resonance to adjust the filter. Resonance turned more clockwise will also increase attack time on filter sweep envelope for accented notes and slightly on volume envelope.

The decay of the volume envelope is fixed.

Use decay knob to adjust the amount of filter sweep envelope decay. Filter sweep decay for accented notes are very short and cannot be adjusted with the decay knob.

If accented notes are sent in very quick succession the filter will open more up on each note to a certain degree.

I opted for no built in oscillator as people probably have their favorites. So its recommended to experiment with waveforms, slide, accent and frequencies. I mostly used Autinn Saw and Square to test with. The red light next to OSC input will indicate oscillator volume is so high its stressing the filter and might be soft clipped too much. Also beware that OSC input of more than +-7.5V might result in hard clipping on accented notes.

If the GATE input is set to gate the note output will end when the GATE is off. Is it set to trigger, the note will play out until next note. Do not mistake this for sustain, there is no sustain.

## Saw

A special saw oscillator. Goes well with Bass module.

![Autinn Saw](https://s19.postimg.cc/ag91h9uhv/saw.png)

## Square

A special square oscillator. Goes well with Bass module.

![Autinn Square](https://s19.postimg.cc/9qq94x1o3/square.png)

## Flora

A lowpass filter emulating a 4-pole transistor ladder.

![Autinn Flora](https://s19.postimg.cc/4fbck6d0j/flora.png)

When input is unplugged or go silent at very high resonance setting it can self-oscillate, if resonance is dialed too low again, it will stop and the procedure have to be repeated to get it going again.

The linearity knob is to adjust for how strong the input signal is. The stronger it is the more you want to increase the knob, which in turn makes the filter act more linear. It just adjust the voltage going into the filter, and opposite for the signal going out. Best I can recommend is to experiment and find out what sounds the best. The default value should work good with +-5V peak audio input.

It attenuate around 24dB/octave. The cutoff attenuation is in the 12dB range.

## Jette

Oscillator. Control the fundamental and its harmonics amplitudes with the sliders.

![Autinn Jette](https://s19.postimg.cc/at0fnen1f/jette.png)

The button selects waveform.

Approx up to -5.9V to 5.9V output.

## Digi

Digitizes a voltage, into discrete steps. Steps can be from 0V (smooth) to 1V (very jagged).

Can be used for audio rate as well as CV voltages.

![Autinn Digi](https://s19.postimg.cc/qbazz8wdf/digi.png)

## Deadband

This is a deadband. The width is how much the deadband zone should be on each side of origin of input.
Width can be from 0V to 5V.
Optionally the gap can be specified from 0% to 100%. Having a gap means the output will not start from 0V when it passes the width, at 100% gap everything within width will be 0V and everything outside will just as input.

![Autinn Digi](https://s19.postimg.cc/5eerukiwz/deadband.png)

## Conv

Converts to and from uni/bi CV voltages.

![Autinn Digi](https://s19.postimg.cc/wc8owbb9v/conv.png)

## Oxcart

Oscillator with a frequency knob and a CV in.

![Autinn Oxcart](https://s19.postimg.cc/917gsj69f/oxcart.png)

It is a very aggresive sound.

Approx -1V to +5V output.

## DC

AC to DC module.

![Autinn DC](https://s13.postimg.cc/fiu7oofzr/image.png)

A typical use for this is to just measure DC offset of audio. Just connect the output to RJModules Display or Fundamentals Scope, and you get a readout of the value.
In theory you could also invert the polarity and add it to the audio, and then you have a DC blocker, this I haven't tested though.

The light will be green if DC is low, become more blue for negative DC and more red for positive DC.

Is meant for audio, not CV signals.

## Flopper

Takes the top part of the wave from input 1, and the lower part from input 2 to output 1.

Does the opposite for output 2.

![Autinn Flopper](https://s13.postimg.cc/e3smzz21z/flopper.png)

The knob and CV determines which voltage the switch from top to bottom will take place.

## Amp

Simple Amplifier/attenuator. The yellow light will turn on when output signal amplitude peaks above +-10V.

![Autinn Amp](https://s19.postimg.cc/ws6ualgqb/amp.png)

Max 6dB amplification. Middle setting of knob is 0dB.

No clipping.

## Mera

Module that amplifies/attenuates before some other module, then after the module it applies the opposite gain.

Use pre in/out into another module, then from that module use post in/out.

Suggested usage: For non-linear DSP modules without drive or linearity knobs.

![Autinn Mera](https://s19.postimg.cc/4jykgh203/mera.png)

No clipping.

## Vibrato

Vibrato effect with optional Flanger.

The vibrato is not modulation of volume or pitch, its a real vibrato effect.

![Autinn Vibrato](https://s19.postimg.cc/x8rn1guz7/vibrato.png)

No clipping.

## Sjip

An oscillator with a very rounded waveform.

Approx. +-5V output.

![Autinn Sjip](https://s13.postimg.cc/w6lpr80hj/sjip.png)

## Vxy

This simulates a x-y coordinate field. Imagine the current point having a velocity vector that is turning with some random speed. That will move the point around in the field. When it hit a boundary (+-5V) it will stay there until the velocity vector directs it away from the boundary again. For this reason the outputs tend to spend around half their time at the extremes.

The speed knob sets the speed the point is moving with.

Suggested usage: For modulating other modules CV inputs in a smooth semi-random way.

+-5V output.

![Autinn Vxy](https://s19.postimg.cc/3jqwfjnv7/vxy.png)

## Aura

It uses ITD and IID to make stereo panning for headphones.

![Autinn Aura](https://s13.postimg.cc/6b1z7z17r/aura.png)

Use CV to control which pitch is the main freq in the input signal.

You have to press the apply button each time you change the source listening angle with the knob.

## Rails

Mono to fake stereo converter. It doesn't pan, the output power is still the same in both channels, but a delay in one channel.

![Autinn Rails](https://s13.postimg.cc/tcikdrb5z/rails.png)

## Dirt

Might remove it soon, as haven't found a any use for it and don't feel like improving it.

Just a very simple high-pass filter.

![Autinn Dirt](https://s19.postimg.cc/omoscg7wz/dirt.png)

## Download

https://github.com/NikolaiVChr/Autinn/releases

## Changelog
0.6.4
* Added Conv module.
* Added Digi module.
* Added Deadband module.

0.6.3
* Added 2 missing screws in Vibrato module.
* In Bass module accented notes filter sweep now has faster attack to make fast notes sound better. Slow notes will sound slightly less wauwy, but the tradeoff had to be made.
* Increased max value for resonance knob in Bass module.
* Many filter sweep envelope and volume envelope changes in Bass module.
* Added button to Bass that can switch input from gate to trigger.
* Added red light on Bass that will indicate oscillator gain is stressing the filter and might be soft clipped too much.
* Added Vxy module.

0.6.2
* Saw and Square module now morph between wave forms at lower frequencies than before.
* Made Flora and Bass passband gain not decrease too much when turning up resonance.
* Made some minor adjustments to accented envelopes in Bass module.
* Decreased MOD ENV range from 0-7500 to 0-4500 in Bass module.
* Increased VCA envelope decay in Bass module.
* Added Vibrato module.
* Added Mera module.
* Fixed missing screws on all panels.

0.6.1
* Made the Rails delay lower.

0.6.0
* Ported Autinn modules to Rack v0.6. (consider this a beta build for now)

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
