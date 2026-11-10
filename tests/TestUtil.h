#pragma once
// Shared helpers for Lodestone's offline DSP tests: voice-like test signals
// (harmonics shaped by fixed formants, optional vibrato and glides), block
// rendering like a host, and measurements (pitch, level, spectral centroid).

#include "dsp/VoiceEngine.h"

#include <cmath>
#include <functional>
#include <vector>

namespace testutil
{
using namespace lodestone::dsp;

inline constexpr double kFs = 48000.0;
inline constexpr double kPiD = 3.14159265358979323846;

inline double midiToHz (double m, double ref = 440.0) { return ref * std::pow (2.0, (m - 69.0) / 12.0); }
inline double hzToMidi (double hz, double ref = 440.0) { return 69.0 + 12.0 * std::log2 (hz / ref); }

// Spectral envelope of an "ah" vowel: three formant bumps.
inline double formantGain (double hz, double shift = 1.0)
{
    auto bump = [] (double f, double c, double bw) { const double x = (f - c) / bw; return std::exp (-0.5 * x * x); };
    return 0.08 + bump (hz, 750 * shift, 130) + 0.6 * bump (hz, 1150 * shift, 150) + 0.35 * bump (hz, 2700 * shift, 250);
}

// A sung note: pitch (MIDI float) as a function of time, formants fixed.
inline std::vector<float> voice (std::function<double (double)> pitchAt, double seconds, double fs = kFs, float amp = 0.3f,
                                 double formantShift = 1.0)
{
    std::vector<float> x ((size_t) (seconds * fs));
    double phase = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / fs;
        const double f0 = midiToHz (pitchAt (t));
        phase += f0 / fs;
        phase -= std::floor (phase);
        double s = 0.0;
        for (int k = 1; k * f0 < std::min (6000.0, 0.45 * fs); ++k)
            s += formantGain (k * f0, formantShift) / k * std::sin (2.0 * kPiD * k * phase);
        x[i] = (float) s;
    }
    // Normalise to the requested peak.
    float pk = 1.0e-9f;
    for (float v : x) pk = std::max (pk, std::abs (v));
    for (auto& v : x) v *= amp / pk;
    return x;
}

inline std::vector<float> steady (double midi, double seconds, double fs = kFs)
{
    return voice ([midi] (double) { return midi; }, seconds, fs);
}

struct StereoOut { std::vector<float> l, r, dry; };

// Runs the engine like a host would: blocks of `block`, split into 32-sample steps.
inline StereoOut run (VoiceEngine& e, const std::vector<float>& in, int block = 256)
{
    StereoOut o;
    o.l.resize (in.size()); o.r.resize (in.size()); o.dry.resize (in.size());
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - pos);
        for (int s = 0; s < n; s += VoiceEngine::kBlock)
        {
            const int m = std::min (VoiceEngine::kBlock, n - s);
            e.process (&in[pos + (size_t) s], &o.l[pos + (size_t) s], &o.r[pos + (size_t) s], &o.dry[pos + (size_t) s], m);
        }
    }
    return o;
}

inline bool allFinite (const std::vector<float>& x)
{
    for (float v : x) if (! std::isfinite (v)) return false;
    return true;
}

inline float rms (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min (to, x.size());
    double acc = 0.0;
    for (size_t i = from; i < to; ++i) acc += (double) x[i] * x[i];
    return (float) std::sqrt (acc / (double) std::max<size_t> (1, to - from));
}

inline float peak (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min (to, x.size());
    float p = 0.0f;
    for (size_t i = from; i < to; ++i) p = std::max (p, std::abs (x[i]));
    return p;
}

// Median pitch (MIDI) of a segment, measured with a fresh detector.
inline double measurePitch (const std::vector<float>& x, size_t from, size_t to, double fs = kFs, int range = 3)
{
    PitchDetector d;
    d.prepare (fs);
    d.setRange (range);
    d.setSensitivity (0.5f);
    std::vector<double> v;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
        if (d.push (x[i]) && d.reading().voiced) v.push_back (hzToMidi (d.reading().hz));
    if (v.size() < 5) return -1.0;
    std::sort (v.begin(), v.end());
    // Skip the first readings (window still filling with older audio).
    return v[v.size() / 2];
}

// Magnitude of a tone at `hz` (Hann, 4096-sample frames).
inline float toneLevel (const std::vector<float>& x, double hz, size_t from, size_t to, double fs = kFs)
{
    to = std::min (to, x.size());
    double acc = 0.0;
    int frames = 0;
    for (size_t s = from; s + 4096 <= to; s += 2048)
    {
        double re = 0, im = 0, ws = 0;
        for (int i = 0; i < 4096; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * kPiD * i / 4095.0);
            const double a = 2 * kPiD * hz * (double) (s + (size_t) i) / fs;
            re += w * x[s + (size_t) i] * std::cos (a);
            im += w * x[s + (size_t) i] * std::sin (a);
            ws += w;
        }
        acc += 2.0 * std::sqrt (re * re + im * im) / ws;
        ++frames;
    }
    return frames ? (float) (acc / frames) : 0.0f;
}

// Spectral centroid of the harmonic envelope (energy-weighted mean of
// 100 Hz-band levels up to 4 kHz).
inline double centroid (const std::vector<float>& x, size_t from, size_t to, double fs = kFs)
{
    double num = 0.0, den = 0.0;
    for (double f = 150.0; f <= 4000.0; f += 50.0)
    {
        const double e = std::pow ((double) toneLevel (x, f, from, std::min (to, from + 8192), fs), 2.0);
        num += f * e;
        den += e;
    }
    return den > 0.0 ? num / den : 0.0;
}

// Normalised cross-correlation of a and b shifted by `lag` (b delayed).
inline double correlation (const std::vector<float>& a, const std::vector<float>& b, int lag, size_t from, size_t to)
{
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = from; i < to && i < b.size(); ++i)
    {
        if ((long) i - lag < 0) continue;
        const double x = a[i - (size_t) lag], y = b[i];
        sab += x * y; saa += x * x; sbb += y * y;
    }
    return saa > 0 && sbb > 0 ? sab / std::sqrt (saa * sbb) : 0.0;
}

// A source-filter voice: glottal pulses (Rosenberg-style, differentiated flow)
// through three formant resonators. Its energy is concentrated at each pulse,
// like a real voice, which is what pitch-synchronous processing relies on.
inline std::vector<float> pulseVoice (double midi, double seconds, double fs = kFs)
{
    struct Res
    {
        double b0 = 0, a1 = 0, a2 = 0, y1 = 0, y2 = 0;
        double tick (double v) { const double y = b0 * v - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; }
    };
    auto make = [fs] (double f, double bw)
    {
        Res r;
        const double R = std::exp (-kPiD * bw / fs);
        r.a1 = -2.0 * R * std::cos (2.0 * kPiD * f / fs);
        r.a2 = R * R;
        r.b0 = (1.0 - R * R) * 0.5;
        return r;
    };
    Res f1 = make (750, 90), f2 = make (1150, 110), f3 = make (2700, 160);
    const double f0 = midiToHz (midi);
    std::vector<float> x ((size_t) (seconds * fs));
    double ph = 0.0, prev = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        ph += f0 / fs;
        if (ph >= 1.0) ph -= 1.0;
        double src = 0.0;
        if (ph < 0.4) src = 0.5 * (1.0 - std::cos (kPiD * ph / 0.4));
        else if (ph < 0.55) src = std::cos (kPiD * (ph - 0.4) / 0.3);
        const double d = src - prev;
        prev = src;
        x[i] = (float) (f1.tick (d) + 0.7 * f2.tick (d) + 0.4 * f3.tick (d));
    }
    float pk = 1.0e-9f;
    for (float v : x) pk = std::max (pk, std::abs (v));
    for (auto& v : x) v *= 0.3f / pk;
    return x;
}

// Spectral-envelope error (dB RMS, overall gain removed) of `out`'s harmonics
// against `in`'s envelope moved by `formantScale` (1 = formants unchanged).
inline double envelopeError (const std::vector<float>& in, double f0In, const std::vector<float>& out, double f0Out,
                             double formantScale = 1.0, size_t from = 24000, size_t to = 48000)
{
    std::vector<std::pair<double, double>> hin;
    for (int k = 1; k * f0In < 4500.0; ++k) hin.push_back ({ k * f0In, 20.0 * std::log10 (toneLevel (in, k * f0In, from, to) + 1e-9) });
    auto envAt = [&] (double f)
    {
        if (f <= hin.front().first) return hin.front().second;
        for (size_t i = 1; i < hin.size(); ++i)
            if (f <= hin[i].first)
            {
                const double t = (f - hin[i - 1].first) / (hin[i].first - hin[i - 1].first);
                return hin[i - 1].second + t * (hin[i].second - hin[i - 1].second);
            }
        return hin.back().second;
    };
    std::vector<double> err;
    for (int k = 1; k * f0Out < 4500.0; ++k)
    {
        const double src = k * f0Out / formantScale;
        if (src < hin.front().first || src > hin.back().first) continue;
        err.push_back (20.0 * std::log10 (toneLevel (out, k * f0Out, from, to) + 1e-9) - envAt (src));
    }
    if (err.empty()) return 0.0;
    double m = 0.0;
    for (double e : err) m += e;
    m /= (double) err.size();
    double s = 0.0;
    for (double e : err) s += (e - m) * (e - m);
    return std::sqrt (s / (double) err.size());
}

} // namespace testutil
