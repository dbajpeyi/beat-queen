# 2. From scanned schematic to netlist

A **netlist** is a list of components and the nodes they connect: what
SPICE takes as input, and what Beat Queen's simulator takes too. Getting it
right is the most important step. A perfect simulator of the wrong circuit
is worthless.

## Getting a usable image

Service manuals usually exist as scanned PDFs. This one was 300 dpi, black
and white, with the large drawings turned sideways. To work with it:

1. **Extract pages at the scan's native resolution.** The command
   `pdfimages -list` shows the embedded image resolution; render at that
   resolution and no higher (`pdftoppm -r 300 -gray`). Higher only invents
   pixels.
2. **Rotate the landscape drawings upright** before reading them.
3. **Crop each sub-circuit and zoom 1.5-2×.** Part labels like `.0047` are
   a few pixels tall. Reading them in context at full-page scale is how
   mistakes happen.
4. **Keep the crops.** You will come back to the same few square
   centimetres many times.

## Decimal points that aren't there

Capacitor values in 1960s schematics are in microfarads unless marked, and
the leading zero and decimal point are often omitted or lost in the scan. On
the MRK-1 instrumentation board you will read values like `1`, `12`, `15`,
`22`, `56` and `39`.

Read literally, those would be 1-56 µF. That's absurd in most of these
positions. A "15 µF" capacitor across the bass drum's output would make it
inaudible. The fix is the **parts list**. The MRK-1's instrumentation parts
list names only Mylar capacitors from 0.0015 µF to 0.56 µF, plus one 50 µF
electrolytic in the power supply. So:

| Printed | Actual | Where |
|---|---|---|
| `1` | 0.1 µF | pulse amp collector caps, one-shot timing |
| `12` | 0.12 µF | bass drum coupling, tom pulse amp, snare trigger node |
| `15` | 0.15 µF | bass drum output filter |
| `22` | 0.22 µF | bass drum input, hi-hat one-shot, hi-hat push button |
| `56` | 0.56 µF | snare trigger timing |
| `39` | 0.39 µF | cymbal push button |

**Lesson:** always cross-check against the parts list, and sanity-check
every value against what the part is doing. A coupling capacitor, a timing
capacitor and a filter capacitor each have a plausible range.

## Reading a sub-circuit

Take the bass drum (Q32/Q33 on Drawing No. 4). Work node by node, writing
down each part and the two nodes it connects:

```
* Bass drum pulse amp
RK    key  k1    15k        keying input resistor
CK    k1   bpa   0.22u      keying input capacitor
RBPA  bpa  0     100k       base pull-down
QPA   cpa  bpa   0          pulse amp transistor (collector, base, emitter)
CPA   cpa  0     0.1u       collector capacitor ("1" on the drawing)
R33K  p    cpa   33k
R2K2  vcc  p     2.2k
* Twin-T generator
RP    p    m     12k        pulse amp feeds the twin-T centre
C1    a    m     0.082u     series capacitor arm
C2    m    b     0.082u
R1    a    n     56k        series resistor arm
R2    n    b     56k
C3    n    0     0.082u     shunt capacitor
QG    c    b     e          generator transistor
RC    vcc  c     68k        collector load ("69K" on the drawing: no such E12 value)
RF    c    b     1.2meg     bias feedback
CC    c    a     0.12u      collector coupling ("12")
RE    e    0     270+trim   270 ohm + VR1 (1K rheostat)
* Output to the summing bus
RO1   a    o1    33k
CO1   o1   0     0.15u      ("15")
RO2   o1   bus   22k
```

Things to watch for:

- **Crossing lines without a dot are not connected.**
- **Potentiometers wired as rheostats.** The wiper is joined to one end, so
  the pot behaves as a variable resistor, not a voltage divider. VR1-VR12
  are all rheostats.
- **Diode orientation.** It decides whether a clamp drains an envelope or
  props it up. On the MRK-1, the "choke" diodes (D17, D25) point *towards*
  the +6 V supply. A charged envelope line therefore discharges into
  +6 V through 47K until it reaches about 6.6 V. That is the "drops to about
  one third" the manual describes.
- **Labels that belong to something else.** "HI-HAT CYMBAL PUSH BUTTON" is
  printed right next to keying point "D". It names the button drawn below
  it, not the keying point.
- **Keying points** (boxed letters like `BD`, `X`, `med`, `long`, `A`, `C`)
  are where other boards connect. Find every one and work out what drives it.

## Switches: buttons and keying points

Anything that changes the circuit's topology becomes a **switch** in the
model:

- **A voice push button** moves a contact from ground to +17 V. That's two
  switches: one closed at rest, one closed while pressed.
- **A clock pulse arriving at a keying point** works the same way: the
  keying point is driven high for a few milliseconds.
- **A rhythm switch connecting a control function**, such as "med" to
  ground, is one switch that stays closed while that rhythm is selected.
- **A transistor used purely as a switch** can sometimes be modeled as one,
  for example a one-shot's output stage. Beat Queen keeps the pulse amp and
  voice transistors as real transistors. It models logic-level stages
  (one-shots, the snare multivibrator outputs, the clock) as switches with
  timing computed from their RC values.

## Check the netlist independently

Before writing any C++, run the netlist in **ngspice**, a free SPICE
simulator, and confirm it does something sensible. It should ring, decay
and sit at a plausible DC bias. `tools/spice/generators.py` does this for
all eight ringing voices. If SPICE doesn't produce a drum sound, your
netlist is wrong, not your simulator. Fix it now, while it's easy.

## When a value can't be read at all

Some things are not in the manual at all. Examples: the exact transistor
type (Gibson used house part numbers), the amplitude of the noise source,
and the width of a clock pulse. Treat each one as an explicit
**assumption**:

- Write it down where it's used. Beat Queen's source marks these
  `ASSUMPTION`.
- Pick a physically reasonable value.
- Later, calibrate it against what the manual says the machine should do
  (page 9).
