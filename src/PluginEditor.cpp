#include "PluginEditor.h"

namespace lodestone
{
using namespace juce;

static constexpr const char* kUiWidthProp = "uiWidth";
static constexpr const char* kUiViewProp = "uiView";

// ---------------------------------------------------------------------------
LodestoneEditor::Content::Content (LodestoneProcessor& p)
    : proc (p), presetBar (p), easy (p), proView (p), hint (p)
{
    for (auto* c : std::initializer_list<Component*> { &presetBar, &easyTab, &proTab, &undoBtn, &redoBtn, &easy, &proView, &hint })
        addAndMakeVisible (c);
    for (auto* b : { &easyTab, &proTab, &undoBtn, &redoBtn }) b->setMouseCursor (MouseCursor::PointingHandCursor);

    easyTab.onClick = [this] { setView (false); };
    proTab.onClick = [this] { setView (true); };
    undoBtn.onClick = [this] { proc.undo.undo(); };
    redoBtn.onClick = [this] { proc.undo.redo(); };
    easyTab.setTooltip ("Easy view: key, the field, SNAP, HOLD, the main tuning knobs and the macros. Every preset is playable here.");
    proTab.setTooltip ("Pro view: every control, harmonies, the modulation matrix and global settings");
    undoBtn.setTooltip ("Undo");
    redoBtn.setTooltip ("Redo");

    setSize (kDesignW, kDesignH);
    setView ((bool) proc.apvts.state.getProperty (kUiViewProp, false));
}

void LodestoneEditor::Content::setView (bool shouldBePro)
{
    pro = shouldBePro;
    proc.apvts.state.setProperty (kUiViewProp, pro, nullptr);
    easy.setVisible (! pro);
    proView.setVisible (pro);
    easyTab.setToggleState (! pro, dontSendNotification);
    proTab.setToggleState (pro, dontSendNotification);
}

void LodestoneEditor::Content::showProPage (int page)
{
    setView (true);
    proView.showPage (page);
}

void LodestoneEditor::Content::tick()
{
    if (pro) proView.tick(); else easy.tick();
    hint.tick();
    undoBtn.setEnabled (proc.undo.canUndo());
    redoBtn.setEnabled (proc.undo.canRedo());
}

void LodestoneEditor::Content::paint (Graphics& g)
{
    // Chassis: dark iron gradient, fine grain, a faint blue field haze from below.
    ColourGradient chassis (theme::chassisTop, 0, 0, theme::chassisBot, 0, (float) getHeight(), false);
    g.setGradientFill (chassis);
    g.fillAll();
    ColourGradient haze (theme::ironGlow.withAlpha (0.45f), (float) getWidth() * 0.5f, (float) getHeight() * 1.1f,
                         theme::ironGlow.withAlpha (0.0f), (float) getWidth() * 0.5f, (float) getHeight() * 0.35f, true);
    g.setGradientFill (haze);
    g.fillAll();
    g.setTiledImageFill (grainTexture(), 0, 0, 0.8f);
    g.fillAll();

    // Wordmark with a compass needle: gold north, iron south.
    auto logo = Rectangle<float> (24.0f, 12.0f, 250.0f, 42.0f);
    theme::glow (g, { logo.getX() + 100.0f, logo.getCentreY() }, 120.0f, theme::gold, 0.10f);
    {
        const Point<float> c (logo.getX() + 11.0f, logo.getY() + 14.0f);
        Path north, south;
        north.addTriangle (c.x - 5.0f, c.y, c.x + 5.0f, c.y, c.x, c.y - 13.0f);
        south.addTriangle (c.x - 5.0f, c.y, c.x + 5.0f, c.y, c.x, c.y + 13.0f);
        const auto rot = AffineTransform::rotation (0.5f, c.x, c.y);
        theme::glow (g, c, 16.0f, theme::gold, 0.35f);
        g.setColour (theme::gold);
        g.fillPath (north, rot);
        g.setColour (Colour (0xff6b7a88));
        g.fillPath (south, rot);
        g.setColour (theme::text);
        g.fillEllipse (Rectangle<float> (3.5f, 3.5f).withCentre (c));
    }
    auto words = logo.withTrimmedLeft (34.0f);
    theme::legend (g, "Lodestone", words.removeFromTop (28.0f), theme::text, 26.0f, Justification::centredLeft, true, 0.30f);
    theme::legend (g, "LowKei Audio  -  Every note finds home", words, theme::gold, 9.5f, Justification::centredLeft, true, 0.14f);

    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawHorizontalLine (66, 16.0f, (float) getWidth() - 16.0f);
    g.setColour (theme::gold.withAlpha (0.16f));
    g.drawHorizontalLine (67, 16.0f, (float) getWidth() - 16.0f);

    g.setColour (theme::textFaint);
    g.setFont (theme::font (11.0f));
    g.drawText ("v" JucePlugin_VersionString, Rectangle<int> (getWidth() - 90, getHeight() - 16, 80, 14), Justification::centredRight, false);
}

void LodestoneEditor::Content::resized()
{
    auto top = Rectangle<int> (0, 14, getWidth(), 40).reduced (20, 0);
    top.removeFromLeft (270);
    auto right = top.removeFromRight (330);
    redoBtn.setBounds (right.removeFromRight (62).withSizeKeepingCentre (62, 32));
    right.removeFromRight (6);
    undoBtn.setBounds (right.removeFromRight (62).withSizeKeepingCentre (62, 32));
    right.removeFromRight (18);
    proTab.setBounds (right.removeFromRight (78).withSizeKeepingCentre (78, 32));
    right.removeFromRight (4);
    easyTab.setBounds (right.removeFromRight (78).withSizeKeepingCentre (78, 32));
    presetBar.setBounds (top.reduced (10, 0));

    auto main = Rectangle<int> (20, 80, getWidth() - 40, getHeight() - 80 - 66);
    easy.setBounds (main);
    proView.setBounds (main);
    hint.setBounds (20, getHeight() - 58, getWidth() - 40, 42);
}

// ---------------------------------------------------------------------------
LodestoneEditor::LodestoneEditor (LodestoneProcessor& p)
    : AudioProcessorEditor (p), proc (p), content (p)
{
    // Read the saved size first: setting the resize limits below resizes the
    // editor to its minimum, and resized() would overwrite the saved width.
    const int savedW = proc.apvts.state.getProperty (kUiWidthProp, kDesignW);

    setLookAndFeel (&lnf);
    content.setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    setResizable (true, true);
    setResizeLimits (kDesignW * 7 / 10, kDesignH * 7 / 10, kDesignW * 2, kDesignH * 2);
    getConstrainer()->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);

    const int w = jlimit (kDesignW * 7 / 10, kDesignW * 2, savedW);
    setSize (w, w * kDesignH / kDesignW);
    startTimerHz (30);
}

LodestoneEditor::~LodestoneEditor()
{
    stopTimer();
    content.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void LodestoneEditor::paint (Graphics& g) { g.fillAll (theme::background); }

void LodestoneEditor::resized()
{
    const float scale = (float) getWidth() / (float) kDesignW;
    content.setTransform (AffineTransform::scale (scale));
    content.setBounds (0, 0, kDesignW, kDesignH);
    proc.apvts.state.setProperty (kUiWidthProp, getWidth(), nullptr);
}

} // namespace lodestone
