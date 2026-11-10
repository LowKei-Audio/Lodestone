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
