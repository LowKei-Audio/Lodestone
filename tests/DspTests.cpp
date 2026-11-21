// Offline tests for Lodestone's DSP core. Every claim the manual makes about
// the sound (accuracy, scales, speed, formants, harmonies, latency, safety)
// is measured here on synthetic voices.

#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cstdio>

using namespace testutil;
using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
TEST_CASE ("Scales: masks, nearest note, steps and harmony intervals")
{
    const Mask cMaj = scaleMask (0, Scale::major);
    REQUIRE (countNotes (cMaj) == 7);
    REQUIRE (has (cMaj, 0)); REQUIRE (has (cMaj, 4)); REQUIRE_FALSE (has (cMaj, 1));
    REQUIRE (countNotes (scaleMask (9, Scale::minorPent)) == 5);
    REQUIRE (scaleMask (0, Scale::custom, 0x0005) == 0x0005);

    REQUIRE (nearestAllowed (61.2f, cMaj) == 62);      // C#+20 -> D
    REQUIRE (nearestAllowed (60.4f, cMaj) == 60);
    REQUIRE (nearestAllowed (66.0f, scaleMask (0, Scale::chromatic)) == 66);

    // Sticky: 64.4 is nearer F (65) only by 0.2 st; with 0.3 hysteresis E holds.
    REQUIRE (stickyNearest (64.6f, cMaj, 64, 0.3f) == 64);
    REQUIRE (stickyNearest (64.9f, cMaj, 64, 0.3f) == 65);

    REQUIRE (scaleStep (64, 2, cMaj) == 67);           // E + 2 steps = G
    REQUIRE (scaleStep (60, -2, cMaj) == 57);          // C - 2 steps = A
    REQUIRE (harmonyNote (64, Ivl::thirdUp, cMaj) == 67);
    REQUIRE (harmonyNote (60, Ivl::fifthUp, cMaj) == 67);
    REQUIRE (harmonyNote (60, Ivl::fifthDown, cMaj) == 53);
    REQUIRE (harmonyNote (60, Ivl::sixthDown, cMaj) == 52);   // E below
    REQUIRE (harmonyNote (60, Ivl::octUp, cMaj) == 72);
    REQUIRE (harmonyNote (62, Ivl::p4st, cMaj) == 66);        // fixed semitones ignore the scale
}

TEST_CASE ("Key finder recognises major and minor keys")
{
    std::array<float, 12> c {};
    for (int s : { 0, 2, 4, 5, 7, 9, 11 }) c[(size_t) ((s + 7) % 12)] = 1.0f;   // G major notes
    c[7] = 3.0f; c[11] = 1.5f; c[2] = 2.0f;                                     // tonic triad emphasis
    auto g = findKey (c);
    REQUIRE (g.key == 7);
    REQUIRE_FALSE (g.minor);

    std::array<float, 12> m {};
    for (int s : { 0, 2, 3, 5, 7, 8, 10 }) m[(size_t) ((s + 9) % 12)] = 1.0f;   // A minor notes
    m[9] = 3.0f; m[0] = 2.0f; m[4] = 2.0f;
    g = findKey (m);
    REQUIRE (g.key == 9);
    REQUIRE (g.minor);
    REQUIRE (g.confidence > 0.3f);
}

// ---------------------------------------------------------------------------
TEST_CASE ("Detector: within 2 cents across the vocal range, no octave errors")
{
    for (double midi : { 43.0, 50.3, 57.0, 64.7, 69.0, 76.2, 81.0, 88.0 })
    {
        const auto x = steady (midi, 0.6);
        const double got = measurePitch (x, (size_t) (0.1 * kFs), x.size());
        INFO ("note " << midi << " measured " << got);
        REQUIRE (std::abs (got - midi) < 0.02);
    }
    // Source-filter voices (strong formant ringing tempts octave errors).
    for (double midi : { 40.0, 45.5, 52.0, 57.0, 63.3, 69.0, 76.0, 83.0 })
    {
        const auto x = pulseVoice (midi, 0.6);
        const double got = measurePitch (x, (size_t) (0.1 * kFs), x.size());
        INFO ("pulse voice " << midi << " measured " << got);
        REQUIRE (std::abs (got - midi) < 0.02);
    }
    // Other sample rates.
    for (double fs : { 44100.0, 96000.0 })
    {
        const auto x = voice ([] (double) { return 62.4; }, 0.6, fs);
        const double got = measurePitch (x, (size_t) (0.1 * fs), x.size(), fs);
        INFO ("fs " << fs << " measured " << got);
        REQUIRE (std::abs (got - 62.4) < 0.02);
    }
}

TEST_CASE ("Detector: silence and noise are unvoiced")
{
    PitchDetector d;
    d.prepare (kFs);
    d.setRange (1);
    Noise noise (3);
    int voiced = 0, readings = 0;
    for (int i = 0; i < (int) kFs; ++i)
    {
        const float x = i < (int) (kFs / 2) ? 0.0f : 0.3f * noise.next();
        if (d.push (x)) { ++readings; voiced += d.reading().voiced ? 1 : 0; }
    }
    REQUIRE (readings > 100);
    REQUIRE (voiced < readings / 20);
}

// ---------------------------------------------------------------------------
TEST_CASE ("Tuner: hard tune, retune speed, stability, flex, bounce")
{
    TunerParams p;
    p.retuneMs = 0.0f;
    const Mask cMaj = scaleMask (0, Scale::major);
    const float dt = 32.0f / 48000.0f;

    SECTION ("instant hard tune lands exactly on the note")
    {
        Tuner t; t.prepare (kFs);
        TunerState s;
        for (int i = 0; i < 50; ++i) s = t.advance (p, true, 60.37f, cMaj, -1, dt, 1);
        REQUIRE (s.targetNote == 60);
        REQUIRE_THAT (s.outputPitch, WithinAbs (60.0, 0.001));
    }
    SECTION ("retune speed: ~63% of the way after one retune time")
    {
        Tuner t; t.prepare (kFs);
        p.retuneMs = 50.0f;
        t.advance (p, true, 60.0f, cMaj, -1, dt, 1);          // starts in tune
        TunerState s;
        const int steps = (int) std::lround (0.05f / dt);
        for (int i = 0; i < steps; ++i) s = t.advance (p, true, 60.4f, cMaj, -1, dt, 1);
        const float moved = -s.shift / 0.4f;
        REQUIRE (moved > 0.55f);
        REQUIRE (moved < 0.72f);
    }
    SECTION ("note stability: wide vibrato never flips the target")
    {
        Tuner t; t.prepare (kFs);
        p.stability = 0.6f;
        p.retuneMs = 10.0f;
        int flips = 0, last = -1;
        for (int i = 0; i < 3000; ++i)
        {
            const float pitch = 64.0f + 0.6f * std::sin (kTwoPi * 5.5f * (float) i * dt);   // +/-60 cents
            const auto& s = t.advance (p, true, pitch, cMaj, -1, dt, 1);
            if (last >= 0 && s.targetNote != last) ++flips;
            last = s.targetNote;
        }
        REQUIRE (flips == 0);
        REQUIRE (last == 64);
    }
    SECTION ("flex leaves far-off pitches alone, corrects close ones")
    {
        Tuner t; t.prepare (kFs);
        p.flex = 0.8f;
        TunerState s;
        for (int i = 0; i < 200; ++i) s = t.advance (p, true, 61.0f, cMaj, -1, dt, 1);   // a semitone between C and D
        REQUIRE (std::abs (s.shift) < 0.05f);
        Tuner t2; t2.prepare (kFs);
        for (int i = 0; i < 200; ++i) s = t2.advance (p, true, 62.08f, cMaj, -1, dt, 1);  // 8 cents sharp D
        REQUIRE (std::abs (s.outputPitch - 62.0f) < 0.04f);
    }
    SECTION ("bounce overshoots, then settles")
    {
        Tuner t; t.prepare (kFs);
        p.retuneMs = 30.0f;
        p.bounce = 0.8f;
        t.advance (p, true, 60.0f, cMaj, -1, dt, 1);
        float minShift = 0.0f;
        TunerState s;
        for (int i = 0; i < 1500; ++i) { s = t.advance (p, true, 60.45f, cMaj, -1, dt, 1); minShift = std::min (minShift, s.shift); }
        REQUIRE (minShift < -0.5f);                       // went past the target (-0.45)
        REQUIRE_THAT (s.outputPitch, WithinAbs (60.0, 0.01));
    }
    SECTION ("vibrato keep: 0 flattens, 1 passes the singer's vibrato")
    {
        for (float keep : { 0.0f, 1.0f })
        {
            Tuner t; t.prepare (kFs);
            p.keep = keep;
            p.stability = 0.8f;
            float lo = 1e9f, hi = -1e9f;
            for (int i = 0; i < 4000; ++i)
            {
                const float pitch = 64.1f + 0.3f * std::sin (kTwoPi * 5.0f * (float) i * dt);
                const auto& s = t.advance (p, true, pitch, cMaj, -1, dt, 1);
                if (i > 2000) { lo = std::min (lo, s.outputPitch); hi = std::max (hi, s.outputPitch); }
            }
            INFO ("keep " << keep << " swing " << hi - lo);
            if (keep == 0.0f) REQUIRE (hi - lo < 0.02f);
            else              REQUIRE (std::abs ((hi - lo) - 0.6f) < 0.12f);
        }
    }
    SECTION ("hold pins the note; exact MIDI forces it")
    {
        Tuner t; t.prepare (kFs);
        TunerState s;
        for (int i = 0; i < 20; ++i) s = t.advance (p, true, 64.1f, cMaj, -1, dt, 1);
        p.hold = true;
        for (int i = 0; i < 100; ++i) s = t.advance (p, true, 67.2f, cMaj, -1, dt, 1);
        REQUIRE (s.targetNote == 64);
        p.hold = false;
        for (int i = 0; i < 100; ++i) s = t.advance (p, true, 67.2f, cMaj, 59, dt, 1);
        REQUIRE (s.targetNote == 59);
        REQUIRE_THAT (s.outputPitch, WithinAbs (59.0, 0.01));
    }
}

// ---------------------------------------------------------------------------
namespace
{
EngineParams hardTune (int scale = Scale::chromatic)
{
    EngineParams p;
    p.scale = scale;
    p.key = 0;
    p.range = 3;
    p.tuner.retuneMs = 0.0f;
    p.tuner.stability = 0.3f;
    return p;
}

VoiceEngine& makeEngine (VoiceEngine& e, const EngineParams& p, bool live = false, double fs = kFs)
{
    e.setLowLatency (live);
    e.prepare (fs);
    e.setParams (p);
    return e;
}
} // namespace

TEST_CASE ("Engine: corrects an off-key voice onto the note (Studio and Live)")
{
    for (bool live : { false, true })
        for (double sung : { 57.3, 64.62, 71.8 })
        {
            VoiceEngine e;
            makeEngine (e, hardTune(), live);
            const auto in = steady (sung, 1.2);
            const auto out = run (e, in);
            REQUIRE (allFinite (out.l));
            const double got = measurePitch (out.l, (size_t) (0.4 * kFs), out.l.size());
            const double want = std::round (sung);
            INFO ("live " << live << " sung " << sung << " got " << got);
            REQUIRE (std::abs (got - want) < 0.03);
            // Level stays close to the input's.
            const float ratio = rms (out.l, (size_t) (0.4 * kFs)) / rms (in, (size_t) (0.4 * kFs));
            REQUIRE (ratio > 0.6f);
            REQUIRE (ratio < 1.5f);
        }
}

TEST_CASE ("Engine: in tune and unprocessed it is (almost) the delayed input")
{
    VoiceEngine e;
    auto p = hardTune();
    makeEngine (e, p);
    const auto in = steady (69.0, 1.0);
    const auto out = run (e, in);
    const double c = correlation (in, out.l, e.getLatency(), (size_t) (0.3 * kFs), in.size());
    INFO ("correlation at latency " << e.getLatency() << ": " << c);
    REQUIRE (c > 0.9);
    REQUIRE (correlation (in, out.dry, e.getLatency(), (size_t) (0.3 * kFs), in.size()) > 0.999);

    // A hair off pitch (1 cent): the grains go back exactly where they were cut.
    VoiceEngine f;
    makeEngine (f, p);
    const auto in2 = pulseVoice (69.01, 1.0);
    const auto out2 = run (f, in2);
    const double c2 = correlation (in2, out2.l, f.getLatency(), (size_t) (0.3 * kFs), in2.size());
    INFO ("1 cent off: correlation " << c2);
    REQUIRE (c2 > 0.9);
}

TEST_CASE ("Engine: transpose keeps the formants (Modern), moves them (Classic)")
{
    // Source-filter voices from C3 to A4, shifted a fifth up and down.
    for (double note : { 48.0, 55.0, 62.0, 69.0 })
        for (float transpose : { -7.0f, 7.0f })
            for (bool modern : { true, false })
            {
                VoiceEngine e;
                auto p = hardTune();
                p.formantPreserve = modern;
                p.tuner.transpose = transpose;
                makeEngine (e, p);
                const auto in = pulseVoice (note, 1.0);
                const auto out = run (e, in);
                const double got = measurePitch (out.l, (size_t) (0.4 * kFs), out.l.size());
                const double err = envelopeError (in, midiToHz (note), out.l, midiToHz (note + transpose));
                INFO ("note " << note << " transpose " << transpose << (modern ? " modern" : " classic") << ": pitch " << got
                      << ", envelope error " << err << " dB");
                REQUIRE (std::abs (got - (note + transpose)) < 0.05);
                if (modern) REQUIRE (err < 3.0);    // formants stay where they were
                else        REQUIRE (err > 6.0);    // formants slide with the pitch
                const float ratio = rms (out.l, (size_t) (0.5 * kFs)) / rms (in, (size_t) (0.5 * kFs));
                if (modern) { REQUIRE (ratio > 0.6f); REQUIRE (ratio < 1.6f); }
            }
}

TEST_CASE ("Engine: throat moves the formants by the amount asked, pitch unchanged")
{
    for (double note : { 48.0, 60.0 })
        for (float throat : { -6.0f, 6.0f })
        {
            VoiceEngine e;
            auto p = hardTune();
            p.formant = throat;
            makeEngine (e, p);
            const auto in = pulseVoice (note, 1.0);
            const auto out = run (e, in);
            const double scale = std::pow (2.0, throat / 12.0);
            const double moved = envelopeError (in, midiToHz (note), out.l, midiToHz (note), scale);
            const double kept = envelopeError (in, midiToHz (note), out.l, midiToHz (note), 1.0);
            INFO ("note " << note << " throat " << throat << ": error vs moved envelope " << moved << ", vs original " << kept);
            REQUIRE (moved < 2.0);
            REQUIRE (kept > moved + 2.0);
            REQUIRE (std::abs (measurePitch (out.l, (size_t) (0.4 * kFs), out.l.size()) - note) < 0.05);
        }
}

TEST_CASE ("Engine: scale-aware harmonies and the doubler")
{
    VoiceEngine e;
    auto p = hardTune (Scale::major);
    p.tuner.retuneMs = 5.0f;
    p.leadLevel = 0.0f;                      // listen to the harmony alone
    p.harmony[0].on = true;
    p.harmony[0].interval = Ivl::thirdUp;
    p.harmony[0].level = 1.0f;
    p.harmonyHumanize = 0.0f;
    makeEngine (e, p);
    const auto in = steady (64.1, 1.2);      // E4, a little sharp
    const auto out = run (e, in);
    const double got = measurePitch (out.l, (size_t) (0.5 * kFs), out.l.size());
    INFO ("3rd up from E in C major: " << got);
    REQUIRE (std::abs (got - 67.0) < 0.05);  // G4

    VoiceEngine d;
    auto q = hardTune();
    q.leadLevel = 0.0f;
    q.doubleOn = true;
    q.doubleLevel = 1.0f;
    q.doubleSpread = 1.0f;
    makeEngine (d, q);
    const auto dout = run (d, in);
    const float l = rms (dout.l, (size_t) (0.5 * kFs)), r = rms (dout.r, (size_t) (0.5 * kFs));
    REQUIRE (l > 0.03f);
    REQUIRE (r > 0.03f);
    // The two takes differ (wide, not mono).
    REQUIRE (correlation (dout.l, dout.r, 0, (size_t) (0.5 * kFs), dout.l.size()) < 0.9);
}

TEST_CASE ("Engine: MIDI exact and MIDI harmony modes")
{
    VoiceEngine e;
    auto p = hardTune();
    p.midiMode = MidiMode::exact;
    makeEngine (e, p);
    const int notes[] = { 55 };
    e.setHeldNotes (notes, 1, 55);
    const auto out = run (e, steady (60.2, 1.0));
    REQUIRE (std::abs (measurePitch (out.l, (size_t) (0.4 * kFs), out.l.size()) - 55.0) < 0.05);

    VoiceEngine h;
    auto q = hardTune();
    q.midiMode = MidiMode::harmony;
    q.leadLevel = 0.0f;
    q.harmonyHumanize = 0.0f;
    makeEngine (h, q);
    const int chord[] = { 64 };
    h.setHeldNotes (chord, 1, 64);
    const auto hout = run (h, steady (60.0, 1.2));
    REQUIRE (std::abs (measurePitch (hout.l, (size_t) (0.5 * kFs), hout.l.size()) - 64.0) < 0.05);
}

TEST_CASE ("Engine: a glide is followed note by note (hard tune steps)")
{
    VoiceEngine e;
    makeEngine (e, hardTune (Scale::major));
    // Slide from C4 up to G4 over a second: output should sit on scale notes.
    const auto in = voice ([] (double t) { return 60.0 + 7.0 * std::clamp ((t - 0.3) / 1.0, 0.0, 1.0); }, 1.6);
    const auto out = run (e, in);
    PitchDetector d; d.prepare (kFs); d.setRange (3);
    int onNote = 0, total = 0;
    for (size_t i = (size_t) (0.35 * kFs); i < out.l.size(); ++i)
        if (d.push (out.l[i]) && d.reading().voiced)
        {
            const double m = hzToMidi (d.reading().hz);
            ++total;
            const int n = (int) std::lround (m);
            if (std::abs (m - n) < 0.15 && has (scaleMask (0, Scale::major), n)) ++onNote;
        }
    INFO (onNote << " of " << total << " readings on a scale note");
    REQUIRE (total > 50);
    REQUIRE (onNote > total * 8 / 10);
}

TEST_CASE ("Engine: safe on silence, noise, clipping input and every sample rate")
{
    for (double fs : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        VoiceEngine e;
        auto p = hardTune (Scale::minor);
        p.harmony[0].on = p.harmony[1].on = p.harmony[2].on = true;
        p.harmony[1].interval = Ivl::octDown;
        p.harmony[2].interval = Ivl::octFifth;
        p.doubleOn = true;
        p.grit = 0.7f;
        p.tuner.vibOn = true;
        p.tuner.bounce = 1.0f;
        p.tuner.retuneMs = 40.0f;
        makeEngine (e, p, false, fs);

        std::vector<float> in ((size_t) (fs * 2.0));
        Noise n (9);
        for (size_t i = 0; i < in.size(); ++i)
        {
            const double t = (double) i / fs;
            if (t < 0.4) in[i] = 0.0f;
            else if (t < 0.8) in[i] = 0.5f * n.next();
            else if (t < 1.4) in[i] = 4.0f * (float) std::sin (2 * kPiD * 220 * t);   // +12 dBFS
            else in[i] = (float) std::sin (2 * kPiD * (100 + 2000 * (t - 1.4)) * t);   // wild sweep
        }
        const auto out = run (e, in, 512);
        INFO ("fs " << fs);
        REQUIRE (allFinite (out.l));
        REQUIRE (allFinite (out.r));
        REQUIRE (peak (out.l) < 30.0f);
        // Silence in -> silence out (no idle noise).
        REQUIRE (peak (out.l, 0, (size_t) (0.35 * fs)) < 1.0e-6f);
    }
}

TEST_CASE ("Engine: CPU cost with every voice on")
{
    VoiceEngine e;
    auto p = hardTune (Scale::major);
    p.harmony[0].on = p.harmony[1].on = p.harmony[2].on = true;
    p.doubleOn = true;
    p.tuner.vibOn = true;
    p.grit = 0.3f;
    makeEngine (e, p);
    const auto in = voice ([] (double t) { return 57.0 + 5.0 * std::sin (t * 1.3) + 0.3 * std::sin (t * 34.0); }, 10.0);
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = run (e, in, 256);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const double pct = 100.0 * secs / 10.0;
    std::printf ("  CPU: 6 voices + detector at 48 kHz = %.2f%% of one core\n", pct);
    REQUIRE (allFinite (out.l));
    REQUIRE (pct < 15.0);
}

// ---------------------------------------------------------------------------
// Static report (Serato, Nov 2026, the whole LowKei family). These fail on
// Lodestone 1.0.0 and pass from 1.0.1.

namespace
{
// Sharpest moment (largest second difference, either channel) in [from, to).
float sharpness (const StereoOut& o, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t> (from, 2); i < std::min (to, o.l.size()); ++i)
        m = std::max ({ m, std::abs (o.l[i] - 2 * o.l[i - 1] + o.l[i - 2]), std::abs (o.r[i] - 2 * o.r[i - 1] + o.r[i - 2]) });
    return m;
}

// Runs a held note, applies `change` at 0.5 s, and returns how much sharper the
// change is than the voice's own sharpest moment before and after it.
float clickRatio (EngineParams p, const std::function<void (EngineParams&)>& change)
{
    VoiceEngine e;
    e.prepare (kFs);
    e.setParams (p);
    const auto in = steady (64.3, 1.2);
    StereoOut o;
    o.l.resize (in.size()); o.r.resize (in.size()); o.dry.resize (in.size());
    const size_t at = (size_t) (0.5 * kFs) / VoiceEngine::kBlock * VoiceEngine::kBlock;
    for (size_t pos = 0; pos < in.size(); pos += VoiceEngine::kBlock)
    {
        if (pos == at) { change (p); e.setParams (p); }
        e.process (&in[pos], &o.l[pos], &o.r[pos], &o.dry[pos], VoiceEngine::kBlock);
    }
    const size_t win = 4096;
    const float ref = std::max (sharpness (o, at - (size_t) (0.3 * kFs), at - 512), sharpness (o, at + win, at + (size_t) (0.6 * kFs))) + 1.0e-6f;
    return sharpness (o, at - 16, at + win) / ref;
}
} // namespace

TEST_CASE ("Static: Lead, Breath, Grit and Throat steps glide instead of clicking")
{
    EngineParams p;
    CHECK (clickRatio (p, [] (EngineParams& q) { q.leadLevel = 0.0f; }) < 1.25f);
    p.leadLevel = 0.0f;
    CHECK (clickRatio (p, [] (EngineParams& q) { q.leadLevel = 1.0f; }) < 1.25f);
    p = {};
    CHECK (clickRatio (p, [] (EngineParams& q) { q.grit = 1.0f; }) < 1.25f);
    CHECK (clickRatio (p, [] (EngineParams& q) { q.formant = -12.0f; }) < 1.25f);
    p.tuner.transpose = -12.0f;
    CHECK (clickRatio (p, [] (EngineParams& q) { q.formantPreserve = false; }) < 1.25f);   // Character to Classic
    p = {};
    p.harmony[0].on = true;
    CHECK (clickRatio (p, [] (EngineParams& q) { q.harmony[0].formant = 12.0f; }) < 1.25f);
}

TEST_CASE ("Static: a synced LFO eases across a playhead jump instead of snapping")
{
    LfoParams lp;
    lp.shape = LfoShape::triangle;
    lp.sync = true;
    lp.division = Div::bars2;
    Transport t;
    t.sampleRate = kFs;
    t.bpm = 120.0;
    t.playing = true;

    auto runTo = [&] (ModLfo& lfo, double beats)
    {
        float v = 0.0f;
        while (t.ppq < beats) { v = lfo.advance (lp, t, 32, 1); t.ppq += t.beatsPerSample() * 32; }
        return v;
    };

    ModLfo lfo;
    t.ppq = 0.0;
    const float before = runTo (lfo, 4.0);   // one bar in: the 2-bar triangle is at its top
    REQUIRE (before > 0.95f);

    // A 1-bar loop wraps back to beat 0, where the triangle is at its bottom.
    t.ppq = 0.0;
    lfo.easeNextJump();
    const float first = lfo.advance (lp, t, 32, 1);
    CHECK (std::abs (first - before) < 0.1f);              // no jump: it starts where it was
    t.ppq += t.beatsPerSample() * 32;
    float v = first;
    for (int b = 0; b < (int) (0.1 * kFs / 32); ++b) { v = lfo.advance (lp, t, 32, 1); t.ppq += t.beatsPerSample() * 32; }
    ModLfo plain;
    const float raw = plain.advance (lp, t, 32, 1);
    CHECK (std::abs (v - raw) < 0.01f);                     // and is back on the grid within 100 ms

    // Without a jump nothing changes: playback still equals a bounce.
    ModLfo a, b;
    t.ppq = 1.0;
    for (int k = 0; k < 100; ++k) { REQUIRE (a.advance (lp, t, 32, 1) == b.advance (lp, t, 32, 1)); t.ppq += t.beatsPerSample() * 32; }
}

TEST_CASE ("Static: a sound change fades to dry, holds, then fades back smoothly")
{
    OutputStage out;
    out.prepare (kFs);
    OutputParams op;
    out.setParams (op);
    constexpr int n = 32;
    float dry[n], wl[n], wr[n];
    auto block = [&] (float d, float w)
    {
        std::fill (dry, dry + n, d);
        std::fill (wl, wl + n, w);
        std::fill (wr, wr + n, w);
        out.process (dry, dry, wl, wr, n);
    };

    block (0.1f, 0.5f);
    REQUIRE_THAT (wl[n - 1], WithinAbs (0.5f, 1e-4));
    out.setTransition (true);
    float last = wl[n - 1], maxStep = 0.0f;
    int samples = 0;
    while (! out.readyForSwap() && samples < (int) kFs)
    {
        block (0.1f, 0.5f);
        for (int i = 0; i < n; ++i) { maxStep = std::max (maxStep, std::abs (wl[i] - last)); last = wl[i]; }
        samples += n;
    }
    CHECK_THAT (wl[n - 1], WithinAbs (0.1f, 1e-4));                     // fully dry
    CHECK (samples <= (int) (0.032 * kFs));                              // 20 ms fade + 10 ms hold
    CHECK (samples >= (int) (0.029 * kFs));
    CHECK (maxStep < 0.4f * 2.0f / (0.02f * (float) kFs) * 1.6f);       // an S-curve, no step

    out.setTransition (false);
    for (int k = 0; k < (int) (0.025 * kFs) / n; ++k) block (0.1f, 0.5f);
    CHECK_THAT (wl[n - 1], WithinAbs (0.5f, 1e-4));                      // the new sound fully in
    CHECK (out.getTransition() == 0.0f);
}

// ===========================================================================
// Lodestone 2.0
// ===========================================================================

// A beat for the chord analyzer: pad chord (7 harmonics per note, middle C
// octave), a bass note two octaves down and, optionally, kick and hats.
static std::vector<float> beat (const std::vector<std::pair<int, int>>& chords, double secondsEach, double fs = kFs,
                                bool drums = true)
{
    const size_t n = (size_t) ((double) chords.size() * secondsEach * fs);
    std::vector<float> x (n);
    std::uint32_t noise = 12345u;
    double ph[5] {};
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / fs;
        const auto [root, shape] = chords[(size_t) (t / secondsEach)];
        const auto& sh = kChordShapes[shape];
        double s = 0.0;
        for (int k = 0; k < sh.count; ++k)
        {
            ph[k] += midiToHz (60 + (root + sh.tones[(size_t) k]) % 12) / fs;
            ph[k] -= std::floor (ph[k]);
            for (int h = 1; h < 8; ++h) s += 0.15 / h * std::sin (2.0 * kPiD * h * ph[k]);
        }
        ph[4] += midiToHz (36 + root) / fs;
        ph[4] -= std::floor (ph[4]);
        s += 0.5 * std::sin (2.0 * kPiD * ph[4]) + 0.15 * std::sin (4.0 * kPiD * ph[4]);
        if (drums)
        {
            const double bt = std::fmod (t, 0.5), ht = std::fmod (t, 0.25);
            noise = noise * 1664525u + 1013904223u;
            s += 0.8 * std::exp (-bt * 30.0) * std::sin (2.0 * kPiD * 55.0 * bt * (1.0 + 2.0 * std::exp (-bt * 40.0)));
            s += 0.15 * std::exp (-ht * 80.0) * ((double) (noise >> 8) / 8388608.0 - 1.0);
        }
        x[i] = (float) (0.25 * s);
    }
    return x;
}

static std::vector<float> drumsOnly (double seconds, double fs = kFs)
{
    std::vector<float> x ((size_t) (seconds * fs));
    std::uint32_t noise = 777u;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / fs, bt = std::fmod (t, 0.5), ht = std::fmod (t, 0.25);
        noise = noise * 1664525u + 1013904223u;
        const double r = (double) (noise >> 8) / 8388608.0 - 1.0;
        x[i] = (float) (0.2 * (0.8 * std::exp (-bt * 30.0) * std::sin (2.0 * kPiD * 55.0 * bt * (1.0 + 2.0 * std::exp (-bt * 40.0)))
                               + 0.15 * std::exp (-ht * 80.0) * r + 0.02 * r));
    }
    return x;
}

TEST_CASE ("v2 Tuner: legato leaps land fast and never visit a wrong note")
{
    // 1.x held the voice on the old note through the whole Stability wait,
    // then the slow vibrato-free centre swept across the notes in between and
    // could pick a wrong one for ~20 ms (e.g. 64 on a 60 -> 65 leap).
    for (const bool live : { false, true })
        for (const double leap : { 2.0, 5.0, 7.0, -5.0, 12.0 })
        {
            VoiceEngine e;
            auto p = hardTune();
            p.tuner.flex = 0.15f;
            e.setLowLatency (live);
            makeEngine (e, p, live);
            const auto in = voice ([leap] (double t) { return (t < 0.6 ? 60.0 : 60.0 + leap) + 0.4; }, 1.0);
            std::vector<float> l (32), r (32), d (32);
            const int lat = e.getLatency(), newNote = (int) std::lround (60.0 + leap);
            double landed = -1.0;
            int wrong = 0;
            for (size_t pos = 0; pos + 32 <= in.size(); pos += 32)
            {
                e.process (&in[pos], l.data(), r.data(), d.data(), 32);
                const double t = (double) ((std::int64_t) pos - lat) / kFs - 0.6;
                const auto& v = e.getView();
                if (t < 0.0 || t > 0.3 || ! v.voiced) continue;
                if (v.targetNote != 60 && v.targetNote != newNote) ++wrong;
                if (landed < 0.0 && v.targetNote == newNote && std::abs (v.outputPitch - (float) newNote) < 0.1f) landed = t;
            }
            INFO ((live ? "Live" : "Studio") << " leap " << leap << ": on the new note after " << landed * 1000.0 << " ms");
            REQUIRE (wrong == 0);
            REQUIRE (landed >= 0.0);
            // Instrument range (the widest analysis window). 1.x: 35-122 ms with
            // wrong notes on every leap, and octave leaps never landed.
            REQUIRE (landed < (live ? 0.045 : 0.032));
        }
}

TEST_CASE ("v2 Chord analyzer: names chords at every sample rate, ignores drums")
{
    const std::vector<std::pair<int, int>> prog = { { 9, 1 }, { 5, 0 }, { 0, 0 }, { 7, 0 }, { 2, 3 }, { 0, 4 }, { 11, 5 }, { 2, 6 }, { 4, 0 } };
    for (const double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const auto x = beat (prog, 2.0, fs);
        ChordAnalyzer a;
        a.prepare (fs);
        int right = 0;
        double slowest = 0.0;
        for (size_t c = 0; c < prog.size(); ++c)
        {
            double named = -1.0;
            const size_t from = (size_t) ((double) c * 2.0 * fs), to = (size_t) ((double) (c + 1) * 2.0 * fs);
            for (size_t i = from; i < to; ++i)
            {
                a.push (x[i]);
                if (named < 0.0 && a.chord().root == prog[c].first && a.chord().shape == prog[c].second) named = (double) (i - from) / fs;
            }
            if (a.chord().root == prog[c].first && a.chord().shape == prog[c].second) ++right;
            if (c > 0) slowest = std::max (slowest, named < 0.0 ? 9.0 : named);
        }
        INFO ("fs " << fs << ": " << right << "/" << prog.size() << " chords, slowest " << slowest * 1000.0 << " ms");
        REQUIRE (right == (int) prog.size());
        REQUIRE (slowest < 0.35);
    }

    SECTION ("drums and noise alone never name a chord")
    {
        ChordAnalyzer a;
        a.prepare (kFs);
        for (float v : drumsOnly (4.0)) a.push (v);
        REQUIRE_FALSE (a.chord().valid());
        REQUIRE (a.hearing());
    }
    SECTION ("the chord is kept through drum-only bars and silence")
    {
        ChordAnalyzer a;
        a.prepare (kFs);
        for (float v : beat ({ { 9, 1 } }, 2.0)) a.push (v);
        for (float v : drumsOnly (3.0)) a.push (v);
        REQUIRE (a.chord().root == 9);
        REQUIRE (a.chord().shape == 1);
        for (int i = 0; i < (int) (2.0 * kFs); ++i) a.push (0.0f);
        REQUIRE (a.chord().root == 9);
        REQUIRE_FALSE (a.hearing());
    }
    SECTION ("key evidence for LEARN: A minor from an Am - Dm - E - Am progression")
    {
        ChordAnalyzer a;
        a.prepare (kFs);
        for (float v : beat ({ { 9, 1 }, { 2, 1 }, { 4, 0 }, { 9, 1 } }, 1.5)) a.push (v);
        const auto k = findKey (a.getKeyChroma());
        REQUIRE (k.key == 9);
        REQUIRE (k.minor);
    }
    SECTION ("CPU: well under 1% of one core")
    {
        const auto x = beat (prog, 2.0);
        ChordAnalyzer a;
        a.prepare (kFs);
        const auto t0 = std::chrono::steady_clock::now();
        for (float v : x) a.push (v);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double pct = 100.0 * secs / ((double) x.size() / kFs);
        std::printf ("  CPU: chord analyzer at 48 kHz = %.2f%% of one core\n", pct);
        REQUIRE (pct < 1.0);
    }
}

TEST_CASE ("v2 Chord Lock: which note a voice lands on")
{
    const Mask cMaj = scaleMask (0, Scale::major), cChord = chordMask (0, 0), d7 = chordMask (2, 2);
    SECTION ("0% is exactly the scale")
    {
        const auto p = chordPull (cMaj, cChord, 0.0f);
        for (float x = 55.0f; x < 72.0f; x += 0.07f) REQUIRE (nearestPreferred (x, p) == nearestAllowed (x, cMaj));
    }
    SECTION ("100%: chord tones only, the nearest one")
    {
        const auto p = chordPull (cMaj, cChord, 1.0f);
        REQUIRE (nearestPreferred (65.2f, p) == 64);   // F -> E
        REQUIRE (nearestPreferred (62.2f, p) == 64);   // D (a little sharp) -> E
        REQUIRE (nearestPreferred (69.0f, p) == 67);   // A -> G
    }
    SECTION ("50%: a note a semitone off a chord tone moves to it; others stay")
    {
        const auto p = chordPull (cMaj, cChord, 0.55f);
        REQUIRE (nearestPreferred (65.0f, p) == 64);   // F, a semitone above E
        REQUIRE (nearestPreferred (62.0f, p) == 62);   // D, two semitones from C and E: stays
        REQUIRE (nearestPreferred (69.0f, p) == 69);   // A stays
    }
    SECTION ("chord tones outside the scale become reachable (secondary dominant)")
    {
        const auto p = chordPull (cMaj, d7, 0.6f);   // D7 has F#
        REQUIRE (nearestPreferred (66.1f, p) == 66);
        REQUIRE (nearestPreferred (66.1f, chordPull (cMaj, d7, 0.0f)) != 66);
    }
    SECTION ("chord harmonies: the next chord tones")
    {
        const Mask f = chordMask (5, 0);                       // F A C
        REQUIRE (chordHarmonyNote (60, Ivl::thirdUp, f) == 65);   // C -> F
        REQUIRE (chordHarmonyNote (60, Ivl::fifthUp, f) == 69);   // C -> A
        REQUIRE (chordHarmonyNote (60, Ivl::thirdDown, f) == 57); // C -> A below
        REQUIRE (chordHarmonyNote (60, Ivl::octUp, f) == 72);     // fixed intervals stay fixed
        REQUIRE (chordHarmonyNote (60, Ivl::octThird, f) == 77);  // F an octave up
    }
}

TEST_CASE ("v2 Engine: harmonies stack on the chord's tones (Chord Lock 50%+)")
{
    auto p = hardTune (Scale::major);
    p.tuner.retuneMs = 5.0f;
    p.leadLevel = 0.0f;
    p.harmony[0].on = true;
    p.harmony[0].interval = Ivl::thirdUp;
    p.harmony[0].level = 1.0f;
    p.harmonyHumanize = 0.0f;
    const auto in = steady (60.1, 1.2);   // C4

    VoiceEngine scale;
    makeEngine (scale, p);
    scale.setChord (chordMask (5, 0));      // F major heard, but Chord Lock 0
    const auto a = run (scale, in);
    REQUIRE (std::abs (measurePitch (a.l, (size_t) (0.5 * kFs), a.l.size()) - 64.0) < 0.05);   // E: the scale's 3rd

    p.chordLock = 0.6f;
    VoiceEngine chord;
    makeEngine (chord, p);
    chord.setChord (chordMask (5, 0));
    const auto b = run (chord, in);
    REQUIRE (std::abs (measurePitch (b.l, (size_t) (0.5 * kFs), b.l.size()) - 65.0) < 0.05);   // F: the chord's next tone
}

TEST_CASE ("v2 Engine: Motion steps through the scale, restarts with each phrase")
{
    auto p = hardTune (Scale::major);
    p.motionOn = true;
    p.motionStart = MotionStart::phrase;
    p.motionDivision = Div::d4;           // quarter notes at 120 bpm: 0.5 s per step
    p.motionLength = 2;
    p.motionSteps[0] = 0;
    p.motionSteps[1] = 4;                 // C -> G (four notes up C major)
    p.motionGlideMs = 0.0f;

    // Two phrases with a pause: each starts on step 1 (the sung note).
    auto in = voice ([] (double) { return 60.1; }, 2.6);
    for (size_t i = (size_t) (1.0 * kFs); i < (size_t) (1.6 * kFs); ++i) in[i] = 0.0f;
    VoiceEngine e;
    makeEngine (e, p);
    e.setTransport (0.0, 120.0);
    std::vector<float> l (32), r (32), d (32), out;
    for (size_t pos = 0; pos + 32 <= in.size(); pos += 32)
    {
        e.setTransport (0.0, 120.0);   // a stopped host: Phrase mode does not need the playhead
        e.process (&in[pos], l.data(), r.data(), d.data(), 32);
        out.insert (out.end(), l.begin(), l.end());
    }
    auto at = [&] (double a, double b) { return measurePitch (out, (size_t) (a * kFs), (size_t) (b * kFs)); };
    const double lat = e.getLatency() / kFs;
    INFO ("phrase 1: " << at (0.15 + lat, 0.45 + lat) << " then " << at (0.62 + lat, 0.95 + lat)
          << "; phrase 2: " << at (1.68 + lat, 2.05 + lat) << " then " << at (2.15 + lat, 2.5 + lat));
    REQUIRE (std::abs (at (0.15 + lat, 0.45 + lat) - 60.0) < 0.06);
    REQUIRE (std::abs (at (0.62 + lat, 0.95 + lat) - 67.0) < 0.06);
    REQUIRE (std::abs (at (1.68 + lat, 2.05 + lat) - 60.0) < 0.06);   // restarted on step 1
    REQUIRE (std::abs (at (2.15 + lat, 2.50 + lat) - 67.0) < 0.06);
}

TEST_CASE ("v2 Engine: Motion off and Chord Lock 0 leave the output untouched by v2")
{
    // The same engine settings with and without a chord heard and a Motion
    // pattern loaded (but off) render sample for sample the same.
    auto p = hardTune (Scale::major);
    p.tuner.retuneMs = 20.0f;
    p.harmony[0].on = true;
    const auto in = voice ([] (double t) { return 62.3 + 0.3 * std::sin (2.0 * kPiD * 5.0 * t) + (t > 0.7 ? 3.0 : 0.0); }, 1.5);
    VoiceEngine a, b;
    makeEngine (a, p);
    auto q = p;
    q.motionSteps = { 3, -2, 5, 1 };
    q.motionOn = false;
    q.chordLock = 0.0f;
    makeEngine (b, q);
    b.setChord (chordMask (9, 1));
    const auto oa = run (a, in), ob = run (b, in);
    REQUIRE (oa.l == ob.l);
    REQUIRE (oa.r == ob.r);
}

TEST_CASE ("v2 Detector: octave leaps are followed in every input range; weak fundamentals hold")
{
    // 1.x kept following the old octave after a legato octave leap (up, and
    // in the Bass range down too) for as long as the note lasted.
    struct R { int range; double lo, hi; };
    const R ranges[] = { { 0, 55, 78 }, { 1, 45, 74 }, { 2, 38, 62 }, { 3, 33, 80 }, { 4, 26, 52 } };
    auto wrongAfterLeap = [] (int range, double from, double to, bool vib)
    {
        const auto in = voice ([&] (double t) { return (t < 0.5 ? from : to) + (vib ? 0.3 * std::sin (2.0 * kPiD * 5.5 * t) : 0.0); }, 1.0);
        PitchDetector d;
        d.prepare (kFs);
        d.setRange (range);
        int wrong = 0;
        for (float x : in)
            if (d.push (x) && d.reading().voiced && (double) d.reading().centre / kFs > 0.62
                && std::abs (hzToMidi (d.reading().hz) - to) > 2.0) ++wrong;
        return wrong;
    };
    for (const auto& r : ranges)
        for (double a = r.lo; a + 12.0 <= r.hi; a += 5.0)
            for (const bool vib : { false, true })
            {
                INFO ("range " << r.range << (vib ? " with vibrato " : " ") << a << " <-> " << a + 12.0);
                REQUIRE (wrongAfterLeap (r.range, a, a + 12.0, vib) == 0);
                REQUIRE (wrongAfterLeap (r.range, a + 12.0, a, vib) == 0);
            }

    // A voice whose 2nd harmonic is ten times its fundamental keeps its octave.
    for (const auto& r : ranges)
        for (double m = r.lo; m <= r.hi; m += 4.0)
        {
            std::vector<float> x ((size_t) kFs);
            double ph = 0.0;
            for (auto& v : x)
            {
                ph += midiToHz (m) / kFs;
                ph -= std::floor (ph);
                v = (float) (0.2 * (0.1 * std::sin (2.0 * kPiD * ph) + std::sin (4.0 * kPiD * ph) + 0.6 * std::sin (6.0 * kPiD * ph)
                                    + 0.3 * std::sin (8.0 * kPiD * ph)));
            }
            PitchDetector d;
            d.prepare (kFs);
            d.setRange (r.range);
            int wrong = 0;
            for (float v : x) if (d.push (v) && d.reading().voiced && std::abs (hzToMidi (d.reading().hz) - m) > 2.0) ++wrong;
            INFO ("weak fundamental, range " << r.range << ", note " << m);
            REQUIRE (wrong == 0);
        }
}

TEST_CASE ("v2 Engine: no clicks when a glide crosses zero shift (Motion trills, octave hops)")
{
    // The lead snaps its grains back onto the input's own pulses when the shift
    // is ~0; in 1.x that snap could cut a grain's start off mid-glide: a click.
    float worst = 0.0f;
    for (const double sung : { 64.1, 64.3, 63.8 })
        for (const float glide : { 0.0f, 8.0f, 15.0f, 40.0f })
            for (const auto& pattern : { std::array<int, 2> { 0, 1 }, std::array<int, 2> { 0, 7 } })
            {
                auto p = hardTune (Scale::major);
                p.tuner.retuneMs = 5.0f;
                p.tuner.keep = 0.2f;
                p.motionOn = true;
                p.motionDivision = Div::d16;
                p.motionLength = 2;
                p.motionSteps[0] = pattern[0];
                p.motionSteps[1] = pattern[1];
                p.motionGlideMs = glide;
                VoiceEngine e;
                makeEngine (e, p);
                e.setTransport (0.0, 120.0);
                const auto in = voice ([sung] (double t) { return sung + 0.2 * std::sin (2.0 * kPiD * 5.3 * t); }, 2.0);
                const auto out = run (e, in);
                std::vector<float> d;
                for (size_t k = (size_t) (0.5 * kFs); k < out.l.size(); ++k)
                    d.push_back (std::abs (out.l[k] - 2.0f * out.l[k - 1] + out.l[k - 2]));
                auto sorted = d;
                std::nth_element (sorted.begin(), sorted.begin() + (long) (sorted.size() * 99 / 100), sorted.end());
                const float ratio = *std::max_element (d.begin(), d.end()) / (sorted[sorted.size() * 99 / 100] + 1.0e-6f);
                INFO ("sung " << sung << ", glide " << glide << " ms, steps 0/" << pattern[1] << ": peak " << ratio << "x p99");
                REQUIRE (ratio < 2.0f);
                worst = std::max (worst, ratio);
            }
    std::printf ("  Motion glides: worst sharpness %.2fx the voice's own (limit 2.0x)\n", worst);
}
