#pragma once
// The Pro view's five pages:
//   TUNE    - key, scale, the correction controls, scale notes and micro-tuning
//   VOICE   - character, throat, grit, breath, transpose and the added vibrato
//   HARMONY - three harmony voices, the double and their ensemble
//   BEAT    - Chord Lock (the chord heard on the sidechain) and Motion (v2)
//   MOD     - LFO 1, LFO 2, Envelope, Drift and the 16-slot matrix
//   GLOBAL  - levels, latency, tempo, seed, about

#include "Widgets.h"
#include "PitchDisplay.h"

namespace lodestone
{

// ---------------------------------------------------------------------------
// A titled panel with an optional power LED; children laid out in rows of
// equal-width cells.
class Panel : public juce::Component
{
public:
    Panel (juce::String titleText, juce::Colour accentColour, juce::Component* powerSwitch = nullptr)
        : title (std::move (titleText)), accent (accentColour), power (powerSwitch)
    {
        if (power != nullptr) addAndMakeVisible (*power);
    }

    void addRow (std::vector<juce::Component*> row, float weight = 1.0f)
    {
        for (auto* c : row) if (c != nullptr) addAndMakeVisible (c);
        rows.push_back ({ std::move (row), weight });
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), accent);
        if (power == nullptr)
            theme::legend (g, title, getLocalBounds().toFloat().reduced (16.0f, 10.0f).removeFromTop (20.0f), theme::text, 13.0f,
                           juce::Justification::centredLeft, true, 0.24f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        auto head = r.removeFromTop (26);
        if (power != nullptr) power->setBounds (head.removeFromLeft (180).withTrimmedLeft (4));
        r.removeFromTop (4);
        float total = 0.0f;
        for (auto& row : rows) total += row.weight;
        float y = (float) r.getY();
        for (auto& row : rows)
        {
            const float h = (float) r.getHeight() * row.weight / juce::jmax (0.001f, total);
            auto rr = juce::Rectangle<float> ((float) r.getX(), y, (float) r.getWidth(), h).toNearestInt();
            y += h;
            const int n = (int) row.cells.size();
            for (int i = 0; i < n; ++i)
                if (auto* c = row.cells[(size_t) i])
                    c->setBounds (rr.withX (rr.getX() + rr.getWidth() * i / n).withWidth (rr.getWidth() / n).reduced (3, 2));
        }
    }

private:
    struct Row { std::vector<juce::Component*> cells; float weight; };
    juce::String title;
    juce::Colour accent;
    juce::Component* power;
    std::vector<Row> rows;
};

// Owns knobs and other controls so pages can declare them in one line each.
class ControlStore
{
public:
    explicit ControlStore (LodestoneProcessor& p) : proc (p) {}

    Knob* knob (P p, juce::Colour c, juce::String legend = {}, Knob::Size s = Knob::Size::medium)
    {
        knobs.push_back (std::make_unique<Knob> (proc, p, c, s, legend));
        return knobs.back().get();
    }
    LedButton* led (P p, juce::Colour c, bool large = false, juce::String legend = {})
    {
        leds.push_back (std::make_unique<LedButton> (proc.apvts, p, c, large, legend));
        return leds.back().get();
    }
    ChoiceBox* choice (P p, juce::Colour c, juce::String legend = {})
    {
        choices.push_back (std::make_unique<ChoiceBox> (proc.apvts, p, c, legend));
        return choices.back().get();
    }
    juce::Component* segment (P p, juce::StringArray labels, juce::Colour c, juce::String legend = {}, int firstValue = 0)
    {
        segments.push_back (std::make_unique<SegmentSwitch> (proc.apvts, p, std::move (labels), c, firstValue));
        labelled.push_back (std::make_unique<Labelled> (legend.isNotEmpty() ? legend : juce::String (def (p).label), *segments.back()));
        return labelled.back().get();
    }
    juce::Component* labelledLed (P p, juce::Colour c, juce::String legend = {})
    {
        auto* l = led (p, c, false, "On");
        labelled.push_back (std::make_unique<Labelled> (legend.isNotEmpty() ? legend : juce::String (def (p).label), *l));
        return labelled.back().get();
    }
    SyncedRate* rate (P r, P sync, P div, juce::Colour c, juce::String legend = {})
    {
        rates.push_back (std::make_unique<SyncedRate> (proc, r, sync, div, c, legend));
        return rates.back().get();
    }
    void tick()
    {
        for (auto& k : knobs) k->tick();
        for (auto& r : rates) r->tick();
    }
    Knob* findKnob (P p)
    {
        for (auto& k : knobs) if (k->paramIndex() == idx (p)) return k.get();
        return nullptr;
    }

private:
    LodestoneProcessor& proc;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<LedButton>> leds;
    std::vector<std::unique_ptr<ChoiceBox>> choices;
    std::vector<std::unique_ptr<SegmentSwitch>> segments;
    std::vector<std::unique_ptr<Labelled>> labelled;
    std::vector<std::unique_ptr<SyncedRate>> rates;
};

// ---------------------------------------------------------------------------
class TunePage : public juce::Component
{
public:
    explicit TunePage (LodestoneProcessor& p)
        : store (p), keys (p), learner (p),
          correction ("Correction", theme::stage (0), store.led (P::tuneOn, theme::stage (0), true, "Correction")),
          notes ("Scale Notes & Micro-Tuning", theme::stage (0))
    {
        const auto c = theme::stage (0);
        correction.addRow ({ store.choice (P::key, c), store.choice (P::scale, c), store.choice (P::range, c),
                             store.choice (P::midiMode, c), store.knob (P::sensitivity, c), store.knob (P::refPitch, c) }, 0.7f);
        correction.addRow ({ store.knob (P::retune, c), store.knob (P::humanize, c), store.knob (P::flex, c), store.knob (P::amount, c),
                             store.knob (P::bounce, c), store.knob (P::stability, c) });
        notes.addRow ({ &learner }, 0.35f);
        notes.addRow ({ &keys }, 0.9f);
        std::vector<juce::Component*> cents;
        for (int i = 0; i < 12; ++i) cents.push_back (store.knob (centsParam (i), c, Choices::notes[i], Knob::Size::small));
        notes.addRow (cents, 0.8f);
        addAndMakeVisible (correction);
        addAndMakeVisible (notes);
    }

    void tick() { store.tick(); keys.tick(); learner.tick(); }

    void resized() override
    {
        auto r = getLocalBounds();
        correction.setBounds (r.removeFromTop (r.getHeight() * 52 / 100));
        r.removeFromTop (10);
        notes.setBounds (r);
    }

private:
    ControlStore store;
    NoteKeyboard keys;
    KeyLearner learner;
    Panel correction, notes;
};

// ---------------------------------------------------------------------------
class VoicePage : public juce::Component
{
public:
    explicit VoicePage (LodestoneProcessor& p)
        : store (p), character ("Character", theme::stage (0)),
          vibrato ("Vibrato", theme::stage (1), store.led (P::vbOn, theme::stage (1), true, "Vibrato"))
    {
        const auto c = theme::stage (0), v = theme::stage (1);
        character.addRow ({ store.choice (P::character, c), store.knob (P::formant, c), store.knob (P::keep, c),
                            store.knob (P::breath, c), store.knob (P::lead, c) });
        character.addRow ({ store.knob (P::transpose, c), store.knob (P::detune, c), store.knob (P::grit, c),
                            store.choice (P::gritMode, c), nullptr });
        vibrato.addRow ({ store.knob (P::vbDepth, v), store.knob (P::vbRate, v), store.knob (P::vbDelay, v),
                          store.knob (P::vbOnset, v), store.knob (P::vbVar, v) });
        addAndMakeVisible (character);
        addAndMakeVisible (vibrato);
    }

    void tick() { store.tick(); }

    void resized() override
    {
        auto r = getLocalBounds();
        character.setBounds (r.removeFromTop (r.getHeight() * 60 / 100));
        r.removeFromTop (10);
        vibrato.setBounds (r);
    }

private:
    ControlStore store;
    Panel character, vibrato;
};

// ---------------------------------------------------------------------------
class HarmonyPage : public juce::Component
{
public:
    explicit HarmonyPage (LodestoneProcessor& p)
        : store (p),
          h1 ("Harmony 1", theme::stage (2), store.led (P::h1On, theme::stage (2), true, "Harmony 1")),
          h2 ("Harmony 2", theme::stage (3), store.led (P::h2On, theme::stage (3), true, "Harmony 2")),
          h3 ("Harmony 3", theme::stage (4), store.led (P::h3On, theme::stage (4), true, "Harmony 3")),
          dbl ("Double", theme::stage (5), store.led (P::dbOn, theme::stage (5), true, "Double")),
          ensemble ("Ensemble", theme::global)
    {
        Panel* hp[3] = { &h1, &h2, &h3 };
        for (int h = 0; h < 3; ++h)
        {
            const auto c = theme::stage (Stage::harmony1 + h);
            hp[h]->addRow ({ store.choice (harmonyParam (h, HField::interval), c) }, 0.55f);
            hp[h]->addRow ({ store.knob (harmonyParam (h, HField::level), c), store.knob (harmonyParam (h, HField::pan), c),
                             store.knob (harmonyParam (h, HField::formant), c) });
            addAndMakeVisible (*hp[h]);
        }
        const auto d = theme::stage (5);
        dbl.addRow ({ store.knob (P::dbLevel, d), store.knob (P::dbSpread, d), store.knob (P::dbDetune, d), store.knob (P::dbDelay, d) });
        ensemble.addRow ({ store.knob (P::harmHumanize, theme::global), store.knob (P::width, theme::global) });
        addAndMakeVisible (dbl);
        addAndMakeVisible (ensemble);
    }

    void tick() { store.tick(); }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop ((r.getHeight() - 10) * 55 / 100);
        r.removeFromTop (10);
        const int w = (top.getWidth() - 20) / 3;
        h1.setBounds (top.removeFromLeft (w));
        top.removeFromLeft (10);
        h2.setBounds (top.removeFromLeft (w));
        top.removeFromLeft (10);
        h3.setBounds (top);
        dbl.setBounds (r.removeFromLeft (r.getWidth() * 62 / 100));
        r.removeFromLeft (10);
        ensemble.setBounds (r);
    }

private:
    ControlStore store;
    Panel h1, h2, h3, dbl, ensemble;
};


// ---------------------------------------------------------------------------
// BEAT page (v2).

// Sets one parameter from the UI as a host-visible gesture.
inline void setParamFromUi (LodestoneProcessor& p, P param, float real)
{
    if (auto* prm = p.apvts.getParameter (def (param).id))
    {
        const float n = prm->convertTo0to1 (real);
        if (std::abs (prm->getValue() - n) < 1.0e-7f) return;
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (n);
        prm->endChangeGesture();
    }
}

// What the beat is playing: the chord name, whether the sidechain is
// connected and has sound, and the beat's notes as 12 bars (chord tones lit).
class ChordView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ChordView (LodestoneProcessor& p) : proc (p)
    {
        setTooltip ("The chord the beat is playing, heard on Lodestone's sidechain input. Bars: how much of each note the beat "
                    "plays; lit bars are the chord's notes.");
    }

    void tick()
    {
        const int root = proc.uiChordRoot.load(), shape = proc.uiChordShape.load();
        const bool hearing = proc.uiBeatHearing.load(), connected = proc.uiSidechainConnected.load();
        bool changed = root != lastRoot || shape != lastShape || hearing != lastHearing || connected != lastConnected;
        for (int i = 0; i < 12; ++i)
        {
            const float v = proc.uiBeatChroma[(size_t) i].load();
            if (std::abs (v - bars[(size_t) i]) > 0.004f) { bars[(size_t) i] = v; changed = true; }
        }
        lastRoot = root; lastShape = shape; lastHearing = hearing; lastConnected = connected;
        if (changed) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::screen);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (theme::edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        r = r.reduced (14.0f, 10.0f);

        dsp::Chord c;
        c.root = lastRoot;
        c.shape = juce::jlimit (0, dsp::kNumChordShapes - 1, lastShape);
        char name[16];
        dsp::chordName (c, name, 16);
        theme::legend (g, "Chord", r.removeFromTop (14.0f), theme::textFaint, 9.5f, juce::Justification::centredLeft);
        g.setColour (c.valid() ? theme::beat : theme::textFaint);
        g.setFont (theme::font (40.0f, true));
        g.drawText (c.valid() ? juce::String (name) : juce::String ("--"), r.removeFromTop (46.0f), juce::Justification::centredLeft, false);

        const juce::String status = ! lastConnected ? "No sidechain: route the beat to Lodestone's sidechain input"
                                  : ! lastHearing   ? "Sidechain connected: waiting for the beat"
                                                    : "Hearing the beat";
        g.setColour (lastConnected && lastHearing ? theme::beat : theme::textDim);
        g.setFont (theme::font (13.0f));
        g.drawFittedText (status, r.removeFromTop (34.0f).toNearestInt(), juce::Justification::centredLeft, 2);
        r.removeFromTop (6.0f);

        // Chroma bars.
        const auto mask = c.mask();
        float mx = 0.02f;
        for (float v : bars) mx = juce::jmax (mx, v);
        auto labels = r.removeFromBottom (16.0f);
        const float w = r.getWidth() / 12.0f;
        for (int i = 0; i < 12; ++i)
        {
            auto col = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (3.0f, 0.0f);
            g.setColour (theme::well);
            g.fillRoundedRectangle (col, 3.0f);
            const bool tone = dsp::has (mask, i);
            const float h = col.getHeight() * juce::jlimit (0.0f, 1.0f, bars[(size_t) i] / mx);
            g.setColour (tone ? theme::beat : theme::textFaint.withAlpha (0.6f));
            g.fillRoundedRectangle (col.removeFromBottom (h), 3.0f);
            g.setColour (tone ? theme::beat : theme::textDim);
            g.setFont (theme::font (11.0f, tone));
            g.drawText (dsp::noteName (i), labels.withX (r.getX() + w * (float) i).withWidth (w), juce::Justification::centred, false);
        }
    }

private:
    LodestoneProcessor& proc;
    std::array<float, 12> bars {};
    int lastRoot = -2, lastShape = 0;
    bool lastHearing = false, lastConnected = false;
};

// The Motion pattern: 16 steps, each a number of notes up or down the scale.
// Drag to draw (across steps), double-click a step for 0; the step playing
// lights up, steps past the pattern length are dimmed.
class MotionGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int kRange = 12;

    explicit MotionGrid (LodestoneProcessor& p) : proc (p)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("The Motion pattern: drag up or down to set how many notes each step moves (draw across steps). "
                    "Double-click a step for 0. Steps past the Steps setting are skipped.");
    }

    void tick()
    {
        int h = proc.uiMotionStep.load();
        for (int i = 0; i < dsp::kMotionSteps; ++i)
            h = h * 31 + (int) proc.apvts.getRawParameterValue (def (motionStepParam (i)).id)->load();
        h = h * 31 + (int) proc.apvts.getRawParameterValue (def (P::motionLength).id)->load();
        h = h * 31 + (proc.apvts.getRawParameterValue (def (P::motionOn).id)->load() > 0.5f ? 1 : 0);
        if (h != lastHash) { lastHash = h; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::screen);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (theme::edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        const auto accent = theme::stage (Stage::motion);
        const bool on = value (P::motionOn) > 0.5f;
        const int len = juce::roundToInt (value (P::motionLength));
        const int playing = proc.uiMotionStep.load();
        auto area = cells();
        const float zeroY = yFor (0);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawHorizontalLine ((int) zeroY, area.getX(), area.getRight());

        for (int i = 0; i < dsp::kMotionSteps; ++i)
        {
            auto col = column (i);
            const int v = juce::roundToInt (value (motionStepParam (i)));
            const bool used = i < len;
            const bool now = on && i == playing;
            g.setColour (theme::well.withAlpha (used ? 1.0f : 0.5f));
            g.fillRoundedRectangle (col.reduced (2.0f, 0.0f), 4.0f);
            if (now)
            {
                g.setColour (accent.withAlpha (0.16f));
                g.fillRoundedRectangle (col.reduced (2.0f, 0.0f), 4.0f);
            }
            const float y = yFor (v);
            auto bar = juce::Rectangle<float> (col.getX() + 5.0f, juce::jmin (y, zeroY), col.getWidth() - 10.0f, juce::jmax (3.0f, std::abs (zeroY - y)));
            if (v == 0) bar = bar.withHeight (3.0f).withCentre ({ bar.getCentreX(), zeroY });
            g.setColour ((used ? accent : theme::textFaint).withAlpha (used ? (on ? 1.0f : 0.55f) : 0.35f));
            g.fillRoundedRectangle (bar, 2.5f);

            g.setColour (used ? (now ? accent : theme::textDim) : theme::textFaint);
            g.setFont (theme::font (11.5f, now));
            g.drawText ((v > 0 ? "+" : "") + juce::String (v), col.withY (area.getBottom() + 2.0f).withHeight (16.0f), juce::Justification::centred, false);
            g.setColour (theme::textFaint);
            g.setFont (theme::font (10.0f));
            g.drawText (juce::String (i + 1), col.withY (r.getY() + 3.0f).withHeight (14.0f), juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        proc.undo.beginNewTransaction ("Motion step");
        lastCol = -1;
        paintAt (e.position);
    }
    void mouseDrag (const juce::MouseEvent& e) override { paintAt (e.position); }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = columnAt (e.position.x);
        if (i < 0) return;
        proc.undo.beginNewTransaction ("Motion step");
        setParamFromUi (proc, motionStepParam (i), 0.0f);
    }

private:
    float value (P p) const { return proc.apvts.getRawParameterValue (def (p).id)->load(); }
    juce::Rectangle<float> cells() const { return getLocalBounds().toFloat().reduced (8.0f, 0.0f).withTrimmedTop (20.0f).withTrimmedBottom (22.0f); }
    juce::Rectangle<float> column (int i) const
    {
        const auto a = cells();
        const float w = a.getWidth() / (float) dsp::kMotionSteps;
        return { a.getX() + w * (float) i, a.getY(), w, a.getHeight() };
    }
    float yFor (int v) const
    {
        const auto a = cells().reduced (0.0f, 4.0f);
        return a.getCentreY() - (float) v / (float) kRange * a.getHeight() * 0.5f;
    }
    int columnAt (float x) const
    {
        const auto a = cells();
        if (x < a.getX() || x >= a.getRight()) return -1;
        return juce::jlimit (0, dsp::kMotionSteps - 1, (int) ((x - a.getX()) / (a.getWidth() / (float) dsp::kMotionSteps)));
    }
    void paintAt (juce::Point<float> pos)
    {
        const int i = columnAt (juce::jlimit (cells().getX(), cells().getRight() - 1.0f, pos.x));
        if (i < 0) return;
        const auto a = cells().reduced (0.0f, 4.0f);
        const int v = juce::jlimit (-kRange, kRange, juce::roundToInt ((a.getCentreY() - pos.y) / (a.getHeight() * 0.5f) * (float) kRange));
        setParamFromUi (proc, motionStepParam (i), (float) v);
        lastCol = i;
        repaint();
    }

    LodestoneProcessor& proc;
    int lastHash = 0, lastCol = -1;
};

// Ready-made Motion patterns: written to the steps, Steps and Rate as one undoable edit.
struct MotionPattern { const char* name; std::vector<int> steps; int division; };
inline const std::vector<MotionPattern>& motionPatterns()
{
    static const std::vector<MotionPattern> p = {
        { "Up and Down",  { 0, 2, 4, 2, 0, 2, 4, 7 }, dsp::Div::d8 },
        { "Trill",        { 0, 1 },                   dsp::Div::d16 },
        { "Rising Arp",   { 0, 2, 4, 7 },             dsp::Div::d16 },
        { "Falling Arp",  { 7, 4, 2, 0 },             dsp::Div::d16 },
        { "Octave Hop",   { 0, 7 },                   dsp::Div::d8 },
        { "Turn",         { 0, 1, 0, -1 },            dsp::Div::d16 },
        { "Fifth Stab",   { 0, 0, 4, 0 },             dsp::Div::d8 },
        { "Staircase",    { 0, 1, 2, 3, 4, 5, 6, 7 }, dsp::Div::d16 },
        { "Low Answer",   { 0, 0, 0, -3, 0, 0, -5, -7 }, dsp::Div::d8 },
        { "Triplet Roll", { 0, 2, 4 },                dsp::Div::d8T },
    };
    return p;
}

inline void applyMotionPattern (LodestoneProcessor& proc, const MotionPattern& m)
{
    proc.undo.beginNewTransaction ("Motion pattern");
    for (int i = 0; i < dsp::kMotionSteps; ++i)
        setParamFromUi (proc, motionStepParam (i), i < (int) m.steps.size() ? (float) m.steps[(size_t) i] : 0.0f);
    setParamFromUi (proc, P::motionLength, (float) m.steps.size());
    setParamFromUi (proc, P::motionRate, (float) m.division);
}

class BeatPage : public juce::Component
{
public:
    explicit BeatPage (LodestoneProcessor& p)
        : proc (p), store (p), chordView (p), grid (p),
          chord ("Chord Lock: Follow the Beat", theme::beat),
          motion ("Motion", theme::stage (Stage::motion), store.led (P::motionOn, theme::stage (Stage::motion), true, "Motion"))
    {
        const auto m = theme::stage (Stage::motion);
        chord.addRow ({ &chordView }, 1.5f);
        chord.addRow ({ store.knob (P::chordLock, theme::beat, "Chord Lock", Knob::Size::large) }, 1.0f);
        addAndMakeVisible (chord);

        patterns.setTooltip ("Ready-made patterns: trills, arps, octave hops");
        patterns.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        patterns.onClick = [this]
        {
            juce::PopupMenu menu;
            const auto& list = motionPatterns();
            for (int i = 0; i < (int) list.size(); ++i) menu.addItem (i + 1, list[(size_t) i].name);
            menu.addSeparator();
            menu.addItem (1000, "Clear (all steps 0)");
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (patterns), [this] (int r)
            {
                if (r == 1000) applyMotionPattern (proc, { "Clear", { 0, 0, 0, 0, 0, 0, 0, 0 }, dsp::Div::d8 });
                else if (r > 0) applyMotionPattern (proc, motionPatterns()[(size_t) (r - 1)]);
            });
        };
        motion.addRow ({ store.choice (P::motionRate, m), store.knob (P::motionLength, m), store.knob (P::motionGlide, m),
                         store.segment (P::motionStart, { "GRID", "PHRASE" }, m), &patterns }, 0.75f);
        motion.addRow ({ &grid }, 1.25f);
        addAndMakeVisible (motion);
    }

    void tick() { store.tick(); chordView.tick(); grid.tick(); }

    void resized() override
    {
        auto r = getLocalBounds();
        chord.setBounds (r.removeFromLeft (r.getWidth() * 34 / 100));
        r.removeFromLeft (10);
        motion.setBounds (r);
        // The pattern button sits in a knob-sized cell: keep it button-sized.
        patterns.setBounds (patterns.getBounds().withSizeKeepingCentre (juce::jmin (patterns.getWidth(), 110), 30));
    }

private:
    LodestoneProcessor& proc;
    ControlStore store;
    ChordView chordView;
    MotionGrid grid;
    juce::TextButton patterns { "PATTERNS" };
    Panel chord, motion;
};

// ---------------------------------------------------------------------------
// Live meter for a modulation source (bipolar or 0..1).
class SourceMeter : public juce::Component
{
public:
    SourceMeter (std::function<float()> fn, bool bipolarSource, juce::Colour c) : get (std::move (fn)), bipolar (bipolarSource), colour (c) {}
    void tick() { const float v = get(); if (std::abs (v - value) > 0.003f) { value = v; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), 8.0f);
        g.setColour (theme::well);
        g.fillRoundedRectangle (r, 4.0f);
        const float zero = bipolar ? r.getCentreX() : r.getX();
        const float x = bipolar ? r.getCentreX() + 0.5f * r.getWidth() * juce::jlimit (-1.0f, 1.0f, value)
                                : r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, value);
        g.setColour (colour);
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (zero, x), r.getY(), juce::jmax (2.0f, std::abs (x - zero)), r.getHeight()), 4.0f);
    }

private:
    std::function<float()> get;
    bool bipolar;
    juce::Colour colour;
    float value = 0.0f;
};

// One matrix slot: [n] [source] [destination] [amount] [x]
class MatrixRow : public juce::Component
{
public:
    MatrixRow (LodestoneProcessor& p, int slotIndex) : proc (p), slot (slotIndex)
    {
        for (auto* b : { &srcBtn, &dstBtn, &clearBtn }) { addAndMakeVisible (b); b->setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        amount.setSliderStyle (juce::Slider::LinearHorizontal);
        amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        amount.setRange (-1.0, 1.0, 0.001);
        amount.setDoubleClickReturnValue (true, 0.0);
        amount.setColour (juce::Slider::trackColourId, theme::mod);
        amount.setPopupDisplayEnabled (true, true, nullptr);
        amount.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        addAndMakeVisible (amount);
        amount.onDragStart = [this] { proc.undo.beginNewTransaction ("Mod amount"); };
        amount.onValueChange = [this]
        {
            auto s = proc.getSlot (slot);
            if (std::abs (s.amount - (float) amount.getValue()) < 1.0e-4f) return;
            s.amount = (float) amount.getValue();
            proc.setSlot (slot, s);
        };
        srcBtn.onClick = [this] { showSourceMenu(); };
        dstBtn.onClick = [this] { showDestMenu(); };
        clearBtn.onClick = [this] { proc.undo.beginNewTransaction ("Clear slot"); proc.setSlot (slot, {}); };
        srcBtn.setTooltip ("Modulation source");
        dstBtn.setTooltip ("What it moves");
        amount.setTooltip ("How far, in either direction (double-click: 0)");
        clearBtn.setTooltip ("Clear this slot");
        refresh();
    }

    void refresh()
    {
        const auto s = proc.getSlot (slot);
        srcBtn.setButtonText (sourceNames()[(int) s.src]);
        dstBtn.setButtonText (s.dst >= 0 ? juce::String (def (s.dst).name) : juce::String ("-"));
        if (! amount.isMouseButtonDown()) amount.setValue (s.amount, juce::dontSendNotification);
        active = s.isActive();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (active ? theme::mod : theme::textFaint);
        g.setFont (theme::font (12.0f, true));
        g.drawText (juce::String (slot + 1), getLocalBounds().removeFromLeft (24), juce::Justification::centred, false);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 2);
        r.removeFromLeft (26);
        clearBtn.setBounds (r.removeFromRight (26));
        r.removeFromRight (6);
        srcBtn.setBounds (r.removeFromLeft (104));
        r.removeFromLeft (6);
        dstBtn.setBounds (r.removeFromLeft (r.getWidth() * 55 / 100));
        r.removeFromLeft (6);
        amount.setBounds (r);
    }

private:
    void showSourceMenu()
    {
        juce::PopupMenu m;
        for (int s = 0; s < kNumSources; ++s) m.addItem (s + 1, sourceNames()[s], true, (int) proc.getSlot (slot).src == s);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (srcBtn), [this] (int r)
        {
            if (r <= 0) return;
            auto s = proc.getSlot (slot);
            s.src = (Src) (r - 1);
            if (s.src != Src::none && s.amount == 0.0f) s.amount = 0.25f;
            proc.undo.beginNewTransaction ("Mod source");
            proc.setSlot (slot, s);
        });
    }

    void showDestMenu()
    {
        juce::PopupMenu m;
        auto addGroup = [&] (const juce::String& name, int first, int last)
        {
            juce::PopupMenu sub;
            for (int i = first; i <= last; ++i)
                if (def (i).modulatable) sub.addItem (i + 1, def (i).name, true, proc.getSlot (slot).dst == i);
            m.addSubMenu (name, sub);
        };
        addGroup ("Global", idx (P::inGain), idx (P::width));
        addGroup ("Tune", idx (P::tuneOn), idx (P::gritMode));
        addGroup ("Vibrato", idx (P::vbOn), idx (P::vbVar));
        addGroup ("Harmonies", idx (P::h1On), idx (P::harmHumanize));
        addGroup ("Double", idx (P::dbOn), idx (P::dbDelay));
        addGroup ("Beat", idx (P::chordLock), idx (P::mStep16));
        addGroup ("Modulators", idx (P::lfo1Shape), idx (P::driftSmooth));
        m.addSeparator();
        m.addItem (100000, "None");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (dstBtn), [this] (int r)
        {
            if (r <= 0) return;
            auto s = proc.getSlot (slot);
            s.dst = r == 100000 ? -1 : r - 1;
            if (s.dst >= 0 && s.src == Src::none) s.src = Src::lfo1;
            if (s.dst >= 0 && s.amount == 0.0f) s.amount = 0.25f;
            proc.undo.beginNewTransaction ("Mod destination");
            proc.setSlot (slot, s);
        });
    }

    LodestoneProcessor& proc;
    int slot;
    juce::TextButton srcBtn, dstBtn, clearBtn { "x" };
    juce::Slider amount;
    bool active = false;
};

class ModPage : public juce::Component
{
public:
    explicit ModPage (LodestoneProcessor& p)
        : proc (p), store (p),
          lfo1 ("LFO 1", theme::mod), lfo2 ("LFO 2", theme::mod), envP ("Envelope", theme::mod), driftP ("Drift", theme::mod),
          matrix ("Modulation Matrix", theme::mod),
          m1 ([&p] { return p.uiLfo[0].load(); }, true, theme::mod), m2 ([&p] { return p.uiLfo[1].load(); }, true, theme::mod),
          m3 ([&p] { return p.uiEnv.load(); }, false, theme::mod), m4 ([&p] { return p.uiDrift.load(); }, true, theme::mod)
    {
        const auto c = theme::mod;
        lfo1.addRow ({ store.choice (P::lfo1Shape, c), store.labelledLed (P::lfo1Sync, c), store.rate (P::lfo1Rate, P::lfo1Sync, P::lfo1Div, c) });
        lfo1.addRow ({ &m1 }, 0.25f);
        lfo2.addRow ({ store.choice (P::lfo2Shape, c), store.labelledLed (P::lfo2Sync, c), store.rate (P::lfo2Rate, P::lfo2Sync, P::lfo2Div, c) });
        lfo2.addRow ({ &m2 }, 0.25f);
        envP.addRow ({ store.knob (P::envAttack, c), store.knob (P::envRelease, c), store.knob (P::envGain, c) });
        envP.addRow ({ &m3 }, 0.25f);
        driftP.addRow ({ store.knob (P::driftRate, c), store.knob (P::driftSmooth, c), nullptr });
        driftP.addRow ({ &m4 }, 0.25f);

        for (int i = 0; i < kMatrixSlots; ++i)
        {
            rows.push_back (std::make_unique<MatrixRow> (p, i));
            matrix.addAndMakeVisible (*rows.back());
        }
        clearAll.onClick = [this]
        {
            proc.undo.beginNewTransaction ("Clear matrix");
            proc.clearMatrix();
        };
        clearAll.setTooltip ("Clear every modulation slot (macros included)");
        matrix.addAndMakeVisible (clearAll);

        for (auto* x : { &lfo1, &lfo2, &envP, &driftP, &matrix }) addAndMakeVisible (x);
    }

    void tick()
    {
        store.tick();
        for (auto* m : { &m1, &m2, &m3, &m4 }) m->tick();
        if (++refreshCounter % 6 == 0) for (auto& r : rows) r->refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto left = r.removeFromLeft (360);
        const int h = (left.getHeight() - 30) / 4;
        for (auto* x : { &lfo1, &lfo2, &envP, &driftP })
        {
            x->setBounds (left.removeFromTop (h));
            left.removeFromTop (10);
        }
        r.removeFromLeft (10);
        matrix.setBounds (r);
        auto mr = r.withZeroOrigin().reduced (14, 10);
        clearAll.setBounds (mr.removeFromTop (24).removeFromRight (90));
        mr.removeFromTop (8);
        const int rh = mr.getHeight() / kMatrixSlots;
        for (auto& row : rows) row->setBounds (mr.removeFromTop (rh));
    }

private:
    LodestoneProcessor& proc;
    ControlStore store;
    Panel lfo1, lfo2, envP, driftP, matrix;
    SourceMeter m1, m2, m3, m4;
    std::vector<std::unique_ptr<MatrixRow>> rows;
    juce::TextButton clearAll { "CLEAR ALL" };
    int refreshCounter = 0;
};

// ---------------------------------------------------------------------------
// LIVE | STUDIO latency (a session setting, not a parameter).
class LatencySwitch : public juce::Component
{
public:
    explicit LatencySwitch (LodestoneProcessor& p) : proc (p)
    {
        for (auto* b : { &live, &studio }) { addAndMakeVisible (b); b->setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        live.onClick = [this] { proc.setLowLatency (true); refresh(); };
        studio.onClick = [this] { proc.setLowLatency (false); refresh(); };
        live.setTooltip ("LIVE: about 3 ms. For singing through Lodestone while recording or on stage.");
        studio.setTooltip ("STUDIO: about 15 ms, reported to the DAW so it is compensated. Corrections land exactly on time. Best for mixing.");
        refresh();
    }
    void tick() { if (proc.isLowLatency() != lastLive) refresh(); }
    void refresh()
    {
        lastLive = proc.isLowLatency();
        live.setToggleState (lastLive, juce::dontSendNotification);
        studio.setToggleState (! lastLive, juce::dontSendNotification);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        theme::legend (g, "Latency", r.removeFromTop (16.0f), theme::textDim, 10.5f);
        const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        const double ms = 1000.0 * LodestoneProcessor::latencyFor (fs, lastLive) / fs;
        g.setColour (theme::textFaint);
        g.setFont (theme::font (12.0f));
        g.drawText (juce::String (ms, 1) + " ms", r.removeFromBottom (16.0f), juce::Justification::centred, false);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (18);
        r.removeFromBottom (18);
        auto row = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 30));
        live.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 0));
        studio.setBounds (row.reduced (2, 0));
    }

private:
    LodestoneProcessor& proc;
    juce::TextButton live { "LIVE" }, studio { "STUDIO" };
    bool lastLive = false;
};

class GlobalPage : public juce::Component
{
public:
    explicit GlobalPage (LodestoneProcessor& p)
        : store (p), latency (p), levels ("Levels & Output", theme::global), timing ("Latency, Tempo & Random", theme::global),
          about ("About Lodestone", theme::gold)
    {
        const auto g = theme::global;
        levels.addRow ({ store.knob (P::inGain, g), store.knob (P::outGain, g), store.knob (P::mix, g), store.knob (P::width, g),
                         store.labelledLed (P::limiter, g) });
        timing.addRow ({ &latency, store.knob (P::tempo, g), store.knob (P::seed, g) });
        for (auto* x : { &levels, &timing, &about }) addAndMakeVisible (x);
        about.addRow ({ &aboutText });
        aboutText.setText ("Lodestone " JucePlugin_VersionString "  -  LowKei Audio\n\n"
                           "Real-time pitch correction, harmony and doubling. Lodestone hears the voice, pulls each note onto "
                           "the scale (Speed, Humanize, Flex, Bounce), keeps the singer's own tone (Modern) and builds harmonies "
                           "and doubles from the same performance.\n\n"
                           "Right-click any knob to modulate it or assign it to a macro. MIDI notes can set the scale, the melody "
                           "or the harmony (MIDI Mode on the TUNE page).",
                           juce::dontSendNotification);
        aboutText.setJustificationType (juce::Justification::topLeft);
        aboutText.setFont (theme::font (14.0f));
        aboutText.setColour (juce::Label::textColourId, theme::textDim);
    }

    void tick() { store.tick(); latency.tick(); }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (r.getHeight() / 2 - 5);
        levels.setBounds (top);
        r.removeFromTop (10);
        timing.setBounds (r.removeFromLeft (r.getWidth() * 50 / 100));
        r.removeFromLeft (10);
        about.setBounds (r);
    }

private:
    ControlStore store;
    LatencySwitch latency;
    Panel levels, timing, about;
    juce::Label aboutText;
};

} // namespace lodestone
