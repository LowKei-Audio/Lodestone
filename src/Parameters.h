#pragma once
// Every automatable parameter, defined once in a table (Parameters.cpp):
// ID, display name, range, default, unit and whether the mod matrix can
// reach it. IDs are permanent once shipped (DAW projects and presets store
// them), so rename the display name, never the ID.

#include <juce_audio_processors/juce_audio_processors.h>

namespace lodestone
{

// Order matches the table in Parameters.cpp (checked at startup).
enum class P : int
{
    // Global
    inGain, outGain, mix, limiter, snap, hold, macro1, macro2, macro3, macro4, tempo, seed, width,
    // Tune
    tuneOn, key, scale, refPitch, range, sensitivity, retune, humanize, flex, amount, bounce, stability,
    keep, transpose, detune, midiMode, character, formant, breath, lead, grit, gritMode,
    // Custom scale notes and per-note tuning (pitch class C .. B)
    noteC, noteCs, noteD, noteDs, noteE, noteF, noteFs, noteG, noteGs, noteA, noteAs, noteB,
    centsC, centsCs, centsD, centsDs, centsE, centsF, centsFs, centsG, centsGs, centsA, centsAs, centsB,
    // Vibrato (created)
    vbOn, vbDepth, vbRate, vbDelay, vbOnset, vbVar,
    // Harmony voices
    h1On, h1Interval, h1Level, h1Pan, h1Formant,
    h2On, h2Interval, h2Level, h2Pan, h2Formant,
    h3On, h3Interval, h3Level, h3Pan, h3Formant,
    harmHumanize,
    // Doubler
    dbOn, dbLevel, dbSpread, dbDetune, dbDelay,
    // Beat (v2): Chord Lock and the Motion pattern
    chordLock, motionOn, motionRate, motionLength, motionGlide, motionStart,
    mStep1, mStep2, mStep3, mStep4, mStep5, mStep6, mStep7, mStep8,
    mStep9, mStep10, mStep11, mStep12, mStep13, mStep14, mStep15, mStep16,
    // Modulation sources
    lfo1Shape, lfo1Rate, lfo1Sync, lfo1Div,
    lfo2Shape, lfo2Rate, lfo2Sync, lfo2Div,
    envAttack, envRelease, envGain,
    driftRate, driftSmooth,
    count
};

inline constexpr int kNumParams = (int) P::count;
inline constexpr int idx (P p) noexcept { return (int) p; }
inline P noteParam (int pc) noexcept  { return (P) (idx (P::noteC) + pc); }
inline P centsParam (int pc) noexcept { return (P) (idx (P::centsC) + pc); }
inline P motionStepParam (int i) noexcept { return (P) (idx (P::mStep1) + i); }

enum class Kind { flt, boolean, choice, integer };
enum class Unit { plain, pct, pctBipolar, db, hz, hzFine, ms, sec, semis, cents, bpm, steps };

struct ParamDef
{
    P p;
    const char* id;
    const char* name;       // full name (DAW automation lane)
    const char* label;      // short knob legend
    Kind kind;
    float min, max, def;
    float centre;           // skew so this value sits mid-knob (0 = linear)
    Unit unit;
    bool modulatable;       // may be a mod-matrix destination
    const juce::StringArray* choices = nullptr;
    const char* help = "";  // one-line description for tooltips / hint bar
};

const ParamDef& def (P p);
inline const ParamDef& def (int i) { return def ((P) i); }
int findParam (const juce::String& id);   // -1 if unknown

namespace Choices
{
    extern const juce::StringArray notes, scales, ranges, midiModes, characters, gritModes, intervals, divisions, lfoShapes,
                                   motionStarts;
}

// Text shown for a value of a parameter (used by the host and the UI).
juce::String formatValue (const ParamDef& d, float value);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// ---------------------------------------------------------------------------
// Sections with a power switch (Pro view and the Easy view's voice switches).
struct StageInfo
{
    const char* name;
    const char* blurb;
    P on;
};
inline constexpr int kNumStages = 7;
extern const StageInfo kStages[kNumStages];
namespace Stage { enum : int { tune = 0, vibrato, harmony1, harmony2, harmony3, doubler, motion }; }

// Harmony voice h (0..2): its first parameter (On); the others follow in order.
inline P harmonyParam (int h, int field) noexcept { return (P) (idx (P::h1On) + 5 * h + field); }
namespace HField { enum : int { on = 0, interval, level, pan, formant }; }

// ---------------------------------------------------------------------------
// Modulation sources for the matrix.
enum class Src : int { none = 0, lfo1, lfo2, env, drift, macro1, macro2, macro3, macro4, count };
inline constexpr int kNumSources = (int) Src::count;
const juce::StringArray& sourceNames();
inline bool isBipolar (Src s) noexcept { return s == Src::lfo1 || s == Src::lfo2 || s == Src::drift; }

inline constexpr int kMatrixSlots = 16;
inline constexpr int kNumMacros = 4;
inline constexpr const char* kMacroNames[kNumMacros] = { "Pull", "Throat", "Choir", "Width" };

} // namespace lodestone
