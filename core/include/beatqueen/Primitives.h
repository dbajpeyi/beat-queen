#pragma once

// Generic DSP building blocks shared by the voice models.
//
// These are deliberately circuit-agnostic. Once the MRK-1 schematics are
// transcribed, the voices will be rebuilt from models of the actual circuits
// (see docs/DESIGN.md); these primitives remain useful for the noise source,
// envelopes and filtering around those models.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace beatqueen::dsp {

constexpr double kPi = 3.14159265358979323846;

// Per-sample multiplier for an exponential decay that falls 60 dB in t60 seconds.
inline float decayCoefficient(double t60Seconds, double sampleRate)
{
    if (t60Seconds <= 0.0 || sampleRate <= 0.0)
        return 0.0f;
    return static_cast<float>(std::exp(-6.907755278982137 / (t60Seconds * sampleRate)));
}

// Fast deterministic white noise in [-1, 1).
class WhiteNoise
{
public:
    explicit WhiteNoise(uint32_t seed = 0x9E3779B9u) : state_(seed != 0 ? seed : 1u) {}

    float next()
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(state_ >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

private:
    uint32_t state_;
};

// Retriggerable exponential decay envelope.
class DecayEnvelope
{
public:
    void setDecay(double t60Seconds, double sampleRate) { coeff_ = decayCoefficient(t60Seconds, sampleRate); }
    void trigger(float level) { value_ = level; }
    void reset() { value_ = 0.0f; }

    float next()
    {
        const float v = value_;
        value_ *= coeff_;
        return v;
    }

private:
    float coeff_ = 0.0f;
    float value_ = 0.0f;
};

// Impulse-excited two-pole resonator: y[n] = a1*y[n-1] + a2*y[n-2] + g*x[n].
// The input gain is chosen so a unit impulse rings with a peak of ~1.
class Resonator
{
public:
    void setup(double frequencyHz, double t60Seconds, double sampleRate)
    {
        const double w = 2.0 * kPi * std::min(frequencyHz, 0.49 * sampleRate) / sampleRate;
        const double r = decayCoefficient(t60Seconds, sampleRate);
        a1_ = static_cast<float>(2.0 * r * std::cos(w));
        a2_ = static_cast<float>(-r * r);
        gain_ = static_cast<float>(std::sin(w));
    }

    void excite(float amount) { pending_ += amount; }
    void reset() { y1_ = y2_ = pending_ = 0.0f; }

    float next()
    {
        const float y = a1_ * y1_ + a2_ * y2_ + gain_ * pending_;
        pending_ = 0.0f;
        y2_ = y1_;
        y1_ = y;
        return y;
    }

private:
    float a1_ = 0.0f, a2_ = 0.0f, gain_ = 0.0f;
    float y1_ = 0.0f, y2_ = 0.0f, pending_ = 0.0f;
};

// Topology-preserving-transform state variable filter (Zavalishin).
class StateVariableFilter
{
public:
    struct Output
    {
        float lowpass, bandpass, highpass;
    };

    void setup(double cutoffHz, double q, double sampleRate)
    {
        const double g = std::tan(kPi * std::min(cutoffHz, 0.49 * sampleRate) / sampleRate);
        k_ = static_cast<float>(1.0 / q);
        a1_ = static_cast<float>(1.0 / (1.0 + g * (g + k_)));
        a2_ = static_cast<float>(g) * a1_;
        a3_ = static_cast<float>(g) * a2_;
    }

    void reset() { ic1_ = ic2_ = 0.0f; }

    Output process(float v0)
    {
        const float v3 = v0 - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0f * v1 - ic1_;
        ic2_ = 2.0f * v2 - ic2_;
        return { v2, v1, v0 - k_ * v1 - v2 };
    }

private:
    float k_ = 1.0f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float ic1_ = 0.0f, ic2_ = 0.0f;
};

} // namespace beatqueen::dsp
