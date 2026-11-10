#pragma once
// The two faces of Lodestone:
//   EasyView - key and scale (with LEARN), the field, the keyboard, SNAP and
//              HOLD, the five tuning knobs that matter most, the macros and
//              the voice switches. Every preset plays from here.
//   ProView  - TUNE / VOICE / HARMONY / MOD / GLOBAL pages for deep work,
//              with the field, SNAP, HOLD and the macros always in reach.

#include "Pages.h"

namespace lodestone
{

inline PerformButton* makeSnap (LodestoneProcessor& p)
{
    return new PerformButton (p, P::snap, "Snap", "snapLatch", theme::snap,
                              [&p] { return p.apvts.getRawParameterValue ("snap")->load(); }, "Hard tune",
                              "SNAP: instant hard tune (the robot effect) while engaged. Automate it to throw the effect onto single words. "
                              "MOMENT = while pressed, LATCH = click on / off");
}
inline PerformButton* makeHold (LodestoneProcessor& p)
{
    return new PerformButton (p, P::hold, "Hold", "holdLatch", theme::hold,
                              [&p] { return p.apvts.getRawParameterValue ("hold")->load(); }, "Freeze the note",
                              "HOLD: freezes the note being sung. Whatever comes next lands on that note until you let go. "
                              "MOMENT = while pressed, LATCH = click on / off");
}

// ---------------------------------------------------------------------------
class EasyView : public juce::Component
{
public:
    explicit EasyView (LodestoneProcessor& p)
        : proc (p), store (p), field (p), keys (p), learner (p), snap (makeSnap (p)), hold (makeHold (p)),
          snapMode (*snap), holdMode (*hold), macros (p, Knob::Size::medium), voices (p, 2)
    {
        const auto c = theme::stage (0);
        key = store.choice (P::key, c);
        scale = store.choice (P::scale, c);
        range = store.choice (P::range, c, "Input");
        character = store.choice (P::character, c);
        speed = store.knob (P::retune, c, "Speed", Knob::Size::large);
        humanize = store.knob (P::humanize, c);
        flex = store.knob (P::flex, c);
        keep = store.knob (P::keep, c, "Vibrato");
        throat = store.knob (P::formant, c);
        stability = store.knob (P::stability, c);
        transpose = store.knob (P::transpose, c);
        tracking = store.knob (P::sensitivity, c);
        mix = store.knob (P::mix, theme::global);
        output = store.knob (P::outGain, theme::global);

        for (auto* x : std::initializer_list<juce::Component*> { key, scale, range, character, &learner, &voices, mix, output, &field, &keys,
                                                                 snap.get(), hold.get(), &snapMode, &holdMode, speed, humanize, flex,
                                                                 keep, throat, stability, &macros, transpose, tracking })
            addAndMakeVisible (x);
    }

    void tick()
    {
        store.tick();
        field.tick();
        keys.tick();
        learner.tick();
        snap->tick();
        hold->tick();
        snapMode.refresh();
        holdMode.refresh();
        macros.tick();
        // Correction knobs are dimmed while correction is off (they keep their settings).
        const bool tuneOn = proc.apvts.getRawParameterValue ("tune_on")->load() > 0.5f;
        for (auto* k : { speed, humanize, flex, keep, stability }) k->setAlpha (tuneOn ? 1.0f : 0.45f);
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, leftPanel.toFloat(), theme::gold);
        drawPanel (g, rightPanel.toFloat(), theme::gold);
        auto title = [&] (const char* s, juce::Rectangle<int> r)
        {
            theme::legend (g, s, r.toFloat(), theme::text, 13.0f, juce::Justification::centredLeft, true, 0.24f);
        };
        title ("Key", leftPanel.reduced (18, 10).removeFromTop (22));
        title ("Voices", voicesTitle);
        title ("Tune", rightPanel.reduced (18, 10).removeFromTop (22));
        title ("Macros", macroTitle);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        leftPanel = r.removeFromLeft (290);
        rightPanel = r.removeFromRight (340);
        auto centre = r.reduced (12, 0);

        // Left: key, scale, learn, input, character, voices, levels.
        auto l = leftPanel.reduced (14, 10);
        l.removeFromTop (28);
        auto row = l.removeFromTop (54);
        key->setBounds (row.removeFromLeft (row.getWidth() * 40 / 100).reduced (2, 2));
        scale->setBounds (row.reduced (2, 2));
        l.removeFromTop (6);
        learner.setBounds (l.removeFromTop (40).reduced (2, 0));
        l.removeFromTop (6);
        row = l.removeFromTop (54);
        range->setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 2));
        character->setBounds (row.reduced (2, 2));
        l.removeFromTop (14);
        voicesTitle = l.removeFromTop (20).withTrimmedLeft (4);
        voices.setBounds (l.removeFromTop (100));
        l.removeFromTop (12);
        auto extra = l.removeFromTop (104);
        transpose->setBounds (extra.removeFromLeft (extra.getWidth() / 2).reduced (14, 0));
        tracking->setBounds (extra.reduced (14, 0));
        l.removeFromTop (10);
        auto lv = l.removeFromBottom (juce::jmin (l.getHeight(), 118));
        mix->setBounds (lv.removeFromLeft (lv.getWidth() / 2).reduced (14, 0));
        output->setBounds (lv.reduced (14, 0));

        // Centre: the field and the keyboard.
        auto kb = centre.removeFromBottom (96);
        centre.removeFromBottom (10);
        field.setBounds (centre);
        keys.setBounds (kb);

        // Right: SNAP / HOLD, the tuning knobs, the macros.
        auto rr = rightPanel.reduced (12, 10);
        rr.removeFromTop (28);
        auto perf = rr.removeFromTop (110);
        auto half = perf.removeFromLeft (perf.getWidth() / 2);
        snap->setBounds (half.removeFromTop (78).reduced (4, 0));
        snapMode.setBounds (half.removeFromTop (28).reduced (16, 2));
        hold->setBounds (perf.removeFromTop (78).reduced (4, 0));
        holdMode.setBounds (perf.removeFromTop (28).reduced (16, 2));
        rr.removeFromTop (8);

        auto k1 = rr.removeFromTop (128);
        const int side = (k1.getWidth() - 132) / 2;
        humanize->setBounds (k1.removeFromLeft (side).withTrimmedTop (34).reduced (2, 0));
        flex->setBounds (k1.removeFromRight (side).withTrimmedTop (34).reduced (2, 0));
        speed->setBounds (k1);
        rr.removeFromTop (4);
        auto k2 = rr.removeFromTop (96);
        const int kw = k2.getWidth() / 3;
        keep->setBounds (k2.removeFromLeft (kw).reduced (4, 0));
        throat->setBounds (k2.removeFromLeft (kw).reduced (4, 0));
        stability->setBounds (k2.reduced (4, 0));
        rr.removeFromTop (6);
        macroTitle = rr.removeFromTop (20).withTrimmedLeft (6);
        macros.setBounds (rr);
    }

private:
    LodestoneProcessor& proc;
    ControlStore store;
    PitchDisplay field;
    NoteKeyboard keys;
    KeyLearner learner;
    std::unique_ptr<PerformButton> snap, hold;
    PerformModeSwitch snapMode, holdMode;
    MacroBank macros;
    StageSwitches voices;
    juce::Component *key = nullptr, *scale = nullptr, *range = nullptr, *character = nullptr;
    Knob *speed = nullptr, *humanize = nullptr, *flex = nullptr, *keep = nullptr, *throat = nullptr, *stability = nullptr,
         *transpose = nullptr, *tracking = nullptr, *mix = nullptr, *output = nullptr;
    juce::Rectangle<int> leftPanel, rightPanel, voicesTitle, macroTitle;
};

// ---------------------------------------------------------------------------
class ProView : public juce::Component
{
public:
    static constexpr int kPages = 5;

    explicit ProView (LodestoneProcessor& p)
        : tunePage (p), voicePage (p), harmonyPage (p), modPage (p), globalPage (p), field (p),
          snap (makeSnap (p)), hold (makeHold (p)), macros (p, Knob::Size::medium)
    {
        const char* names[kPages] = { "TUNE", "VOICE", "HARMONY", "MOD", "GLOBAL" };
        const char* tips[kPages] = { "Key, scale, the correction controls, scale notes and micro-tuning",
                                     "Character, throat, grit, breath, transpose and the added vibrato",
                                     "Three harmony voices, the double and their ensemble",
                                     "LFOs, envelope, drift and the modulation matrix",
                                     "Levels, latency, tempo and seed" };
        for (int i = 0; i < kPages; ++i)
        {
            tabs[i].setButtonText (names[i]);
            tabs[i].setClickingTogglesState (false);
            tabs[i].setMouseCursor (juce::MouseCursor::PointingHandCursor);
            tabs[i].onClick = [this, i] { showPage (i); };
            tabs[i].setTooltip (tips[i]);
            addAndMakeVisible (tabs[i]);
        }
        field.setCompact (true);
        snap->setCompact (true);
        hold->setCompact (true);
        for (auto* c : std::initializer_list<juce::Component*> { &tunePage, &voicePage, &harmonyPage, &modPage, &globalPage, &field,
                                                                 snap.get(), hold.get(), &macros })
            addAndMakeVisible (c);
        showPage (0);
    }

    void showPage (int page)
    {
        current = juce::jlimit (0, kPages - 1, page);
        juce::Component* pages[kPages] = { &tunePage, &voicePage, &harmonyPage, &modPage, &globalPage };
        for (int i = 0; i < kPages; ++i)
        {
            pages[i]->setVisible (i == current);
            tabs[i].setToggleState (i == current, juce::dontSendNotification);
        }
    }

    void tick()
    {
        switch (current)
        {
            case 0: tunePage.tick(); break;
            case 1: voicePage.tick(); break;
            case 2: harmonyPage.tick(); break;
            case 3: modPage.tick(); break;
            default: globalPage.tick(); break;
        }
        field.tick();
        snap->tick();
        hold->tick();
        macros.tick();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, sidePanel.toFloat(), theme::gold);
        theme::legend (g, "Macros", macroTitle.toFloat(), theme::text, 12.0f, juce::Justification::centredLeft, true, 0.24f);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        sidePanel = r.removeFromRight (300);
        r.removeFromRight (10);
        auto tabRow = r.removeFromTop (34);
        for (auto& t : tabs) t.setBounds (tabRow.removeFromLeft (112).reduced (2, 2));
        r.removeFromTop (8);
        for (auto* pg : std::initializer_list<juce::Component*> { &tunePage, &voicePage, &harmonyPage, &modPage, &globalPage })
            pg->setBounds (r);

        auto s = sidePanel.reduced (12, 12);
        field.setBounds (s.removeFromTop (250));
        s.removeFromTop (8);
        auto perf = s.removeFromTop (52);
        snap->setBounds (perf.removeFromLeft (perf.getWidth() / 2).reduced (2, 0));
        hold->setBounds (perf.reduced (2, 0));
        s.removeFromTop (8);
        macroTitle = s.removeFromTop (20).withTrimmedLeft (6);
        macros.setBounds (s);
    }

private:
    juce::TextButton tabs[kPages];
    TunePage tunePage;
    VoicePage voicePage;
    HarmonyPage harmonyPage;
    ModPage modPage;
    GlobalPage globalPage;
    PitchDisplay field;
    std::unique_ptr<PerformButton> snap, hold;
    MacroBank macros;
    juce::Rectangle<int> sidePanel, macroTitle;
    int current = 0;
};

} // namespace lodestone
