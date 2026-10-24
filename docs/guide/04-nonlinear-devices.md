# 4. Diodes, transistors and Newton's method

Linear circuits can't make drum sounds. The MRK-1's character comes from:

- transistors that saturate and cut off;
- diodes that rectify noise;
- gates that open only above a threshold.

This page adds those devices.

## Device equations

**Diode** (Shockley equation). The current depends exponentially on the
voltage across it:

$$i = I_S\left(e^{v/(nV_T)} - 1\right)$$

- $I_S$ is the saturation current, about 2.5 nA for a 1N4148.
- $n$ is the emission coefficient, about 1.75.
- $V_T = kT/q \approx 25.85$ mV at room temperature.

**NPN transistor** (Ebers-Moll transport model with the Early effect). The
transistor has two junctions, base-emitter ($v_{be}$) and base-collector
($v_{bc}$):

$$I_F = I_S(e^{v_{be}/V_T} - 1),\qquad I_R = I_S(e^{v_{bc}/V_T} - 1)$$

$$i_c = (I_F - I_R)\left(1 - \frac{v_{bc}}{V_A}\right) - \frac{I_R}{\beta_R},\qquad i_b = \frac{I_F}{\beta_F} + \frac{I_R}{\beta_R}$$

In normal operation $I_F$ dominates: $i_c \approx \beta_F i_b$. When the
collector falls to near the emitter voltage, $v_{bc}$ turns positive,
$I_R$ grows, and the transistor **saturates**. The MRK-1 voices do exactly
this on every loud hit.

The Early voltage $V_A$ (74 V) models how collector voltage slightly changes
gain. Leaving it out shifted the bass drum's bias point by 1% compared with
SPICE; adding it closed the gap.

Beat Queen uses 2N3904-class parameters ($I_S$ = 6.7 fA, $\beta_F$ = 200),
because the MRK-1's transistors carry only Gibson house numbers.

## Newton-Raphson

With exponential devices, $\mathbf{G}\mathbf{v} = \mathbf{i}$ becomes
$\mathbf{F}(\mathbf{v}) = 0$ for a nonlinear $\mathbf{F}$. Newton's method
works like this:

1. Guess $\mathbf{v}$.
2. Linearize every device around the guess. A diode becomes a conductance
   $g = di/dv$ plus a current source.
3. Solve the linear system.
4. Repeat until the update is tiny.

Near the answer, Newton converges quadratically: the number of correct
digits roughly doubles each iteration. Far from it, exponentials make it
overshoot wildly. One step can ask a diode for $e^{400}$.

**Junction limiting** (SPICE's `pnjlim`) prevents this. If a proposed
junction voltage jumps far beyond the "critical" voltage where the
exponential gets steep, it is replaced with a logarithmic step:

$$v_{new} \leftarrow v_{old} + nV_T\ln\!\left(1 + \frac{v_{new}-v_{old}}{nV_T}\right)$$

Beat Queen's `limitJunction()` is a direct implementation.

## The DC operating point

Before time-stepping you need the circuit's resting state, its **bias**:

- Capacitors are open circuits at DC.
- Inductors are short circuits.
- Solve the nonlinear system.

The difficulty is the starting point. If every node starts at 0 V,
Newton can wander. **Source stepping** fixes this: solve with every supply at
5% of its value, use that answer as the next starting guess at 10%, and so
on up to 100%. Each step starts close to its answer. Beat Queen uses 20
steps (`Circuit::solveOperatingPoint`). When a trimmer moves, it re-solves
starting from the previous answer instead, which takes one step.

Check the operating point against SPICE's `.op` before trusting anything
else. For the bass drum, Beat Queen gives $V_c$ = 6.508 V, $V_b$ = 0.730 V,
$V_e$ = 0.1156 V. ngspice gives 6.511 V, 0.730 V and 0.1156 V.

## The K-method: Newton on the junctions only

A naive simulator rebuilds and refactors the whole $N \times N$ matrix on
every Newton iteration, every sample. For a 14-node voice that's wasteful,
because only a handful of numbers are nonlinear: the junction voltages.

Split the circuit into its linear part, which we can pre-invert, and the
devices. Treat the devices as current sources $\mathbf{i}_d$ whose values
depend on a short vector of **port voltages**
$\mathbf{p} = (v_{be}, v_{bc}, v_{diode}, \dots)$:

$$\mathbf{v} = \mathbf{M}(\mathbf{b} + \mathbf{h}) - \mathbf{M}\mathbf{D}\,\mathbf{i}_d(\mathbf{p})$$

- $\mathbf{b}$ is the rails.
- $\mathbf{h}$ is the companion-model histories.
- $\mathbf{D}$ maps each device current to the nodes it leaves and enters.

The port voltages are differences of node voltages, $\mathbf{p} = \mathbf{E}\mathbf{v}$, so:

$$\mathbf{p} = \underbrace{\mathbf{E}\mathbf{M}(\mathbf{b}+\mathbf{h})}_{\mathbf{p}_0} \;+\; \underbrace{(-\mathbf{E}\mathbf{M}\mathbf{D})}_{\mathbf{K}}\;\mathbf{i}_d(\mathbf{p})$$

Each sample:

1. Compute $\mathbf{p}_0$ with one matrix-vector product.
2. Solve the small nonlinear system $\mathbf{p} = \mathbf{p}_0 + \mathbf{K}\,\mathbf{i}_d(\mathbf{p})$
   with Newton. Its Jacobian is $\mathbf{I} - \mathbf{K}\,\partial\mathbf{i}_d/\partial\mathbf{p}$.
3. Recover all node voltages with $\mathbf{v} = \mathbf{v}_0 - \mathbf{M}\mathbf{D}\,\mathbf{i}_d$.

For a ringing voice that's a 4×4 Newton solve (two transistors) instead of
14×14. $\mathbf{K}$ and $\mathbf{M}\mathbf{D}$ are precomputed with
$\mathbf{M}$.

The idea comes from the literature on real-time circuit simulation for
audio, where it's known as the K-method or "nodal DK" method. It is exact,
not an approximation. It's still the full trapezoidal simulation, just
solved in a smarter order.

## Two more details that make it work

- **Block-diagonal Jacobian.** A device's currents depend only on its own
  ports, so $\partial\mathbf{i}_d/\partial\mathbf{p}$ is block diagonal:
  1×1 for each diode, 2×2 for each transistor. Building
  $\mathbf{K}\,\partial\mathbf{i}/\partial\mathbf{p}$ visits only those
  blocks.
- **First-order final correction.** When Newton's last step $\Delta p$ is
  below tolerance, update the currents by $g\,\Delta p$ instead of
  re-evaluating every exponential. The remaining error is then second order
  in the tolerance. That lets the tolerance be a relaxed 10 µV.

## Robustness

A hard transient, such as a transistor slamming into saturation while noise
drives it, can occasionally need dozens of iterations. Beat Queen allows up
to 60. If a solve still fails, it holds the junctions at their previous
values rather than return garbage. Before this was added, one such sample
produced a 130 V spike at the output. Page 9 describes how it was found.
