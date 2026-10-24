# 11. Glossary and further reading

## Glossary

**Astable multivibrator.** Two transistors that alternately switch each other
off through capacitors, producing a square wave. Each half lasts about
$\ln 2 \cdot RC$. The MRK-1's snare roll is one.

**Bilinear transform.** Mapping $s \to \frac{2}{T}\frac{z-1}{z+1}$ from
continuous to discrete time. It is equivalent to the trapezoidal rule, and
it warps frequencies (page 3).

**Bridge (waveform relaxation).** Splitting a circuit at a high-value
resistor and letting each half see the other's previous-sample voltage
(page 5).

**Companion model.** The resistor-plus-current-source equivalent of a
capacitor or inductor during one time step (page 3).

**Conductance matrix ($\mathbf{G}$).** The matrix of nodal analysis;
$\mathbf{G}\mathbf{v} = \mathbf{i}$.

**Early effect.** The slight rise in a transistor's collector current with
collector voltage. It's modelled with the Early voltage $V_A$.

**Ebers-Moll model.** A two-junction transistor model that covers the
active, saturated and cut-off regions.

**Keying point.** An instrument input on the MRK-1 instrumentation board,
driven by clock pulses through the rhythm switches.

**K-method.** Reducing a circuit's nonlinear solve to its device ports:
$\mathbf{p} = \mathbf{p}_0 + \mathbf{K}\,\mathbf{i}(\mathbf{p})$ (page 4).

**LC tank.** An inductor and capacitor in parallel, resonating at
$1/(2\pi\sqrt{LC})$.

**Matrix (of a rhythm machine).** The diode and resistor network that
combines clock pulses into the timing signals for each rhythm.

**Netlist.** A list of components and the nodes they connect.

**Newton-Raphson.** Iteratively solving nonlinear equations by
linearizing. It converges quadratically near the solution.

**Operating point (bias).** A circuit's DC resting state.

**pnjlim / junction limiting.** SPICE's damping of large Newton steps on
exponential junctions.

**Pre-warping.** Choosing the bilinear transform's constant so one chosen
frequency maps exactly.

**Pulse amp.** On the MRK-1, a transistor that turns a keying pulse into a
sharp kick for a voice generator.

**Rheostat.** A potentiometer wired as a variable resistor (wiper joined to
one end).

**Ring counter / ring multivibrator.** A chain of stages that turn on one
after another, round and round. It is the MRK-1's clock.

**Source stepping.** Ramping supply voltages up from zero to help Newton find
a DC operating point.

**SPICE.** The standard family of circuit simulators. ngspice is the free
one used here.

**Trapezoidal rule.** A second-order time integration that adds no
artificial damping.

**Twin-T.** A two-path RC network with a notch at $1/(2\pi RC)$ when
balanced. In an amplifier's feedback path it makes a ringing oscillator
(page 6).

**VCA.** Voltage-controlled amplifier. On the MRK-1 it's a transistor whose
bias, and therefore its gain, comes from the envelope.

## Key formulas

| What | Formula |
|---|---|
| Capacitor companion (trapezoidal) | $G = 2C/T$, $J_{n+1} = 2Gv_{n+1} - J_n$ |
| Inductor companion | $G = T/2L$, $K_{n+1} = K_n + 2Gv_{n+1}$ |
| Frequency warping | $\omega_d = \frac{2}{T}\arctan(\omega T/2)$ |
| Pre-warp constant | $c = \omega_0 / \tan(\omega_0 T/2)$ |
| Diode | $i = I_S(e^{v/nV_T} - 1)$ |
| Emitter resistance | $r_e = V_T / I_C$ |
| Common-emitter gain | $A \approx R_C / (r_e + R_E)$ |
| Twin-T notch (balanced) | $f_0 = 1/(2\pi RC)$ |
| LC tank | $f = 1/(2\pi\sqrt{LC})$, $Q = R/(\omega L)$ |
| RC timing to a threshold | $t = RC\ln\frac{V_B + \Delta V}{V_B - V_{th}}$ |
| Astable half-period | $t \approx 0.693\,RC$ |

## Further reading

- L. W. Nagel, *SPICE2: A Computer Program to Simulate Semiconductor
  Circuits* (1975): the origin of companion models, Newton with junction
  limiting, and source stepping.
- D. T. Yeh, J. S. Abel, J. O. Smith, "Automated physical modeling of
  nonlinear audio circuits for real-time audio effects" (IEEE TASLP, 2012):
  the K-method / nodal DK approach.
- M. Holters, U. Zölzer, "Physical modelling of a wah-wah effect pedal as a
  case study for application of the nodal DK method to circuits with
  variable parts" (DAFx 2011).
- J. Pakarinen, D. T. Yeh, "A review of digital techniques for modeling
  vacuum-tube guitar amplifiers" (Computer Music Journal, 2009): a survey of
  circuit-modeling approaches.
- U. Zölzer (ed.), *DAFX: Digital Audio Effects*, 2nd ed. (Wiley, 2011):
  background on digital filters and the bilinear transform.
- The ngspice manual: device models and analysis options.
- The JUCE tutorials: plugin structure, the value tree state, and the audio
  thread rules.

## Try it yourself

1. **Change a part.** Halve the tom's twin-T capacitors in
   `kGenerators` (`Instrumentation.cpp`), render with
   `tools/render_wav.cpp`, and confirm the pitch roughly doubles. Then run
   the SPICE script with the same change.
2. **Turn off pre-warping** for the cymbal and measure how flat its tank
   rings.
3. **Merge the two stages** of a noise voice back into one circuit, and
   compare CPU cost and output.
4. **Add a new rhythm** as a grid in `Rhythms.cpp`, using only positions
   that land on clock pulses. The tests will tell you if one doesn't.
5. **Model another machine.** The same simulator runs any netlist of
   resistors, capacitors, inductors, diodes, NPN transistors and switches.
