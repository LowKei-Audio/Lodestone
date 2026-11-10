#pragma once
// Preset browsing (factory + user), saving, A/B comparison and the
// "Launch" randomizer. Message-thread only (called from the UI).
//
// A preset is a complete patch: every parameter plus the mod matrix. The
// latency setting is not part of a preset (it belongs to the session).

#include "FactoryPresets.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace lodestone
{

class LodestoneProcessor;

class PresetManager : public juce::ChangeBroadcaster
{
public:
    struct Entry
    {
        juce::String name, category, description;
        int factoryIndex = -1;   // >= 0 for factory presets
        juce::File file;         // for user presets
    };

    static constexpr const char* kExtension = ".lodestone";

    explicit PresetManager (LodestoneProcessor& p);

    const juce::Array<Entry>& getEntries() const noexcept { return entries; }
    int getCurrentIndex() const noexcept { return currentIndex; }
    juce::String getCurrentName() const;
    juce::String getCurrentDescription() const;
    void load (int index);
    void next()     { step (+1); }
    void previous() { step (-1); }
    void refreshUserPresets();

    static juce::File getUserPresetFolder();
    bool saveUserPreset (const juce::String& name);

    // "Spin": a random but musical patch. Same seed = same patch.
    void randomize (std::uint32_t seed);

    bool isBActive() const noexcept { return activeSlot == 1; }
    void selectSlot (int slot);
    void copyActiveToOther();

    void restoreSelectionFromState();

    // Builds the full patch tree for a factory preset (also used by tests).
    juce::ValueTree buildFactoryPatch (const FactoryPreset& fp) const;

private:
    void step (int delta);
    void applyTree (const juce::ValueTree& tree, const juce::String& name);

    LodestoneProcessor& proc;
    juce::Array<Entry> entries;
    int currentIndex = 0;
    int activeSlot = 0;
    juce::ValueTree slots[2];
};

} // namespace lodestone
