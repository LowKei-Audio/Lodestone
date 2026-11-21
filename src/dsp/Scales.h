#pragma once
// Music theory for Lodestone: scales as 12-bit pitch-class masks, nearest
// allowed note with hysteresis, scale-step harmony intervals and key
// detection (Krumhansl-Kessler profiles). Plain C++, no JUCE.
//
// Pitch is handled as a "MIDI float": 69.0 = A4 at the reference pitch,
// 1.0 = one semitone. Pitch class 0 = C.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace lodestone::dsp
{

using Mask = std::uint16_t;   // bit i set = pitch class i allowed
inline constexpr Mask kAllNotes = 0x0FFF;

inline bool has (Mask m, int pc) noexcept { return (m >> (((pc % 12) + 12) % 12)) & 1; }
inline int countNotes (Mask m) noexcept { int n = 0; for (int i = 0; i < 12; ++i) n += (m >> i) & 1; return n; }

// Scale intervals relative to the key. The last entry, Custom, uses the 12
// note switches instead.
struct ScaleDef { const char* name; std::array<int, 12> steps; int count; };
inline constexpr ScaleDef kScales[] = {
    { "Chromatic",        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 12 },
    { "Major",            { 0, 2, 4, 5, 7, 9, 11 }, 7 },
    { "Minor",            { 0, 2, 3, 5, 7, 8, 10 }, 7 },
    { "Harmonic Minor",   { 0, 2, 3, 5, 7, 8, 11 }, 7 },
    { "Melodic Minor",    { 0, 2, 3, 5, 7, 9, 11 }, 7 },
    { "Major Pentatonic", { 0, 2, 4, 7, 9 }, 5 },
    { "Minor Pentatonic", { 0, 3, 5, 7, 10 }, 5 },
    { "Blues",            { 0, 3, 5, 6, 7, 10 }, 6 },
    { "Dorian",           { 0, 2, 3, 5, 7, 9, 10 }, 7 },
    { "Phrygian",         { 0, 1, 3, 5, 7, 8, 10 }, 7 },
    { "Lydian",           { 0, 2, 4, 6, 7, 9, 11 }, 7 },
    { "Mixolydian",       { 0, 2, 4, 5, 7, 9, 10 }, 7 },
    { "Locrian",          { 0, 1, 3, 5, 6, 8, 10 }, 7 },
    { "Double Harmonic",  { 0, 1, 4, 5, 7, 8, 11 }, 7 },
    { "Whole Tone",       { 0, 2, 4, 6, 8, 10 }, 6 },
    { "Custom",           { 0 }, 0 },
};
inline constexpr int kNumScales = (int) (sizeof (kScales) / sizeof (kScales[0]));
inline constexpr int kCustomScale = kNumScales - 1;
namespace Scale { enum : int { chromatic = 0, major, minor, harmonicMinor, melodicMinor, majorPent, minorPent, blues,
                               dorian, phrygian, lydian, mixolydian, locrian, doubleHarmonic, wholeTone, custom }; }

inline Mask scaleMask (int key, int scale, Mask custom = kAllNotes) noexcept
{
    if (scale < 0 || scale >= kNumScales) scale = 0;
    if (scale == kCustomScale) return custom & kAllNotes;
    Mask m = 0;
    const auto& s = kScales[scale];
    for (int i = 0; i < s.count; ++i) m |= (Mask) (1u << (((key + s.steps[(size_t) i]) % 12 + 12) % 12));
    return m;
}

// Nearest allowed note (integer MIDI) to `pitch`. An empty mask allows all.
inline int nearestAllowed (float pitch, Mask m) noexcept
{
    if ((m & kAllNotes) == 0) m = kAllNotes;
    const int base = (int) std::lround (pitch);
    int best = base;
    float bestD = 1.0e9f;
    for (int d = -12; d <= 12; ++d)
    {
        const int n = base + d;
        if (! has (m, n)) continue;
        const float dist = std::abs ((float) n - pitch);
        if (dist < bestD) { bestD = dist; best = n; }
    }
    return best;
}

// Keeps the current note until the pitch is clearly closer to another
// allowed note: the new note must win by `hysteresis` semitones.
inline int stickyNearest (float pitch, Mask m, int current, float hysteresis) noexcept
{
    const int cand = nearestAllowed (pitch, m);
    if (cand == current || ! has (m == 0 ? kAllNotes : m, current)) return cand;
    const float dCur = std::abs (pitch - (float) current), dCand = std::abs (pitch - (float) cand);
    return dCur - dCand > hysteresis ? cand : current;
}

// ---------------------------------------------------------------------------
// Chord Lock (v2). The notes a voice may land on, and how much the chord's own
// tones are preferred:
//   lock 0        the scale as set (bit for bit the 1.x behaviour)
//   0 < lock < 1  scale plus chord tones; a chord tone counts as 2 x lock
//                 semitones closer (50%: a note a semitone off a chord tone
//                 goes to it)
//   lock 1        chord tones only
struct Pull
{
    Mask allowed = kAllNotes;
    Mask preferred = 0;
    float bias = 0.0f;   // semitones
};

inline Pull chordPull (Mask scale, Mask chord, float lock) noexcept
{
    Pull p;
    p.allowed = scale;
    if ((chord & kAllNotes) == 0 || lock <= 0.0f) return p;
    if (lock >= 0.999f) { p.allowed = chord & kAllNotes; return p; }
    p.allowed = (Mask) ((scale | chord) & kAllNotes);
    p.preferred = chord & kAllNotes;
    p.bias = 2.0f * lock;
    return p;
}

// Nearest allowed note, preferred notes counting `bias` semitones closer.
inline int nearestPreferred (float pitch, const Pull& p) noexcept
{
    if (p.preferred == 0 || p.bias <= 0.0f) return nearestAllowed (pitch, p.allowed);
    Mask m = (p.allowed & kAllNotes) == 0 ? kAllNotes : p.allowed;
    const int base = (int) std::lround (pitch);
    int best = base;
    float bestD = 1.0e9f;
    for (int d = -12; d <= 12; ++d)
    {
        const int n = base + d;
        if (! has (m, n)) continue;
        const float dist = std::abs ((float) n - pitch) - (has (p.preferred, n) ? p.bias : 0.0f);
        if (dist < bestD) { bestD = dist; best = n; }
    }
    return best;
}

// stickyNearest with chord preference (same rule: the new note must win by `hysteresis`).
inline int stickyPreferred (float pitch, const Pull& p, int current, float hysteresis) noexcept
{
    if (p.preferred == 0 || p.bias <= 0.0f) return stickyNearest (pitch, p.allowed, current, hysteresis);
    const Mask m = (p.allowed & kAllNotes) == 0 ? kAllNotes : p.allowed;
    const int cand = nearestPreferred (pitch, p);
    if (cand == current || ! has (m, current)) return cand;
    auto cost = [&] (int n) { return std::abs (pitch - (float) n) - (has (p.preferred, n) ? p.bias : 0.0f); };
    return cost (current) - cost (cand) > hysteresis ? cand : current;
}

// Moves `steps` notes up (positive) or down through the allowed set.
inline int scaleStep (int note, int steps, Mask m) noexcept
{
    if ((m & kAllNotes) == 0) m = kAllNotes;
    if (! has (m, note)) note = nearestAllowed ((float) note, m);
    const int dir = steps > 0 ? 1 : -1;
    for (int k = 0; k != steps; k += dir)
    {
        int n = note + dir;
        for (int guard = 0; guard < 12 && ! has (m, n); ++guard) n += dir;
        note = n;
    }
    return note;
}

// Harmony intervals: scale steps (follow the key) or fixed semitones.
struct Interval { const char* name; int value; bool scaleSteps; };
inline constexpr Interval kIntervals[] = {
    { "Oct Down", -12, false }, { "6th Down", -2, true },  { "5th Down", -4, true }, { "4th Down", -3, true },
    { "3rd Down", -2, true },   { "Unison", 0, false },    { "3rd Up", 2, true },    { "4th Up", 3, true },
    { "5th Up", 4, true },      { "6th Up", 5, true },     { "Oct Up", 12, false },  { "Oct + 3rd", 9, true },
    { "Oct + 5th", 11, true },  { "-7 st", -7, false },    { "-5 st", -5, false },   { "+3 st", 3, false },
    { "+4 st", 4, false },      { "+7 st", 7, false },
};
inline constexpr int kNumIntervals = (int) (sizeof (kIntervals) / sizeof (kIntervals[0]));
namespace Ivl { enum : int { octDown = 0, sixthDown, fifthDown, fourthDown, thirdDown, unison, thirdUp, fourthUp, fifthUp,
                             sixthUp, octUp, octThird, octFifth, m7st, m5st, p3st, p4st, p7st }; }

// The harmony note for a lead note. "6th Down" is a 3rd below the octave:
// it steps down two notes, then drops an octave.
inline int harmonyNote (int leadNote, int intervalIndex, Mask m) noexcept
{
    if (intervalIndex < 0 || intervalIndex >= kNumIntervals) return leadNote;
    const auto& iv = kIntervals[intervalIndex];
    if (! iv.scaleSteps) return leadNote + iv.value;
    if (intervalIndex == Ivl::sixthDown) return scaleStep (leadNote, 2, m) - 12;
    if (intervalIndex == Ivl::octThird)  return scaleStep (leadNote, 2, m) + 12;
    if (intervalIndex == Ivl::octFifth)  return scaleStep (leadNote, 4, m) + 12;
    return scaleStep (leadNote, iv.value, m);
}

// The harmony note when the harmonies follow the chord (Chord Lock 50% and up):
// scale-step intervals step through the chord's tones instead, two scale steps
// to one chord step ("3rd Up" = the next chord tone up, "5th Up" = the one
// after), so every harmony is a note of the chord the beat is playing.
inline int chordHarmonyNote (int leadNote, int intervalIndex, Mask chord) noexcept
{
    if (intervalIndex < 0 || intervalIndex >= kNumIntervals) return leadNote;
    const auto& iv = kIntervals[intervalIndex];
    if (! iv.scaleSteps) return leadNote + iv.value;
    if ((chord & kAllNotes) == 0) return harmonyNote (leadNote, intervalIndex, kAllNotes);
    auto chordSteps = [] (int scaleSteps) { return scaleSteps > 0 ? (scaleSteps + 1) / 2 : -((-scaleSteps + 1) / 2); };
    if (intervalIndex == Ivl::sixthDown) return scaleStep (leadNote, 1, chord) - 12;
    if (intervalIndex == Ivl::octThird)  return scaleStep (leadNote, 1, chord) + 12;
    if (intervalIndex == Ivl::octFifth)  return scaleStep (leadNote, 2, chord) + 12;
    return scaleStep (leadNote, chordSteps (iv.value), chord);
}

// ---------------------------------------------------------------------------
// Key detection from a pitch-class histogram (Krumhansl-Kessler profiles).
struct KeyGuess { int key = 0; bool minor = false; float confidence = 0.0f; };

inline KeyGuess findKey (const std::array<float, 12>& chroma) noexcept
{
    static constexpr float major[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static constexpr float minor[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    auto corr = [&] (const float* prof, int key)
    {
        float mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += chroma[(size_t) i]; my += prof[i]; }
        mx /= 12.0f; my /= 12.0f;
        float sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const float x = chroma[(size_t) ((i + key) % 12)] - mx, y = prof[i] - my;
            sxy += x * y; sxx += x * x; syy += y * y;
        }
        return sxx > 1.0e-12f ? sxy / std::sqrt (sxx * syy) : 0.0f;
    };

    KeyGuess best;
    float bestR = -2.0f, second = -2.0f;
    for (int k = 0; k < 12; ++k)
        for (int mode = 0; mode < 2; ++mode)
        {
            const float r = corr (mode ? minor : major, k);
            if (r > bestR) { second = bestR; bestR = r; best.key = k; best.minor = mode == 1; }
            else if (r > second) second = r;
        }
    // Confidence: how strongly it correlates and how clearly it beats the runner-up.
    best.confidence = std::clamp (0.6f * std::max (0.0f, bestR) + 4.0f * std::max (0.0f, bestR - second), 0.0f, 1.0f);
    return best;
}

inline const char* noteName (int pc) noexcept
{
    static constexpr const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((pc % 12) + 12) % 12];
}

} // namespace lodestone::dsp
