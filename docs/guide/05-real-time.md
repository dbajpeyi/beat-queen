# 5. Making it real-time

At 48 kHz you have about 20 µs per sample for *everything*: 11 voice
circuits, the preamp and the sequencer. A plugin also shares the CPU with
everything else in the user's session. This page covers the engineering that
took Beat Queen from "slower than real time" to about 1-25% of one core,
depending on what's playing.

## Rule 1: precompute everything that doesn't change

The nodal matrix depends only on component values, the sample rate and
switch states. Everything derived from it is computed ahead of time:

- $\mathbf{M} = \mathbf{G}^{-1}$
- $\mathbf{M}\mathbf{D}$
- $\mathbf{K}$
- the rail terms

Per sample, what remains is assembling a right-hand side, one $N \times N$
matrix-vector product, a tiny Newton solve, and history updates.

## Switches: one matrix per combination

A voice with $S$ switches has $2^S$ possible matrices. Beat Queen
**allocates** storage for all of them in `prepare()`. It **builds** each one
lazily, the first time that combination is used.

That gives three properties:

- **Memory is fixed**, and nothing is allocated on the audio thread.
- **A first use costs one small matrix inversion**: about 20 µs for a
  14-node circuit.
- **Most combinations never occur.** A push button's "rest" and "pressed"
  contacts are never both closed, for example.

The hi-hat has 8 switches (256 combinations). In practice only a handful get
built.

A **trimmer** changes a resistor value. That invalidates every built matrix
and the operating point: they are rebuilt lazily, and the operating point
is re-solved starting from its previous value. It takes microseconds.

## Rule 2: don't simulate silence

A drum voice spends most of its time silent at its operating point. Each
voice watches a few nodes (its output and, for noise voices, its envelope
line). When everything has been within a threshold of the operating point
for 100 ms, and no timers or held buttons are pending, the voice **sleeps**:

- It stops stepping and outputs exactly zero.
- On the next trigger it resets to the stored operating point and resumes.

Choosing thresholds needs physical reasoning, not guesswork. After a hit,
diode leakage leaves a noise voice's envelope line hovering around a volt
for a very long time. That's real behavior, so "wait until it returns to
the operating point" never happens. But the voice's gate can't conduct
until the line reaches about 2.2-2.8 V: two diode drops above its +1 V
emitter rail. Below 1.2 V the voice is provably silent, so that is the
sleep threshold. An idle machine costs about 1% of a core (just the preamp).

## Rule 3: split loosely coupled circuits

The noise voices each have about 11 nonlinear ports: several diodes and two
transistors. An 11-port Newton solve every sample is expensive. Look at the
circuit, though:

- **The envelope stage** (trigger, envelope capacitor and control switches)
  connects to **the audio stage** (noise amp, gate and tank) only through
  resistors of 560K to 10M.
- Every node on both sides of those resistors has a capacitor giving a time
  constant of milliseconds or more.

So the two stages are simulated **separately**. Each sees the other through
a **bridge**: the large resistor is replaced on each side by a resistor to
ground plus a current source, driven by the *other* side's voltage from the
previous sample. One sample is 21 µs; the time constants are milliseconds.
The lag is far below anything audible, and stability isn't at risk, because
no fast loop crosses the bridge.

This is a simple form of **waveform relaxation**, a classic technique for
simulating large circuits in parts. The stages' operating points are found
together by alternating between them until they agree (Gauss-Seidel).
`Voice::settle()` does this.

## Rule 4: profile, don't guess

The first working version cost 5% of a core per drum voice and 70% for the
cymbal alone. Two plausible-sounding fixes did almost nothing:

- loosening the Newton tolerance;
- replacing `std::exp` with a polynomial approximation.

An instruction-level profile (`valgrind --tool=callgrind`) showed where the
time actually went:

| Hot spot | Fix |
|---|---|
| Generic loops building the Newton Jacobian | Use its block-diagonal structure (page 4) |
| A `log()` call per junction per iteration, recomputing a constant | Precompute each junction's critical voltage once |
| A redundant device evaluation after every converged solve | First-order final correction (page 4) |
| Branchy node lookups when assembling the right-hand side | Flattened index tables built in `prepare()` |
| `ldexp` in the fast exponential | Build the exponent bits directly |
| Hard samples timing out and producing garbage | More iterations allowed, with a hold-previous fallback |

The final numbers, on a 2.1 GHz cloud CPU:

| Load | Cost (share of one core) |
|---|---|
| Idle | ~1% |
| One ringing drum voice | ~2.5% |
| One noise voice | 8-11% |
| A busy rhythm | ~20-25% |

A desktop CPU at 4+ GHz roughly halves these.

## The fast exponential

```
exp(x) = 2^n * 2^f,   with n = round(x * log2(e)) and |f| <= 0.5
2^f    = e^(f ln 2)   from a degree-10 Taylor polynomial (relative error < 1e-11)
2^n    = build the IEEE-754 exponent bits directly
```

It also skips the exponential entirely when $x < -40$: a junction reverse
biased by 40 thermal voltages carries less than $e^{-40} I_S$, which is
nothing.

## Audio-thread hygiene

- **No allocation, locks or system calls in the render path.** Every buffer
  is sized in `prepare()`.
- **UI-to-audio communication uses atomics.** The voice push buttons set
  bits in a `std::atomic<uint32_t>`; the audio thread reads the bits at the
  start of each block.
- **Audio-to-UI communication uses atomics too.** The current clock pulse
  and the down-beat lamp are atomics that the UI polls 30 times a second.
- **Expensive changes are rate-limited.** At most one trimmer change is
  applied per audio block.
- **Denormals are flushed** (`juce::ScopedNoDenormals`). Decaying ringing
  circuits are exactly where tiny denormal numbers appear, and they can be
  100× slower on some CPUs.
