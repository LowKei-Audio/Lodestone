#pragma once
// Lodestone-specific controls built on Controls.h:
//   SyncedRate     - a Rate knob that becomes a Division menu when Sync is on
//   PerformButton  - SNAP / HOLD: MOMENTARY or LATCH (toggle), lit while engaged
//   PerformModeSwitch - the MOMENT | LATCH selector under a PerformButton
//   MacroBank      - the four macros, with how many targets each one moves
//   StageSwitches  - one-click power for the sections (correction, voices)
//   NoteKeyboard   - one octave: lit keys are the target notes; click a key to
//                    add or remove it (switches the scale to Custom)
//   KeyLearner     - LEARN: listens to the performance and sets Key and Scale

#include "Controls.h"

namespace lodestone
{

// ---------------------------------------------------------------------------
class SyncedRate : public juce::Component
{
public:
    SyncedRate (LodestoneProcessor& p, P rate, P sync, P division, juce::Colour accent, juce::String legend = {})
        : knob (p, rate, accent, Knob::Size::medium, legend), div (p.apvts, division, accent, legend.isNotEmpty() ? legend : juce::String ("Rate"))
    {
        addAndMakeVisible (knob);
        addChildComponent (div);
        auto* sp = p.apvts.getParameter (def (sync).id);
        attachment = std::make_unique<juce::ParameterAttachment> (*sp, [this] (float v)
        {
            synced = v > 0.5f;
            knob.setVisible (! synced);
            div.setVisible (synced);
        });
        attachment->sendInitialUpdate();
    }
    void tick() { knob.tick(); }
    void resized() override
    {
        knob.setBounds (getLocalBounds());
        div.setBounds (getLocalBounds().withSizeKeepingCentre (getWidth(), juce::jmin (getHeight(), 50)));
    }

private:
    Knob knob;
    ChoiceBox div;
    bool synced = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// ---------------------------------------------------------------------------
class PerformButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    PerformButton (LodestoneProcessor& p, P param, juce::String title_, juce::String latchProperty, juce::Colour accentColour,
                   std::function<float()> rampSource, juce::String holdHint, juce::String tip)
        : proc (p), title (std::move (title_)), latchProp (std::move (latchProperty)), accent (accentColour),
          ramp (std::move (rampSource)), hint (std::move (holdHint))
    {
        auto* prm = p.apvts.getParameter (def (param).id);
        attachment = std::make_unique<juce::ParameterAttachment> (*prm, [this] (float v) { engaged = v > 0.5f; repaint(); });
        attachment->sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (tip);
    }

    bool isLatch() const { return (bool) proc.apvts.state.getProperty (latchProp, false); }
    void setLatch (bool l) { proc.apvts.state.setProperty (latchProp, l, nullptr); repaint(); }
    void setCompact (bool c) { compact = c; repaint(); }

    void tick()
    {
        const float r = ramp ? ramp() : (engaged ? 1.0f : 0.0f);
        if (std::abs (r - level) > 0.002f || engaged) { level = r; pulse += 0.12f; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (3.0f);
        const float glowAmt = level * (0.6f + 0.4f * std::sin (pulse) * (engaged ? 1.0f : 0.0f));
        theme::glow (g, r.getCentre(), r.getWidth() * 0.6f, accent, 0.08f + 0.4f * glowAmt);

        g.setColour (theme::screen);
        g.fillRoundedRectangle (r, 10.0f);
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip;
            clip.addRoundedRectangle (r, 10.0f);
            g.reduceClipRegion (clip);
            auto fill = r.withTop (r.getBottom() - r.getHeight() * level);
            juce::ColourGradient grad (accent, fill.getX(), fill.getY(), accent.darker (1.2f), fill.getX(), r.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRect (fill);
        }
        g.setColour (engaged ? accent : theme::edge);
        g.drawRoundedRectangle (r, 10.0f, engaged ? 2.0f : 1.2f);

        const bool lit = level > 0.55f;
        auto t = r.reduced (8.0f);
        if (compact)
        {
            theme::legend (g, title, t, lit ? juce::Colour (0xff1a1203) : theme::text, juce::jmin (17.0f, r.getHeight() * 0.38f),
                           juce::Justification::centred, true, 0.22f);
            return;
        }
        theme::legend (g, title, t.removeFromTop (t.getHeight() * 0.6f), lit ? juce::Colour (0xff1a1203) : theme::text,
                       juce::jmin (22.0f, r.getHeight() * 0.24f), juce::Justification::centredBottom, true, 0.24f);
        theme::legend (g, isLatch() ? "Latch  -  click" : hint, t, lit ? juce::Colour (0xff1a1203) : theme::textDim, 10.5f,
                       juce::Justification::centredTop);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        if (isLatch()) attachment->setValueAsCompleteGesture (engaged ? 0.0f : 1.0f);
        else           attachment->setValueAsCompleteGesture (1.0f);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() && ! isLatch()) attachment->setValueAsCompleteGesture (0.0f);
    }

    std::function<void()> onModeChange;

private:
    void showMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Momentary (while held)", true, ! isLatch());
        m.addItem (2, "Latch (toggle)", true, isLatch());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            if (r == 1) setLatch (false);
            if (r == 2) setLatch (true);
            if (r > 0 && onModeChange) onModeChange();
        });
    }

    LodestoneProcessor& proc;
    juce::String title, latchProp;
    juce::Colour accent;
    std::function<float()> ramp;
    juce::String hint;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    bool engaged = false, compact = false;
    float level = 0.0f, pulse = 0.0f;
};

class PerformModeSwitch : public juce::Component
{
public:
    explicit PerformModeSwitch (PerformButton& b) : button (b)
    {
        for (auto* t : { &hold, &latch }) { addAndMakeVisible (t); t->setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        hold.onClick = [this] { button.setLatch (false); refresh(); };
        latch.onClick = [this] { button.setLatch (true); refresh(); };
        hold.setTooltip ("Engaged only while the button is held");
        latch.setTooltip ("Toggles on and off with each click");
        button.onModeChange = [this] { refresh(); };
        refresh();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        hold.setBounds (r.removeFromLeft (r.getWidth() / 2).reduced (2, 0));
        latch.setBounds (r.reduced (2, 0));
    }
    void refresh()
    {
        hold.setToggleState (! button.isLatch(), juce::dontSendNotification);
        latch.setToggleState (button.isLatch(), juce::dontSendNotification);
    }

private:
    PerformButton& button;
    juce::TextButton hold { "MOMENT" }, latch { "LATCH" };
};

// ---------------------------------------------------------------------------
class MacroBank : public juce::Component
{
public:
    MacroBank (LodestoneProcessor& p, Knob::Size size) : proc (p)
    {
        for (int k = 0; k < kNumMacros; ++k)
        {
            knobs.push_back (std::make_unique<Knob> (p, (P) (idx (P::macro1) + k), theme::gold, size, kMacroNames[k]));
            addAndMakeVisible (*knobs.back());
        }
    }
    void tick()
    {
        for (auto& k : knobs) k->tick();
        for (int k = 0; k < kNumMacros; ++k)
        {
            int n = 0;
            for (int s = 0; s < kMatrixSlots; ++s)
            {
                const auto sl = proc.getSlot (s);
                if (sl.isActive() && (int) sl.src == (int) Src::macro1 + k) ++n;
            }
            if (n != counts[(size_t) k]) { counts[(size_t) k] = n; repaint(); }
        }
    }
    void paint (juce::Graphics& g) override
    {
        for (int k = 0; k < kNumMacros; ++k)
        {
            auto r = knobs[(size_t) k]->getBounds().toFloat();
            g.setColour (theme::textFaint);
            g.setFont (theme::font (10.5f));
            const int n = counts[(size_t) k];
            g.drawText (n == 0 ? juce::String ("not assigned") : juce::String (n) + (n == 1 ? " target" : " targets"),
                        r.withY (r.getBottom()).withHeight (14.0f), juce::Justification::centred, false);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        const int cw = r.getWidth() / 2, rh = r.getHeight() / 2;
        for (int k = 0; k < kNumMacros; ++k)
            knobs[(size_t) k]->setBounds (juce::Rectangle<int> (r.getX() + (k % 2) * cw, r.getY() + (k / 2) * rh, cw, rh - 14).reduced (6, 2));
    }

private:
    LodestoneProcessor& proc;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::array<int, kNumMacros> counts { -1, -1, -1, -1 };
};

// ---------------------------------------------------------------------------
class StageSwitches : public juce::Component
{
public:
    explicit StageSwitches (LodestoneProcessor& p, int columns = 2) : cols (columns)
    {
        for (int s = 0; s < kNumStages; ++s)
        {
            leds.push_back (std::make_unique<LedButton> (p.apvts, kStages[s].on, theme::stage (s), false, kStages[s].name));
            leds.back()->setTooltip (juce::String (kStages[s].name) + ": " + kStages[s].blurb);
            addAndMakeVisible (*leds.back());
        }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        const int rows = (kNumStages + cols - 1) / cols;
        const int rh = r.getHeight() / rows, cw = r.getWidth() / cols;
        for (int s = 0; s < kNumStages; ++s)
            leds[(size_t) s]->setBounds (juce::Rectangle<int> (r.getX() + (s % cols) * cw, r.getY() + (s / cols) * rh, cw, rh).reduced (4, 3));
    }

private:
    int cols;
    std::vector<std::unique_ptr<LedButton>> leds;
};

// ---------------------------------------------------------------------------
class NoteKeyboard : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit NoteKeyboard (LodestoneProcessor& p) : proc (p)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Lit keys are the target notes. Click a key to add or remove it (the Scale switches to Custom). "
                    "A ring marks the note being sung; a dot marks a held MIDI note.");
    }

    void tick()
    {
        const int allowed = proc.uiAllowedMask.load(), heldM = proc.uiHeldMask.load();
        const int target = proc.uiVoiced.load() ? ((proc.uiTargetNote.load() % 12) + 12) % 12 : -1;
        int centsHash = 0;
        for (int i = 0; i < 12; ++i)
            centsHash = centsHash * 31 + juce::roundToInt (proc.apvts.getRawParameterValue (def (centsParam (i)).id)->load());
        if (allowed != lastAllowed || heldM != lastHeld || target != lastTarget || centsHash != lastCents)
        {
            lastAllowed = allowed; lastHeld = heldM; lastTarget = target; lastCents = centsHash;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::well);
        g.fillRoundedRectangle (r, 6.0f);
        const float ww = r.getWidth() / 7.0f;
        static constexpr int whites[7] = { 0, 2, 4, 5, 7, 9, 11 };
        for (int k = 0; k < 7; ++k) drawKey (g, whites[k], whiteRect (k, r, ww), false);
        for (int pc : { 1, 3, 6, 8, 10 }) drawKey (g, pc, blackRect (pc, r, ww), true);
        g.setColour (theme::edge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int pc = keyAt (e.position);
        if (pc >= 0) proc.toggleNote (pc);
    }

private:
    static juce::Rectangle<float> whiteRect (int k, juce::Rectangle<float> r, float ww)
    {
        return { r.getX() + ww * (float) k, r.getY(), ww, r.getHeight() };
    }
    static juce::Rectangle<float> blackRect (int pc, juce::Rectangle<float> r, float ww)
    {
        // Centred on the line between two white keys.
        static constexpr float pos[12] = { 0, 1, 0, 2, 0, 0, 4, 0, 5, 0, 6, 0 };
        const float x = r.getX() + ww * pos[pc] - ww * 0.32f;
        return { x, r.getY(), ww * 0.64f, r.getHeight() * 0.6f };
    }
    int keyAt (juce::Point<float> p) const
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float ww = r.getWidth() / 7.0f;
        for (int pc : { 1, 3, 6, 8, 10 }) if (blackRect (pc, r, ww).contains (p)) return pc;
        static constexpr int whites[7] = { 0, 2, 4, 5, 7, 9, 11 };
        for (int k = 0; k < 7; ++k) if (whiteRect (k, r, ww).contains (p)) return whites[k];
        return -1;
    }

    void drawKey (juce::Graphics& g, int pc, juce::Rectangle<float> k, bool black)
    {
        const bool lit = (lastAllowed >> pc) & 1;
        const bool tgt = pc == lastTarget, heldNote = (lastHeld >> pc) & 1;
        auto body = k.reduced (black ? 0.0f : 1.5f, 0.0f).withTrimmedBottom (black ? 0.0f : 1.0f);
        juce::Colour fill = black ? juce::Colour (0xff0c1116) : juce::Colour (0xff1d2630);
        if (lit) fill = black ? theme::gold.darker (0.35f) : theme::gold.withMultipliedBrightness (0.95f);
        g.setColour (fill);
        g.fillRoundedRectangle (body, 3.5f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (body, 3.5f, 1.0f);
        if (tgt)
        {
            theme::glow (g, { body.getCentreX(), body.getBottom() - body.getWidth() * 0.5f }, body.getWidth(), theme::gold, 0.5f);
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (body.reduced (2.0f), 3.0f, 2.2f);
        }
        if (heldNote)
        {
            const float d = juce::jmin (10.0f, body.getWidth() * 0.35f);
            g.setColour (theme::hold);
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ body.getCentreX(), body.getY() + d }));
        }
        const auto name = Choices::notes[pc];
        const float cents = proc.apvts.getRawParameterValue (def (centsParam (pc)).id)->load();
        const auto textCol = lit ? juce::Colour (0xff1a1203) : theme::textDim;
        if (! black)
        {
            theme::legend (g, name, body.removeFromBottom (18.0f), textCol, 11.0f);
            if (std::abs (cents) >= 0.5f)
                theme::legend (g, (cents > 0 ? "+" : "") + juce::String (juce::roundToInt (cents)), body.removeFromBottom (14.0f),
                               textCol.withAlpha (0.8f), 9.0f);
        }
        else if (std::abs (cents) >= 0.5f)
            theme::legend (g, (cents > 0 ? "+" : "") + juce::String (juce::roundToInt (cents)), body.removeFromBottom (14.0f),
                           lit ? textCol : theme::textFaint, 8.5f);
    }

    LodestoneProcessor& proc;
    int lastAllowed = -1, lastHeld = -1, lastTarget = -2, lastCents = 0;
};

// ---------------------------------------------------------------------------
// LEARN: listens to what is sung or played and sets Key and Scale.
class KeyLearner : public juce::Component
{
public:
    explicit KeyLearner (LodestoneProcessor& p) : proc (p)
    {
        addAndMakeVisible (button);
        button.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        button.setClickingTogglesState (false);
        button.setColour (juce::TextButton::buttonOnColourId, theme::gold);
        button.setTooltip ("LEARN: play the song (or sing over it) and Lodestone finds the key. With the beat on the sidechain "
                           "it listens to the beat instead (faster and surer). Click again to set it now; it also sets itself once it is sure.");
        button.onClick = [this] { clicked(); };
        status = "Finds the key by listening";
    }

    void tick()
    {
        button.setToggleState (proc.keyLearning, juce::dontSendNotification);
        button.setButtonText (proc.keyLearning ? "SET KEY" : "LEARN");
        if (! proc.keyLearning) return;
        const auto hist = proc.getKeyHistogram();
        float total = 0.0f;
        for (float v : hist) total += v;
        juce::String s;
        if (total < 1.0f) s = "Listening...";
        else
        {
            const auto k = dsp::findKey (hist);
            s = juce::String (proc.keyFromBeat() ? "Hearing the beat: " : "Hearing ")
                + juce::String (dsp::noteName (k.key)) + (k.minor ? " minor" : " major") + "  "
                + juce::String (juce::roundToInt (k.confidence * 100.0f)) + "%";
            if (total >= 15.0f && k.confidence >= 0.35f) { finish (true); return; }
        }
        setStatus (s);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (proc.keyLearning ? theme::gold : theme::textDim);
        g.setFont (theme::font (12.5f));
        g.drawFittedText (status, textArea, juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        button.setBounds (r.removeFromLeft (84).withSizeKeepingCentre (84, juce::jmin (30, r.getHeight())));
        r.removeFromLeft (8);
        textArea = r;
    }

private:
    void clicked()
    {
        if (! proc.keyLearning)
        {
            proc.clearKeyHistogram();
            proc.keyLearning = true;
            setStatus ("Listening...");
            return;
        }
        float total = 0.0f;
        for (float v : proc.getKeyHistogram()) total += v;
        finish (total >= 2.0f);
    }

    void finish (bool apply)
    {
        proc.keyLearning = false;
        if (! apply) { setStatus ("Not enough heard yet: try again with a few bars"); return; }
        const auto k = dsp::findKey (proc.getKeyHistogram());
        proc.applyKey (k.key, k.minor);
        setStatus ("Set to " + juce::String (dsp::noteName (k.key)) + (k.minor ? " minor" : " major"));
    }

    void setStatus (const juce::String& s) { if (s != status) { status = s; repaint(); } }

    LodestoneProcessor& proc;
    juce::TextButton button { "LEARN" };
    juce::String status;
    juce::Rectangle<int> textArea;
};

} // namespace lodestone
