#!/usr/bin/env python3
"""Reference simulations of the MRK-1 twin-T voice generators in ngspice.

Writes one netlist per generator (Drawing No. 4 component values), runs a
transient with ngspice, and prints the ringing frequency and -40 dB decay time
that tests/core_tests.cpp checks the real-time circuit models against.

    python3 tools/spice/generators.py [output-dir]

Requires ngspice and numpy. `x` is the length trimmer's resistance as a
fraction of its 1K travel; Beat Queen's trim position is 1 - x.
"""

import os
import subprocess
import sys

import numpy as np

# name: C-arm series caps, R-arm shunt cap, pulse-amp feed resistor, pulse-amp
# collector cap, collector coupling cap, emitter series resistor, output network.
GENERATORS = {
    "bass_drum": dict(c1="0.082u", cs="0.082u", rp="12k", cpa="0.1u", cc="0.12u", rs=270,
                      out="RO1 a o1 33k\nCO1 o1 0 0.15u\nRO2 o1 bus 22k"),
    "tom_tom": dict(c1="0.027u", cs="0.047u", rp="6.8k", cpa="0.12u", cc="0.047u", rs=470,
                    out="RO1 a o1 33k\nCO1 o1 0 0.01u\nRO2 o1 bus 82k"),
    "bongo": dict(c1="0.015u", cs="0.047u", rp="6.8k", cpa="0.1u", cc="0.047u", rs=820,
                  out="RO1 a o1 33k\nCO1 o1 0 0.01u\nRO2 o1 bus 47k"),
    "blocks": dict(c1="3.3n", cs="0.01u", rp="6.8k", cpa="0.1u", cc="0.047u", rs=820, out="RO1 a bus 220k"),
    "clave": dict(c1="1.5n", cs="4.7n", rp="6.8k", cpa="0.022u", cc="0.047u", rs=820, out="RO1 a bus 180k"),
    "rim_shot": dict(c1="1.5n", cs="8.2n", rp="6.8k", cpa="0.1u", cc="0.047u", rs=560, out="RO1 a bus 47k"),
    "hi_drum": dict(c1="8.2n", cs="0.022u", rp="3.3k", cpa="0.1u", cc="0.047u", rs=1200,
                    out="RO1 a o1 150k\nCO1 o1 bus 1.5n"),
    "lo_drum": dict(c1="0.012u", cs="0.033u", rp="4.7k", cpa="0.1u", cc="0.047u", rs=1000,
                    out="RO1 a o1 100k\nCO1 o1 bus 3.3n"),
}

# Trimmer positions used by the test reference table (x = 1 - trim).
REFERENCE_X = {"blocks": 1.0, "clave": 1.0}

NETLIST = """* MRK-1 {name} generator (service manual Dwg 4)
.model NPN1 NPN(IS=6.734f BF=200 VAF=74 IKF=66.78m XTB=1.5 BR=.7371 CJC=3.638p CJE=4.493p TR=239.5n TF=301.2p)
VCC vcc 0 17
* keying pulse into the pulse amp
VK key 0 PULSE(0 15 20m 10u 10u 1m 1)
RK key k1 10k
CK k1 bpa 0.1u
RBPA bpa 0 100k
QPA cpa bpa 0 NPN1
CPA cpa 0 {cpa}
R33K p cpa 33k
R2K2 vcc p 2.2k
RP p m {rp}
* twin-T: C arm a-m-b, R arm a-n-b
C1 a m {c1}
C2 m b {c1}
R1 a n 56k
R2 n b 56k
C3 n 0 {cs}
QG c b e NPN1
RC vcc c 68k
RF c b 1.2meg
CC c a {cc}
RE e 0 {re}
{out}
* summing bus load
RBUS bus 0 10k
.tran 10u 1.0 0 10u
.control
run
wrdata {data} v(bus)
.endc
.end
"""


def analyze(path, trigger=0.02, fs=48000.0):
    d = np.loadtxt(path)
    t, v = d[:, 0], d[:, 1]
    tu = np.arange(t[0], t[-1], 1 / fs)
    vu = np.interp(tu, t, v)
    dc = vu[(tu > trigger - 0.01) & (tu < trigger)].mean()
    seg = vu[tu >= trigger] - dc
    s = seg[int(0.005 * fs):int(0.2 * fs)]
    zc = np.where(np.diff(np.signbit(s)))[0]
    freq = (len(zc) - 1) / 2 / ((zc[-1] - zc[0]) / fs) if len(zc) > 2 else float("nan")
    win = int(0.01 * fs)
    env = np.array([np.abs(seg[i:i + win]).max() for i in range(0, len(seg) - win, win)])
    ipk = env.argmax()
    below = np.where(env[ipk:] < env[ipk] * 0.01)[0]
    decay = below[0] * 0.01 if len(below) else float("inf")
    return freq, decay


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "spice-out"
    os.makedirs(out_dir, exist_ok=True)
    for name, g in GENERATORS.items():
        x = REFERENCE_X.get(name, 0.5)
        data = os.path.join(out_dir, f"{name}.txt")
        cir = os.path.join(out_dir, f"{name}.cir")
        with open(cir, "w") as f:
            f.write(NETLIST.format(name=name, re=g["rs"] + 1000 * x, data=data, **g))
        subprocess.run(["ngspice", "-b", cir], check=True, capture_output=True)
        freq, decay = analyze(data)
        print(f"{name:10s} trim {1 - x:.2f}: {freq:8.1f} Hz, -40 dB decay {decay:.2f} s")


if __name__ == "__main__":
    main()
