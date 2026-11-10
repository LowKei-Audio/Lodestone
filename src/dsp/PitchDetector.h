#pragma once
// Real-time monophonic pitch detector.
//
//   1. The input is low-passed and decimated to ~12 kHz (vocal fundamentals
//      never go near that Nyquist) so the search is cheap.
//   2. Every hop (~2.7 ms) YIN's cumulative-mean-normalised difference is
//      computed over a window sized by the input range: just one longest
//      period of integration, so a soprano setting reacts in ~13 ms and a
//      bass setting still sees 30 Hz.
//   3. The best lag is refined on the full-rate signal (sub-cent accuracy),
//      then checked against the previous estimate to reject octave jumps.
//
// Results are time-stamped at the centre of the analysed span, so the engine
// can line them up with its delayed audio.

#include "Primitives.h"

#include <array>
#include <vector>

namespace lodestone::dsp
{

struct DetectorRange { const char* name; float minHz, maxHz; };
inline constexpr DetectorRange kRanges[] = {
    { "Soprano",    150.0f, 1400.0f },
    { "Alto/Tenor",  80.0f, 1100.0f },
    { "Low Male",    60.0f,  700.0f },
    { "Instrument",  45.0f, 1600.0f },
    { "Bass Inst",   30.0f,  450.0f },
};
inline constexpr int kNumRanges = (int) (sizeof (kRanges) / sizeof (kRanges[0]));

struct PitchReading
{
    bool voiced = false;
    float hz = 0.0f;              // fundamental (valid when voiced)
    float period = 0.0f;          // in full-rate samples
    float clarity = 0.0f;         // 1 - aperiodicity, 0..1
    float level = 0.0f;           // RMS of the analysed span
    std::int64_t centre = 0;      // input sample index this reading describes
};

class PitchDetector
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        decim = std::max (1, (int) std::lround (fs / 12000.0));
        fsd = fs / decim;
        for (auto* f : { &aa1, &aa2 }) { f->prepare (fs); f->set ((float) (fsd * 0.42), 1.41421356f); }
        dec.assign (kDecSize, 0.0f);
        full.assign (kFullSize, 0.0f);
        diff.assign (kDecSize / 2 + 2, 0.0f);
        cmnd.assign (kDecSize / 2 + 2, 1.0f);
        reset();
    }

    void reset()
    {
        aa1.reset(); aa2.reset();
        std::fill (dec.begin(), dec.end(), 0.0f);
        std::fill (full.begin(), full.end(), 0.0f);
        nFull = 0; nDec = 0; phase = 0; hopCount = 0;
        last = {};
        prevTau = 0.0f;
        fresh = false;
    }

    // Range index (kRanges), sensitivity 0..1 (higher = tracks quieter and
    // breathier sounds; lower = only clear, strong notes).
    void setRange (int r) noexcept { range = std::clamp (r, 0, kNumRanges - 1); }
    void setSensitivity (float s) noexcept { sensitivity = clamp01 (s); }

    // Push one full-rate sample. Returns true when a new reading is ready.
    bool push (float x) noexcept
    {
        full[(size_t) (nFull & (kFullSize - 1))] = x;
        ++nFull;
        const float y = aa2.tick (aa1.tick (x).lp).lp;
        if (++phase < decim) return false;
        phase = 0;
        dec[(size_t) (nDec & (kDecSize - 1))] = y;
        ++nDec;
        if (++hopCount < kHop) return false;
        hopCount = 0;
        analyse();
        return true;
    }

    const PitchReading& reading() const noexcept { return last; }

    // Latency of a reading (full-rate samples between its centre and "now").
    int analysisLag() const noexcept { return spanFull() / 2; }
    int hopSamples() const noexcept { return kHop * decim; }
    int decimation() const noexcept { return decim; }

private:
    static constexpr int kDecSize = 2048;     // ~170 ms at 12 kHz
    static constexpr int kFullSize = 1 << 15; // full-rate history for refinement
    static constexpr int kHop = 32;

    int tauMax() const noexcept { return std::min (kDecSize / 4, (int) std::ceil (fsd / kRanges[range].minHz) + 2); }
    int tauMin() const noexcept { return std::max (2, (int) std::floor (fsd / kRanges[range].maxHz)); }
    int spanFull() const noexcept { return 2 * tauMax() * decim; }

    float decAt (std::int64_t i) const noexcept { return dec[(size_t) (i & (kDecSize - 1))]; }
    float fullAt (std::int64_t i) const noexcept { return full[(size_t) (i & (kFullSize - 1))]; }

    void analyse() noexcept
    {
        const int tMax = tauMax(), tMin = tauMin();
        const int W = tMax;                       // integration window
        const std::int64_t start = nDec - (W + tMax);
        if (start < 0) return;

        // Level of the span (decides silence before any search).
        double e = 0.0;
        for (int j = 0; j < W + tMax; ++j) { const float v = decAt (start + j); e += (double) v * v; }
        const float rms = (float) std::sqrt (e / (W + tMax));

        PitchReading r;
        r.level = rms;
        r.centre = nFull - spanFull() / 2;
        const float gate = dbToGain (lerp (-38.0f, -62.0f, sensitivity));
        if (rms < gate) { publish (r); return; }

        // Difference function and its cumulative-mean normalisation.
        diff[0] = 0.0f;
        cmnd[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= tMax; ++tau)
        {
            double d = 0.0;
            for (int j = 0; j < W; ++j)
            {
                const float a = decAt (start + j) - decAt (start + j + tau);
                d += (double) a * a;
            }
            diff[(size_t) tau] = (float) d;
            running += d;
            cmnd[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
        }

        // First dip under the threshold (YIN), then down to its local minimum.
        const float thresh = lerp (0.10f, 0.22f, sensitivity);
        int best = -1;
        for (int tau = tMin; tau <= tMax; ++tau)
            if (cmnd[(size_t) tau] < thresh)
            {
                while (tau + 1 <= tMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)   // nothing under the threshold: the global minimum, judged below
        {
            float m = 2.0f;
            for (int tau = tMin; tau <= tMax; ++tau)
                if (cmnd[(size_t) tau] < m) { m = cmnd[(size_t) tau]; best = tau; }
        }
        if (best < 0) { publish (r); return; }

        // Continuity: if the previous period sits at an octave of this one and
        // is nearly as good, keep following the previous octave.
        if (prevTau > 0.0f && last.voiced)
        {
            for (float ratio : { 2.0f, 0.5f })
            {
                const int alt = (int) std::lround ((float) best * ratio);
                if (alt < tMin || alt > tMax) continue;
                if (std::abs ((float) alt - prevTau) > 0.06f * prevTau) continue;
                int a = alt;
                for (int k = -2; k <= 2; ++k)
                {
                    const int c = std::clamp (alt + k, tMin, tMax);
                    if (cmnd[(size_t) c] < cmnd[(size_t) a]) a = c;
                }
                if (cmnd[(size_t) a] < cmnd[(size_t) best] + 0.08f) best = a;
            }
        }

        const float aper = cmnd[(size_t) best];
        const float voicedThresh = lerp (0.22f, 0.42f, sensitivity);
        if (aper > voicedThresh) { publish (r); return; }

        // Refine on the full-rate signal around decim * best.
        const float period = refine (best);
        if (! (period > 1.0f)) { publish (r); return; }

        r.voiced = true;
        r.period = period;
        r.hz = (float) (fs / period);
        r.clarity = clamp01 (1.0f - aper);
        prevTau = (float) best;
        publish (r);
    }

    float refine (int tauDec) const noexcept
    {
        const int D = decim;
        const int centreLag = tauDec * D;
        const int W = std::min (kFullSize / 4, std::max (64, tauMax() * D));
        const std::int64_t end = nFull;              // newest sample + 1
        const std::int64_t s0 = end - W - centreLag - D - 1;
        if (s0 < 0) return (float) centreLag;

        auto d = [&] (int lag)
        {
            double acc = 0.0;
            const std::int64_t s = end - W - lag;
            for (int j = 0; j < W; j += (D > 2 ? 2 : 1))   // every 2nd sample: plenty for a smooth minimum
            {
                const float a = fullAt (s + j) - fullAt (s + j + lag);
                acc += (double) a * a;
            }
            return acc;
        };

        int bestLag = centreLag;
        double bestV = 1.0e300;
        std::array<double, 64> vals {};
        const int lo = std::max (2, centreLag - D), hi = centreLag + D;
        for (int lag = lo; lag <= hi && lag - lo < 64; ++lag)
        {
            vals[(size_t) (lag - lo)] = d (lag);
            if (vals[(size_t) (lag - lo)] < bestV) { bestV = vals[(size_t) (lag - lo)]; bestLag = lag; }
        }
        // Parabolic interpolation around the minimum.
        if (bestLag > lo && bestLag < hi)
        {
            const double a = vals[(size_t) (bestLag - lo - 1)], b = vals[(size_t) (bestLag - lo)], c = vals[(size_t) (bestLag - lo + 1)];
            const double den = a - 2.0 * b + c;
            if (den > 1.0e-20)
                return (float) bestLag + (float) std::clamp (0.5 * (a - c) / den, -0.5, 0.5);
        }
        return (float) bestLag;
    }

    void publish (const PitchReading& r) noexcept
    {
        if (! r.voiced) prevTau = 0.0f;
        last = r;
        fresh = true;
    }

    double fs = 48000.0, fsd = 12000.0;
    int decim = 4, range = 1, phase = 0, hopCount = 0;
    float sensitivity = 0.5f;
    Svf aa1, aa2;
    std::vector<float> dec, full, diff, cmnd;
    std::int64_t nFull = 0, nDec = 0;
    PitchReading last;
    float prevTau = 0.0f;
    bool fresh = false;
};

} // namespace lodestone::dsp
