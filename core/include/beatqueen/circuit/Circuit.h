#pragma once

// A small real-time circuit simulator for the MRK-1 voice circuits.
//
// Circuits are built from resistors, capacitors, inductors, ideal switches,
// current sources, diodes and NPN transistors, using component values taken
// straight from the service manual schematics. prepare() then:
//
//   1. solves the DC operating point (Newton-Raphson, with source stepping),
//   2. discretizes the linear part with the trapezoidal rule and inverts the
//      nodal matrix once for every combination of switch states,
//   3. reduces the nonlinear devices to a handful of junction voltages
//      ("K-method"), so each sample costs one matrix-vector product plus a
//      tiny Newton solve over just those junctions.
//
// The result behaves like a SPICE transient run of the same netlist, but is
// cheap enough to run every voice in real time.

#include <cstdint>
#include <vector>

namespace beatqueen::circuit {

using NodeId = int;
constexpr NodeId kGround = 0;

// Gummel-Poon style parameters for a generic small-signal silicon NPN
// (2N3904-class). The MRK-1 used house-numbered parts; see docs/DESIGN.md.
struct BjtModel
{
    double is = 6.734e-15;
    double betaF = 200.0;
    double betaR = 0.7371;
    double earlyVoltage = 74.0;
};

// Generic small-signal silicon diode (1N4148-class).
struct DiodeModel
{
    double is = 2.52e-9;
    double n = 1.752;
};

class Circuit
{
public:
    // --- Building -------------------------------------------------------
    NodeId addNode();
    NodeId addRail(double volts); // ideal fixed-voltage node (power rail)

    int addResistor(NodeId a, NodeId b, double ohms); // returns an index for setResistor()
    void addCapacitor(NodeId a, NodeId b, double farads);
    void addInductor(NodeId a, NodeId b, double henries);
    void addDiode(NodeId anode, NodeId cathode, DiodeModel model = {});
    void addBjt(NodeId collector, NodeId base, NodeId emitter, BjtModel model = {});

    // An ideal switch with the given on-resistance; open until set.
    int addSwitch(NodeId a, NodeId b, double onOhms);

    // A current source pushing current out of `from` and into `to`.
    int addCurrentSource(NodeId from, NodeId to);

    // The trapezoidal rule compresses frequencies near Nyquist (a 9.7 kHz tank
    // at 48 kHz would ring 11% flat). Setting the circuit's main resonance here
    // pre-warps every capacitor and inductor so that frequency is exact.
    // Call before prepare(); 0 (the default) means no pre-warping.
    void setPrewarpFrequency(double hz) { prewarpHz_ = hz; }

    // --- Running --------------------------------------------------------
    // Solves the operating point (with the switches and sources as currently
    // set) and prepares the discrete-time model. The nodal matrix for each
    // switch combination is built the first time that combination is used;
    // storage for all of them is allocated here.
    void prepare(double sampleRate);

    // Re-solves the operating point with the switches and sources as currently
    // set, and returns the circuit to it.
    void updateOperatingPoint();

    // Changes a resistor (e.g. a trimmer) and re-solves the operating point.
    // Returns the circuit to that operating point. Allocation-free after prepare().
    void setResistor(int index, double ohms);

    // Returns every node to the operating point found by prepare().
    void resetToOperatingPoint();

    void setSwitch(int index, bool closed);
    void setCurrent(int index, double amps) { sources_[static_cast<size_t>(index)].amps = amps; }

    // Advances one sample.
    void step();

    double voltage(NodeId node) const;
    double operatingPointVoltage(NodeId node) const;

    // Largest node-voltage difference from the operating point (for sleep detection).
    double deviationFromOperatingPoint() const;

    int numFreeNodes() const { return numFree_; }

    // Samples whose Newton solve hit its iteration limit (diagnostics).
    long long nonConvergedSamples() const { return nonConverged_; }

private:
    struct TwoTerminal
    {
        NodeId a, b;
        double value;
    };
    struct Switch
    {
        NodeId a, b;
        double onOhms;
    };
    struct Source
    {
        NodeId from, to;
        double amps = 0.0;
    };
    struct Device
    {
        enum class Kind { Diode, Bjt } kind;
        NodeId n1, n2, n3; // diode: anode, cathode; bjt: c, b, e
        DiodeModel diode;
        BjtModel bjt;
    };

    // Index of a node in the unknown vector, or -1 for ground/rails.
    int freeIndex(NodeId n) const { return nodeFree_[static_cast<size_t>(n)]; }
    double fixedVoltage(NodeId n) const { return nodeFixed_[static_cast<size_t>(n)]; }
    double nodeVoltage(const std::vector<double>& v, NodeId n) const;

    struct Configuration
    {
        bool built = false;
        std::vector<double> m;      // N x N inverse nodal matrix
        std::vector<double> md;     // N x Q  (M * D)
        std::vector<double> k;      // P x Q  (-E * M * D)
        std::vector<double> bconst; // N      (rail contributions)
    };

    void stampConductance(std::vector<double>& g, std::vector<double>& b, NodeId a, NodeId c, double siemens,
                          double railScale) const;
    void buildConfiguration(uint32_t switchMask, bool dc, double railScale, Configuration& out);
    const Configuration& configuration(uint32_t switchMask);

    // Device currents i(p) and their Jacobian. Ports and currents are numbered
    // together (a diode adds one of each, a transistor two), so di/dp is block
    // diagonal: gSelf[k] = di_k/dp_k and gCross[k] = di_k/dp_partner(k).
    void evaluateDevices(const double* p, double* i, double* gSelf, double* gCross) const;
    void limitJunctions(const double* pOld, double* pNew) const;
    bool solveDevices(const double* p0, const double* k, double* p, double* i, int maxIterations);
    void buildIndexTables();

    // From scratch (source stepping), or starting from the last operating point.
    void solveOperatingPoint(bool fromOperatingPoint);

    // Topology
    std::vector<int> nodeFree_ { -1 };
    std::vector<double> nodeFixed_ { 0.0 };
    int numFree_ = 0;

    std::vector<TwoTerminal> resistors_, capacitors_, inductors_;
    std::vector<Switch> switches_;
    std::vector<Source> sources_;
    std::vector<Device> devices_;

    struct DeviceBlock
    {
        int port, ports, current, currents;
    };
    std::vector<DeviceBlock> deviceBlocks_;

    // Nonlinear ports: (plus, minus) node pairs, and current incidence (out, in).
    std::vector<NodeId> portPlus_, portMinus_, currentOut_, currentIn_;
    int numPorts_ = 0, numCurrents_ = 0;

    // Flattened per-sample lookups, built in prepare().
    struct Branch
    {
        int a, b;              // free indices, -1 for ground/rail
        double fixedA, fixedB; // rail voltage where the index is -1
        double geq;            // companion-model conductance
    };
    std::vector<Branch> capBranches_, indBranches_;
    std::vector<int> sourceFrom_, sourceTo_, portPlusIdx_, portMinusIdx_, partner_;
    std::vector<double> portPlusFixed_, portMinusFixed_, portNVt_, portVCrit_;

    // Discrete-time model, one entry per switch configuration.
    double sampleRate_ = 48000.0;
    double prewarpHz_ = 0.0;
    double sMap_ = 96000.0; // s -> sMap * (z - 1) / (z + 1); 2 * sampleRate unless pre-warped
    std::vector<Configuration> configs_;
    Configuration dcConfig_;
    uint32_t switchMask_ = 0;
    long long nonConverged_ = 0;

    // State
    std::vector<double> v_, vOp_;
    std::vector<double> capHist_, capHistOp_, indHist_, indHistOp_;
    std::vector<double> p_, pOp_, i_;

    // Scratch (sized in prepare, reused per sample)
    std::vector<double> rhs_, v0_, p0_, newtonJ_, newtonR_, gSelf_, gCross_, pTrial_, pSave_, invScratch_;
};

} // namespace beatqueen::circuit
