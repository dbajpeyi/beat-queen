#include "beatqueen/circuit/Circuit.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace beatqueen::circuit {

namespace {

constexpr double kThermalVoltage = 0.025852; // kT/q at 300 K
constexpr double kGmin = 1e-12;              // keeps every node referenced to ground
constexpr double kDcInductorSiemens = 1e3;   // inductors are shorts at DC
constexpr double kMaxExponent = 80.0;
constexpr int kSourceSteps = 20;             // DC solve ramps the rails up in this many steps
// Converged currents are corrected to first order in the last step, so the
// remaining error is second order in this tolerance.
constexpr double kNewtonToleranceVolts = 1e-5;
constexpr int kMaxNewtonIterations = 60;

// Inverts the n x n row-major matrix `a` in place (Gauss-Jordan, partial pivoting).
// `scratch` must hold n*n values.
void invert(double* a, double* scratch, int n)
{
    double* inv = scratch;
    std::fill(inv, inv + n * n, 0.0);
    for (int i = 0; i < n; ++i)
        inv[i * n + i] = 1.0;

    for (int col = 0; col < n; ++col)
    {
        int pivot = col;
        for (int r = col + 1; r < n; ++r)
            if (std::fabs(a[r * n + col]) > std::fabs(a[pivot * n + col]))
                pivot = r;
        if (a[pivot * n + col] == 0.0)
            throw std::runtime_error("circuit: singular nodal matrix");
        if (pivot != col)
            for (int c = 0; c < n; ++c)
            {
                std::swap(a[pivot * n + c], a[col * n + c]);
                std::swap(inv[pivot * n + c], inv[col * n + c]);
            }

        const double d = 1.0 / a[col * n + col];
        for (int c = 0; c < n; ++c)
        {
            a[col * n + c] *= d;
            inv[col * n + c] *= d;
        }
        for (int r = 0; r < n; ++r)
        {
            const double f = a[r * n + col];
            if (r == col || f == 0.0)
                continue;
            for (int c = 0; c < n; ++c)
            {
                a[r * n + c] -= f * a[col * n + c];
                inv[r * n + c] -= f * inv[col * n + c];
            }
        }
    }
    std::copy(inv, inv + n * n, a);
}

// Solves a x = b in place for small n (partial pivoting). Returns false if singular.
bool solveSmall(double* a, double* b, int n)
{
    for (int col = 0; col < n; ++col)
    {
        int pivot = col;
        for (int r = col + 1; r < n; ++r)
            if (std::fabs(a[r * n + col]) > std::fabs(a[pivot * n + col]))
                pivot = r;
        if (std::fabs(a[pivot * n + col]) < 1e-300)
            return false;
        if (pivot != col)
        {
            for (int c = 0; c < n; ++c)
                std::swap(a[pivot * n + c], a[col * n + c]);
            std::swap(b[pivot], b[col]);
        }
        for (int r = col + 1; r < n; ++r)
        {
            const double f = a[r * n + col] / a[col * n + col];
            if (f == 0.0)
                continue;
            for (int c = col; c < n; ++c)
                a[r * n + c] -= f * a[col * n + c];
            b[r] -= f * b[col];
        }
    }
    for (int r = n - 1; r >= 0; --r)
    {
        double s = b[r];
        for (int c = r + 1; c < n; ++c)
            s -= a[r * n + c] * b[c];
        b[r] = s / a[r * n + r];
    }
    return true;
}

// SPICE-style junction voltage limiting, keeps Newton from overshooting exponentials.
double limitJunction(double vNew, double vOld, double nVt, double vCrit)
{
    if (vNew > vCrit && std::fabs(vNew - vOld) > 2.0 * nVt)
    {
        if (vOld > 0.0)
        {
            const double arg = 1.0 + (vNew - vOld) / nVt;
            return arg > 0.0 ? vOld + nVt * std::log(arg) : vCrit;
        }
        return nVt * std::log(vNew / nVt);
    }
    return vNew;
}

double criticalVoltage(double is, double nVt) { return nVt * std::log(nVt / (std::sqrt(2.0) * is)); }

// exp() of a normalized junction voltage. A junction reverse-biased by more than
// ~40 thermal voltages contributes nothing measurable, so skip it there.
// Otherwise exp(x) = 2^n * 2^f with |f| <= 0.5 and 2^f from its Taylor series
// to degree 10 (relative error < 1e-11), which is several times faster than
// std::exp and plenty for Newton iterations.
inline double junctionExp(double x)
{
    if (x < -40.0)
        return 0.0;
    x = std::min(x, kMaxExponent);
    const double t = x * 1.4426950408889634; // log2(e)
    const double n = std::nearbyint(t);
    const double y = (t - n) * 0.6931471805599453; // |y| <= 0.347
    double p = 1.0 / 3628800.0;
    p = p * y + 1.0 / 362880.0;
    p = p * y + 1.0 / 40320.0;
    p = p * y + 1.0 / 5040.0;
    p = p * y + 1.0 / 720.0;
    p = p * y + 1.0 / 120.0;
    p = p * y + 1.0 / 24.0;
    p = p * y + 1.0 / 6.0;
    p = p * y + 0.5;
    p = p * y + 1.0;
    p = p * y + 1.0;
    // Scale by 2^n by building the exponent bits directly (n is within +-116 here).
    const uint64_t bits = static_cast<uint64_t>(static_cast<int64_t>(n) + 1023) << 52;
    double scale;
    std::memcpy(&scale, &bits, sizeof scale);
    return p * scale;
}

} // namespace

// ---------------------------------------------------------------------------
// Building

NodeId Circuit::addNode()
{
    nodeFree_.push_back(numFree_++);
    nodeFixed_.push_back(0.0);
    return static_cast<NodeId>(nodeFree_.size() - 1);
}

NodeId Circuit::addRail(double volts)
{
    nodeFree_.push_back(-1);
    nodeFixed_.push_back(volts);
    return static_cast<NodeId>(nodeFree_.size() - 1);
}

int Circuit::addResistor(NodeId a, NodeId b, double ohms)
{
    resistors_.push_back({ a, b, ohms });
    return static_cast<int>(resistors_.size() - 1);
}

void Circuit::addCapacitor(NodeId a, NodeId b, double farads) { capacitors_.push_back({ a, b, farads }); }
void Circuit::addInductor(NodeId a, NodeId b, double henries) { inductors_.push_back({ a, b, henries }); }

void Circuit::addDiode(NodeId anode, NodeId cathode, DiodeModel model)
{
    deviceBlocks_.push_back({ numPorts_, 1, numCurrents_, 1 });
    devices_.push_back({ Device::Kind::Diode, anode, cathode, kGround, model, {} });
    portPlus_.push_back(anode);
    portMinus_.push_back(cathode);
    currentOut_.push_back(anode);
    currentIn_.push_back(cathode);
    ++numPorts_;
    ++numCurrents_;
}

void Circuit::addBjt(NodeId collector, NodeId base, NodeId emitter, BjtModel model)
{
    deviceBlocks_.push_back({ numPorts_, 2, numCurrents_, 2 });
    devices_.push_back({ Device::Kind::Bjt, collector, base, emitter, {}, model });
    // Ports: vbe, vbc. Currents: ic (collector -> emitter), ib (base -> emitter).
    portPlus_.insert(portPlus_.end(), { base, base });
    portMinus_.insert(portMinus_.end(), { emitter, collector });
    currentOut_.insert(currentOut_.end(), { collector, base });
    currentIn_.insert(currentIn_.end(), { emitter, emitter });
    numPorts_ += 2;
    numCurrents_ += 2;
}

int Circuit::addSwitch(NodeId a, NodeId b, double onOhms)
{
    switches_.push_back({ a, b, onOhms });
    return static_cast<int>(switches_.size() - 1);
}

int Circuit::addCurrentSource(NodeId from, NodeId to)
{
    sources_.push_back({ from, to, 0.0 });
    return static_cast<int>(sources_.size() - 1);
}

void Circuit::setSwitch(int index, bool closed)
{
    const uint32_t bit = 1u << index;
    switchMask_ = closed ? (switchMask_ | bit) : (switchMask_ & ~bit);
}

double Circuit::nodeVoltage(const std::vector<double>& v, NodeId n) const
{
    const int f = freeIndex(n);
    return f >= 0 ? v[static_cast<size_t>(f)] : fixedVoltage(n);
}

double Circuit::voltage(NodeId node) const { return nodeVoltage(v_, node); }
double Circuit::operatingPointVoltage(NodeId node) const { return nodeVoltage(vOp_, node); }

double Circuit::deviationFromOperatingPoint() const
{
    double d = 0.0;
    for (size_t i = 0; i < v_.size(); ++i)
        d = std::max(d, std::fabs(v_[i] - vOp_[i]));
    return d;
}

// ---------------------------------------------------------------------------
// Matrices

void Circuit::stampConductance(std::vector<double>& g, std::vector<double>& b, NodeId a, NodeId c, double siemens,
                               double railScale) const
{
    const int n = numFree_;
    const int fa = freeIndex(a), fc = freeIndex(c);
    if (fa >= 0)
        g[static_cast<size_t>(fa * n + fa)] += siemens;
    if (fc >= 0)
        g[static_cast<size_t>(fc * n + fc)] += siemens;
    if (fa >= 0 && fc >= 0)
    {
        g[static_cast<size_t>(fa * n + fc)] -= siemens;
        g[static_cast<size_t>(fc * n + fa)] -= siemens;
    }
    else if (fa >= 0)
        b[static_cast<size_t>(fa)] += siemens * fixedVoltage(c) * railScale;
    else if (fc >= 0)
        b[static_cast<size_t>(fc)] += siemens * fixedVoltage(a) * railScale;
}

void Circuit::buildConfiguration(uint32_t switchMask, bool dc, double railScale, Configuration& out)
{
    const int n = numFree_, q = numCurrents_, p = numPorts_;
    auto& m = out.m;
    std::fill(m.begin(), m.end(), 0.0);
    std::fill(out.bconst.begin(), out.bconst.end(), 0.0);

    for (int i = 0; i < n; ++i)
        m[static_cast<size_t>(i * n + i)] += kGmin;
    for (const auto& r : resistors_)
        stampConductance(m, out.bconst, r.a, r.b, 1.0 / r.value, railScale);
    for (size_t s = 0; s < switches_.size(); ++s)
        if ((switchMask & (1u << s)) != 0)
            stampConductance(m, out.bconst, switches_[s].a, switches_[s].b, 1.0 / switches_[s].onOhms, railScale);
    if (!dc)
        for (const auto& c : capacitors_)
            stampConductance(m, out.bconst, c.a, c.b, c.value * sMap_, railScale);
    for (const auto& l : inductors_)
        stampConductance(m, out.bconst, l.a, l.b, dc ? kDcInductorSiemens : 1.0 / (l.value * sMap_), railScale);

    invert(m.data(), invScratch_.data(), n);

    for (int col = 0; col < q; ++col)
    {
        const int from = freeIndex(currentOut_[static_cast<size_t>(col)]);
        const int to = freeIndex(currentIn_[static_cast<size_t>(col)]);
        for (int row = 0; row < n; ++row)
        {
            double s = 0.0;
            if (from >= 0)
                s += m[static_cast<size_t>(row * n + from)];
            if (to >= 0)
                s -= m[static_cast<size_t>(row * n + to)];
            out.md[static_cast<size_t>(row * q + col)] = s;
        }
    }

    for (int row = 0; row < p; ++row)
    {
        const int plus = freeIndex(portPlus_[static_cast<size_t>(row)]);
        const int minus = freeIndex(portMinus_[static_cast<size_t>(row)]);
        for (int col = 0; col < q; ++col)
        {
            double s = 0.0;
            if (plus >= 0)
                s += out.md[static_cast<size_t>(plus * q + col)];
            if (minus >= 0)
                s -= out.md[static_cast<size_t>(minus * q + col)];
            out.k[static_cast<size_t>(row * q + col)] = -s;
        }
    }
    out.built = true;
}

const Circuit::Configuration& Circuit::configuration(uint32_t switchMask)
{
    auto& cfg = configs_[switchMask];
    if (!cfg.built)
        buildConfiguration(switchMask, false, 1.0, cfg);
    return cfg;
}

// ---------------------------------------------------------------------------
// Nonlinear devices

void Circuit::evaluateDevices(const double* p, double* i, double* gSelf, double* gCross) const
{
    int k = 0;
    for (const auto& d : devices_)
    {
        if (d.kind == Device::Kind::Diode)
        {
            const double nVt = d.diode.n * kThermalVoltage;
            const double ex = junctionExp(p[k] / nVt);
            i[k] = d.diode.is * (ex - 1.0) + kGmin * p[k];
            gSelf[k] = d.diode.is / nVt * ex + kGmin;
            gCross[k] = 0.0;
            k += 1;
        }
        else
        {
            // Ebers-Moll transport model with base-width modulation (Early effect).
            // Port k is vbe, k+1 is vbc; current k is ic, k+1 is ib.
            const double vbe = p[k], vbc = p[k + 1];
            const double ef = junctionExp(vbe / kThermalVoltage);
            const double er = junctionExp(vbc / kThermalVoltage);
            const double is = d.bjt.is;
            const double iF = is * (ef - 1.0), iR = is * (er - 1.0);
            const double gF = is / kThermalVoltage * ef, gR = is / kThermalVoltage * er;
            const double early = std::max(1.0 - vbc / d.bjt.earlyVoltage, 0.1);
            const double dEarly = vbc / d.bjt.earlyVoltage < 0.9 ? -1.0 / d.bjt.earlyVoltage : 0.0;

            i[k] = (iF - iR) * early - iR / d.bjt.betaR - kGmin * vbc;            // ic
            i[k + 1] = iF / d.bjt.betaF + iR / d.bjt.betaR + kGmin * (vbe + vbc); // ib
            gSelf[k] = gF * early;                                                 // dic/dvbe
            gCross[k] = -gR * early + (iF - iR) * dEarly - gR / d.bjt.betaR - kGmin; // dic/dvbc
            gSelf[k + 1] = gR / d.bjt.betaR + kGmin;                               // dib/dvbc
            gCross[k + 1] = gF / d.bjt.betaF + kGmin;                              // dib/dvbe
            k += 2;
        }
    }
}

void Circuit::limitJunctions(const double* pOld, double* pNew) const
{
    for (int r = 0; r < numPorts_; ++r)
        pNew[r] = limitJunction(pNew[r], pOld[r], portNVt_[static_cast<size_t>(r)], portVCrit_[static_cast<size_t>(r)]);
}

// Solves p = p0 + K i(p) for the junction voltages p (Newton-Raphson).
bool Circuit::solveDevices(const double* p0, const double* k, double* p, double* i, int maxIterations)
{
    const int np = numPorts_;
    if (np == 0)
        return true;

    double* jac = newtonJ_.data();
    double* step = newtonR_.data();
    double* gSelf = gSelf_.data();
    double* gCross = gCross_.data();
    double* trial = pTrial_.data();
    const int* partner = partner_.data();

    for (int iter = 0; iter < maxIterations; ++iter)
    {
        evaluateDevices(p, i, gSelf, gCross);

        // Residual and Jacobian J = I - K * di/dp.
        for (int r = 0; r < np; ++r)
        {
            const double* kr = k + r * np;
            double* jr = jac + r * np;
            double ki = 0.0;
            for (int c = 0; c < np; ++c)
            {
                ki += kr[c] * i[c];
                const int pc = partner[c];
                jr[c] = -kr[c] * gSelf[c] - (pc != c ? kr[pc] * gCross[pc] : 0.0);
            }
            jr[r] += 1.0;
            step[r] = p0[r] + ki - p[r];
        }
        if (!solveSmall(jac, step, np))
            break;

        for (int r = 0; r < np; ++r)
            trial[r] = p[r] + step[r];
        limitJunctions(p, trial);
        double maxStep = 0.0;
        for (int r = 0; r < np; ++r)
        {
            step[r] = trial[r] - p[r];
            maxStep = std::max(maxStep, std::fabs(step[r]));
            p[r] = trial[r];
        }
        if (maxStep < kNewtonToleranceVolts)
        {
            // Converged: move the currents to the final junction voltages to first order.
            for (int q = 0; q < np; ++q)
                i[q] += gSelf[q] * step[q] + gCross[q] * step[partner[q]];
            return true;
        }
    }
    evaluateDevices(p, i, gSelf, gCross);
    return false;
}

// ---------------------------------------------------------------------------
// Operating point and simulation

void Circuit::solveOperatingPoint(bool fromOperatingPoint)
{
    const int n = numFree_, np = numPorts_, nq = numCurrents_;
    int firstStep = kSourceSteps;
    if (fromOperatingPoint)
        std::copy(pOp_.begin(), pOp_.end(), p_.begin()); // a small change: start from the last solution
    else
    {
        std::fill(v_.begin(), v_.end(), 0.0);
        std::fill(p_.begin(), p_.end(), 0.0);
        std::fill(i_.begin(), i_.end(), 0.0);
        firstStep = 1;
    }

    // Source stepping: ramp the rails and sources up so Newton always starts close.
    for (int step = firstStep; step <= kSourceSteps; ++step)
    {
        const double scale = static_cast<double>(step) / kSourceSteps;
        buildConfiguration(switchMask_, true, scale, dcConfig_);

        std::copy(dcConfig_.bconst.begin(), dcConfig_.bconst.end(), rhs_.begin());
        for (const auto& src : sources_)
        {
            if (const int t = freeIndex(src.to); t >= 0)
                rhs_[static_cast<size_t>(t)] += src.amps * scale;
            if (const int f = freeIndex(src.from); f >= 0)
                rhs_[static_cast<size_t>(f)] -= src.amps * scale;
        }
        for (int r = 0; r < n; ++r)
        {
            double s = 0.0;
            for (int c = 0; c < n; ++c)
                s += dcConfig_.m[static_cast<size_t>(r * n + c)] * rhs_[static_cast<size_t>(c)];
            v0_[static_cast<size_t>(r)] = s;
        }
        auto scaled = [&](NodeId node) {
            const int f = freeIndex(node);
            return f >= 0 ? v0_[static_cast<size_t>(f)] : fixedVoltage(node) * scale;
        };
        for (int r = 0; r < np; ++r)
            p0_[static_cast<size_t>(r)] = scaled(portPlus_[static_cast<size_t>(r)]) - scaled(portMinus_[static_cast<size_t>(r)]);

        solveDevices(p0_.data(), dcConfig_.k.data(), p_.data(), i_.data(), 200);

        for (int r = 0; r < n; ++r)
        {
            double s = v0_[static_cast<size_t>(r)];
            for (int c = 0; c < nq; ++c)
                s -= dcConfig_.md[static_cast<size_t>(r * nq + c)] * i_[static_cast<size_t>(c)];
            v_[static_cast<size_t>(r)] = s;
        }
    }

    // Companion-model histories consistent with the DC solution.
    for (size_t c = 0; c < capacitors_.size(); ++c)
    {
        const auto& cap = capacitors_[c];
        capHist_[c] = cap.value * sMap_ * (nodeVoltage(v_, cap.a) - nodeVoltage(v_, cap.b));
    }
    for (size_t l = 0; l < inductors_.size(); ++l)
    {
        const auto& ind = inductors_[l];
        const double vab = nodeVoltage(v_, ind.a) - nodeVoltage(v_, ind.b);
        indHist_[l] = kDcInductorSiemens * vab + vab / (ind.value * sMap_);
    }

    vOp_ = v_;
    pOp_ = p_;
    capHistOp_ = capHist_;
    indHistOp_ = indHist_;
}

void Circuit::buildIndexTables()
{
    auto branch = [&](const TwoTerminal& t, double geq) {
        return Branch { freeIndex(t.a), freeIndex(t.b), fixedVoltage(t.a), fixedVoltage(t.b), geq };
    };
    capBranches_.clear();
    for (const auto& c : capacitors_)
        capBranches_.push_back(branch(c, c.value * sMap_));
    indBranches_.clear();
    for (const auto& l : inductors_)
        indBranches_.push_back(branch(l, 1.0 / (l.value * sMap_)));

    sourceFrom_.clear();
    sourceTo_.clear();
    for (const auto& src : sources_)
    {
        sourceFrom_.push_back(freeIndex(src.from));
        sourceTo_.push_back(freeIndex(src.to));
    }

    portPlusIdx_.clear();
    portMinusIdx_.clear();
    portPlusFixed_.clear();
    portMinusFixed_.clear();
    for (int r = 0; r < numPorts_; ++r)
    {
        portPlusIdx_.push_back(freeIndex(portPlus_[static_cast<size_t>(r)]));
        portMinusIdx_.push_back(freeIndex(portMinus_[static_cast<size_t>(r)]));
        portPlusFixed_.push_back(fixedVoltage(portPlus_[static_cast<size_t>(r)]));
        portMinusFixed_.push_back(fixedVoltage(portMinus_[static_cast<size_t>(r)]));
    }

    partner_.clear();
    portNVt_.clear();
    portVCrit_.clear();
    for (const auto& d : devices_)
    {
        const int k = static_cast<int>(partner_.size());
        if (d.kind == Device::Kind::Diode)
        {
            const double nVt = d.diode.n * kThermalVoltage;
            partner_.push_back(k);
            portNVt_.push_back(nVt);
            portVCrit_.push_back(criticalVoltage(d.diode.is, nVt));
        }
        else
        {
            partner_.insert(partner_.end(), { k + 1, k });
            const double vCrit = criticalVoltage(d.bjt.is, kThermalVoltage);
            portNVt_.insert(portNVt_.end(), { kThermalVoltage, kThermalVoltage });
            portVCrit_.insert(portVCrit_.end(), { vCrit, vCrit });
        }
    }
}

void Circuit::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    constexpr double kPi = 3.14159265358979323846;
    sMap_ = prewarpHz_ > 0.0 && prewarpHz_ < 0.45 * sampleRate
                ? 2.0 * kPi * prewarpHz_ / std::tan(kPi * prewarpHz_ / sampleRate)
                : 2.0 * sampleRate;
    const int n = numFree_, np = numPorts_, nq = numCurrents_;
    if (switches_.size() > 12)
        throw std::runtime_error("circuit: too many switches");

    auto sized = [](std::vector<double>& v, int count) { v.assign(static_cast<size_t>(count), 0.0); };
    sized(rhs_, n);
    sized(v0_, n);
    sized(p0_, np);
    sized(newtonJ_, np * np);
    sized(newtonR_, np);
    sized(gSelf_, np);
    sized(gCross_, np);
    sized(pTrial_, np);
    sized(pSave_, np);
    sized(invScratch_, n * n);
    sized(v_, n);
    sized(p_, np);
    sized(i_, nq);
    capHist_.assign(capacitors_.size(), 0.0);
    indHist_.assign(inductors_.size(), 0.0);
    buildIndexTables();
    pOp_.assign(static_cast<size_t>(np), 0.0);

    auto allocate = [&](Configuration& cfg) {
        cfg.built = false;
        sized(cfg.m, n * n);
        sized(cfg.md, n * nq);
        sized(cfg.k, np * nq);
        sized(cfg.bconst, n);
    };
    allocate(dcConfig_);
    configs_.resize(size_t { 1 } << switches_.size());
    for (auto& cfg : configs_)
        allocate(cfg);

    solveOperatingPoint(false);
}

void Circuit::updateOperatingPoint() { solveOperatingPoint(true); }

void Circuit::setResistor(int index, double ohms)
{
    resistors_[static_cast<size_t>(index)].value = ohms;
    for (auto& cfg : configs_)
        cfg.built = false;
    solveOperatingPoint(true);
}

void Circuit::resetToOperatingPoint()
{
    v_ = vOp_;
    p_ = pOp_;
    capHist_ = capHistOp_;
    indHist_ = indHistOp_;
}

void Circuit::step()
{
    const int n = numFree_, np = numPorts_;
    const auto& cfg = configuration(switchMask_);
    double* rhs = rhs_.data();
    double* v0 = v0_.data();
    double* v = v_.data();

    std::copy(cfg.bconst.begin(), cfg.bconst.end(), rhs);
    for (size_t c = 0; c < capBranches_.size(); ++c)
    {
        const auto& br = capBranches_[c];
        if (br.a >= 0)
            rhs[br.a] += capHist_[c];
        if (br.b >= 0)
            rhs[br.b] -= capHist_[c];
    }
    for (size_t l = 0; l < indBranches_.size(); ++l)
    {
        const auto& br = indBranches_[l];
        if (br.a >= 0)
            rhs[br.a] -= indHist_[l];
        if (br.b >= 0)
            rhs[br.b] += indHist_[l];
    }
    for (size_t s = 0; s < sources_.size(); ++s)
    {
        if (sourceTo_[s] >= 0)
            rhs[sourceTo_[s]] += sources_[s].amps;
        if (sourceFrom_[s] >= 0)
            rhs[sourceFrom_[s]] -= sources_[s].amps;
    }

    const double* m = cfg.m.data();
    for (int r = 0; r < n; ++r)
    {
        const double* row = m + r * n;
        double acc = 0.0;
        for (int c = 0; c < n; ++c)
            acc += row[c] * rhs[c];
        v0[r] = acc;
    }

    if (np > 0)
    {
        double* p0 = p0_.data();
        for (int r = 0; r < np; ++r)
        {
            const int a = portPlusIdx_[static_cast<size_t>(r)], b = portMinusIdx_[static_cast<size_t>(r)];
            p0[r] = (a >= 0 ? v0[a] : portPlusFixed_[static_cast<size_t>(r)])
                    - (b >= 0 ? v0[b] : portMinusFixed_[static_cast<size_t>(r)]);
        }
        // Hard transients (a transistor slamming into saturation) can need many
        // iterations; easy samples converge in one or two. If a solve still
        // fails, hold the junctions at the previous sample rather than emit garbage.
        std::copy(p_.begin(), p_.end(), pSave_.begin());
        if (!solveDevices(p0, cfg.k.data(), p_.data(), i_.data(), kMaxNewtonIterations))
        {
            ++nonConverged_;
            std::copy(pSave_.begin(), pSave_.end(), p_.begin());
            evaluateDevices(p_.data(), i_.data(), gSelf_.data(), gCross_.data());
        }

        const double* md = cfg.md.data();
        const double* cur = i_.data();
        for (int r = 0; r < n; ++r)
        {
            const double* row = md + r * np;
            double acc = v0[r];
            for (int c = 0; c < np; ++c)
                acc -= row[c] * cur[c];
            v[r] = acc;
        }
    }
    else
        std::copy(v0, v0 + n, v);

    for (size_t c = 0; c < capBranches_.size(); ++c)
    {
        const auto& br = capBranches_[c];
        const double vab = (br.a >= 0 ? v[br.a] : br.fixedA) - (br.b >= 0 ? v[br.b] : br.fixedB);
        capHist_[c] = 2.0 * br.geq * vab - capHist_[c];
    }
    for (size_t l = 0; l < indBranches_.size(); ++l)
    {
        const auto& br = indBranches_[l];
        const double vab = (br.a >= 0 ? v[br.a] : br.fixedA) - (br.b >= 0 ? v[br.b] : br.fixedB);
        indHist_[l] += 2.0 * br.geq * vab;
    }
}

} // namespace beatqueen::circuit
