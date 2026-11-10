#include "Parameters.h"
#include "dsp/Module.h"
#include "dsp/ModSources.h"
#include "dsp/VoiceEngine.h"
#include "presets/FactoryPresets.h"

#include <unordered_map>

namespace lodestone
{

namespace Choices
{
    const juce::StringArray notes { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const juce::StringArray scales = []
    {
        juce::StringArray s;
        for (const auto& sc : dsp::kScales) s.add (sc.name);
        return s;
    }();
    const juce::StringArray ranges = []
    {
        juce::StringArray s;
        for (const auto& r : dsp::kRanges) s.add (r.name);
        return s;
    }();
    const juce::StringArray midiModes  { "Off", "Scale", "Exact", "Harmony" };
    const juce::StringArray characters { "Modern", "Classic" };
    const juce::StringArray gritModes = []
    {
        juce::StringArray s;
        for (const auto& g : dsp::kGritModes) s.add (g.name);
        return s;
    }();
    const juce::StringArray intervals = []
    {
        juce::StringArray s;
        for (const auto& i : dsp::kIntervals) s.add (i.name);
        return s;
    }();
    const juce::StringArray divisions = []
    {
        juce::StringArray s;
        for (const auto& d : dsp::kDivisions) s.add (d.name);
        return s;
    }();
    const juce::StringArray lfoShapes { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Rnd" };
}

namespace
{
using U = Unit;

ParamDef F (P p, const char* id, const char* name, const char* label, float lo, float hi, float d, float centre,
            Unit u, bool mod, const char* help)
{
    return { p, id, name, label, Kind::flt, lo, hi, d, centre, u, mod, nullptr, help };
}
ParamDef B (P p, const char* id, const char* name, const char* label, bool d, const char* help)
{
    return { p, id, name, label, Kind::boolean, 0, 1, d ? 1.0f : 0.0f, 0, U::plain, false, nullptr, help };
}
ParamDef C (P p, const char* id, const char* name, const char* label, const juce::StringArray& c, int d, const char* help)
{
    return { p, id, name, label, Kind::choice, 0, (float) (c.size() - 1), (float) d, 0, U::plain, false, &c, help };
}
ParamDef I (P p, const char* id, const char* name, const char* label, int lo, int hi, int d, Unit u, const char* help)
{
    return { p, id, name, label, Kind::integer, (float) lo, (float) hi, (float) d, 0, u, false, nullptr, help };
}

const std::vector<ParamDef>& table()
{
    using namespace Choices;
    static const std::vector<ParamDef> t = [] {
    std::vector<ParamDef> v = {
        // Global ---------------------------------------------------------------------------------------
        F (P::inGain,  "in_gain",  "Input",  "Input",  -24, 24, 0, 0, U::db, false, "Level going into Lodestone (also what the pitch tracker hears)"),
        F (P::outGain, "out_gain", "Output", "Output", -24, 24, 0, 0, U::db, false, "Final output level"),
        F (P::mix,     "mix",      "Mix",    "Mix",    0, 1, 1, 0, U::pct, true, "Blend of the untouched voice and Lodestone (both stay in time)"),
        B (P::limiter, "limiter",  "Safety Limiter", "Limiter", true, "Catches peaks at -0.3 dBFS so stacked harmonies can never clip"),
        B (P::snap,    "snap",     "Snap", "Snap", false, "Instant hard tune: the robot effect, for as long as it is engaged"),
        B (P::hold,    "hold",     "Hold", "Hold", false, "Freezes the current note: whatever is sung comes out on that note"),
        F (P::macro1, "macro1", "Macro 1 Pull",   "Pull",   0, 1, 0, 0, U::pct, false, "How hard the lodestone pulls: faster, firmer, more robotic"),
        F (P::macro2, "macro2", "Macro 2 Throat", "Throat", 0, 1, 0, 0, U::pct, false, "Throat and character: longer, darker, grittier"),
        F (P::macro3, "macro3", "Macro 3 Choir",  "Choir",  0, 1, 0, 0, U::pct, false, "Brings up the harmonies and the double"),
        F (P::macro4, "macro4", "Macro 4 Width",  "Width",  0, 1, 0, 0, U::pct, false, "Spreads the harmonies and the double wider"),
        F (P::tempo,   "tempo", "Free Tempo", "Tempo", 40, 240, 120, 0, U::bpm, false, "Tempo used when the host does not send one (standalone)"),
        I (P::seed,    "seed", "Random Seed", "Seed", 1, 9999, 1, U::plain, "Same seed = same humanize drift and random modulation every time"),
        F (P::width,   "width", "Width", "Width", 0, 2, 1, 1, U::pct, true, "Stereo width of the harmonies and double (100% = as panned)"),

        // Tune -----------------------------------------------------------------------------------------
        B (P::tuneOn,  "tune_on", "Correction On", "Correction", true, "Pitch correction. Off: the voice passes untouched, harmonies and double still play"),
        C (P::key,     "key",     "Key",   "Key",   notes, 0, "The key of the song (Learn can find it for you)"),
        C (P::scale,   "scale",   "Scale", "Scale", scales, dsp::Scale::major, "The notes the voice is pulled to. Custom = pick them on the keyboard"),
        F (P::refPitch, "ref_pitch", "Reference Pitch", "A =", 415, 466, 440, 0, U::hzFine, false, "Tuning reference: the frequency of A4"),
        C (P::range,   "range",   "Input Type", "Input", ranges, 1, "The voice's range: tracks faster and more reliably when set to match"),
        F (P::sensitivity, "sensitivity", "Tracking", "Tracking", 0, 1, 0.5f, 0, U::pct, false, "Higher follows quieter and breathier notes; lower only clear, strong notes"),
        F (P::retune,   "retune",   "Retune Speed", "Speed", 0, 400, 25, 40, U::ms, true, "How fast notes are pulled in tune. 0 ms = the hard-tuned robot effect"),
        F (P::humanize, "humanize", "Humanize", "Humanize", 0, 1, 0.25f, 0, U::pct, true, "Relaxes the pull on long notes so they keep their life; short notes stay tight"),
        F (P::flex,     "flex",     "Flex", "Flex", 0, 1, 0.15f, 0, U::pct, true, "Leaves expressive moves alone: only notes sung close to a target are corrected"),
        F (P::amount,   "amount",   "Amount", "Amount", 0, 1, 1, 0, U::pct, true, "How much of the way to the note it pulls (100% = all the way)"),
        F (P::bounce,   "bounce",   "Bounce", "Bounce", 0, 1, 0, 0, U::pct, true, "The pull overshoots and springs back on note changes: a flutter or a yodel"),
        F (P::stability, "stability", "Note Stability", "Stability", 0, 1, 0.5f, 0, U::pct, true, "How sure it must be before changing note. Stops vibrato and scoops from flipping notes"),
        F (P::keep,     "keep",     "Natural Vibrato", "Vibrato", 0, 2, 1, 1, U::pct, true, "How much of the singer's own vibrato survives (0% flat, 100% as sung, 200% exaggerated)"),
        I (P::transpose, "transpose", "Transpose", "Transpose", -24, 24, 0, U::semis, "Moves the output up or down in semitones"),
        F (P::detune,   "detune",   "Fine Tune", "Fine", -100, 100, 0, 0, U::cents, true, "Moves the output up or down in cents"),
        C (P::midiMode, "midi_mode", "MIDI Mode", "MIDI", midiModes, 0,
           "MIDI notes: Scale = held notes become the scale, Exact = sing on the played note, Harmony = held notes become harmony voices"),
        C (P::character, "character", "Character", "Character", characters, 0, "Modern keeps the voice's natural tone; Classic lets the tone shift with the pitch (old-school)"),
        F (P::formant,  "formant",  "Throat", "Throat", -12, 12, 0, 0, U::semis, true, "Throat length: down = longer, deeper, bigger; up = shorter, younger, smaller"),
        F (P::breath,   "breath",   "Breath", "Breath", 0, 1.5f, 1, 0, U::pct, true, "Level of breaths and consonants (the parts that have no pitch)"),
        F (P::lead,     "lead",     "Lead Level", "Lead", 0, 1, 1, 0, U::pct, true, "Level of the tuned lead voice (0 = harmonies and double only)"),
        F (P::grit,     "grit",     "Grit", "Grit", 0, 1, 0, 0, U::pct, true, "Pitch-locked growl: a ring modulator that follows the voice"),
        C (P::gritMode, "grit_mode", "Grit Type", "Grit Type", gritModes, 0, "Sub (an octave-down rumble), Growl, Buzz or Metal"),
    };

    static const char* noteIds[12]   = { "note_c", "note_cs", "note_d", "note_ds", "note_e", "note_f", "note_fs", "note_g", "note_gs", "note_a", "note_as", "note_b" };
    static const char* noteNames[12] = { "Note C", "Note C#", "Note D", "Note D#", "Note E", "Note F", "Note F#", "Note G", "Note G#", "Note A", "Note A#", "Note B" };
    static const char* centIds[12]   = { "cents_c", "cents_cs", "cents_d", "cents_ds", "cents_e", "cents_f", "cents_fs", "cents_g", "cents_gs", "cents_a", "cents_as", "cents_b" };
    static const char* centNames[12] = { "Tune C", "Tune C#", "Tune D", "Tune D#", "Tune E", "Tune F", "Tune F#", "Tune G", "Tune G#", "Tune A", "Tune A#", "Tune B" };
    for (int i = 0; i < 12; ++i)
        v.push_back (B (noteParam (i), noteIds[i], noteNames[i], Choices::notes[i].toRawUTF8(), true, "In the Custom scale: on = a target note, off = skipped"));
    for (int i = 0; i < 12; ++i)
        v.push_back (F (centsParam (i), centIds[i], centNames[i], Choices::notes[i].toRawUTF8(), -50, 50, 0, 0, U::cents, false,
                        "Micro-tuning: moves this note's target up or down (for blue notes, maqam and other tunings)"));

    const std::vector<ParamDef> rest = {
        // Vibrato --------------------------------------------------------------------------------------
        B (P::vbOn,    "vb_on",    "Vibrato On", "Vibrato", false, "Adds a vibrato of its own, on top of (or instead of) the singer's"),
        F (P::vbDepth, "vb_depth", "Vibrato Depth", "Depth", 0, 100, 25, 30, U::cents, true, "How wide the added vibrato swings"),
        F (P::vbRate,  "vb_rate",  "Vibrato Rate", "Rate", 1, 10, 5.5f, 0, U::hz, true, "How fast it swings"),
        F (P::vbDelay, "vb_delay", "Vibrato Delay", "Delay", 0, 1500, 300, 400, U::ms, false, "Each note starts straight for this long"),
        F (P::vbOnset, "vb_onset", "Vibrato Onset", "Onset", 0, 1500, 400, 400, U::ms, false, "Then the vibrato grows in over this long"),
        F (P::vbVar,   "vb_var",   "Vibrato Variation", "Variation", 0, 1, 0.25f, 0, U::pct, true, "Random wander in its rate and depth, like a real singer"),

        // Harmonies ------------------------------------------------------------------------------------
        B (P::h1On,       "h1_on",       "Harmony 1 On", "Harmony 1", false, "A harmony voice built from the lead, in key"),
        C (P::h1Interval, "h1_interval", "Harmony 1 Interval", "Interval", intervals, dsp::Ivl::thirdUp, "Interval from the lead. Named intervals follow the scale; 'st' ones are fixed"),
        F (P::h1Level,    "h1_level",    "Harmony 1 Level", "Level", 0, 1, 0.6f, 0, U::pct, true, "Level of harmony 1"),
        F (P::h1Pan,      "h1_pan",      "Harmony 1 Pan", "Pan", -1, 1, -0.5f, 0, U::pctBipolar, true, "Left to right"),
        F (P::h1Formant,  "h1_formant",  "Harmony 1 Throat", "Throat", -12, 12, 0, 0, U::semis, true, "Gives this voice its own throat: up for a lighter singer, down for a deeper one"),
        B (P::h2On,       "h2_on",       "Harmony 2 On", "Harmony 2", false, "A second harmony voice"),
        C (P::h2Interval, "h2_interval", "Harmony 2 Interval", "Interval", intervals, dsp::Ivl::fifthUp, "Interval from the lead. Named intervals follow the scale; 'st' ones are fixed"),
        F (P::h2Level,    "h2_level",    "Harmony 2 Level", "Level", 0, 1, 0.5f, 0, U::pct, true, "Level of harmony 2"),
        F (P::h2Pan,      "h2_pan",      "Harmony 2 Pan", "Pan", -1, 1, 0.5f, 0, U::pctBipolar, true, "Left to right"),
        F (P::h2Formant,  "h2_formant",  "Harmony 2 Throat", "Throat", -12, 12, 0, 0, U::semis, true, "Gives this voice its own throat"),
        B (P::h3On,       "h3_on",       "Harmony 3 On", "Harmony 3", false, "A third harmony voice"),
        C (P::h3Interval, "h3_interval", "Harmony 3 Interval", "Interval", intervals, dsp::Ivl::octDown, "Interval from the lead. Named intervals follow the scale; 'st' ones are fixed"),
        F (P::h3Level,    "h3_level",    "Harmony 3 Level", "Level", 0, 1, 0.4f, 0, U::pct, true, "Level of harmony 3"),
        F (P::h3Pan,      "h3_pan",      "Harmony 3 Pan", "Pan", -1, 1, 0, 0, U::pctBipolar, true, "Left to right"),
        F (P::h3Formant,  "h3_formant",  "Harmony 3 Throat", "Throat", -12, 12, 0, 0, U::semis, true, "Gives this voice its own throat"),
        F (P::harmHumanize, "harm_humanize", "Harmony Humanize", "Humanize", 0, 1, 0.3f, 0, U::pct, true, "Tiny timing and pitch drift so harmonies sound sung, not cloned"),

        // Doubler --------------------------------------------------------------------------------------
        B (P::dbOn,     "db_on",     "Double On", "Double", false, "Two extra takes of the lead, slightly off in time and pitch: thick and wide"),
        F (P::dbLevel,  "db_level",  "Double Level", "Level", 0, 1, 0.5f, 0, U::pct, true, "Level of the double"),
        F (P::dbSpread, "db_spread", "Double Spread", "Spread", 0, 1, 0.7f, 0, U::pct, true, "How far apart the two takes are panned"),
        F (P::dbDetune, "db_detune", "Double Detune", "Detune", 0, 30, 8, 10, U::cents, true, "How far out of tune each take is (in cents)"),
        F (P::dbDelay,  "db_delay",  "Double Delay", "Delay", 5, 60, 18, 20, U::ms, true, "How late the takes come in"),

        // Modulation sources ---------------------------------------------------------------------------
        C (P::lfo1Shape, "lfo1_shape", "LFO 1 Shape", "Shape", lfoShapes, 0, "LFO 1 waveform"),
        F (P::lfo1Rate,  "lfo1_rate",  "LFO 1 Rate", "Rate", 0.01f, 20, 0.5f, 1.0f, U::hz, true, "LFO 1 speed"),
        B (P::lfo1Sync,  "lfo1_sync",  "LFO 1 Sync", "Sync", false, "Lock LFO 1 to the host tempo"),
        C (P::lfo1Div,   "lfo1_div",   "LFO 1 Division", "Rate", divisions, dsp::Div::bar1, "LFO 1 synced speed"),
        C (P::lfo2Shape, "lfo2_shape", "LFO 2 Shape", "Shape", lfoShapes, 6, "LFO 2 waveform"),
        F (P::lfo2Rate,  "lfo2_rate",  "LFO 2 Rate", "Rate", 0.01f, 20, 2.0f, 1.0f, U::hz, true, "LFO 2 speed"),
        B (P::lfo2Sync,  "lfo2_sync",  "LFO 2 Sync", "Sync", true, "Lock LFO 2 to the host tempo"),
        C (P::lfo2Div,   "lfo2_div",   "LFO 2 Division", "Rate", divisions, dsp::Div::d8, "LFO 2 synced speed"),
        F (P::envAttack,  "env_attack",  "Envelope Attack", "Attack", 0.5f, 500, 10, 30, U::ms, false, "How fast the envelope follows rising level"),
        F (P::envRelease, "env_release", "Envelope Release", "Release", 10, 3000, 250, 300, U::ms, false, "How fast it falls back"),
        F (P::envGain,    "env_gain",    "Envelope Sensitivity", "Sensitivity", -12, 24, 6, 0, U::db, false, "Boosts the envelope for quiet sources"),
        F (P::driftRate,  "drift_rate",  "Drift Rate", "Rate", 0.02f, 20, 0.5f, 1.0f, U::hz, true, "How often Drift picks a new random value"),
        F (P::driftSmooth, "drift_smooth", "Drift Smooth", "Smooth", 0, 1, 0.7f, 0, U::pct, false, "Stepped (0%) to gliding (100%)"),
    };
    v.insert (v.end(), rest.begin(), rest.end());
    return v;
    }();
    return t;
}

const struct TableCheck
{
    TableCheck()
    {
        const auto& t = table();
        jassert ((int) t.size() == kNumParams);
        for (int i = 0; i < (int) t.size(); ++i)
            jassert ((int) t[(size_t) i].p == i);   // table order must match enum P
    }
} tableCheck;
} // namespace

const ParamDef& def (P p) { return table()[(size_t) p]; }

int findParam (const juce::String& id)
{
    static const auto map = []
    {
        std::unordered_map<std::string, int> m;
        for (int i = 0; i < kNumParams; ++i) m[def (i).id] = i;
        return m;
    }();
    const auto it = map.find (id.toStdString());
    return it == map.end() ? -1 : it->second;
}

namespace
{
// Formats v with fine decimals below `limit` and coarse decimals above it.
// The choice is made on the value as it will be printed (rounded), so that
// value -> text -> value -> text is stable (hosts and clap-validator check).
juce::String fmtSplit (float v, float limit, int fineDecimals, int coarseDecimals)
{
    const float scale = std::pow (10.0f, (float) fineDecimals);
    const float shown = std::round (v * scale) / scale;
    return std::abs (shown) < limit ? juce::String (v, fineDecimals) : juce::String (v, coarseDecimals);
}
} // namespace

juce::String formatValue (const ParamDef& d, float v)
{
    using juce::String;
    switch (d.kind)
    {
        case Kind::boolean: return v > 0.5f ? "On" : "Off";
        case Kind::choice:  return (*d.choices)[juce::jlimit (0, d.choices->size() - 1, juce::roundToInt (v))];
        case Kind::integer:
        {
            const int k = juce::roundToInt (v);
            if (d.unit == Unit::semis) return (k > 0 ? "+" : "") + String (k) + " st";
            return String (k);
        }
        case Kind::flt:     break;
    }
    switch (d.unit)
    {
        case Unit::pct:        return String (juce::roundToInt (v * 100.0f)) + "%";
        case Unit::pctBipolar:
        {
            const int k = juce::roundToInt (v * 100.0f);
            if (k == 0) return "C";
            return String (std::abs (k)) + (k < 0 ? " L" : " R");
        }
        case Unit::db:         return (v > 0.05f ? "+" : "") + String (v, 1) + " dB";
        case Unit::hz:
            if (std::abs (std::round (v)) >= 1000.0f) return String (v / 1000.0f, 2) + " kHz";
            return fmtSplit (v, 10.0f, 2, 1) + " Hz";
        case Unit::hzFine:     return String (v, 1) + " Hz";
        case Unit::ms:         return fmtSplit (v, 10.0f, 1, 0) + " ms";
        case Unit::sec:        return fmtSplit (v, 10.0f, 2, 1) + " s";
        case Unit::semis:      { const float r = std::round (v * 10.0f) / 10.0f; return (r > 0 ? "+" : "") + String (r, 1) + " st"; }
        case Unit::cents:      { const int k = juce::roundToInt (v); return (k > 0 ? "+" : "") + String (k) + " ct"; }
        case Unit::bpm:        return String (v, 1) + " bpm";
        case Unit::plain:      return fmtSplit (v, 10.0f, 1, 0);
    }
    return String (v);
}

// The default each parameter reports to the host: its value in the Init preset,
// which is what the plugin loads on startup. Hosts (and clap-validator) expect a
// fresh instance to sit at its reported defaults.
float hostDefault (int i)
{
    for (const auto& [p, val] : factoryPresets().front().values)
        if (idx (p) == i) return val;
    return def (i).def;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = def (i);
        const float dflt = hostDefault (i);
        const ParameterID pid { d.id, 1 };
        switch (d.kind)
        {
            case Kind::flt:
            {
                NormalisableRange<float> r (d.min, d.max);
                if (d.centre != 0.0f && d.centre > d.min && d.centre < d.max) r.setSkewForCentre (d.centre);
                auto attrs = AudioParameterFloatAttributes()
                                 .withStringFromValueFunction ([&d] (float v, int) { return formatValue (d, v); })
                                 .withValueFromStringFunction ([&d] (const String& s)
                                 {
                                     const float x = s.getFloatValue();
                                     if (d.unit == Unit::pct) return x / 100.0f;
                                     if (d.unit == Unit::pctBipolar)
                                     {
                                         if (s.trim().equalsIgnoreCase ("C")) return 0.0f;
                                         return (s.containsIgnoreCase ("L") ? -x : x) / 100.0f;
                                     }
                                     if (d.unit == Unit::hz && s.containsIgnoreCase ("k")) return x * 1000.0f;
                                     return x;
                                 });
                layout.add (std::make_unique<AudioParameterFloat> (pid, d.name, r, dflt, attrs));
                break;
            }
            case Kind::boolean:
                layout.add (std::make_unique<AudioParameterBool> (pid, d.name, dflt > 0.5f));
                break;
            case Kind::choice:
                layout.add (std::make_unique<AudioParameterChoice> (pid, d.name, *d.choices, (int) dflt));
                break;
            case Kind::integer:
            {
                auto attrs = AudioParameterIntAttributes()
                                 .withStringFromValueFunction ([&d] (int v, int) { return formatValue (d, (float) v); })
                                 .withValueFromStringFunction ([] (const String& s) { return s.getIntValue(); });
                layout.add (std::make_unique<AudioParameterInt> (pid, d.name, (int) d.min, (int) d.max, (int) dflt, attrs));
                break;
            }
        }
    }
    return layout;
}

// ---------------------------------------------------------------------------
const StageInfo kStages[kNumStages] = {
    { "Correction", "Pulls the voice onto the notes of the scale", P::tuneOn },
    { "Vibrato",    "An added vibrato with delay and onset",       P::vbOn },
    { "Harmony 1",  "A harmony voice in key",                      P::h1On },
    { "Harmony 2",  "A second harmony voice",                      P::h2On },
    { "Harmony 3",  "A third harmony voice",                       P::h3On },
    { "Double",     "Two extra takes for a thick, wide lead",      P::dbOn },
};

const juce::StringArray& sourceNames()
{
    static const juce::StringArray s { "None", "LFO 1", "LFO 2", "Envelope", "Drift",
                                       "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    return s;
}

} // namespace lodestone
