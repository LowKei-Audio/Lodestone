#include "FactoryPresets.h"
#include "../dsp/Module.h"
#include "../dsp/VoiceEngine.h"

namespace lodestone
{

// ---------------------------------------------------------------------------
// Standard macro kit. Every preset gets the same four meanings:
//   1 Pull   - faster retune, less humanize / flex / vibrato: harder tuning
//   2 Throat - a longer throat and a little grit
//   3 Choir  - the harmonies and the double that are switched on come up
//   4 Width  - the double spreads and the voices widen
std::vector<Route> macroKit (const std::array<bool, kNumStages>& used)
{
    std::vector<Route> r;
    r.push_back ({ Src::macro1, P::retune, -0.4f });
    r.push_back ({ Src::macro1, P::humanize, -0.3f });
    r.push_back ({ Src::macro1, P::flex, -0.35f });
    r.push_back ({ Src::macro1, P::keep, -0.3f });

    r.push_back ({ Src::macro2, P::formant, -0.22f });
    r.push_back ({ Src::macro2, P::grit, 0.35f });

    for (int h = 0; h < 3; ++h)
        if (used[(size_t) (Stage::harmony1 + h)]) r.push_back ({ Src::macro3, harmonyParam (h, HField::level), 0.4f });
    if (used[Stage::doubler]) r.push_back ({ Src::macro3, P::dbLevel, 0.4f });

    if (used[Stage::doubler]) r.push_back ({ Src::macro4, P::dbSpread, 0.35f });
    r.push_back ({ Src::macro4, P::width, 0.35f });
    return r;
}

// ---------------------------------------------------------------------------
namespace
{
using namespace dsp;

constexpr float on = 1.0f;
namespace Key { enum { C = 0, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B }; }
namespace Lfo { enum { sine = 0, tri, sawUp, sawDown, square, sh, smooth }; }
namespace Midi { enum { off = 0, scale, exact, harmony }; }
constexpr float classic = 1.0f;

std::vector<FactoryPreset> build()
{
    std::vector<FactoryPreset> v;

    // ======================================================================= Init
    v.push_back ({ "Init", "Init",
        "Natural correction in C major. Set the Key and Scale (or press LEARN), then shape it with Speed and Humanize.",
        {} });

    // ======================================================================= Natural
    v.push_back ({ "Transparent", "Natural",
        "Barely there: only notes that drift get nudged. Scoops, slides and vibrato stay exactly as sung.",
        { { P::retune, 55.0f }, { P::humanize, 0.5f }, { P::flex, 0.4f }, { P::stability, 0.65f } } });
    v.push_back ({ "Studio Polish", "Natural",
        "Everyday lead vocal tuning: in tune, still human.",
        { { P::retune, 22.0f }, { P::humanize, 0.35f }, { P::flex, 0.15f }, { P::keep, 0.9f } } });
    v.push_back ({ "Gentle Ballad", "Natural",
        "Slow, soft pull for long notes and big vibrato. Short notes still land.",
        { { P::retune, 70.0f }, { P::humanize, 0.6f }, { P::flex, 0.3f }, { P::keep, 1.1f }, { P::stability, 0.75f } } });
    v.push_back ({ "Backing Section", "Natural",
        "Tidy tuning for backing vocals, with a subtle double for width.",
        { { P::retune, 30.0f }, { P::humanize, 0.3f }, { P::dbOn, on }, { P::dbLevel, 0.35f }, { P::dbSpread, 0.5f },
          { P::dbDetune, 6.0f } } });

    // ======================================================================= Pop
    v.push_back ({ "Modern Pop", "Pop",
        "Tight, bright, radio-ready. Vibrato reined in, notes snap into place.",
        { { P::retune, 10.0f }, { P::humanize, 0.2f }, { P::flex, 0.05f }, { P::keep, 0.6f }, { P::stability, 0.55f } } });
    v.push_back ({ "Glossy Lead", "Pop",
        "A polished, slightly doubled lead that sits on top of a busy mix.",
        { { P::retune, 6.0f }, { P::humanize, 0.15f }, { P::keep, 0.4f }, { P::dbOn, on }, { P::dbLevel, 0.25f }, { P::dbSpread, 0.6f } } });
    v.push_back ({ "Tight Hook", "Pop",
        "For the hook: firm and precise, with only a hint of the singer's vibrato.",
        { { P::retune, 3.0f }, { P::humanize, 0.1f }, { P::flex, 0.0f }, { P::keep, 0.3f }, { P::stability, 0.6f } } });

    // ======================================================================= Hard Tune
    v.push_back ({ "Robot", "Hard Tune",
        "The classic hard-tuned effect: notes jump instantly, no vibrato.",
        { { P::retune, 0.0f }, { P::humanize, 0.0f }, { P::flex, 0.0f }, { P::keep, 0.0f }, { P::stability, 0.35f } } });
    v.push_back ({ "Classic 98", "Hard Tune",
        "Old-school hard tune: the tone moves with the pitch for that late-90s sound.",
        { { P::retune, 0.0f }, { P::humanize, 0.0f }, { P::flex, 0.0f }, { P::keep, 0.0f }, { P::character, classic },
          { P::stability, 0.3f } } });
    v.push_back ({ "Flutter", "Hard Tune",
        "Hard tune with a spring: every note change flutters before it lands.",
        { { P::retune, 4.0f }, { P::humanize, 0.0f }, { P::flex, 0.0f }, { P::keep, 0.0f }, { P::bounce, 0.6f } } });
    v.push_back ({ "Yodel Jump", "Hard Tune",
        "A wide, bouncy overshoot on every jump. Great on big intervals.",
        { { P::retune, 22.0f }, { P::humanize, 0.0f }, { P::flex, 0.0f }, { P::keep, 0.0f }, { P::bounce, 0.9f } } });
    v.push_back ({ "Hard But Human", "Hard Tune",
        "Robot edges on note changes, but long notes keep a little life.",
        { { P::retune, 2.0f }, { P::humanize, 0.45f }, { P::flex, 0.0f }, { P::keep, 0.2f } } });

    // ======================================================================= Melodic Trap
    v.push_back ({ "Melodic Trap", "Melodic Trap",
        "Hard tune in a minor key with a wide double. The modern melodic rap sound.",
        { { P::scale, (float) Scale::minor }, { P::key, (float) Key::A }, { P::retune, 0.0f }, { P::humanize, 0.0f },
          { P::flex, 0.0f }, { P::keep, 0.1f }, { P::stability, 0.45f },
          { P::dbOn, on }, { P::dbLevel, 0.3f }, { P::dbSpread, 0.8f } } });
    v.push_back ({ "Late Night", "Melodic Trap",
        "Dark and close: fast tune, a slightly longer throat and a soft double.",
        { { P::scale, (float) Scale::minor }, { P::key, (float) Key::Fs }, { P::retune, 4.0f }, { P::keep, 0.3f },
          { P::formant, -1.5f }, { P::dbOn, on }, { P::dbLevel, 0.25f } } });
    v.push_back ({ "Wavy", "Melodic Trap",
        "A loose pull with a little bounce and an added fast vibrato.",
        { { P::scale, (float) Scale::minorPent }, { P::key, (float) Key::E }, { P::retune, 12.0f }, { P::bounce, 0.35f },
          { P::keep, 0.5f }, { P::vbOn, on }, { P::vbDepth, 20.0f }, { P::vbRate, 6.5f }, { P::vbDelay, 150.0f } } });
    v.push_back ({ "Drill Tune", "Melodic Trap",
        "Stiff and robotic with a sub growl underneath.",
        { { P::scale, (float) Scale::harmonicMinor }, { P::key, (float) Key::Cs }, { P::retune, 0.0f }, { P::keep, 0.0f },
          { P::humanize, 0.0f }, { P::flex, 0.0f }, { P::grit, 0.18f } } });

    // ======================================================================= R&B
    v.push_back ({ "Silk", "R&B",
        "Smooth and expressive: slides and big vibrato survive, sustained notes settle gently.",
        { { P::retune, 30.0f }, { P::humanize, 0.45f }, { P::flex, 0.25f }, { P::keep, 1.2f }, { P::stability, 0.7f } } });
    v.push_back ({ "Runs", "R&B",
        "Built for fast runs: quick note changes are followed instead of smoothed over.",
        { { P::retune, 8.0f }, { P::stability, 0.15f }, { P::humanize, 0.1f }, { P::keep, 0.7f } } });
    v.push_back ({ "Neo Soul", "R&B",
        "Minor-pentatonic targets with a loose pull: blue notes are bent home, not flattened.",
        { { P::scale, (float) Scale::minorPent }, { P::key, (float) Key::D }, { P::retune, 40.0f }, { P::flex, 0.4f }, { P::keep, 1.0f },
          { P::centsF, 20.0f } } });

    // ======================================================================= Harmony
    v.push_back ({ "Thirds Above", "Harmony",
        "A third above, in key, panned slightly left.",
        { { P::h1On, on }, { P::h1Interval, (float) Ivl::thirdUp }, { P::h1Level, 0.55f }, { P::h1Pan, -0.3f } } });
    v.push_back ({ "Power Fifths", "Harmony",
        "A fifth above and an octave below: a huge, open stack.",
        { { P::h1On, on }, { P::h1Interval, (float) Ivl::fifthUp }, { P::h1Level, 0.5f }, { P::h1Pan, -0.4f },
          { P::h2On, on }, { P::h2Interval, (float) Ivl::octDown }, { P::h2Level, 0.45f }, { P::h2Pan, 0.3f } } });
    v.push_back ({ "Stack of Three", "Harmony",
        "Third and fifth above plus a soft octave on top: an instant triad.",
        { { P::h1On, on }, { P::h1Interval, (float) Ivl::thirdUp }, { P::h1Level, 0.5f }, { P::h1Pan, -0.55f },
          { P::h2On, on }, { P::h2Interval, (float) Ivl::fifthUp }, { P::h2Level, 0.45f }, { P::h2Pan, 0.55f },
          { P::h3On, on }, { P::h3Interval, (float) Ivl::octUp }, { P::h3Level, 0.25f }, { P::h3Pan, 0.0f }, { P::h3Formant, 1.5f } } });
    v.push_back ({ "Gospel Pad", "Harmony",
        "A loose, humanised choir around the lead, with a soft double.",
        { { P::h1On, on }, { P::h1Interval, (float) Ivl::thirdUp }, { P::h1Level, 0.45f }, { P::h1Pan, -0.6f },
          { P::h2On, on }, { P::h2Interval, (float) Ivl::fifthUp }, { P::h2Level, 0.4f }, { P::h2Pan, 0.6f },
          { P::h3On, on }, { P::h3Interval, (float) Ivl::sixthDown }, { P::h3Level, 0.35f }, { P::h3Pan, 0.0f },
          { P::harmHumanize, 0.6f }, { P::dbOn, on }, { P::dbLevel, 0.25f }, { P::retune, 30.0f }, { P::humanize, 0.4f } } });
    v.push_back ({ "Octave Choir", "Harmony",
        "An octave up (lighter throat) and down (deeper throat).",
        { { P::h1On, on }, { P::h1Interval, (float) Ivl::octUp }, { P::h1Level, 0.4f }, { P::h1Formant, 2.0f }, { P::h1Pan, -0.4f },
          { P::h2On, on }, { P::h2Interval, (float) Ivl::octDown }, { P::h2Level, 0.5f }, { P::h2Formant, -2.0f }, { P::h2Pan, 0.4f } } });
    v.push_back ({ "Low Shadow", "Harmony",
        "A deep voice an octave below, right behind the lead.",
        { { P::h1On, on }, { P::h1Interval, (float) Ivl::octDown }, { P::h1Level, 0.55f }, { P::h1Formant, -3.0f }, { P::h1Pan, 0.0f } } });
    v.push_back ({ "Sixths Below", "Harmony",
        "A sweet sixth below, the country and soul classic.",
        { { P::h1On, on }, { P::h1Interval, (float) Ivl::sixthDown }, { P::h1Level, 0.5f }, { P::h1Pan, 0.25f } } });

    // ======================================================================= Double
    v.push_back ({ "Wide Double", "Double",
        "Two extra takes spread wide: a thick, stereo lead from one vocal.",
        { { P::dbOn, on }, { P::dbLevel, 0.55f }, { P::dbSpread, 0.9f }, { P::dbDetune, 9.0f }, { P::dbDelay, 20.0f } } });
    v.push_back ({ "Tight ADT", "Double",
        "Close and subtle, like tape double tracking.",
        { { P::dbOn, on }, { P::dbLevel, 0.45f }, { P::dbSpread, 0.5f }, { P::dbDetune, 4.0f }, { P::dbDelay, 9.0f } } });
    v.push_back ({ "Stadium", "Double",
        "Big, loose and very wide.",
        { { P::dbOn, on }, { P::dbLevel, 0.5f }, { P::dbSpread, 1.0f }, { P::dbDetune, 14.0f }, { P::dbDelay, 30.0f }, { P::width, 1.4f } } });

    // ======================================================================= Character
    v.push_back ({ "Bigger Singer", "Character",
        "A longer throat: deeper, fuller and bigger, at the same pitch.",
        { { P::formant, -3.0f } } });
    v.push_back ({ "Younger", "Character",
        "A shorter throat: lighter and younger, at the same pitch.",
        { { P::formant, 3.0f } } });
    v.push_back ({ "Deep Voice", "Character",
        "Down a fifth with a matching throat. Still sounds like a person.",
        { { P::transpose, -7.0f }, { P::formant, -3.0f }, { P::retune, 15.0f } } });
    v.push_back ({ "Chipmunk", "Character",
        "Up an octave, tone and all.",
        { { P::transpose, 12.0f }, { P::character, classic }, { P::retune, 10.0f } } });
    v.push_back ({ "Monster", "Character",
        "Down an octave with a sub growl.",
        { { P::transpose, -12.0f }, { P::character, classic }, { P::grit, 0.35f }, { P::retune, 10.0f } } });
    v.push_back ({ "Growler", "Character",
        "A pitch-locked growl under a tight tune.",
        { { P::grit, 0.5f }, { P::gritMode, 1.0f }, { P::retune, 5.0f }, { P::keep, 0.3f } } });
    v.push_back ({ "Metal Voice", "Character",
        "Hard tune through a metallic ring: android vocals.",
        { { P::grit, 0.55f }, { P::gritMode, 3.0f }, { P::retune, 0.0f }, { P::keep, 0.0f }, { P::humanize, 0.0f } } });

    // ======================================================================= FX
    v.push_back ({ "Warble", "FX",
        "A deep, fast vibrato on every note.",
        { { P::vbOn, on }, { P::vbDepth, 60.0f }, { P::vbRate, 7.0f }, { P::vbDelay, 0.0f }, { P::vbOnset, 60.0f }, { P::keep, 0.0f },
          { P::retune, 5.0f } } });
    v.push_back ({ "Tape Wobble", "FX",
        "Slow, uneven pitch drift like a worn tape machine.",
        { { P::driftRate, 1.6f }, { P::driftSmooth, 0.9f }, { P::retune, 20.0f } },
        { { Src::drift, P::detune, 0.12f } } });
    v.push_back ({ "Robo Choir", "FX",
        "A hard-tuned lead with a hard-tuned choir.",
        { { P::retune, 0.0f }, { P::keep, 0.0f }, { P::humanize, 0.0f }, { P::flex, 0.0f },
          { P::h1On, on }, { P::h1Interval, (float) Ivl::thirdUp }, { P::h1Level, 0.5f }, { P::h1Pan, -0.6f },
          { P::h2On, on }, { P::h2Interval, (float) Ivl::fifthUp }, { P::h2Level, 0.45f }, { P::h2Pan, 0.6f },
          { P::h3On, on }, { P::h3Interval, (float) Ivl::octDown }, { P::h3Level, 0.4f }, { P::harmHumanize, 0.0f } } });
    v.push_back ({ "Octaver", "FX",
        "A locked octave below for weight.",
        { { P::h1On, on }, { P::h1Interval, (float) Ivl::octDown }, { P::h1Level, 0.7f }, { P::h1Pan, 0.0f }, { P::harmHumanize, 0.0f } } });
    v.push_back ({ "Throat Sweep", "FX",
        "The throat breathes slowly from long to short.",
        { { P::lfo1Shape, (float) Lfo::tri }, { P::lfo1Sync, on }, { P::lfo1Div, (float) Div::bars2 } },
        { { Src::lfo1, P::formant, 0.25f } } });

    // ======================================================================= Live
    v.push_back ({ "Live Stage", "Live",
        "Safe for performing: steady notes, quick response. Set LATENCY to LIVE on the GLOBAL page.",
        { { P::retune, 15.0f }, { P::humanize, 0.3f }, { P::flex, 0.1f }, { P::stability, 0.6f }, { P::sensitivity, 0.4f } } });
    v.push_back ({ "Live Robot", "Live",
        "Hard tune for the stage. Set LATENCY to LIVE on the GLOBAL page.",
        { { P::retune, 0.0f }, { P::keep, 0.0f }, { P::humanize, 0.0f }, { P::flex, 0.0f }, { P::sensitivity, 0.4f } } });

    // ======================================================================= MIDI
    v.push_back ({ "MIDI Melody", "MIDI",
        "Sing anything: it comes out on the notes you play on a MIDI keyboard.",
        { { P::midiMode, (float) Midi::exact }, { P::retune, 0.0f }, { P::keep, 0.0f }, { P::humanize, 0.0f } } });
    v.push_back ({ "MIDI Scale", "MIDI",
        "The notes you hold on a MIDI keyboard become the scale (no keys held = the Key and Scale).",
        { { P::midiMode, (float) Midi::scale }, { P::retune, 10.0f } } });
    v.push_back ({ "MIDI Chords", "MIDI",
        "Play chords: each held note (up to three) becomes a harmony voice around the lead.",
        { { P::midiMode, (float) Midi::harmony }, { P::harmHumanize, 0.4f }, { P::h1Level, 0.5f }, { P::h2Level, 0.5f },
          { P::h3Level, 0.45f }, { P::h1Pan, -0.6f }, { P::h2Pan, 0.6f } } });

    return v;
}
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = build();
    return presets;
}

} // namespace lodestone
