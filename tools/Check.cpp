// lodestone_check: headless verification of the real processor + editor.
//
//   lodestone_check snapshot <out.png> [width] [view] [preset]
//        view: easy | easy-snap | tune | voice | harmony | mod | global
//   lodestone_check presets   every factory preset on a sung melody: as saved,
//                             with SNAP, with HOLD and with all four macros at
//                             100%: no NaN, no clipping past the limiter, no
//                             silence, nothing runaway, tails that end
//   lodestone_check state     save/restore round trip, latency modes, undo,
//                             end-to-end tuning, key learning, preset data
//                             integrity, randomizer safety
//
// Exits non-zero on any failure: CI treats that as a failed build.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <functional>

using namespace lodestone;

namespace
{
constexpr double kFs = 48000.0;

void setParam (LodestoneProcessor& p, P param, float real)
{
    auto* prm = p.apvts.getParameter (def (param).id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (real));
}

float getParam (LodestoneProcessor& p, P param) { return p.apvts.getRawParameterValue (def (param).id)->load(); }

// A sung phrase: notes of a melody, each a little off pitch, with vibrato,
// scoops into some notes, breaths between phrases and a little noise.
std::vector<float> sungMelody (double seconds, std::vector<double> notes = { 64.2, 67.3, 69.0, 71.6, 67.1, 64.0, 62.3, 60.4 },
                               double noteLen = 0.4)
{
    std::vector<float> x ((size_t) (seconds * kFs));
    juce::Random rng (5);
    double phase = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / kFs;
        const int k = (int) (t / noteLen);
        const double inNote = t - k * noteLen;
        const bool gap = inNote > noteLen - 0.05 && (k % 4 == 3);   // a breath every fourth note
        double m = notes[(size_t) k % notes.size()];
        if (inNote < 0.06) m -= 0.6 * (1.0 - inNote / 0.06);         // scoop into the note
        m += 0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t) * std::min (1.0, inNote / 0.2);
        const double f0 = 440.0 * std::pow (2.0, (m - 69.0) / 12.0);
        phase += f0 / kFs;
        phase -= std::floor (phase);
        double s = 0.0;
        for (int h = 1; h * f0 < 5000.0; ++h)
        {
            const double f = h * f0;
            const double env = 0.1 + std::exp (-0.5 * std::pow ((f - 750.0) / 140.0, 2.0)) + 0.6 * std::exp (-0.5 * std::pow ((f - 1150.0) / 150.0, 2.0));
            s += env / h * std::sin (2.0 * juce::MathConstants<double>::pi * h * phase);
        }
        const double amp = gap ? 0.0 : 0.22 * std::min (1.0, inNote / 0.01);
        x[i] = (float) (amp * s) + (gap ? 0.004f : 0.002f) * (rng.nextFloat() * 2.0f - 1.0f);
    }
    return x;
}

struct Render
{
    float peak = 0.0f;
    double rmsDb = -200.0, dryDb = -200.0, tailDb = -200.0;
    bool finite = true;
    std::vector<float> out;
};

Render render (LodestoneProcessor& proc, const std::vector<float>& input, double tailSeconds,
               std::function<void()> atOneSecond = {}, int block = 512)
{
    proc.setPlayConfigDetails (2, 2, kFs, block);
    proc.prepareToPlay (kFs, block);
    proc.reset();
    juce::MidiBuffer midi;
    const int signalLen = (int) input.size(), total = signalLen + (int) (kFs * tailSeconds);
    juce::AudioBuffer<float> buf (2, block);
    double sumOut = 0.0, sumIn = 0.0, tail = 0.0;
    Render r;
    r.out.reserve ((size_t) total);
    for (int pos = 0; pos < total; pos += block)
    {
        if (atOneSecond && pos >= (int) kFs && pos < (int) kFs + block) atOneSecond();
        const int n = std::min (block, total - pos);
        buf.setSize (2, n, false, false, true);
        for (int s = 0; s < n; ++s)
        {
            const float x = pos + s < signalLen ? input[(size_t) (pos + s)] : 0.0f;
            sumIn += (double) x * x;
            buf.setSample (0, s, x);
            buf.setSample (1, s, x);
        }
        proc.processBlock (buf, midi);
        for (int s = 0; s < n; ++s)
        {
            const float l = buf.getSample (0, s), rr = buf.getSample (1, s);
            r.finite = r.finite && std::isfinite (l) && std::isfinite (rr);
            r.peak = std::max ({ r.peak, std::abs (l), std::abs (rr) });
            const double e = 0.5 * ((double) l * l + (double) rr * rr);
            if (pos + s < signalLen) sumOut += e;
            if (pos + s >= total - (int) (kFs * 0.05)) tail += e;
            r.out.push_back (0.5f * (l + rr));
        }
    }
    const auto db = [] (double ms) { return 10.0 * std::log10 (ms + 1e-20); };
    r.rmsDb = db (sumOut / signalLen);
    r.dryDb = db (sumIn / signalLen);
    r.tailDb = db (tail / (kFs * 0.05));
    return r;
}

juce::String judge (const Render& r, double loudDb = 9.0)
{
    if (! r.finite)                  return "NaN/Inf";
    if (r.peak > 0.97f)              return "over ceiling";   // limiter must hold -0.3 dBFS
    if (r.rmsDb < r.dryDb - 12.0)    return "too quiet";
    if (r.rmsDb > r.dryDb + loudDb)  return "too loud";
    if (r.tailDb > -70.0)            return "tail does not end";
    return {};
}

// Median pitch (MIDI) of a section of a render.
double medianPitch (const std::vector<float>& x, double from, double to)
{
    dsp::PitchDetector d;
    d.prepare (kFs);
    d.setRange (3);
    std::vector<double> v;
    for (size_t i = (size_t) (from * kFs); i < std::min (x.size(), (size_t) (to * kFs)); ++i)
        if (d.push (x[i]) && d.reading().voiced) v.push_back (69.0 + 12.0 * std::log2 (d.reading().hz / 440.0));
    if (v.size() < 5) return -1.0;
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}

int checkPresets()
{
    LodestoneProcessor proc;
    int failures = 0;
    const auto& entries = proc.presets.getEntries();
    const auto melody = sungMelody (3.2);
    std::printf ("%-18s %-13s %7s %7s %7s %7s %7s  %s\n", "preset", "category", "dry dB", "saved", "snap", "hold", "macros", "result");

    for (int i = 0; i < entries.size(); ++i)
    {
        if (entries[i].factoryIndex < 0) continue;
        proc.presets.load (i);
        const auto normal = render (proc, melody, 0.6);
        proc.presets.load (i);
        const auto snapped = render (proc, melody, 0.6, [&] { setParam (proc, P::snap, 1.0f); });
        setParam (proc, P::snap, 0.0f);
        proc.presets.load (i);
        const auto held = render (proc, melody, 0.6, [&] { setParam (proc, P::hold, 1.0f); });
        setParam (proc, P::hold, 0.0f);
        proc.presets.load (i);
        for (auto m : { P::macro1, P::macro2, P::macro3, P::macro4 }) setParam (proc, m, 1.0f);
        const auto macros = render (proc, melody, 0.6);

        juce::String problem = judge (normal);
        if (problem.isEmpty()) { problem = judge (snapped); if (problem.isNotEmpty()) problem = "snap: " + problem; }
        if (problem.isEmpty()) { problem = judge (held); if (problem.isNotEmpty()) problem = "hold: " + problem; }
        if (problem.isEmpty()) { problem = judge (macros, 10.0); if (problem.isNotEmpty()) problem = "macros: " + problem; }

        std::printf ("%-18s %-13s %7.1f %7.1f %7.1f %7.1f %7.1f  %s\n", entries[i].name.toRawUTF8(), entries[i].category.toRawUTF8(),
                     normal.dryDb, normal.rmsDb, snapped.rmsDb, held.rmsDb, macros.rmsDb,
                     problem.isEmpty() ? "ok" : ("FAIL: " + problem).toRawUTF8());
        failures += problem.isNotEmpty() ? 1 : 0;
    }
    std::printf ("%d preset(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}

int fail (const char* what) { std::printf ("FAIL: %s\n", what); return 1; }

int checkState()
{
    int failures = 0;

    // The editor reopens at the size and view saved with the project.
    {
        LodestoneProcessor a;
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (a.createEditor());
            ed->setSize (1500, 950);
        }
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        LodestoneProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        std::unique_ptr<juce::AudioProcessorEditor> ed (b.createEditor());
        if (ed->getWidth() != 1500) failures += fail ("editor reopens at the saved size");
        else std::printf ("editor reopens at the saved size: ok\n");
    }
    std::printf ("factory presets: %d\n", (int) factoryPresets().size());

    // Preset data integrity: unique names, valid routes, matrix fits, values in range.
    {
        LodestoneProcessor proc;
        juce::StringArray names;
        int bad = 0;
        for (const auto& fp : factoryPresets())
        {
            if (names.contains (fp.name)) { std::printf ("  duplicate preset name %s\n", fp.name); ++bad; }
            names.add (fp.name);
            for (const auto& r : fp.routes) if (! def (r.dst).modulatable) { std::printf ("  unmodulatable route in %s\n", fp.name); ++bad; }
            auto t = proc.presets.buildFactoryPatch (fp);
            if (t.getChildWithName (LodestoneProcessor::kMatrixType).getNumChildren() != kMatrixSlots)
            { std::printf ("  matrix size wrong in %s\n", fp.name); ++bad; }
            std::array<bool, kNumStages> used {};
            for (int s = 0; s < kNumStages; ++s)
            {
                float v = def (kStages[s].on).def;
                for (const auto& [p, val] : fp.values) if (p == kStages[s].on) v = val;
                used[(size_t) s] = v > 0.5f;
            }
            if (fp.macroKit && macroKit (used).size() + fp.routes.size() > (size_t) kMatrixSlots)
            { std::printf ("  too many routes in %s\n", fp.name); ++bad; }
            for (const auto& [p, val] : fp.values)
            {
                const auto& d = def (p);
                if (val < d.min - 1e-4f || val > d.max + 1e-4f) { std::printf ("  %s out of range in %s\n", d.id, fp.name); ++bad; }
            }
        }
        failures += bad;
        std::printf ("preset integrity: %s\n", bad == 0 ? "ok" : "FAILED");
    }

    // Round trip: preset, tweaks, a route, custom notes and the latency mode.
    {
        LodestoneProcessor a;
        a.presets.load (5);
        setParam (a, P::retune, 77.0f);
        setParam (a, P::scale, 6.0f);
        setParam (a, P::centsE, -14.0f);
        a.addRoute (Src::lfo2, idx (P::formant), -0.4f);
        a.setLowLatency (true);

        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        LodestoneProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        int bad = 0;
        if (std::abs (getParam (b, P::retune) - 77.0f) > 0.05f) bad += fail ("param round trip");
        if (std::abs (getParam (b, P::scale) - 6.0f) > 0.01f) bad += fail ("choice round trip");
        if (std::abs (getParam (b, P::centsE) + 14.0f) > 0.05f) bad += fail ("micro-tuning round trip");
        if (b.routesTo (idx (P::formant)).isEmpty()) bad += fail ("matrix round trip");
        if (! b.isLowLatency()) bad += fail ("latency mode round trip");
        if (b.getLatencySamples() != LodestoneProcessor::latencyFor (b.getSampleRate() > 0 ? b.getSampleRate() : 48000.0, true))
            bad += fail ("latency reported after load");
        juce::MemoryBlock mb2;
        b.getStateInformation (mb2);
        if (mb != mb2) bad += fail ("state is not reload-stable");
        failures += bad;
        std::printf ("state round trip: %s\n", bad == 0 ? "ok" : "FAILED");
    }

    // Latency: Studio and Live are reported to the host, and presets do not change it.
    {
        LodestoneProcessor p;
        p.setPlayConfigDetails (2, 2, kFs, 512);
        p.prepareToPlay (kFs, 512);
        const int studio = p.getLatencySamples();
        p.setLowLatency (true);
        const int live = p.getLatencySamples();
        p.presets.load (3);
        const bool kept = p.isLowLatency();
        std::printf ("latency: studio %d, live %d samples at 48 kHz\n", studio, live);
        if (studio != 720 || live != 144 || ! kept) failures += fail ("latency modes");
    }

    // End to end: an out-of-tune phrase comes out on the notes (Robot), in time.
    {
        LodestoneProcessor p;
        for (int i = 0; i < p.presets.getEntries().size(); ++i)
            if (p.presets.getEntries()[i].name == "Robot") p.presets.load (i);
        setParam (p, P::scale, 0.0f);   // chromatic
        const auto in = sungMelody (2.4, { 57.35, 61.7, 64.4 }, 0.8);
        const auto r = render (p, in, 0.2);
        int bad = 0;
        const double targets[] = { 57.0, 62.0, 64.0 };
        for (int k = 0; k < 3; ++k)
        {
            const double got = medianPitch (r.out, k * 0.8 + 0.3, k * 0.8 + 0.7);
            std::printf ("  note %d: sung %.2f, out %.3f (want %.0f)\n", k + 1, std::vector<double> { 57.35, 61.7, 64.4 }[(size_t) k], got, targets[k]);
            if (std::abs (got - targets[k]) > 0.05) ++bad;
        }
        failures += bad;
        std::printf ("end-to-end tuning: %s\n", bad == 0 ? "ok" : "FAILED");
    }

    // Key learning: a G major melody is recognised.
    {
        LodestoneProcessor p;
        const auto in = sungMelody (6.0, { 67.0, 71.0, 74.0, 72.0, 71.0, 69.0, 67.0, 66.0, 67.0, 62.0, 64.0, 67.0 }, 0.5);
        render (p, in, 0.1);
        const auto k = dsp::findKey (p.getKeyHistogram());
        std::printf ("key learn: %s %s (%.0f%%)\n", dsp::noteName (k.key), k.minor ? "minor" : "major", 100.0 * k.confidence);
        if (k.key != 7 || k.minor) failures += fail ("key learning");
        p.applyKey (k.key, k.minor);
        if ((int) getParam (p, P::key) != 7 || (int) getParam (p, P::scale) != dsp::Scale::major) failures += fail ("apply key");
    }

    // Custom notes: toggling a key starts Custom from the current scale.
    {
        LodestoneProcessor p;
        setParam (p, P::key, 2.0f);              // D major
        setParam (p, P::scale, (float) dsp::Scale::major);
        p.toggleNote (6);                        // remove F#
        int bad = 0;
        if ((int) getParam (p, P::scale) != dsp::kCustomScale) bad += fail ("custom: scale switched");
        if (getParam (p, P::noteFs) > 0.5f) bad += fail ("custom: F# removed");
        if (getParam (p, P::noteD) < 0.5f || getParam (p, P::noteC) > 0.5f) bad += fail ("custom: started from D major");
        failures += bad;
        std::printf ("custom notes: %s\n", bad == 0 ? "ok" : "FAILED");
    }

    // Macros move their targets.
    {
        LodestoneProcessor p;
        p.presets.load (0);
        p.setPlayConfigDetails (2, 2, kFs, 256);
        p.prepareToPlay (kFs, 256);
        juce::AudioBuffer<float> buf (2, 256);
        buf.clear();
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        const float r0 = p.effectiveValue (P::retune), f0 = p.effectiveValue (P::formant);
        setParam (p, P::macro1, 1.0f);
        setParam (p, P::macro2, 1.0f);
        p.processBlock (buf, midi);
        if (p.effectiveValue (P::retune) >= r0 - 5.0f) failures += fail ("macro 1 speeds up the retune");
        if (p.effectiveValue (P::formant) >= f0 - 2.0f) failures += fail ("macro 2 lengthens the throat");
        std::printf ("macros: retune %.1f -> %.1f ms, throat %.1f -> %.1f st\n", r0, p.effectiveValue (P::retune), f0,
                     p.effectiveValue (P::formant));
    }

    // Undo restores a matrix edit.
    {
        LodestoneProcessor p;
        p.undo.clearUndoHistory();
        p.undo.beginNewTransaction ("route");
        p.addRoute (Src::drift, idx (P::detune), 0.3f);
        p.undo.beginNewTransaction();
        p.undo.undo();
        if (! p.routesTo (idx (P::detune)).isEmpty()) failures += fail ("undo matrix edit");
        else std::printf ("undo: ok\n");
    }

    // Randomizer: always a safe setting, and it keeps the key.
    {
        LodestoneProcessor p;
        setParam (p, P::key, 9.0f);
        int bad = 0;
        const auto melody = sungMelody (1.6);
        for (std::uint32_t s = 1; s <= 25; ++s)
        {
            p.presets.randomize (s);
            const auto r = render (p, melody, 0.4);
            const auto problem = judge (r);
            if (problem.isNotEmpty()) { std::printf ("  random seed %u: %s\n", s, problem.toRawUTF8()); ++bad; }
            if ((int) getParam (p, P::key) != 9) { std::printf ("  random seed %u changed the key\n", s); ++bad; }
        }
        failures += bad;
        std::printf ("randomizer (25 seeds): %s\n", bad == 0 ? "ok" : "FAILED");
    }

    std::printf ("%d state check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}

int snapshot (const juce::String& path, int width, const juce::String& view, int presetIndex)
{
    LodestoneProcessor proc;
    proc.setPlayConfigDetails (2, 2, kFs, 512);
    proc.prepareToPlay (kFs, 512);
    if (presetIndex >= 0) proc.presets.load (presetIndex);
    if (view == "easy-snap") setParam (proc, P::snap, 1.0f);

    // Sing into it so the field has traces and the meters move.
    const auto in = sungMelody (2.6);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos + 512 <= in.size(); pos += 512)
    {
        for (int i = 0; i < 512; ++i) { buf.setSample (0, i, in[pos + (size_t) i]); buf.setSample (1, i, in[pos + (size_t) i]); }
        proc.processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<LodestoneEditor*> (ed.get());
    auto& content = editor->getContent();
    if (view.startsWith ("easy")) content.setView (false);
    else if (view == "tune") content.showProPage (0);
    else if (view == "voice") content.showProPage (1);
    else if (view == "harmony") content.showProPage (2);
    else if (view == "mod") content.showProPage (3);
    else if (view == "global") content.showProPage (4);
    for (int k = 0; k < 4; ++k) content.tick();

    editor->setSize (width, width * LodestoneEditor::kDesignH / LodestoneEditor::kDesignW);
    auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

    juce::File out (path);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream (image, stream);
    std::printf ("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", path.toRawUTF8(), image.getWidth(), image.getHeight());
    return ok ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String mode = argc > 1 ? argv[1] : "";

    if (mode == "snapshot" && argc > 2)
        return snapshot (argv[2], argc > 3 ? juce::String (argv[3]).getIntValue() : 1200,
                         argc > 4 ? juce::String (argv[4]) : juce::String ("easy"),
                         argc > 5 ? juce::String (argv[5]).getIntValue() : -1);
    if (mode == "presets") return checkPresets();
    if (mode == "state")   return checkState();

    std::printf ("usage: lodestone_check snapshot <out.png> [width] [easy|easy-snap|tune|voice|harmony|mod|global] [preset] | presets | state\n");
    return 2;
}
