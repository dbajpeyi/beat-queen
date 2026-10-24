# 7. The noise voices: hi-hat, cymbal and snare noise

Metallic and noisy sounds can't come from a single resonance, so the MRK-1
uses filtered noise. Each noise voice is a chain of four simple blocks, each
worth understanding.

```
 noise ─▶ noise amp (gain set by the envelope) ─┐
 source                                          ├─▶ diode ─▶ gate transistor ─▶ LC tank ─▶ output
 trigger ─▶ envelope capacitor ─────────────────┘                  (opens only while the envelope is high)
```

## 1. The noise source

Q55 is a transistor with its collector unconnected. Its emitter is fed from
+17 V through 1M, and its base is grounded. That reverse-biases the
base-emitter junction into **breakdown** (around 7 V). The manual calls it
"Zener breakdown". A junction in breakdown at a few microamps produces
strong, broadband noise.

Beat Queen models it as a 7 V source with 4.7K output resistance plus white
Gaussian noise. The noise level isn't in the manual and depends heavily on
the individual transistor, so it was **calibrated** (page 9): 10 mV rms.

## 2. A transistor as a voltage-controlled amplifier

Each noise amp (Q56, Q58 and Q60) gets the noise at its base through a
small capacitor, but its **DC bias comes only from the envelope line,
through 10M**. With no envelope, the transistor has no base current and
passes nothing. With the envelope at $V_{env}$:

$$I_B \approx \frac{V_{env} - 0.6}{10\,\text{M}},\quad I_C = \beta I_B,\quad r_e = \frac{V_T}{I_C},\quad \text{gain} \approx \frac{R_C}{r_e + R_{trim}}$$

So the gain tracks the envelope: this is a **transistor VCA**, and nobody
designed a separate envelope generator. The 1K trimmers VR10-VR12 sit in
the emitters. They are the noise voices' volume controls, giving about
18 dB of range.

## 3. The gate

The amplified noise and the envelope's DC level meet at a node feeding a
**diode**, then 27K into the base of a **gate transistor**. Its emitter sits
on a +1 V rail, with 470K and 0.0033 µF on its base.

The gate conducts only when its base rises above about 1.6 V. That needs
the envelope line around 2.2-2.8 V. Above that, noise peaks push it into
conduction harder or softer: partly a VCA, partly a rectifier.

The diode-and-threshold behavior gives these voices their slightly gritty,
"sizzling" texture. It's also why the envelope matters so much: the gate
**cuts off** as the envelope decays, rather than fading smoothly to zero.

## 4. The LC tank: the voice's colour

The gate's collector load is an inductor (27 mH), a capacitor and a resistor
in parallel. That's a **parallel resonant tank**. The collector current
excites it, and it rings at

$$f = \frac{1}{2\pi\sqrt{LC}}, \qquad Q = \frac{R}{\omega L}$$

| Voice | L, C, R | Tank frequency | Q | Then |
|---|---|---|---|---|
| Snare noise (Q57) | 27 mH, 0.082 µF, 390 Ω | 3.38 kHz | 0.7 (broad) | 0.0047 µF to the bus |
| Hi-hat (Q59) | 27 mH, 0.068 µF, 3.3K | 3.71 kHz | 5.2 (metallic) | 180K ∥ 200 pF (treble lift), 0.01 µF |
| Cymbal (Q61) | 27 mH, 0.01 µF, 6.8K | 9.69 kHz | 4.1 (bright) | 15K / 0.0022 µF low-pass, 0.0022 µF |

These tanks are why the simulator needs inductors, and why pre-warping
matters (page 3). The cymbal tank would otherwise ring 11% flat.

## The envelope: a capacitor and what drains it

A **trigger** charges the envelope capacitor, and the circuit's own
resistors drain it.

| Voice | Trigger | Envelope capacitor | Drained by |
|---|---|---|---|
| Hi-hat | One-shot Q50/Q51, 15 ms; output +17 V through 4.7K and diode D22 | 0.1 µF | Mostly the 820K into the gate path |
| Cymbal | One-shot Q48/Q49, 7 ms | 0.056 µF | Similar paths |
| Snare noise | Phase A of the snare multivibrator, through D20 | 0.068 µF | 560K and 10M |

The **push buttons** are a nice detail. A 0.22 µF capacitor (0.39 µF for
the cymbal) sits charged to +17 V through 6.8K. Pressing the button dumps
it onto the envelope line, so a hand-played hit is a different envelope
from a sequenced one.

## Envelope modes: changing the circuit, not a parameter

Different rhythms need different cymbals and hi-hats: choked, long,
maracas-like. The MRK-1 doesn't have "envelope parameters". The **rhythm
selector switches connect extra parts to the envelope line**:

| Control | What the switch connects | Effect |
|---|---|---|
| **Z** (cymbal) | 0.1 µF to ground | More capacitance, slower decay: **long** (Waltz) |
| **Y** (cymbal) | Diode D17 and 47K to +6 V | Above 6.6 V the line drains quickly into +6 V, then decays normally: a short burst, **maracas** (Bolero, Rumba) |
| **Y + Z** | both | Fast drop to about 6.6 V, then a slow tail: **choked** (Bossa Nova, Slow Rock, Western) |
| **med** (hi-hat) | 0.068 µF to ground | **Medium** hi-hat (Dixie, Shuffle, Swing) |
| **long** (hi-hat) | 0.22 µF to ground | **Long** hi-hat (Slow Fox) |
| **A** (hi-hat) | D25 and 47K to +6 V, plus 0.1 µF | **Choked**: "a strong initial signal which quickly drops to about one third intensity" (Discotheque) |
| **med + A** | both | **Medium choked** (Go-Go) |
| **C** (hi-hat) | Clock transistor Q17, which drains the line through 390K for about 0.1 s after each ring cycle | **Dampened** (Swing) |
| **(2) on D** (cymbal) | A clock pulse driving the gate and noise amp directly | **Accent** (Samba, Mambo) |

In Beat Queen these are literally switches in the netlist. Nothing about
"choked" is coded as a behavior; it emerges from the 47K, the diode and the
+6 V rail. The test `hi-hat envelope modes` measures the choked hi-hat
falling to 0.37 of its peak within 50 ms. That is the manual's "about one
third", and nobody tuned for it.

## The output preamp

All voices meet on a summing bus feeding Q62: a single common-emitter stage
with collector-to-base feedback (1.5M), a 10K load and 100 Ω of emitter
degeneration. Its gain from the bus is about 25. It clips naturally at about
±8.7 V. That's the machine's only "master bus processing", and loud hits
(the rim shot, the cymbal) do drive it. It's simulated like everything else.
The output goes through 0.12 µF to the 100K volume control.
