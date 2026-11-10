#pragma once
// Lodestone colour + type tokens. Always dark: a magnetite chassis (blue-black
// iron) with a gold "compass needle" accent, the LowKei Audio family's
// hardware look. Each section has its own accent so a control's home reads at
// a glance. Text is cool off-white on near-black, well above 7:1 contrast.

#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

namespace lodestone::theme
{
    inline const juce::Colour background  { 0xff06090c };
    inline const juce::Colour chassisTop  { 0xff141b22 };
    inline const juce::Colour chassisBot  { 0xff060a0d };
    inline const juce::Colour panel       { 0xff11171d };
    inline const juce::Colour panelHi     { 0xff1a222b };
    inline const juce::Colour edge        { 0xff2e3a46 };
    inline const juce::Colour well        { 0xff05080b };
    inline const juce::Colour screen      { 0xff03060a };

    inline const juce::Colour text        { 0xfff1f5f8 };   // primary
    inline const juce::Colour textDim     { 0xffbac7d2 };   // secondary labels (~9:1 on panel)
    inline const juce::Colour textFaint   { 0xff8595a3 };   // hints only

    inline const juce::Colour gold        { 0xfff5c04a };   // brand accent: the needle, the tuned voice
    inline const juce::Colour goldDeep    { 0xff5c410c };
    inline const juce::Colour field       { 0xff6fb7ff };   // the voice as sung (input trace)
    inline const juce::Colour snap        { 0xffff5d73 };   // SNAP
    inline const juce::Colour hold        { 0xff6ff0c4 };   // HOLD
    inline const juce::Colour ironGlow    { 0xff1b3550 };   // faint blue haze

    // Section accents (same order as kStages: Correction .. Double).
    inline juce::Colour stage (int s)
    {
        static const juce::Colour c[6] = {
            juce::Colour (0xfff5c04a),   // Correction - gold
            juce::Colour (0xffff9d5c),   // Vibrato    - copper
            juce::Colour (0xff6ff0c4),   // Harmony 1  - mint
            juce::Colour (0xff6fb7ff),   // Harmony 2  - field blue
            juce::Colour (0xffb790ff),   // Harmony 3  - violet
            juce::Colour (0xffff86bd),   // Double     - rose
        };
        return c[juce::jlimit (0, 5, s)];
    }
    inline const juce::Colour global { 0xffe6ecf1 };
    inline const juce::Colour mod    { 0xfff5c04a };

    // Embedded Barlow Semi Condensed (SIL OFL): identical metrics everywhere.
    inline juce::Typeface::Ptr typeface (bool bold)
    {
        static const juce::Typeface::Ptr medium = juce::Typeface::createSystemTypefaceFor (
            LodestoneAssets::BarlowSemiCondensedMedium_ttf, LodestoneAssets::BarlowSemiCondensedMedium_ttfSize);
        static const juce::Typeface::Ptr semiBold = juce::Typeface::createSystemTypefaceFor (
            LodestoneAssets::BarlowSemiCondensedSemiBold_ttf, LodestoneAssets::BarlowSemiCondensedSemiBold_ttfSize);
        return bold ? semiBold : medium;
    }

    inline juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (typeface (bold)).withHeight (size * 1.12f));
    }

    // Letter-spaced caps for hardware-style legends.
    inline void legend (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Colour c, float size,
                        juce::Justification j = juce::Justification::centred, bool bold = true, float tracking = 0.12f)
    {
        g.setColour (c);
        g.setFont (font (size, bold).withExtraKerningFactor (tracking));
        g.drawText (s.toUpperCase(), r, j, false);
    }

    // Soft radial glow (used behind lit elements).
    inline void glow (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour col, float alpha)
    {
        juce::ColourGradient grad (col.withAlpha (alpha), c.x, c.y, col.withAlpha (0.0f), c.x + radius, c.y, true);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
    }

    // "A4", "C#3" for a MIDI note.
    inline juce::String noteName (int midi)
    {
        static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (n[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
    }
}
