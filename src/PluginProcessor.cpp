#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <optional>

namespace lodestone
{
using namespace juce;

namespace
{
const Identifier kSlotType ("SLOT");
const Identifier kSrcProp ("src"), kDstProp ("dst"), kAmtProp ("amt");
} // namespace

// ---------------------------------------------------------------------------
LodestoneProcessor::LodestoneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", AudioChannelSet::stereo(), false)),   // v2: the beat, for Chord Lock
      apvts (*this, &undo, "LODESTONE", createParameterLayout())
{
    for (int i = 0; i < kNumParams; ++i)
    {
        raw[(size_t) i] = apvts.getRawParameterValue (def (i).id);
        jassert (raw[(size_t) i] != nullptr);
        ranges[(size_t) i] = apvts.getParameterRange (def (i).id);
    }
    lfos[0].setIndex (0);
    lfos[1].setIndex (1);
    heldOrder.reserve (128);

    ensureStateStructure();
    apvts.state.addListener (this);
    refreshFromState();

    // A loaded state: audio-side data at once from the incoming tree, then the live
    // tree's structure once stateGuard has put it in place on the message thread.
    stateGuard.onDeferredLoad = [this] (const ValueTree& t)
    {
        refreshFrom (t);
        lowLatencyWanted.store ((bool) t.getProperty (kLowLatencyProp, false));
    };
    stateGuard.onApplied = [this] { ensureStateStructure(); refreshFromState(); syncLatencyFromState(); };

    presets.load (0);            // start on the Init preset
    undo.clearUndoHistory();     // loading Init is not an undoable edit
    setLatencySamples (latencyFor (sampleRateHz, false));
}

LodestoneProcessor::~LodestoneProcessor()
{
    apvts.state.removeListener (this);
}

bool LodestoneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo()) return false;
    if (in != AudioChannelSet::mono() && in != AudioChannelSet::stereo()) return false;
    // The sidechain is optional: off, mono or stereo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != AudioChannelSet::mono() && sc != AudioChannelSet::stereo()) return false;
    }
    return true;
}

void LodestoneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    lowLatencyActive = lowLatencyWanted.load();
    engine.setLowLatency (lowLatencyActive);
    engine.prepare (sampleRate);
    output.prepare (sampleRate);
    inGainS.prepare (sampleRate, 20.0f);
    inGainS.snapTo (Decibels::decibelsToGain (raw[(size_t) idx (P::inGain)]->load()));
    scratch.setSize (5, std::max (samplesPerBlock, kControlBlock));
    chords.prepare (sampleRate);
    for (auto& l : lfos) l.reset();
    env.reset();
    drift.reset();
    held.fill (false);
    heldOrder.clear();
    setLatencySamples (engine.getLatency());
    wasPlaying = false;
    lastBlockPeak = 0.0f;
    holdingSound = false;
    seenSoundChange = soundGate.current();   // a change made while not playing is simply current

    // Prime the engine with the current settings so nothing glides from zero.
    dsp::Transport t;
    t.sampleRate = sampleRate;
    t.bpm = raw[(size_t) idx (P::tempo)]->load();
    runControl (kControlBlock, 0.0f, t);
    engine.snapToTargets();   // levels and glides start at the session's settings, not the defaults
}

void LodestoneProcessor::reset()
{
    engine.reset();   // host stop / seek: clear buffers and the pitch track
    output.reset();
    // The chord analyzer keeps its chord: after a seek the beat is still in the same song.
}

// ---------------------------------------------------------------------------
// Latency mode
bool LodestoneProcessor::isLowLatency() const { return (bool) apvts.state.getProperty (kLowLatencyProp, false); }

void LodestoneProcessor::setLowLatency (bool live)
{
    apvts.state.setProperty (kLowLatencyProp, live, nullptr);   // listener does the rest
}

void LodestoneProcessor::syncLatencyFromState()
{
    const bool live = isLowLatency();
    lowLatencyWanted.store (live);
    // Reported at once; the audio thread switches its delay on the next block.
    setLatencySamples (latencyFor (sampleRateHz, live));
}

// ---------------------------------------------------------------------------
// Undo / redo: stepping over a preset load or an A/B switch is a whole new
// sound, so it goes through the sound-change gate like the load itself.
namespace
{
bool isSoundChange (const String& description)
{
    return description.startsWith ("Load ") || description == "A/B";
}
} // namespace

void LodestoneProcessor::undoEdit()
{
    std::optional<lowkei::SoundChangeGate::Scope> change;
    if (isSoundChange (undo.getUndoDescription())) change.emplace (soundGate);
    undo.undo();
}

void LodestoneProcessor::redoEdit()
{
    std::optional<lowkei::SoundChangeGate::Scope> change;
    if (isSoundChange (undo.getRedoDescription())) change.emplace (soundGate);
    undo.redo();
}

// ---------------------------------------------------------------------------
// Key and custom notes
void LodestoneProcessor::applyKey (int key, bool minor)
{
    undo.beginNewTransaction ("Learn key");
    setParamReal (idx (P::key), (float) jlimit (0, 11, key));
    setParamReal (idx (P::scale), (float) (minor ? dsp::Scale::minor : dsp::Scale::major));
}

void LodestoneProcessor::toggleNote (int pc)
{
    pc = ((pc % 12) + 12) % 12;
    undo.beginNewTransaction ("Edit scale");
    const int scale = (int) raw[(size_t) idx (P::scale)]->load();
    if (scale != dsp::kCustomScale)
    {
        // Start the custom scale from the notes currently in use.
        const auto m = dsp::scaleMask ((int) raw[(size_t) idx (P::key)]->load(), scale);
        for (int i = 0; i < 12; ++i) setParamReal (idx (noteParam (i)), dsp::has (m, i) ? 1.0f : 0.0f);
        setParamReal (idx (P::scale), (float) dsp::kCustomScale);
    }
    const bool now = raw[(size_t) idx (noteParam (pc))]->load() > 0.5f;
    setParamReal (idx (noteParam (pc)), now ? 0.0f : 1.0f);
}

// ---------------------------------------------------------------------------
// Patch data
void LodestoneProcessor::ensureStateStructure()
{
    auto m = apvts.state.getChildWithName (kMatrixType);
    if (! m.isValid()) { m = ValueTree (kMatrixType); apvts.state.appendChild (m, nullptr); }
    while (m.getNumChildren() < kMatrixSlots)
    {
        ValueTree s (kSlotType);
        s.setProperty (kSrcProp, 0, nullptr);
        s.setProperty (kDstProp, "", nullptr);
        s.setProperty (kAmtProp, 0.0f, nullptr);
        m.appendChild (s, nullptr);
    }
}

ValueTree LodestoneProcessor::matrixTree() const { return apvts.state.getChildWithName (kMatrixType); }

void LodestoneProcessor::refreshFrom (const ValueTree& st)
{
    const auto m = st.getChildWithName (kMatrixType);
    for (int i = 0; i < kMatrixSlots; ++i)
    {
        const auto s = m.getChild (i);
        auto& a = slotsA[(size_t) i];
        const int dst = s.isValid() ? findParam (s.getProperty (kDstProp).toString()) : -1;
        a.src.store (s.isValid() ? jlimit (0, kNumSources - 1, (int) s.getProperty (kSrcProp)) : 0);
        a.dst.store (dst >= 0 && def (dst).modulatable ? dst : -1);
        a.amount.store (s.isValid() ? jlimit (-1.0f, 1.0f, (float) s.getProperty (kAmtProp)) : 0.0f);
    }
}

MatrixSlot LodestoneProcessor::getSlot (int i) const
{
    MatrixSlot s;
    const auto t = matrixTree().getChild (i);
    if (! t.isValid()) return s;
    s.src = (Src) jlimit (0, kNumSources - 1, (int) t.getProperty (kSrcProp));
    s.dst = findParam (t.getProperty (kDstProp).toString());
    s.amount = (float) t.getProperty (kAmtProp);
    return s;
}

void LodestoneProcessor::setSlot (int i, const MatrixSlot& s)
{
    auto t = matrixTree().getChild (i);
    if (! t.isValid()) return;
    t.setProperty (kSrcProp, (int) s.src, &undo);
    t.setProperty (kDstProp, s.dst >= 0 ? String (def (s.dst).id) : String(), &undo);
    t.setProperty (kAmtProp, jlimit (-1.0f, 1.0f, s.amount), &undo);
}

void LodestoneProcessor::clearMatrix()
{
    for (int i = 0; i < kMatrixSlots; ++i) setSlot (i, {});
}

int LodestoneProcessor::addRoute (Src src, int dst, float amount)
{
    if (dst < 0 || ! def (dst).modulatable) return -1;
    for (int i = 0; i < kMatrixSlots; ++i)   // reuse a route with the same source and destination
    {
        const auto s = getSlot (i);
        if (s.src == src && s.dst == dst) { setSlot (i, { src, dst, amount }); return i; }
    }
    for (int i = 0; i < kMatrixSlots; ++i)
        if (! getSlot (i).isActive()) { setSlot (i, { src, dst, amount }); return i; }
    return -1;
}

void LodestoneProcessor::removeRoutesTo (int dst)
{
    for (int i = 0; i < kMatrixSlots; ++i)
        if (getSlot (i).dst == dst) setSlot (i, {});
}

Array<int> LodestoneProcessor::routesTo (int dst) const
{
    Array<int> r;
    for (int i = 0; i < kMatrixSlots; ++i)
    {
        const auto& a = slotsA[(size_t) i];
        if (a.dst.load() == dst && a.src.load() != 0 && a.amount.load() != 0.0f) r.add (i);
    }
    return r;
}

void LodestoneProcessor::setParamReal (int i, float real)
{
    if (auto* p = apvts.getParameter (def (i).id))
    {
        const float n = p->convertTo0to1 (real);
        if (std::abs (p->getValue() - n) < 1.0e-7f) return;
        p->beginChangeGesture();
        p->setValueNotifyingHost (n);
        p->endChangeGesture();
    }
}

ValueTree LodestoneProcessor::getPatchState() const
{
    return stateGuard.getState();   // safe from any thread (a host may save off the message thread)
}

void LodestoneProcessor::applyPatchState (const ValueTree& src, bool notifyHost)
{
    if (! src.hasType (stateGuard.stateType())) return;

    if (! notifyHost)
    {
        // Parameters now; the tree on the message thread (see stateGuard hooks).
        stateGuard.setState (src);
        return;
    }

    // Through the host-notifying path, so automation and undo stay in sync.
    // The latency mode is a session setting: presets never change it.
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto child = src.getChildWithProperty ("id", def (i).id);
        setParamReal (i, child.isValid() ? (float) child.getProperty ("value") : def (i).def);
    }
    auto& st = apvts.state;
    if (src.hasProperty (kPresetNameProp)) st.setProperty (kPresetNameProp, src.getProperty (kPresetNameProp), &undo);

    auto from = src.getChildWithName (kMatrixType);
    auto to = st.getChildWithName (kMatrixType);
    if (from.isValid() && to.isValid()) to.copyPropertiesAndChildrenFrom (from, &undo);
    ensureStateStructure();
    refreshFromState();
}

namespace
{
// A saved state must survive save -> load -> save byte for byte (hosts and
// clap-validator compare them). Loading turns each value into the parameter's
// 0..1 position and back, which on skewed ranges can move a value by a few
// millionths, so each value is saved as the point that conversion maps to itself.
void makeParamValuesReloadStable (ValueTree& state, AudioProcessorValueTreeState& apvts)
{
    for (auto child : state)
    {
        if (! child.hasType ("PARAM")) continue;
        auto* p = apvts.getParameter (child.getProperty ("id").toString());
        if (p == nullptr) continue;
        auto v = (float) (double) child.getProperty ("value");
        for (int k = 0; k < 8; ++k)   // converges in one or two steps
        {
            const float next = p->convertFrom0to1 (p->convertTo0to1 (v));
            if (next == v) break;
            v = next;
        }
        child.setProperty ("value", v, nullptr);
    }
}
} // namespace

void LodestoneProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = getPatchState();
    makeParamValuesReloadStable (state, apvts);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LodestoneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (stateGuard.stateType()))
        {
            {
                lowkei::SoundChangeGate::Scope change (soundGate);   // a new sound: fade, swap, fade
                applyPatchState (ValueTree::fromXml (*xml), false);
            }
            updateHostDisplay (ChangeDetails().withProgramChanged (true));
            MessageManager::callAsync ([safe = WeakReference<LodestoneProcessor> (this)]
            {
                if (safe != nullptr) safe->presets.restoreSelectionFromState();
            });
        }
}

AudioProcessorEditor* LodestoneProcessor::createEditor() { return new LodestoneEditor (*this); }

// ---------------------------------------------------------------------------
// Control block: sources -> matrix -> effective values -> engine.
void LodestoneProcessor::runControl (int len, float inPeak, const dsp::Transport& t)
{
    const auto seed = (std::uint32_t) raw[(size_t) idx (P::seed)]->load();

    for (int i = 0; i < kNumParams; ++i)
        norm[(size_t) i] = ranges[(size_t) i].convertTo0to1 (raw[(size_t) i]->load());

    // Sources (LFO / Drift rates use last block's modulated values).
    for (int l = 0; l < 2; ++l)
    {
        const int base = idx (P::lfo1Shape) + l * 4;
        dsp::LfoParams lp;
        lp.shape = (dsp::LfoShape) (int) raw[(size_t) base]->load();
        lp.rateHz = eff[(size_t) base + 1] > 0.0f ? eff[(size_t) base + 1] : raw[(size_t) base + 1]->load();
        lp.sync = raw[(size_t) base + 2]->load() > 0.5f;
        lp.division = (int) raw[(size_t) base + 3]->load();
        srcVal[(int) Src::lfo1 + l] = lfos[l].advance (lp, t, len, seed);
    }
    {
        dsp::EnvParams ep { raw[(size_t) idx (P::envAttack)]->load(), raw[(size_t) idx (P::envRelease)]->load(),
                            raw[(size_t) idx (P::envGain)]->load() };
        srcVal[(int) Src::env] = env.advance (ep, inPeak, len, sampleRateHz);
    }
    {
        const float rate = eff[(size_t) idx (P::driftRate)] > 0.0f ? eff[(size_t) idx (P::driftRate)] : raw[(size_t) idx (P::driftRate)]->load();
        dsp::ChaosParams cp { rate, raw[(size_t) idx (P::driftSmooth)]->load() };
        srcVal[(int) Src::drift] = drift.advance (cp, len, sampleRateHz, seed);
    }
    for (int k = 0; k < kNumMacros; ++k)
        srcVal[(int) Src::macro1 + k] = norm[(size_t) idx (P::macro1) + (size_t) k];

    // Matrix.
    for (auto& s : slotsA)
    {
        const int dst = s.dst.load (std::memory_order_relaxed), src = s.src.load (std::memory_order_relaxed);
        if (dst < 0 || src == 0) continue;
        norm[(size_t) dst] += s.amount.load (std::memory_order_relaxed) * srcVal[src];
    }

    // Real values.
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = def (i);
        if (d.kind == Kind::flt)
        {
            norm[(size_t) i] = dsp::clamp01 (norm[(size_t) i]);
            eff[(size_t) i] = ranges[(size_t) i].convertFrom0to1 (norm[(size_t) i]);
        }
        else
        {
            eff[(size_t) i] = raw[(size_t) i]->load();
            norm[(size_t) i] = ranges[(size_t) i].convertTo0to1 (eff[(size_t) i]);
        }
    }

    for (int i = 0; i < kNumParams; ++i) uiNorm[(size_t) i].store (norm[(size_t) i], std::memory_order_relaxed);
    for (int l = 0; l < 2; ++l) uiLfo[l].store (srcVal[(int) Src::lfo1 + l], std::memory_order_relaxed);
    uiEnv.store (srcVal[(int) Src::env], std::memory_order_relaxed);
    uiDrift.store (srcVal[(int) Src::drift], std::memory_order_relaxed);

    // During a sound change the engine keeps the old settings until the swap.
    if (! holdingSound) pushEngineParams();
}

// Once per control block, before runControl (see SoundChangeGate.h). Returns
// true on the block where the new sound is taken.
bool LodestoneProcessor::updateSoundChange()
{
    const auto change = soundGate.current();
    if (! holdingSound && change != seenSoundChange)
    {
        holdingSound = true;           // keep the old sound, fade to the dry voice
        output.setTransition (true);
    }
    if (holdingSound && ! soundGate.busy() && output.readyForSwap())
    {
        // Fully dry and every setting written: take the whole new sound at once
        // (runControl, next, reads and pushes every setting).
        seenSoundChange = change;
        holdingSound = false;
        engine.snapToTargets();        // voices, glides and levels jump to the new sound (unheard)
        output.setTransition (false);  // fade the new sound in
        return true;
    }
    return false;
}

void LodestoneProcessor::pushEngineParams()
{
    auto v = [this] (P p) { return eff[(size_t) idx (p)]; };
    auto i = [this] (P p) { return (int) std::lround (eff[(size_t) idx (p)]); };
    auto b = [this] (P p) { return eff[(size_t) idx (p)] > 0.5f; };

    dsp::EngineParams ep;
    auto& tp = ep.tuner;
    tp.enabled = b (P::tuneOn);
    tp.retuneMs = v (P::retune);
    tp.humanize = v (P::humanize);
    tp.flex = v (P::flex);
    tp.amount = v (P::amount);
    tp.bounce = v (P::bounce);
    tp.stability = v (P::stability);
    tp.keep = v (P::keep);
    tp.transpose = v (P::transpose);
    tp.detuneCents = v (P::detune);
    for (int k = 0; k < 12; ++k) tp.noteCents[(size_t) k] = v (centsParam (k));
    tp.vibOn = b (P::vbOn);
    tp.vibDepth = 0.01f * v (P::vbDepth);
    tp.vibRate = v (P::vbRate);
    tp.vibDelayMs = v (P::vbDelay);
    tp.vibOnsetMs = v (P::vbOnset);
    tp.vibVariation = v (P::vbVar);
    tp.snap = b (P::snap);
    tp.hold = b (P::hold);

    ep.key = i (P::key);
    ep.scale = i (P::scale);
    dsp::Mask custom = 0;
    for (int k = 0; k < 12; ++k) if (b (noteParam (k))) custom |= (dsp::Mask) (1u << k);
    ep.customMask = custom;
    ep.refHz = v (P::refPitch);
    ep.range = i (P::range);
    ep.sensitivity = v (P::sensitivity);
    ep.midiMode = (dsp::MidiMode) i (P::midiMode);
    ep.formantPreserve = i (P::character) == 0;
    ep.formant = v (P::formant);
    ep.breath = v (P::breath);
    ep.leadLevel = v (P::lead);
    ep.grit = v (P::grit);
    ep.gritMode = i (P::gritMode);
    for (int h = 0; h < dsp::kNumHarmonies; ++h)
    {
        auto& hp = ep.harmony[(size_t) h];
        hp.on = b (harmonyParam (h, HField::on));
        hp.interval = i (harmonyParam (h, HField::interval));
        hp.level = v (harmonyParam (h, HField::level));
        hp.pan = v (harmonyParam (h, HField::pan));
        hp.formant = v (harmonyParam (h, HField::formant));
    }
    ep.harmonyHumanize = v (P::harmHumanize);
    ep.doubleOn = b (P::dbOn);
    ep.doubleLevel = v (P::dbLevel);
    ep.doubleSpread = v (P::dbSpread);
    ep.doubleDetune = v (P::dbDetune);
    ep.doubleDelayMs = v (P::dbDelay);
    ep.chordLock = v (P::chordLock);
    ep.motionOn = b (P::motionOn);
    for (int k = 0; k < dsp::kMotionSteps; ++k) ep.motionSteps[(size_t) k] = i (motionStepParam (k));
    ep.motionLength = i (P::motionLength);
    ep.motionDivision = i (P::motionRate);
    ep.motionGlideMs = v (P::motionGlide);
    ep.motionStart = (dsp::MotionStart) i (P::motionStart);
    ep.seed = (std::uint32_t) i (P::seed);
    engine.setParams (ep);

    for (int s = 0; s < kNumStages; ++s) uiStageOn[(size_t) s].store (b (kStages[s].on), std::memory_order_relaxed);
    uiAllowedMask.store ((int) engine.allowedMask(), std::memory_order_relaxed);

    dsp::OutputParams op;
    op.width = v (P::width);
    op.bassMonoHz = 20.0f;
    op.mix = v (P::mix);
    op.delta = false;
    op.outGain = Decibels::decibelsToGain (v (P::outGain));
    op.limiter = b (P::limiter);
    output.setParams (op);
}

// ---------------------------------------------------------------------------
void LodestoneProcessor::handleMidi (const MidiBuffer& midi)
{
    bool changed = false;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int n = m.getNoteNumber();
            if (! held[(size_t) n]) { held[(size_t) n] = true; heldOrder.push_back (n); changed = true; }
        }
        else if (m.isNoteOff())
        {
            const int n = m.getNoteNumber();
            if (held[(size_t) n])
            {
                held[(size_t) n] = false;
                heldOrder.erase (std::remove (heldOrder.begin(), heldOrder.end(), n), heldOrder.end());
                changed = true;
            }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            held.fill (false);
            heldOrder.clear();
            changed = true;
        }
    }
    if (! changed) return;

    int notes[dsp::VoiceEngine::kMaxHeld];
    int count = 0, pcMask = 0;
    for (int n = 0; n < 128 && count < dsp::VoiceEngine::kMaxHeld; ++n)
        if (held[(size_t) n]) { notes[count++] = n; pcMask |= 1 << (n % 12); }
    engine.setHeldNotes (notes, count, heldOrder.empty() ? -1 : heldOrder.back());
    uiHeldMask.store (pcMask, std::memory_order_relaxed);
}

void LodestoneProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    // Main input channels only: with the sidechain on, the buffer also carries its channels.
    const int numIn = getMainBusNumInputChannels(), numOut = getMainBusNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (scratch.getNumSamples() < n) scratch.setSize (5, n, false, false, true);   // host exceeded its block size
    float* side = scratch.getWritePointer (4);

    // The beat on the sidechain (v2), summed to mono. Read first: its channels
    // can share the buffer with an output channel that is cleared below (a
    // mono input with a stereo sidechain). No sidechain: silence, and the last
    // chord heard is kept.
    bool hasSidechain = false;
    if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled() && scBus->getNumberOfChannels() > 0 && n > 0)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0)
        {
            hasSidechain = true;
            const float* s0 = sc.getReadPointer (0);
            const float* s1 = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : nullptr;
            for (int k = 0; k < n; ++k) side[k] = s1 != nullptr ? 0.5f * (s0[k] + s1[k]) : s0[k];
        }
    }
    if (! hasSidechain && n > 0) FloatVectorOperations::clear (side, n);

    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);
    handleMidi (midi);
    if (n == 0 || numOut == 0) return;

    // Latency mode change (from the GLOBAL page or a loaded session).
    if (const bool want = lowLatencyWanted.load(); want != lowLatencyActive)
    {
        lowLatencyActive = want;
        engine.setLowLatency (want);
        engine.reset();
        output.reset();
    }
    if (clearChromaRequested.exchange (false)) { engine.clearChroma(); chords.clearKeyChroma(); }

    // Tempo and position (for synced LFOs). When the host is stopped (or sends
    // nothing, as in the standalone app) a free-running clock keeps them moving.
    double bpm = raw[(size_t) idx (P::tempo)]->load();
    bool playing = false;
    double hostPpq = 0.0;
    bool hasPpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bp = pos->getBpm()) bpm = *bp;
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { hostPpq = *p; hasPpq = true; }
        }
    bpm = jlimit (20.0, 999.0, bpm);
    uiBpm.store ((float) bpm, std::memory_order_relaxed);

    dsp::Transport t;
    t.bpm = bpm;
    t.sampleRate = sampleRateHz;
    t.playing = playing && hasPpq;
    t.ppq = t.playing ? hostPpq : freePpq;

    // The playhead moved under us: a loop wrapping round or a locate while playing
    // (more than ~1.3 ms from where the last block ended), or the transport starting
    // somewhere else while the voice is sounding. Synced LFOs ease across it. Starting
    // from silence stays exact, so playback still matches a bounce.
    {
        const bool jumped = t.playing && wasPlaying && std::abs (hostPpq - expectedPpq) > 64.0 * t.beatsPerSample();
        const bool startedOverSound = t.playing && ! wasPlaying && lastBlockPeak > 3.0e-5f
                                   && std::abs (hostPpq - freePpq) > 64.0 * t.beatsPerSample();
        if (jumped || startedOverSound)
            for (auto& l : lfos) l.easeNextJump();
        wasPlaying = t.playing;
    }

    float* mono = scratch.getWritePointer (0);
    float* dryL = scratch.getWritePointer (1);
    float* wetL = scratch.getWritePointer (2);
    float* wetR = scratch.getWritePointer (3);
    float* L = buffer.getWritePointer (0);
    float* R = numOut >= 2 ? buffer.getWritePointer (1) : nullptr;
    const float* inR = numIn >= 2 ? buffer.getReadPointer (1) : nullptr;

    // The voice is tracked and tuned as one mono signal (the mid of a stereo input).
    inGainS.setTarget (Decibels::decibelsToGain (raw[(size_t) idx (P::inGain)]->load()));
    float inPeakBlock = 0.0f, outPeak = 0.0f, grMin = 1.0f;
    for (int k = 0; k < n; ++k)
    {
        const float x = inR != nullptr ? 0.5f * (L[k] + inR[k]) : L[k];
        mono[k] = x * inGainS.next();
    }


    for (int start = 0; start < n; start += kControlBlock)
    {
        const int len = std::min (kControlBlock, n - start);
        float pk = 0.0f;
        for (int k = 0; k < len; ++k) pk = std::max (pk, std::abs (mono[start + k]));
        inPeakBlock = std::max (inPeakBlock, pk);

        for (int k = 0; k < len; ++k) chords.push (side[start + k]);
        engine.setChord (chords.chord().mask());
        engine.setTransport (t.ppq, t.bpm);

        const bool swapped = updateSoundChange();
        runControl (len, pk, t);
        if (swapped) output.snapWet();   // width and mix start at the new sound's values
        engine.process (mono + start, wetL + start, wetR + start, dryL + start, len);
        output.process (dryL + start, dryL + start, wetL + start, wetR + start, len);
        grMin = std::min (grMin, output.gainReduction());
        t.ppq += t.beatsPerSample() * len;

        // Display history (every other control block).
        if (++histPhase >= 2)
        {
            histPhase = 0;
            const auto& vw = engine.getView();
            const int h = histCount.load (std::memory_order_relaxed);
            const auto slot = (size_t) (h & (kHist - 1));
            histIn[slot].store (vw.voiced ? vw.inputPitch : -1.0f, std::memory_order_relaxed);
            histOut[slot].store (vw.voiced ? vw.outputPitch : -1.0f, std::memory_order_relaxed);
            histTarget[slot].store (vw.voiced ? vw.targetPitch : -1.0f, std::memory_order_relaxed);
            histCount.store (h + 1, std::memory_order_release);
        }
    }
    freePpq = t.ppq;
    expectedPpq = t.ppq;
    lastBlockPeak = inPeakBlock;

    for (int k = 0; k < n; ++k)
    {
        L[k] = R != nullptr ? wetL[k] : 0.5f * (wetL[k] + wetR[k]);
        if (R != nullptr) R[k] = wetR[k];
        outPeak = std::max ({ outPeak, std::abs (wetL[k]), std::abs (wetR[k]) });
    }

    const auto& vw = engine.getView();
    uiVoiced.store (vw.voiced, std::memory_order_relaxed);
    uiInPitch.store (vw.inputPitch, std::memory_order_relaxed);
    uiOutPitch.store (vw.outputPitch, std::memory_order_relaxed);
    uiTargetPitch.store (vw.targetPitch, std::memory_order_relaxed);
    uiTargetNote.store (vw.targetNote, std::memory_order_relaxed);
    uiShift.store (vw.shift, std::memory_order_relaxed);
    uiClarity.store (vw.clarity, std::memory_order_relaxed);
    uiFlex.store (vw.flexWeight, std::memory_order_relaxed);
    for (int h = 0; h < dsp::kNumHarmonies; ++h)
    {
        uiHarmPitch[(size_t) h].store (vw.harmonyPitch[(size_t) h], std::memory_order_relaxed);
        uiHarmActive[(size_t) h].store (vw.harmonyActive[(size_t) h], std::memory_order_relaxed);
    }
    const auto& chroma = engine.getChroma();
    for (int k = 0; k < 12; ++k) uiChroma[(size_t) k].store (chroma[(size_t) k], std::memory_order_relaxed);

    const auto& chord = chords.chord();
    uiChordRoot.store (chord.root, std::memory_order_relaxed);
    uiChordShape.store (chord.shape, std::memory_order_relaxed);
    uiChordInUse.store ((int) vw.chord, std::memory_order_relaxed);
    // The lit notes on the field and keyboard: what Chord Lock actually allows.
    uiAllowedMask.store ((int) dsp::chordPull (engine.allowedMask(), vw.chord, eff[(size_t) idx (P::chordLock)]).allowed,
                         std::memory_order_relaxed);
    uiMotionStep.store (vw.motionStep, std::memory_order_relaxed);
    uiBeatHearing.store (chords.hearing(), std::memory_order_relaxed);
    uiSidechainConnected.store (hasSidechain, std::memory_order_relaxed);
    const auto& bc = chords.getChroma();
    const auto& bk = chords.getKeyChroma();
    for (int k = 0; k < 12; ++k)
    {
        uiBeatChroma[(size_t) k].store (bc[(size_t) k], std::memory_order_relaxed);
        uiBeatKey[(size_t) k].store (bk[(size_t) k], std::memory_order_relaxed);
    }

    uiInPeak.store (inPeakBlock, std::memory_order_relaxed);
    uiOutPeak.store (outPeak, std::memory_order_relaxed);
    uiGainReduction.store (grMin, std::memory_order_relaxed);
}

} // namespace lodestone

// Plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new lodestone::LodestoneProcessor();
}
