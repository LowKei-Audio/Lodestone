#pragma once
// Listens to the beat (Lodestone's sidechain input) and names the chord that is
// playing, for Chord Lock and for LEARN. Plain C++, no JUCE.
//
//   1. The sidechain is low-passed and decimated to ~12 kHz (chords live well
//      below 6 kHz), then analysed in 2048-sample Hann frames (~170 ms, 5.9 Hz
//      per bin: semitones apart from ~100 Hz up; the bass's harmonics cover
//      lower roots) every 256 samples (~21 ms). A new chord is named about
//      0.2 s after it starts.
//   2. Spectral peaks between 65 Hz and 2.1 kHz are folded into a 12-bin
//      chroma (how much of each pitch class sounds); peaks below 250 Hz also
//      build a bass chroma, which decides the root (C6 and Am7 share notes).
//      Peaks only: drums and noise spread thinly and barely register.
//   3. The chroma is matched against seven chord shapes on all 12 roots. A new
//      chord must win by a margin, twice in a row, before it replaces the
//      current one; through silence or drum-only bars the last chord is kept.
//
// The same chroma, summed over time, is what LEARN uses to find the key when
// a beat is connected (far more reliable than learning from the voice alone).

#include "Primitives.h"
#include "Scales.h"

#include <array>
#include <complex>
#include <cstdio>
#include <vector>

namespace lodestone::dsp
{

struct ChordShape { const char* suffix; std::array<int, 4> tones; int count; };
inline constexpr ChordShape kChordShapes[] = {
    { "",     { 0, 4, 7, 0 },  3 },   // major
    { "m",    { 0, 3, 7, 0 },  3 },   // minor
    { "7",    { 0, 4, 7, 10 }, 4 },
    { "m7",   { 0, 3, 7, 10 }, 4 },
    { "maj7", { 0, 4, 7, 11 }, 4 },
    { "dim",  { 0, 3, 6, 0 },  3 },
    { "sus4", { 0, 5, 7, 0 },  3 },
};
inline constexpr int kNumChordShapes = (int) (sizeof (kChordShapes) / sizeof (kChordShapes[0]));

inline Mask chordMask (int root, int shape) noexcept
{
    if (shape < 0 || shape >= kNumChordShapes) return 0;
    Mask m = 0;
    const auto& s = kChordShapes[shape];
    for (int i = 0; i < s.count; ++i) m |= (Mask) (1u << (((root + s.tones[(size_t) i]) % 12 + 12) % 12));
    return m;
}

struct Chord
{
    int root = -1, shape = 0;            // root < 0: no chord heard yet
    bool valid() const noexcept { return root >= 0; }
    Mask mask() const noexcept { return valid() ? chordMask (root, shape) : (Mask) 0; }
};

class ChordAnalyzer
{
public:
    static constexpr int kFrame = 2048, kHop = 256;
    static constexpr float kMinShape = 0.72f;   // plain shape match needed to name a chord

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        decim = std::max (1, (int) std::lround (fs / 12000.0));
        fsd = fs / decim;
        for (auto* f : { &aa1, &aa2 }) { f->prepare (fs); f->set ((float) (fsd * 0.42), 1.41421356f); }
        ring.assign (kFrame, 0.0f);
        buf.assign (kFrame, {});
        window.resize (kFrame);
        for (int i = 0; i < kFrame; ++i) window[(size_t) i] = hann ((float) i / (float) (kFrame - 1));
        mags.assign (kFrame / 2, 0.0f);

        // Bin -> pitch class, with a triangular weight by distance from the
        // note centre (a bin halfway between two notes counts for neither).
        binPc.assign (kFrame / 2, -1);
        binW.assign (kFrame / 2, 0.0f);
        binBass.assign (kFrame / 2, 0);
        for (int k = 1; k < kFrame / 2; ++k)
        {
            const double hz = k * fsd / kFrame;
            if (hz < 65.0 || hz > 2100.0) continue;
            const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
            const double nearest = std::round (midi);
            const double d = std::abs (midi - nearest);
            binPc[(size_t) k] = (int) (((long) nearest % 12 + 12) % 12);
            binW[(size_t) k] = (float) std::max (0.0, 1.0 - 2.0 * d);
            binBass[(size_t) k] = hz < 250.0 ? 1 : 0;
        }
        hopSeconds = (float) (kHop * decim / fs);
        hopAlpha = 1.0f - std::exp (-(float) (kHop * decim) / (0.06f * (float) fs));   // chroma glide ~60 ms
        reset();
    }

    void reset() noexcept
    {
        aa1.reset(); aa2.reset();
        std::fill (ring.begin(), ring.end(), 0.0f);
        write = 0; phase = 0; hopCount = 0; filled = 0;
        chroma.fill (0.0f);
        bass.fill (0.0f);
        current = {};
        candidate = {};
        candidateHits = 0;
        level = 0.0f;
        silentFrames = 1 << 20;
        keyChroma.fill (0.0f);
    }

    // Pushes one sidechain sample (call for every sample, even when silent).
    void push (float x) noexcept
    {
        const float y = aa2.tick (aa1.tick (x).lp).lp;
        if (++phase < decim) return;
        phase = 0;
        ring[(size_t) write] = y;
        write = (write + 1) & (kFrame - 1);
        if (filled < kFrame) ++filled;
        if (++hopCount < kHop) return;
        hopCount = 0;
        if (filled >= kFrame) analyse();
    }

    // The chord being played (kept through silence), and whether the beat
    // has had sound in the last ~1.5 s.
    const Chord& chord() const noexcept { return current; }
    bool hearing() const noexcept { return silentFrames * kHop * decim < (int) (1.5 * fs); }
    float frameLevel() const noexcept { return level; }
    const std::array<float, 12>& getChroma() const noexcept { return chroma; }

    // Key evidence for LEARN: chroma summed over every frame with sound.
    const std::array<float, 12>& getKeyChroma() const noexcept { return keyChroma; }
    void clearKeyChroma() noexcept { keyChroma.fill (0.0f); }

    // The chord a single chroma vector suggests (best shape and root), with its score.
    // scoreOut: the match including the bass's vote; shapeOut: the plain shape
    // match (0..1), which must be high on its own: a flat, noisy chroma (drums)
    // matches every chord at ~0.5 and must never become one.
    static Chord match (const std::array<float, 12>& c, const std::array<float, 12>& b, float* scoreOut = nullptr,
                        float* shapeOut = nullptr) noexcept
    {
        float norm = 0.0f, bassMax = 0.0f;
        for (int i = 0; i < 12; ++i) { norm += c[(size_t) i] * c[(size_t) i]; bassMax = std::max (bassMax, b[(size_t) i]); }
        norm = std::sqrt (norm);
        Chord best;
        float bestScore = -1.0f;
        float bestShape = 0.0f;
        if (norm < 1.0e-9f) { if (scoreOut) *scoreOut = 0.0f; if (shapeOut) *shapeOut = 0.0f; return best; }
        for (int s = 0; s < kNumChordShapes; ++s)
        {
            const auto& sh = kChordShapes[s];
            const float tplNorm = std::sqrt ((float) sh.count);
            for (int root = 0; root < 12; ++root)
            {
                float dot = 0.0f;
                for (int i = 0; i < sh.count; ++i) dot += c[(size_t) ((root + sh.tones[(size_t) i]) % 12)];
                float score = dot / (norm * tplNorm);
                if (sh.count == 4) score *= 0.97f;                                   // simpler chords win ties
                const float shape = score;
                if (bassMax > 1.0e-9f) score += 0.12f * b[(size_t) root] / bassMax;   // the bass names the root
                if (score > bestScore) { bestScore = score; bestShape = shape; best.root = root; best.shape = s; }
            }
        }
        if (scoreOut) *scoreOut = bestScore;
        if (shapeOut) *shapeOut = bestShape;
        return best;
    }

private:
    void analyse() noexcept
    {
        // Frame, newest sample last.
        double e = 0.0;
        for (int i = 0; i < kFrame; ++i)
        {
            const float v = ring[(size_t) ((write + i) & (kFrame - 1))];
            e += (double) v * v;
            buf[(size_t) i] = { v * window[(size_t) i], 0.0f };
        }
        level = (float) std::sqrt (e / kFrame);
        if (level < dbToGain (-54.0f)) { ++silentFrames; return; }   // silence: keep the last chord
        silentFrames = 0;

        fft (buf);
        for (int k = 0; k < kFrame / 2; ++k) mags[(size_t) k] = std::abs (buf[(size_t) k]);

        // Prominent peaks only (a note stands ~12 dB above its neighbourhood;
        // noise, hats and kick sweeps do not), compressed (sqrt) so one loud
        // note cannot hide the others.
        std::array<float, 12> c {}, b {};
        float total = 0.0f, all = 0.0f;
        for (int k = 2; k < kFrame / 2 - 1; ++k)
        {
            const int pc = binPc[(size_t) k];
            if (pc < 0) continue;
            const float m = mags[(size_t) k];
            all += std::sqrt (m);
            if (m <= mags[(size_t) k - 1] || m < mags[(size_t) k + 1]) continue;
            float local = 0.0f;
            for (int j = k - 8; j <= k + 8; ++j) local += mags[(size_t) std::clamp (j, 0, kFrame / 2 - 1)];
            if (m < 4.0f * local / 17.0f) continue;
            const float v = std::sqrt (m) * binW[(size_t) k];
            c[(size_t) pc] += v;
            if (binBass[(size_t) k]) b[(size_t) pc] += v;
            total += v;
        }
        // Too little pitched content (drums only, noise): keep the last chord.
        if (total <= 1.0e-9f || total < 0.08f * all) return;
        for (int i = 0; i < 12; ++i)
        {
            c[(size_t) i] /= total;
            b[(size_t) i] /= total;
            chroma[(size_t) i] += hopAlpha * (c[(size_t) i] - chroma[(size_t) i]);
            bass[(size_t) i] += hopAlpha * (b[(size_t) i] - bass[(size_t) i]);
            keyChroma[(size_t) i] += hopSeconds * c[(size_t) i];   // ~1 per second of music
        }
        if (keyChroma[0] + keyChroma[7] > 1.0e4f) for (auto& k : keyChroma) k *= 0.5f;   // hours of listening: stay well inside float

        // Chord decision with hysteresis.
        float score = 0.0f, shape = 0.0f;
        const Chord next = match (chroma, bass, &score, &shape);
        if (! next.valid() || shape < kMinShape) { candidateHits = 0; return; }
        if (! current.valid()) { current = next; candidateHits = 0; return; }
        if (next.root == current.root && next.shape == current.shape) { candidateHits = 0; return; }

        float curScore = 0.0f;
        {
            // The current chord's own score on this chroma.
            const auto& sh = kChordShapes[current.shape];
            float norm = 0.0f, dot = 0.0f, bassMax = 0.0f;
            for (int i = 0; i < 12; ++i) { norm += chroma[(size_t) i] * chroma[(size_t) i]; bassMax = std::max (bassMax, bass[(size_t) i]); }
            for (int i = 0; i < sh.count; ++i) dot += chroma[(size_t) ((current.root + sh.tones[(size_t) i]) % 12)];
            curScore = dot / (std::sqrt (norm) * std::sqrt ((float) sh.count) + 1.0e-12f);
            if (sh.count == 4) curScore *= 0.97f;
            if (bassMax > 1.0e-9f) curScore += 0.12f * bass[(size_t) current.root] / bassMax;
        }
        if (score < curScore + 0.03f) { candidateHits = 0; return; }
        if (next.root == candidate.root && next.shape == candidate.shape) ++candidateHits;
        else { candidate = next; candidateHits = 1; }
        if (candidateHits >= 2) { current = next; candidateHits = 0; }
    }

    // In-place iterative radix-2 FFT (kFrame is a power of two).
    static void fft (std::vector<std::complex<float>>& a) noexcept
    {
        const int n = (int) a.size();
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[(size_t) i], a[(size_t) j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const float ang = -kTwoPi / (float) len;
            const std::complex<float> wl (std::cos (ang), std::sin (ang));
            for (int i = 0; i < n; i += len)
            {
                std::complex<float> w (1.0f, 0.0f);
                for (int k = 0; k < len / 2; ++k)
                {
                    const auto u = a[(size_t) (i + k)], v = a[(size_t) (i + k + len / 2)] * w;
                    a[(size_t) (i + k)] = u + v;
                    a[(size_t) (i + k + len / 2)] = u - v;
                    w *= wl;
                }
            }
        }
    }

    double fs = 48000.0, fsd = 12000.0;
    int decim = 4, phase = 0, hopCount = 0, write = 0, filled = 0;
    Svf aa1, aa2;
    std::vector<float> ring, window, mags, binW;
    std::vector<int> binPc;
    std::vector<char> binBass;
    std::vector<std::complex<float>> buf;
    std::array<float, 12> chroma {}, bass {}, keyChroma {};
    float hopAlpha = 0.3f, hopSeconds = 0.02f, level = 0.0f;
    int silentFrames = 0;
    Chord current, candidate;
    int candidateHits = 0;
};

// "Am7", "F#", "Csus4"; "-" before any chord has been heard.
inline void chordName (const Chord& c, char* out, int size) noexcept
{
    if (size <= 0) return;
    if (! c.valid()) { std::snprintf (out, (size_t) size, "-"); return; }
    std::snprintf (out, (size_t) size, "%s%s", noteName (c.root), kChordShapes[c.shape].suffix);
}

} // namespace lodestone::dsp
