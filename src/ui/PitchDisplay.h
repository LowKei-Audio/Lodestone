#pragma once
// The field: Lodestone's live display.
//
//   Lanes      one per semitone, scrolling to follow the voice. Target notes
//              (the scale) are lit lanes; the note being sung glows.
//   Traces     the voice as sung (blue) and as tuned (gold), last ~2.5 s.
//              Where the gold line leaves the blue one, Lodestone is pulling.
//   Head       at the right edge: the pull from sung to tuned, plus a dot for
//              each harmony voice.
//   Readout    the target note, how far off it was sung and where it landed,
//              with a tuner needle (full size only).
//   Chips      SNAP and HOLD when engaged; the chord Chord Lock is using and
//              the Motion step playing (v2). Chord tones get a cyan lane line.

#include "Controls.h"

namespace lodestone
{

class PitchDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PitchDisplay (LodestoneProcessor& p) : proc (p)
    {
        setTooltip ("Blue: the voice as sung. Gold: the voice as tuned. Lit lanes are the target notes.");
        in.assign ((size_t) LodestoneProcessor::kHist, -1.0f);
        out.assign ((size_t) LodestoneProcessor::kHist, -1.0f);
    }

    void setCompact (bool c) { compact = c; repaint(); }

    void tick()
    {
        const int count = proc.histCount.load (std::memory_order_acquire);
        visible = juce::jlimit (64, LodestoneProcessor::kHist - 64, (int) (proc.histRate() * 2.5));
        for (int k = 0; k < visible; ++k)
        {
            const int h = count - visible + k;
            const auto slot = (size_t) (h & (LodestoneProcessor::kHist - 1));
            in[(size_t) k] = h >= 0 ? proc.histIn[slot].load (std::memory_order_relaxed) : -1.0f;
            out[(size_t) k] = h >= 0 ? proc.histOut[slot].load (std::memory_order_relaxed) : -1.0f;
        }

        voiced = proc.uiVoiced.load();
        targetPitch = proc.uiTargetPitch.load();
        targetNote = proc.uiTargetNote.load();
        inPitch = proc.uiInPitch.load();
        outPitch = proc.uiOutPitch.load();
        flexW = proc.uiFlex.load();
        allowed = proc.uiAllowedMask.load();
        chordMask = proc.uiChordInUse.load();
        chordRoot = proc.uiChordRoot.load();
        chordShape = proc.uiChordShape.load();
        motionStep = proc.uiMotionStep.load();
        motionLen = juce::roundToInt (proc.apvts.getRawParameterValue ("motion_length")->load());
        snap = proc.apvts.getRawParameterValue ("snap")->load() > 0.5f;
        hold = proc.apvts.getRawParameterValue ("hold")->load() > 0.5f;
        for (int h = 0; h < dsp::kNumHarmonies; ++h)
        {
            harmPitch[(size_t) h] = proc.uiHarmPitch[(size_t) h].load();
            harmActive[(size_t) h] = proc.uiHarmActive[(size_t) h].load() && voiced;
        }

        // The view follows the voice smoothly; a big jump moves it quickly.
        if (voiced && targetPitch > 0.0f)
        {
            if (! hasCentre) { centre = targetPitch; hasCentre = true; }
            const float d = targetPitch - centre;
            centre += d * (std::abs (d) > span() * 0.7f ? 0.5f : 0.12f);
        }
        const int chips = chordMask * 131 + chordRoot * 17 + chordShape * 7 + motionStep * 3 + motionLen;
        if (count != lastCount || voiced != lastVoiced || chips != lastChips) repaint();
        lastChips = chips;
        lastCount = count;
        lastVoiced = voiced;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::screen);
        g.fillRoundedRectangle (r, 10.0f);
        {
            juce::ColourGradient haze (theme::ironGlow.withAlpha (0.55f), r.getCentreX(), r.getBottom(),
                                       theme::ironGlow.withAlpha (0.0f), r.getCentreX(), r.getY(), false);
            g.setGradientFill (haze);
            g.fillRoundedRectangle (r, 10.0f);
        }

        juce::Graphics::ScopedSaveState clipState (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, 10.0f);
        g.reduceClipRegion (clip);

        const float gutter = compact ? 30.0f : 40.0f;
        plot = r.withTrimmedLeft (gutter).withTrimmedRight (compact ? 22.0f : 34.0f).reduced (0.0f, 6.0f);
        drawLanes (g, r, gutter);
        drawTraces (g);
        drawHead (g);
        if (compact) drawCompactReadout (g, r);
        else         drawReadout (g, r);
        drawTags (g, r);

        g.setColour (theme::edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    }

private:
    float span() const noexcept { return compact ? 5.5f : 7.5f; }
    float yFor (float midi) const noexcept
    {
        const float lo = centre - span(), hi = centre + span();
        return plot.getBottom() - (midi - lo) / (hi - lo) * plot.getHeight();
    }

    void drawLanes (juce::Graphics& g, juce::Rectangle<float> r, float gutter)
    {
        const float rowH = plot.getHeight() / (2.0f * span());
        const int lo = (int) std::floor (centre - span()) - 1, hi = (int) std::ceil (centre + span()) + 1;
        for (int n = lo; n <= hi; ++n)
        {
            const float y = yFor ((float) n);
            const bool lit = (allowed >> (((n % 12) + 12) % 12)) & 1;
            const bool isTarget = voiced && n == targetNote;
            auto lane = juce::Rectangle<float> (plot.getX(), y - rowH * 0.5f, plot.getWidth() + 30.0f, rowH);
            if (isTarget)
            {
                juce::ColourGradient tg (theme::gold.withAlpha (0.02f), lane.getX(), y, theme::gold.withAlpha (0.22f), lane.getRight(), y, false);
                g.setGradientFill (tg);
                g.fillRect (lane);
            }
            else if (lit)
            {
                g.setColour (theme::gold.withAlpha (0.045f));
                g.fillRect (lane);
            }
            const bool chordTone = (chordMask >> (((n % 12) + 12) % 12)) & 1;
            g.setColour (chordTone && ! isTarget ? theme::beat.withAlpha (0.45f)
                         : lit ? theme::gold.withAlpha (isTarget ? 0.75f : 0.28f) : juce::Colours::white.withAlpha (0.045f));
            g.drawHorizontalLine ((int) std::lround (y), plot.getX(), lane.getRight());

            // Gutter labels: every target note, and every C for orientation.
            const bool isC = ((n % 12) + 12) % 12 == 0;
            if (lit || isC)
            {
                auto lab = juce::Rectangle<float> (r.getX() + 4.0f, y - 8.0f, gutter - 8.0f, 16.0f);
                const auto col = isTarget ? theme::gold : (lit ? theme::textDim : theme::textFaint);
                g.setColour (col);
                g.setFont (theme::font (compact ? 10.0f : 11.5f, isTarget));
                g.drawText (theme::noteName (n), lab, juce::Justification::centredRight, false);
            }
        }
    }

    juce::Path tracePath (const std::vector<float>& v) const
    {
        juce::Path p;
        bool pen = false;
        const float step = plot.getWidth() / (float) (visible - 1);
        for (int k = 0; k < visible; ++k)
        {
            const float m = v[(size_t) k];
            if (m < 0.0f) { pen = false; continue; }
            const juce::Point<float> pt (plot.getX() + step * (float) k, yFor (m));
            if (! pen) { p.startNewSubPath (pt); pen = true; }
            else p.lineTo (pt);
        }
        return p;
    }

    void drawTraces (juce::Graphics& g)
    {
        const auto pin = tracePath (in), pout = tracePath (out);
        g.setColour (theme::field.withAlpha (0.85f));
        g.strokePath (pin, juce::PathStrokeType (compact ? 1.2f : 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::gold.withAlpha (0.16f));
        g.strokePath (pout, juce::PathStrokeType (compact ? 5.0f : 7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::gold);
        g.strokePath (pout, juce::PathStrokeType (compact ? 1.8f : 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawHead (juce::Graphics& g)
    {
        if (! voiced) return;
        const float x = plot.getRight();
        const juce::Point<float> pi (x, yFor (inPitch)), po (x, yFor (outPitch));
        // The pull: a short line from where it was sung to where it lands.
        if (std::abs (pi.y - po.y) > 2.0f)
        {
            g.setColour (theme::gold.withAlpha (0.55f));
            g.drawLine ({ pi, po }, 1.5f);
        }
        g.setColour (theme::field);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pi));
        theme::glow (g, po, compact ? 12.0f : 18.0f, theme::gold, 0.6f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (compact ? 7.0f : 9.0f, compact ? 7.0f : 9.0f).withCentre (po));

        for (int h = 0; h < dsp::kNumHarmonies; ++h)
        {
            if (! harmActive[(size_t) h]) continue;
            const auto c = theme::stage (Stage::harmony1 + h);
            const juce::Point<float> ph (x + (compact ? 10.0f : 16.0f), yFor (harmPitch[(size_t) h]));
            theme::glow (g, ph, 10.0f, c, 0.5f);
            g.setColour (c);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (ph));
        }
    }

    static juce::Colour centsColour (float cents)
    {
        const float a = std::abs (cents);
        return a < 10.0f ? theme::hold : (a < 30.0f ? theme::gold : theme::snap);
    }

    void drawReadout (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto box = juce::Rectangle<float> (r.getRight() - 204.0f, r.getY() + 12.0f, 168.0f, 176.0f);
        g.setColour (theme::screen.withAlpha (0.85f));
        g.fillRoundedRectangle (box, 8.0f);
        g.setColour (theme::edge);
        g.drawRoundedRectangle (box, 8.0f, 1.0f);

        // Tuner needle: -50 .. +50 cents, sung (blue) and tuned (gold).
        auto b = box.reduced (10.0f, 8.0f);
        auto arc = b.removeFromTop (52.0f);
        const auto c = juce::Point<float> (arc.getCentreX(), arc.getBottom());
        const float rad = arc.getHeight() - 4.0f;
        juce::Path track;
        track.addCentredArc (c.x, c.y, rad, rad, 0.0f, -1.15f, 1.15f, true);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (int t = -50; t <= 50; t += 10)
        {
            const float a = 1.15f * (float) t / 50.0f;
            const float len = t == 0 ? 10.0f : 5.0f;
            g.setColour (t == 0 ? theme::gold : juce::Colours::white.withAlpha (0.25f));
            g.drawLine (c.x + (rad - len) * std::sin (a), c.y - (rad - len) * std::cos (a), c.x + rad * std::sin (a), c.y - rad * std::cos (a),
                        t == 0 ? 2.0f : 1.0f);
        }
        if (voiced)
        {
            auto needle = [&] (float cents, juce::Colour col, float w)
            {
                const float a = 1.15f * juce::jlimit (-1.0f, 1.0f, cents / 50.0f);
                g.setColour (col);
                g.drawLine (c.x, c.y, c.x + (rad - 2.0f) * std::sin (a), c.y - (rad - 2.0f) * std::cos (a), w);
            };
            needle (100.0f * (inPitch - targetPitch), theme::field.withAlpha (0.9f), 1.8f);
            needle (100.0f * (outPitch - targetPitch), theme::gold, 2.6f);
        }
        g.setColour (theme::textFaint);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (c));
        b.removeFromTop (4.0f);

        theme::legend (g, "Target", b.removeFromTop (14.0f), theme::textFaint, 9.5f, juce::Justification::centredLeft);
        g.setColour (voiced ? theme::gold : theme::textFaint);
        g.setFont (theme::font (36.0f, true));
        g.drawText (voiced ? theme::noteName (targetNote) : juce::String ("--"), b.removeFromTop (40.0f), juce::Justification::centredLeft, false);

        auto row = [&] (const char* label, float cents)
        {
            auto line = b.removeFromTop (19.0f);
            theme::legend (g, label, line.removeFromLeft (56.0f), theme::textFaint, 9.5f, juce::Justification::centredLeft);
            g.setColour (voiced ? centsColour (cents) : theme::textFaint);
            g.setFont (theme::font (15.0f, true));
            const int ct = juce::roundToInt (cents);
            g.drawText (voiced ? (ct > 0 ? "+" : "") + juce::String (ct) + " ct" : juce::String ("--"), line, juce::Justification::centredLeft, false);
        };
        row ("Sung", 100.0f * (inPitch - targetPitch));
        row ("Tuned", 100.0f * (outPitch - targetPitch));

        // Legend and the Flex state.
        auto foot = r.reduced (48.0f, 10.0f).removeFromBottom (16.0f);
        auto swatch = [&] (juce::Colour col, const char* text)
        {
            auto s = foot.removeFromLeft (16.0f);
            g.setColour (col);
            g.fillRoundedRectangle (s.withSizeKeepingCentre (14.0f, 3.0f), 1.5f);
            theme::legend (g, text, foot.removeFromLeft (70.0f).withTrimmedLeft (5.0f), theme::textDim, 9.5f, juce::Justification::centredLeft);
        };
        swatch (theme::field, "Sung");
        swatch (theme::gold, "Tuned");
        if (voiced && flexW < 0.97f)
            theme::legend (g, flexW < 0.1f ? "Flex: left alone" : "Flex: easing off", foot, theme::field, 9.5f, juce::Justification::centredRight);
    }

    void drawCompactReadout (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto b = juce::Rectangle<float> (r.getRight() - 96.0f, r.getY() + 8.0f, 86.0f, 44.0f);
        g.setColour (theme::screen.withAlpha (0.8f));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (voiced ? theme::gold : theme::textFaint);
        g.setFont (theme::font (22.0f, true));
        g.drawText (voiced ? theme::noteName (targetNote) : juce::String ("--"), b.removeFromTop (26.0f), juce::Justification::centred, false);
        const int c = juce::roundToInt (100.0f * (inPitch - targetPitch));
        g.setColour (voiced ? centsColour ((float) c) : theme::textFaint);
        g.setFont (theme::font (12.0f, true));
        g.drawText (voiced ? "sung " + juce::String (c > 0 ? "+" : "") + juce::String (c) + " ct" : juce::String(), b,
                    juce::Justification::centred, false);
    }

    void drawTags (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto tag = juce::Rectangle<float> (r.getX() + (compact ? 36.0f : 50.0f), r.getY() + 10.0f, 64.0f, 22.0f);
        auto draw = [&] (const char* text, juce::Colour c)
        {
            // Width follows the text (chord names such as "C#maj7" are longer than "Snap").
            juce::GlyphArrangement ga;
            ga.addLineOfText (theme::font (11.0f, true).withExtraKerningFactor (0.12f), juce::String (text).toUpperCase(), 0.0f, 0.0f);
            tag.setWidth (juce::jmax (64.0f, ga.getBoundingBox (0, -1, true).getWidth() + 18.0f));
            theme::glow (g, tag.getCentre(), 40.0f, c, 0.35f);
            g.setColour (c);
            g.fillRoundedRectangle (tag, 5.0f);
            theme::legend (g, text, tag, juce::Colour (0xff12090b), 11.0f);
            tag = tag.translated (tag.getWidth() + 8.0f, 0.0f);
        };
        if (snap) draw ("Snap", theme::snap);
        if (hold) draw ("Hold", theme::hold);
        if (chordMask != 0)
        {
            dsp::Chord c;
            c.root = chordRoot;
            c.shape = juce::jlimit (0, dsp::kNumChordShapes - 1, chordShape);
            char name[16];
            dsp::chordName (c, name, 16);
            draw (name, theme::beat);
        }
        if (motionStep >= 0)
        {
            const auto text = juce::String (motionStep + 1) + "/" + juce::String (juce::jmax (1, motionLen));
            draw (text.toRawUTF8(), theme::stage (Stage::motion));
        }
    }

    LodestoneProcessor& proc;
    bool compact = false, voiced = false, lastVoiced = false, snap = false, hold = false, hasCentre = false;
    std::vector<float> in, out;
    int visible = 1800, lastCount = -1, targetNote = 60, allowed = 0;
    int chordMask = 0, chordRoot = -1, chordShape = 0, motionStep = -1, motionLen = 8, lastChips = 0;
    float centre = 60.0f, targetPitch = 60.0f, inPitch = 0.0f, outPitch = 0.0f, flexW = 1.0f;
    std::array<float, dsp::kNumHarmonies> harmPitch {};
    std::array<bool, dsp::kNumHarmonies> harmActive {};
    juce::Rectangle<float> plot;
};

} // namespace lodestone
