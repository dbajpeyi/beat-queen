# 3. Simulating linear circuits

This page builds the core of a circuit simulator: resistors, capacitors,
inductors, current sources and fixed supply voltages. Page 4 adds diodes and
transistors.

## Nodal analysis

A circuit is a set of **nodes** joined by components. One node is
**ground** (0 V). The unknowns are the voltages at every other node.

**Kirchhoff's current law** says the currents into a node sum to zero. For a
resistor between nodes $a$ and $b$, the current from $a$ to $b$ is
$G(v_a - v_b)$, where $G = 1/R$ is the conductance. Writing the law for every
node gives a linear system:

$$\mathbf{G}\,\mathbf{v} = \mathbf{i}$$

- $\mathbf{v}$ is the node voltages.
- $\mathbf{G}$ is the conductance matrix.
- $\mathbf{i}$ is the current injected into each node by sources.

You build $\mathbf{G}$ by **stamping** each component into it:

```
resistor a-b, conductance g:      G[a][a] += g    G[b][b] += g
                                  G[a][b] -= g    G[b][a] -= g
current source pushing I into a:  i[a] += I
```

If one end is ground, only the diagonal entry of the other end is stamped.

**Supply rails.** A rail is a node held at a fixed voltage. Beat Queen
doesn't make rails unknowns. A resistor $g$ from free node $a$ to a rail at
$V$ stamps `G[a][a] += g` and moves the known part to the right-hand side as
`i[a] += g * V`. See `Circuit::stampConductance`.

**A small worked example.** Take a 17 V rail feeding 2.2K into node $p$,
with 33K from $p$ to ground:

$$\left(\tfrac{1}{2200} + \tfrac{1}{33000}\right) v_p = \tfrac{17}{2200} \;\Rightarrow\; v_p = 15.94\ \text{V}$$

## Capacitors and inductors: companion models

A capacitor's current depends on how fast its voltage changes,
$i = C\,dv/dt$, so the system becomes a differential equation. To step it
through time, approximate the derivative over one sample period $T$.

The **trapezoidal rule** averages the derivative at both ends of the step:

$$\frac{i_{n+1} + i_n}{2} = C\,\frac{v_{n+1} - v_n}{T}$$

Rearranged for the new current:

$$i_{n+1} = \underbrace{\tfrac{2C}{T}}_{G_{eq}}\,v_{n+1} \;-\; \underbrace{\left(\tfrac{2C}{T}v_n + i_n\right)}_{J_n}$$

So during one time step, a capacitor looks like a **resistor $G_{eq}$ in
parallel with a current source $J_n$** that carries its history. This is
its **companion model**. Stamp $G_{eq}$ like a resistor, and inject $J_n$
into node $a$ (and out of node $b$). After solving, update the history:

$$J_{n+1} = 2G_{eq}\,v_{n+1} - J_n$$

An inductor ($v = L\,di/dt$) works the same way, with
$G_{eq} = T/(2L)$ and $K_{n+1} = K_n + 2G_{eq}v_{n+1}$.

Two consequences matter a lot:

- **$\mathbf{G}$ does not change from sample to sample.** The $G_{eq}$
  values depend only on $C$, $L$ and $T$. Only the right-hand side changes.
- So you can **invert $\mathbf{G}$ once**, call the inverse $\mathbf{M}$,
  and each sample is just $\mathbf{v} = \mathbf{M}\,\mathbf{i}$: a
  matrix-vector product. Page 5 builds on this.

Each sample, then:

```
rhs  = constant rail terms
rhs += capacitor and inductor history sources
rhs += input currents
v    = M * rhs
update each capacitor's and inductor's history from v
```

This is `Circuit::step()` without the nonlinear part.

## Why trapezoidal?

| Method | Stability | Accuracy | Character |
|---|---|---|---|
| Forward Euler | Can blow up on stiff circuits | 1st order | Unusable for audio circuits |
| Backward Euler | Always stable | 1st order | Adds damping: resonances decay too fast |
| **Trapezoidal** | Stable for passive circuits | 2nd order | **No artificial damping**, but squeezes frequencies (below) |

For drum voices whose whole character is a decaying resonance, adding fake
damping would change the decay time the trimmers are meant to set. The
trapezoidal rule preserves it.

## Frequency warping and pre-warping

The trapezoidal rule is the **bilinear transform**: it replaces $s$ in the
circuit's continuous-time behavior with $\frac{2}{T}\frac{z-1}{z+1}$. That
maps all continuous frequencies from 0 to ∞ onto 0 to Nyquist, so they get
squeezed:

$$\omega_{\text{digital}} = \frac{2}{T}\arctan\!\left(\frac{\omega T}{2}\right)$$

At 48 kHz:

| Circuit resonance | Simulated at | Error |
|---|---|---|
| 53 Hz (bass drum) | 53.0 Hz | ~0 |
| 2.2 kHz (clave) | 2.19 kHz | −0.7% |
| 3.7 kHz (hi-hat tank) | 3.64 kHz | −1.9% |
| 9.7 kHz (cymbal tank) | 8.6 kHz | **−11%** |

The fix is **pre-warping**. Choose the frequency $\omega_0$ that matters
most in a circuit, and use

$$c = \frac{\omega_0}{\tan(\omega_0 T/2)}\quad\text{instead of}\quad \frac{2}{T}$$

in every companion model: $G_{eq} = cC$ for capacitors and $1/(cL)$ for
inductors. The resonance at $\omega_0$ is then exact. Nearby frequencies are
almost exact. Beat Queen pre-warps each circuit at its main resonance (see
`Circuit::setPrewarpFrequency`), and the test `circuit: LC resonance` checks
the hi-hat tank at 3714 Hz.

## Switches

An ideal switch is a resistor that is either present (a small on-resistance)
or absent. Each combination of switch states gives a different $\mathbf{G}$,
so a different inverse $\mathbf{M}$. Page 5 explains how to keep that cheap.

## Exercise

Simulate 10 V switching through 10K into 1 µF at 48 kHz, and compare with
$v(t) = 10(1 - e^{-t/RC})$. You will see an error of about half a sample of
timing. That's the trapezoidal rule treating a step input as arriving
midway through the step. The `circuit: RC step response` test does exactly
this.
