#include "PresetManager.h"
#include "../PluginProcessor.h"

namespace lodestone
{
using namespace juce;

PresetManager::PresetManager (LodestoneProcessor& p) : proc (p)
{
    refreshUserPresets();
}

File PresetManager::getUserPresetFolder()
{
    // Windows: Documents\LowKei Audio\Lodestone\Presets (visible, easy to back up or share)
    return File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("LowKei Audio").getChildFile ("Lodestone").getChildFile ("Presets");
}

void PresetManager::refreshUserPresets()
{
    const auto previous = getCurrentName();
    entries.clear();

    const auto& fps = factoryPresets();
    for (int i = 0; i < (int) fps.size(); ++i)
        entries.add ({ fps[(size_t) i].name, fps[(size_t) i].category, fps[(size_t) i].description, i, {} });

    auto folder = getUserPresetFolder();
    if (folder.isDirectory())
    {
        auto files = folder.findChildFiles (File::findFiles, false, String ("*") + kExtension);
        files.sort();
        for (const auto& f : files)
            entries.add ({ f.getFileNameWithoutExtension(), "User", "Your preset", -1, f });
    }

    for (int k = 0; k < entries.size(); ++k)
        if (entries[k].name == previous) { currentIndex = k; break; }
    sendChangeMessage();
}

String PresetManager::getCurrentName() const
{
    const auto stateName = proc.apvts.state.getProperty (LodestoneProcessor::kPresetNameProp).toString();
    if (stateName.isNotEmpty()) return stateName;
    return isPositiveAndBelow (currentIndex, entries.size()) ? entries[currentIndex].name : String ("Init");
}

String PresetManager::getCurrentDescription() const
{
    if (isPositiveAndBelow (currentIndex, entries.size()) && entries[currentIndex].name == getCurrentName())
        return entries[currentIndex].description;
    return "Your own setting. Turn the macros, or try SNAP and HOLD.";
}

void PresetManager::step (int delta)
{
    if (entries.isEmpty()) return;
    load ((currentIndex + delta + entries.size()) % entries.size());
}

void PresetManager::applyTree (const ValueTree& tree, const String& name)
{
    lowkei::SoundChangeGate::Scope change (proc.soundGate);   // fade, swap every setting at once, fade
    proc.undo.beginNewTransaction ("Load " + name);
    auto t = tree.createCopy();
    t.setProperty (LodestoneProcessor::kPresetNameProp, name, nullptr);
    proc.applyPatchState (t, true);
}

void PresetManager::load (int index)
{
    if (! isPositiveAndBelow (index, entries.size())) return;
    const auto& e = entries.getReference (index);

    if (e.factoryIndex >= 0)
        applyTree (buildFactoryPatch (factoryPresets()[(size_t) e.factoryIndex]), e.name);
    else if (auto xml = parseXML (e.file))
    {
        auto tree = ValueTree::fromXml (*xml);
        if (tree.hasType (proc.apvts.state.getType()))
            applyTree (tree, e.name);
    }

    currentIndex = index;
    sendChangeMessage();
}

ValueTree PresetManager::buildFactoryPatch (const FactoryPreset& fp) const
{
    ValueTree t (proc.apvts.state.getType());

    std::array<float, kNumParams> v {};
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = def (i).def;
    for (const auto& [p, val] : fp.values) v[(size_t) idx (p)] = val;

    for (int i = 0; i < kNumParams; ++i)
    {
        ValueTree c ("PARAM");
        c.setProperty ("id", def (i).id, nullptr);
        c.setProperty ("value", v[(size_t) i], nullptr);
        t.appendChild (c, nullptr);
    }

    // Matrix: the standard macro kit for the stages in use, then the preset's own routes.
    std::vector<Route> routes;
    if (fp.macroKit)
    {
        std::array<bool, kNumStages> used {};
        for (int s = 0; s < kNumStages; ++s) used[(size_t) s] = v[(size_t) idx (kStages[s].on)] > 0.5f;
        routes = macroKit (used);
    }
    routes.insert (routes.end(), fp.routes.begin(), fp.routes.end());
    jassert (routes.size() <= (size_t) kMatrixSlots);

    ValueTree matrix (LodestoneProcessor::kMatrixType);
    for (int s = 0; s < kMatrixSlots; ++s)
    {
        ValueTree slot ("SLOT");
        const bool has = s < (int) routes.size();
        slot.setProperty ("src", has ? (int) routes[(size_t) s].src : 0, nullptr);
        slot.setProperty ("dst", has ? String (def (routes[(size_t) s].dst).id) : String(), nullptr);
        slot.setProperty ("amt", has ? routes[(size_t) s].amount : 0.0f, nullptr);
        matrix.appendChild (slot, nullptr);
    }
    t.appendChild (matrix, nullptr);
    return t;
}

bool PresetManager::saveUserPreset (const String& rawName)
{
    const auto name = File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return false;
    auto folder = getUserPresetFolder();
    if (! folder.createDirectory()) return false;

    auto state = proc.getPatchState();
    state.setProperty (LodestoneProcessor::kPresetNameProp, name, nullptr);
    auto xml = state.createXml();
    const auto file = folder.getChildFile (name + kExtension);
    if (xml == nullptr || ! xml->writeTo (file)) return false;

    proc.apvts.state.setProperty (LodestoneProcessor::kPresetNameProp, name, nullptr);
    refreshUserPresets();
    for (int k = 0; k < entries.size(); ++k)
        if (entries[k].file == file) { currentIndex = k; break; }
    sendChangeMessage();
    return true;
}

// ---------------------------------------------------------------------------
// "Spin": a random setting that is always usable on a real vocal. It keeps
// the current Key and Scale (they belong to the song, not to the sound).
void PresetManager::randomize (std::uint32_t seed)
{
    Random rng ((int64) seed * 7919 + 17);
    auto rf = [&] (float a, float b) { return a + rng.nextFloat() * (b - a); };
    auto ri = [&] (int n) { return rng.nextInt (n); };
    auto cur = [&] (P p) { return proc.apvts.getRawParameterValue (def (p).id)->load(); };

    FactoryPreset fp { "Spun", "Random", "A random setting. Turn the macros, or try SNAP and HOLD.", {}, {} };
    auto& v = fp.values;
    for (auto p : { P::key, P::scale, P::refPitch, P::range, P::sensitivity })
        v.push_back ({ p, cur (p) });
    for (int i = 0; i < 12; ++i)
    {
        v.push_back ({ noteParam (i), cur (noteParam (i)) });
        v.push_back ({ centsParam (i), cur (centsParam (i)) });
    }

    // Correction style: natural, pop or hard.
    const int style = ri (3);
    v.push_back ({ P::retune, style == 0 ? rf (25.0f, 70.0f) : style == 1 ? rf (4.0f, 18.0f) : rf (0.0f, 3.0f) });
    v.push_back ({ P::humanize, style == 2 ? rf (0.0f, 0.2f) : rf (0.15f, 0.55f) });
    v.push_back ({ P::flex, style == 0 ? rf (0.15f, 0.45f) : rf (0.0f, 0.15f) });
    v.push_back ({ P::keep, style == 2 ? rf (0.0f, 0.2f) : rf (0.4f, 1.2f) });
    v.push_back ({ P::bounce, rng.nextFloat() < 0.25f ? rf (0.2f, 0.7f) : 0.0f });
    v.push_back ({ P::stability, rf (0.3f, 0.7f) });
    v.push_back ({ P::formant, rng.nextFloat() < 0.4f ? rf (-3.0f, 3.0f) : 0.0f });
    v.push_back ({ P::grit, rng.nextFloat() < 0.15f ? rf (0.1f, 0.35f) : 0.0f });
    v.push_back ({ P::gritMode, (float) ri (dsp::kNumGritModes) });
    if (rng.nextFloat() < 0.2f) { v.push_back ({ P::vbOn, 1.0f }); v.push_back ({ P::vbDepth, rf (10.0f, 40.0f) }); v.push_back ({ P::vbRate, rf (4.5f, 7.0f) }); }

    // Voices: up to two harmonies and maybe the double.
    static constexpr int ivs[] = { dsp::Ivl::thirdUp, dsp::Ivl::fifthUp, dsp::Ivl::octDown, dsp::Ivl::sixthDown, dsp::Ivl::octUp,
                                   dsp::Ivl::fourthUp, dsp::Ivl::thirdDown };
    const int harmonies = rng.nextFloat() < 0.4f ? 0 : 1 + ri (2);
    for (int h = 0; h < harmonies; ++h)
    {
        v.push_back ({ harmonyParam (h, HField::on), 1.0f });
        v.push_back ({ harmonyParam (h, HField::interval), (float) ivs[ri (7)] });
        v.push_back ({ harmonyParam (h, HField::level), rf (0.3f, 0.55f) });
        v.push_back ({ harmonyParam (h, HField::pan), (h == 0 ? -1.0f : 1.0f) * rf (0.2f, 0.7f) });
        v.push_back ({ harmonyParam (h, HField::formant), rng.nextFloat() < 0.5f ? rf (-2.5f, 2.5f) : 0.0f });
    }
    if (rng.nextFloat() < 0.5f)
    {
        v.push_back ({ P::dbOn, 1.0f });
        v.push_back ({ P::dbLevel, rf (0.25f, 0.55f) });
        v.push_back ({ P::dbSpread, rf (0.4f, 1.0f) });
        v.push_back ({ P::dbDetune, rf (4.0f, 14.0f) });
    }

    applyTree (buildFactoryPatch (fp), "Spun #" + String ((int) (seed % 10000)));
    currentIndex = -1;
    sendChangeMessage();
}

// ---------------------------------------------------------------------------
void PresetManager::selectSlot (int slot)
{
    slot = slot == 0 ? 0 : 1;
    if (slot == activeSlot) return;
    slots[activeSlot] = proc.getPatchState();
    if (! slots[slot].isValid()) slots[slot] = slots[activeSlot].createCopy();
    lowkei::SoundChangeGate::Scope change (proc.soundGate);   // fade, swap every setting at once, fade
    proc.undo.beginNewTransaction ("A/B");
    proc.applyPatchState (slots[slot].createCopy(), true);
    activeSlot = slot;
    sendChangeMessage();
}

void PresetManager::copyActiveToOther()
{
    slots[1 - activeSlot] = proc.getPatchState();
    sendChangeMessage();
}

void PresetManager::restoreSelectionFromState()
{
    const auto name = proc.apvts.state.getProperty (LodestoneProcessor::kPresetNameProp).toString();
    for (int k = 0; k < entries.size(); ++k)
        if (entries[k].name == name) { currentIndex = k; break; }
    sendChangeMessage();
}

} // namespace lodestone
