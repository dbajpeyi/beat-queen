# 6. The drum voices: twin-T ringing oscillators

Eight of the MRK-1's sounds come from one circuit topology with different
part values: bass drum, tom tom, bongo, blocks, clave, rim shot, hi drum and
lo drum. The service manual calls them "audio oscillators" that "produce an
audio signal of diminishing amplitude when triggered". Understanding why
they ring is the key to this whole family of 1960s-1980s drum machines. The
TR-808's bass drum is a descendant.

## The twin-T network

A **twin-T** is two T-shaped networks in parallel between an input and an
output:

```
            C1        C1                 series-C arm, centre to ground through Rshunt
   in ──┬───||───┬───||───┬── out
        │        │        │
        │      Rshunt     │
        │        │        │
        │       gnd       │
        │                 │
        └──R───┬───R──────┘              series-R arm, centre to ground through Cshunt
               │
            Cshunt
               │
              gnd
```

At low frequencies the R arm passes the signal. At high frequencies the C
arm does. At one frequency in between, the two paths arrive with opposite
phase and cancel: a **notch**. For the balanced case
($R_{shunt} = R/2$, $C_{shunt} = 2C$) the notch is at

$$f_0 = \frac{1}{2\pi R C}$$

## Turning a notch into a ringing drum

Put the twin-T in the **feedback path of an inverting amplifier**, from the
transistor's collector back to its base.

- **Away from $f_0$,** the twin-T passes signal back with the amplifier's
  inversion: negative feedback, stable, quiet.
- **Near $f_0$,** the network's output is small, and its phase swings
  through 180°. The inverted, phase-flipped signal becomes **positive
  feedback** in a narrow band.
- **Above unity loop gain** (inverting gain × network transmission), the
  circuit oscillates.
- **Just below it,** the circuit is a very high-Q band-pass filter: a kick
  makes it **ring** at about $f_0$ and decay.

The decay time depends on how close the loop gain is to 1.

The loop gain is set by the common-emitter stage's gain:

$$A \approx \frac{R_{load}}{r_e + R_E}, \qquad r_e = \frac{V_T}{I_C}$$

On the MRK-1, $R_E$ is a fixed resistor (270 Ω to 1.2 kΩ) in series with a
**1 kΩ trimmer** (VR1-VR8). Lowering $R_E$ raises the gain, the loop gets
closer to 1, and the ring gets longer. This is exactly the manual's
adjustment note: "Too little Bass Drum length will cause the Bass Drum to
sound dead, too much and the Bass Drum will sound continuously."

Simulated, the tom's trimmer runs from a 70 ms decay at one end to a
continuous tone at the other. The test
`length trimmer: dead to continuous` checks this.

### Why simulate it instead of using a resonant filter?

You could approximate each voice as a two-pole resonator. The real circuit
does several things a resonator doesn't:

- **The MRK-1's twin-Ts aren't balanced.** The bass drum's arms (56K/56K
  with 0.082 µF everywhere) predict 35 Hz from the formula, but loading and
  imbalance move the real ring to **53 Hz**. Simulation gets this right
  automatically.
- **Loud hits clip.** The collector sits at about 6.5 V and swings to near
  saturation on a hit. That flattens the first cycles and adds the
  characteristic "thump".
- **The excitation is shaped by real parts.** The kick comes through the
  pulse amp, with its own timing (below).
- **The output network filters each voice.** The bass drum goes through
  33K / 0.15 µF to the bus, a low-pass that softens the click. The hi drum
  and lo drum go through 150K + 0.0015 µF and 100K + 0.0033 µF in series,
  high-passes that keep the "crack".

## The pulse amp: how the drum is struck

Each generator has a **pulse amp** transistor. Its collector sits at +17 V
through 2.2K + 33K, with a capacitor (0.1 µF) to ground. The node between
the 2.2K and 33K feeds the twin-T's centre through $R_P$ (12K for the bass
drum).

1. **At rest,** the pulse amp is off and the capacitor is charged to 17 V.
2. **A keying pulse** saturates the transistor. It dumps the capacitor in
   microseconds, and the feed node drops by about 1 V
   ($17 \times 33/35.2 = 15.94$ V).
3. **That step kicks the twin-T,** which starts ringing.
4. **When the keying pulse ends,** the capacitor recharges through 35K
   ($\tau$ = 3.5 ms). That's a second, gentler push.

The **push buttons** use a different input network (47K and 0.02 µF from
+17 V) from the matrix keying (15K and 0.22 µF for the bass drum). So a
hand-played hit and a sequenced hit are subtly different, as on the real
machine.

## The eight generators

| Voice | Twin-T (series C / shunt C) | $R_E$ + trimmer | Simulated ring | ngspice |
|---|---|---|---|---|
| Bass drum | 0.082 / 0.082 µF | 270 Ω + 1K | 52.5 Hz | 53.2 Hz |
| Tom tom | 0.027 / 0.047 µF | 470 Ω + 1K | 160 Hz | 160 Hz |
| Bongo | 0.015 / 0.047 µF | 820 Ω + 1K | 238 Hz | 238 Hz |
| Lo drum | 0.012 / 0.033 µF | 1K + 1K | 320 Hz | 318 Hz |
| Hi drum | 0.0082 / 0.022 µF | 1.2K + 1K | 485 Hz | 482 Hz |
| Blocks | 0.0033 / 0.01 µF | 820 Ω + 1K | 1.02 kHz | 1.03 kHz |
| Rim shot | 0.0015 / 0.0082 µF | 560 Ω + 1K | 2.00 kHz | 2.00 kHz |
| Clave | 0.0015 / 0.0047 µF | 820 Ω + 1K | 2.23 kHz | 2.22 kHz |

All R arms are 56K + 56K. The netlists are in `kGenerators` in
`core/src/mrk1/Instrumentation.cpp`.

**Soft lo drum.** Bolero uses a separate "SOFT" keying point on the lo
drum. On units after serial 09001, it injects the pulse at the transistor's
**emitter** through 47K, instead of through the pulse amp. A positive kick
at the emitter briefly reduces the transistor's current: a gentler strike.
In simulation it comes out at about half amplitude, with a slightly longer
ring.

## The snare drum: three circuits and a multivibrator

The manual explains that the snare "produces two keying pulses which key
three circuits simultaneously": the hi drum, the lo drum and a gated noise
burst (page 7). The trigger is an **astable multivibrator**: two transistors
Q52/Q53 that alternately turn each other off through capacitors.

A transistor held off by a capacitor charging through a resistor stays off
for about $\ln 2 \cdot RC$:

- **Phase A** (Q52 on: strike the drums, charge the noise envelope) lasts
  $0.693 \times (120\text{K} + VR9) \times 0.1\ \mu\text{F}$ = 8.3-25.6 ms.
- **Phase B** lasts $0.693 \times 100\text{K} \times 0.56\ \mu\text{F}$ = 38.8 ms.

A third transistor (Q54) enables the multivibrator only while a keying
pulse (stretched by 0.01 µF and 220K, to about 2 ms) or a held button
holds it on:

- **A short pulse gives one stroke.**
- **A held input gives a roll** at one stroke per (A + B), which is
  **15.6-21 strokes per second**. The manual says "approximately 15 to 20
  beats per second". VR9 is its trimmer.

Beat Queen runs this multivibrator as a small state machine with these
computed phase lengths. It does not simulate it transistor by transistor:
it's a logic-level circuit whose only audible effect is timing.

Each stroke closes a switch to ground at the snare trigger's diode D19. That
pulls each drum's pulse-amp node down through 33K, exactly like a second
pulse amp.
