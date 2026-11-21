#pragma once
// Lodestone's audio processor: reads parameters, runs the modulation system
// and the macros every 32-sample control block, feeds MIDI notes and
// settings to the voice engine, then the output stage.
//
// Threading: patch data that is not a plain parameter (the mod matrix, the
// latency mode, the editor's view) lives in the APVTS ValueTree (saved with
// the project) and is edited on the message thread only. Every change is
// copied into lock-free atomics, which are all the audio thread ever reads.

#include "Parameters.h"
#include "dsp/ChordAnalyzer.h"
#include "dsp/ModSources.h"
#include "dsp/OutputStage.h"
#include "dsp/VoiceEngine.h"
#include "presets/PresetManager.h"
#include "state/SoundChangeGate.h"
#include "state/StateGuard.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace lodestone
{

struct MatrixSlot
{
    Src src = Src::none;
    int dst = -1;          // parameter index, -1 = empty
    float amount = 0.0f;   // -1..1 of the destination's full range
    bool isActive() const noexcept { return src != Src::none && dst >= 0 && amount != 0.0f; }
};

class LodestoneProcessor : public juce::AudioProcessor, private juce::ValueTree::Listener
{
public:
    static constexpr int kControlBlock = dsp::VoiceEngine::kBlock;

    LodestoneProcessor();
    ~LodestoneProcessor() override;

    // Lifecycle -------------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    // Editor / info ---------------------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Patch data (message thread) -------------------------------------------------------
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;
    mutable lowkei::StateGuard stateGuard { apvts };   // host save/load off the message thread

    MatrixSlot getSlot (int i) const;
    void setSlot (int i, const MatrixSlot& s);
    void clearMatrix();
    int addRoute (Src src, int dst, float amount);   // -1 if the matrix is full
    void removeRoutesTo (int dst);
    juce::Array<int> routesTo (int dst) const;

    juce::ValueTree getPatchState() const;
    void applyPatchState (const juce::ValueTree& tree, bool notifyHost);

    // Click-free sound changes: every preset load, Dice, A/B, host load and the
    // undo/redo of one of those opens a scope; the audio thread fades to the dry
    // voice, swaps every setting at once and fades back in (~50 ms).
    lowkei::SoundChangeGate soundGate;   // before presets: they use it from the start

    // Undo / redo for the editor: a preset load or A/B step goes through the gate.
    void undoEdit();
    void redoEdit();

    PresetManager presets { *this };

    // Latency: Live (~3 ms) for tracking and performing, Studio (~15 ms, lined
    // up with the pitch analysis) for mixing. Saved with the session.
    bool isLowLatency() const;
    void setLowLatency (bool live);
    static int latencyFor (double sampleRate, bool live) noexcept
    {
        return std::max (16, (int) std::lround (sampleRate * (live ? 0.003 : 0.015)));
    }

    // Key learning: the engine counts how long each pitch class is sung; with a
    // beat on the sidechain, the beat's own notes are used instead (v2): they are
    // far more reliable, so they count three times as fast.
    void clearKeyHistogram() noexcept { clearChromaRequested.store (true); }
    bool keyFromBeat() const noexcept
    {
        float beat = 0.0f;
        for (int i = 0; i < 12; ++i) beat += uiBeatKey[(size_t) i].load (std::memory_order_relaxed);
        return beat >= 0.5f;
    }
    std::array<float, 12> getKeyHistogram() const noexcept
    {
        std::array<float, 12> c {};
        const bool beat = keyFromBeat();
        for (int i = 0; i < 12; ++i)
            c[(size_t) i] = beat ? 3.0f * uiBeatKey[(size_t) i].load (std::memory_order_relaxed)
                                 : uiChroma[(size_t) i].load (std::memory_order_relaxed);
        return c;
    }
    // LEARN is listening (message thread only; shared by every LEARN button).
    bool keyLearning = false;
    // Sets Key and Scale (major / minor) as one undoable step.
    void applyKey (int key, bool minor);

    // Custom scale helper: switch to Custom keeping the current notes, then toggle one.
    void toggleNote (int pitchClass);

    // Live values for the UI (written by the audio thread, read at 30 fps) ---------------
    std::array<std::atomic<float>, kNumParams> uiNorm {};
    std::atomic<float> uiLfo[2] {}, uiEnv { 0.0f }, uiDrift { 0.0f };
    std::atomic<float> uiInPeak { 0.0f }, uiOutPeak { 0.0f }, uiGainReduction { 1.0f }, uiBpm { 120.0f };
    std::atomic<bool> uiVoiced { false };
    std::atomic<float> uiInPitch { 0.0f }, uiOutPitch { 0.0f }, uiTargetPitch { 60.0f }, uiShift { 0.0f }, uiClarity { 0.0f }, uiFlex { 1.0f };
    std::atomic<int> uiTargetNote { 60 }, uiHeldMask { 0 }, uiAllowedMask { 0 };
    std::array<std::atomic<float>, dsp::kNumHarmonies> uiHarmPitch {};
    std::array<std::atomic<bool>, dsp::kNumHarmonies> uiHarmActive {};
    std::array<std::atomic<bool>, kNumStages> uiStageOn {};
    std::array<std::atomic<float>, 12> uiChroma {};

    // The beat (sidechain), v2: the chord heard, whether sound is arriving, the
    // chord tones in use by Chord Lock, the beat's chroma and the Motion step.
    std::atomic<int> uiChordRoot { -1 }, uiChordShape { 0 }, uiChordInUse { 0 }, uiMotionStep { -1 };
    std::atomic<bool> uiBeatHearing { false }, uiSidechainConnected { false };
    std::array<std::atomic<float>, 12> uiBeatChroma {}, uiBeatKey {};

    // Pitch history for the display: one entry every other control block.
    // Unvoiced entries hold -1. histCount only grows; read the last entries.
    static constexpr int kHist = 2048;
    std::array<std::atomic<float>, kHist> histIn {}, histOut {}, histTarget {};
    std::atomic<int> histCount { 0 };
    double histRate() const noexcept { return sampleRateHz / (2.0 * kControlBlock); }

    // State property names.
    static constexpr const char* kPresetNameProp = "presetName";
    static constexpr const char* kMatrixType = "MATRIX";
    static constexpr const char* kLowLatencyProp = "lowLatency";

    // For tests / tools: the effective value of a parameter after modulation.
    float effectiveValue (P p) const noexcept { return eff[(size_t) idx (p)]; }
    const dsp::VoiceEngine& getEngine() const noexcept { return engine; }
    const dsp::ChordAnalyzer& getChordAnalyzer() const noexcept { return chords; }
    float getSoundTransition() const noexcept { return output.getTransition(); }   // 0 = settled

private:
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& prop) override
    {
        if (t.hasType ("SLOT")) refreshFromState();
        if (t == apvts.state && prop.toString() == kLowLatencyProp) syncLatencyFromState();
    }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { refreshFromState(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { refreshFromState(); }
    void valueTreeRedirected (juce::ValueTree&) override { refreshFromState(); }

    void ensureStateStructure();
    void refreshFromState() { refreshFrom (apvts.state); }
    void refreshFrom (const juce::ValueTree& state);
    void syncLatencyFromState();
    juce::ValueTree matrixTree() const;
    void setParamReal (int i, float real);

    void runControl (int len, float inPeak, const dsp::Transport& t);
    bool updateSoundChange();
    void pushEngineParams();
    void handleMidi (const juce::MidiBuffer& midi);

    struct SlotAtomic { std::atomic<int> src { 0 }, dst { -1 }; std::atomic<float> amount { 0.0f }; };
    std::array<SlotAtomic, kMatrixSlots> slotsA;

    std::array<std::atomic<float>*, kNumParams> raw {};
    std::array<juce::NormalisableRange<float>, kNumParams> ranges;
    std::array<float, kNumParams> norm {}, eff {};
    std::array<float, kNumSources> srcVal {};

    dsp::VoiceEngine engine;
    dsp::ChordAnalyzer chords;   // listens to the sidechain (the beat)
    dsp::OutputStage output;
    dsp::ModLfo lfos[2];
    dsp::EnvFollower env;
    dsp::Chaos drift;
    dsp::Smoother inGainS;
    juce::AudioBuffer<float> scratch;   // mono in, dry, wet L/R, sidechain mono

    std::atomic<bool> lowLatencyWanted { false }, clearChromaRequested { false };
    bool lowLatencyActive = false;
    std::array<bool, 128> held {};
    std::vector<int> heldOrder;          // press order (newest last)
    int histPhase = 0;

    double sampleRateHz = 48000.0;
    double freePpq = 0.0;

    // Audio thread: transport jump detection and the sound-change gate.
    bool wasPlaying = false;
    double expectedPpq = 0.0;
    float lastBlockPeak = 0.0f;
    bool holdingSound = false;     // fading to dry / dry: the engine keeps the old settings
    std::uint32_t seenSoundChange = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (LodestoneProcessor)
};

} // namespace lodestone
