#pragma once
// The tuning brain: from the detected pitch, decide the target note and how
// far (and how fast) to pull the voice onto it. Runs once per control block.
//
//   Target   nearest allowed note (scale, custom notes or held MIDI notes),
//            with Note Stability: a new note must win clearly, and for long
//            enough, so vibrato and scoops never flip between notes.
//   Pull     the correction glides to the target with Retune Speed. Humanize
//            relaxes it on sustained notes; Flex leaves notes alone that are
//            sung far from any target (scoops, falls, expressive slides);
//            Amount scales it; Bounce lets it overshoot and settle like a
//            spring (the "negative speed" robot flutter, tamed).
//   Keep     how much of the singer's own vibrato survives the correction
//            (0 = flattened, 1 = untouched, 2 = exaggerated).
//   Create   an added vibrato with rate, depth, delay, onset and variation.
//   Snap     (performance) instant hard tune while held.
//   Hold     (performance) freezes the target note: the voice is pinned to it.
//   Chord    (v2) Chord Lock: the chord heard on the sidechain pulls notes onto
//            its tones (see chordPull in Scales.h).
//   Motion   (v2) a step of the Motion pattern moves the target that many
//            notes up or down the scale (or the chord), with its own glide.
//
// Output: the pitch shift (semitones) to apply to the audio at this moment.

#include "Scales.h"
#include "Primitives.h"

namespace lodestone::dsp
{

enum class MidiMode { off = 0, scale, exact, harmony };

struct TunerParams
{
    bool enabled = true;          // correction on (off = harmonizer / doubler only)
    float retuneMs = 20.0f;       // 0 = instant
    float humanize = 0.0f;        // 0..1
    float flex = 0.0f;            // 0..1
    float amount = 1.0f;          // 0..1
    float bounce = 0.0f;          // 0..1
    float stability = 0.5f;       // 0..1
    float keep = 0.0f;            // 0..2 vibrato kept
    float transpose = 0.0f;       // semitones (output)
    float detuneCents = 0.0f;     // output fine tune
    std::array<float, 12> noteCents {};   // per pitch class micro-tuning (cents)

    bool vibOn = false;
    float vibDepth = 0.3f;        // semitones (peak)
    float vibRate = 5.5f;         // Hz
    float vibDelayMs = 250.0f;
    float vibOnsetMs = 300.0f;
    float vibVariation = 0.2f;    // 0..1

    bool snap = false;
    bool hold = false;

    Mask chord = 0;               // chord tones heard on the sidechain (0 = none)
    float chordLock = 0.0f;       // 0..1

    bool motionOn = false;
    int motionStep = 0;           // notes up (+) or down (-) for the current step
    float motionGlideMs = 12.0f;
};

struct TunerState
{
    bool voiced = false;
    float inputPitch = 0.0f;      // MIDI float, as sung
    float centrePitch = 0.0f;     // vibrato removed
    int targetNote = 60;          // integer MIDI
    float targetPitch = 60.0f;    // with micro-tuning
    float shift = 0.0f;           // semitones applied now (correction + vibrato + transpose)
    float outputPitch = 0.0f;     // inputPitch + shift
    float flexWeight = 1.0f;      // how much correction Flex allows now (0..1)
    float noteAge = 0.0f;         // seconds on the current target
    int motionNote = 60;          // target moved by the Motion step (= targetNote when off)
};

class Tuner
{
public:
    void prepare (double sampleRate) { fs = sampleRate; reset(); }

    void reset() noexcept
    {
        st = {};
        corr = corrVel = 0.0f;
        centre = 0.0f;
        hasCentre = false;
        candidate = -1;
        candidateTime = 0.0f;
        silentTime = 1.0f;
        vibPhase = 0.0f;
        vibDrift = 0.0f;
        heldNote = -1;
        motionS = 0.0f;
    }

    // dt = seconds in this control block. pitch = detected MIDI float.
    // allowed = scale mask (already including MIDI scale mode). exactNote >= 0
    // forces the target (MIDI Exact mode).
    const TunerState& advance (const TunerParams& p, bool voiced, float pitch, Mask allowed, int exactNote, float dt,
                               std::uint32_t seed) noexcept
    {
        st.voiced = voiced;
        if (! voiced)
        {
            silentTime += dt;
            // Hold the last shift through short gaps (consonants); relax after.
            if (silentTime > 0.25f) { corr *= std::exp (-dt / 0.05f); corrVel = 0.0f; }
            st.shift = corr + motionS + p.transpose + p.detuneCents * 0.01f;
            return st;
        }

        const bool newPhrase = silentTime > 0.12f || ! hasCentre;
        silentTime = 0.0f;
        st.inputPitch = pitch;

        // Vibrato-free centre: ~3.5 Hz low-pass, reset at a new phrase.
        const float a = 1.0f - std::exp (-dt * kTwoPi * 3.5f);
        centre = newPhrase ? pitch : centre + a * (pitch - centre);
        hasCentre = true;
        st.centrePitch = centre;

        // ---- Target note (Chord Lock widens or narrows the allowed notes).
        const Pull pull = chordPull (allowed, p.chord, p.chordLock);
        int target = st.targetNote;
        if (p.hold && heldNote >= 0)
            target = heldNote;
        else if (exactNote >= 0)
            target = exactNote;
        else
        {
            // Near the current note, decide on the vibrato-free centre (vibrato
            // never flips notes); a real jump is followed on the raw pitch at once.
            const float away = std::abs (pitch - (float) st.targetNote);
            const bool jump = away > 0.8f;
            const float decide = jump ? pitch : centre;
            const float hyst = lerp (0.05f, 0.45f, p.stability);
            const int cand = newPhrase ? nearestPreferred (pitch, pull) : stickyPreferred (decide, pull, st.targetNote, hyst);
            if (newPhrase || cand == st.targetNote) { target = cand; candidate = -1; }
            else
            {
                // A different note must persist before we move: Stability x 45 ms
                // near the current note (vibrato and scoops live there), but only
                // Stability x 8 ms for a leap wider than any vibrato (v2: the voice
                // used to be held on the old note for the whole wait).
                if (cand != candidate) { candidate = cand; candidateTime = 0.0f; }
                candidateTime += dt;
                const float wait = p.stability * (away > kLeap ? 0.008f : 0.045f);
                if (candidateTime >= wait) { target = cand; candidate = -1; }
            }
            // v2: a leap re-anchors the vibrato-free centre on the new note. It
            // used to sweep slowly across the notes in between, which could pick
            // a wrong neighbour note for a moment and held Flex back.
            if (target != st.targetNote && jump) centre = pitch;
        }
        if (! p.hold) heldNote = target;
        if (target != st.targetNote) st.noteAge = 0.0f;
        st.noteAge += dt;
        st.targetNote = target;
        const int pc = ((target % 12) + 12) % 12;
        st.targetPitch = (float) target + 0.01f * p.noteCents[(size_t) pc];

        // ---- Desired correction.
        const bool snap = p.snap;
        const float keep = snap ? 0.0f : std::clamp (p.keep, 0.0f, 2.0f);
        const float amount = snap || p.hold ? 1.0f : clamp01 (p.amount);
        float flexW = 1.0f;
        if (! snap && ! p.hold && p.flex > 0.001f && exactNote < 0)
        {
            const float distCents = 100.0f * std::abs (centre - st.targetPitch);
            const float win = lerp (100.0f, 6.0f, p.flex);
            flexW = 1.0f - smoothstep (win, win * 2.0f, distCents);
        }
        st.flexWeight = flexW;
        const float desiredOut = st.targetPitch + keep * (pitch - centre);
        float want = (p.enabled || p.hold || snap) ? amount * flexW * (desiredOut - pitch) : 0.0f;
        if (exactNote >= 0 && ! p.enabled) want = desiredOut - pitch;   // MIDI Exact always plays the note

        // ---- Glide (Retune + Humanize), with optional spring overshoot (Bounce).
        float retune = snap ? 0.0f : std::max (0.0f, p.retuneMs);
        if (! snap && p.humanize > 0.0f)
            retune += p.humanize * 220.0f * smoothstep (0.06f, 0.45f, st.noteAge);
        const float bounce = snap ? 0.0f : clamp01 (p.bounce);
        if (bounce < 0.001f)
        {
            corr = retune < 0.3f ? want : want + (corr - want) * std::exp (-dt / (0.001f * retune));
            corrVel = 0.0f;
        }
        else
        {
            // Damped spring: retune sets the period, Bounce lowers the damping.
            const float tau = std::max (0.003f, 0.001f * retune);
            const float w = 1.6f / tau;
            const float zeta = lerp (1.0f, 0.12f, bounce);
            const int sub = std::max (1, (int) std::ceil (dt * w / 0.25f));
            const float h = dt / (float) sub;
            for (int k = 0; k < sub; ++k)
            {
                const float acc = w * w * (want - corr) - 2.0f * zeta * w * corrVel;
                corrVel += acc * h;          // semi-implicit Euler: stable for w*h < 2
                corr += corrVel * h;
            }
        }

        // ---- Created vibrato.
        float vib = 0.0f;
        if (p.vibOn && ! snap)
        {
            const float age = st.noteAge * 1000.0f;
            const float onset = age < p.vibDelayMs ? 0.0f
                              : clamp01 ((age - p.vibDelayMs) / std::max (1.0f, p.vibOnsetMs));
            vibDriftTimer -= dt;
            if (vibDriftTimer <= 0.0f)
            {
                vibDriftTimer = 0.25f;
                vibDriftTarget = 2.0f * hash01 (seed ^ 0x5EEDu, vibCounter++) - 1.0f;
            }
            vibDrift += (vibDriftTarget - vibDrift) * (1.0f - std::exp (-dt / 0.2f));
            const float var = clamp01 (p.vibVariation);
            const float rate = p.vibRate * (1.0f + 0.12f * var * vibDrift);
            vibPhase += dt * rate;
            vibPhase -= std::floor (vibPhase);
            vib = onset * p.vibDepth * (1.0f + 0.35f * var * vibDrift) * std::sin (kTwoPi * vibPhase);
        }

        // ---- Motion: the pattern's step moves the note through the allowed notes.
        int mNote = target;
        if (p.motionOn && p.motionStep != 0)
            mNote = scaleStep (target, std::clamp (p.motionStep, -24, 24), pull.allowed == 0 ? kAllNotes : pull.allowed);
        const int mpc = ((mNote % 12) + 12) % 12;
        const float mWant = p.motionOn ? (float) (mNote - target) + 0.01f * (p.noteCents[(size_t) mpc] - p.noteCents[(size_t) pc]) : 0.0f;
        motionS = p.motionGlideMs < 0.3f ? mWant : mWant + (motionS - mWant) * std::exp (-dt / (0.001f * p.motionGlideMs));
        if (std::abs (motionS - mWant) < 1.0e-4f) motionS = mWant;
        st.motionNote = mNote;

        st.shift = corr + motionS + vib + p.transpose + 0.01f * p.detuneCents;
        st.outputPitch = pitch + st.shift;
        return st;
    }

    const TunerState& state() const noexcept { return st; }

    static float smoothstep (float e0, float e1, float x) noexcept
    {
        const float t = clamp01 ((x - e0) / std::max (1.0e-6f, e1 - e0));
        return t * t * (3.0f - 2.0f * t);
    }

private:
    static constexpr float kLeap = 1.5f;   // semitones: wider than a sung vibrato swings

    double fs = 48000.0;
    TunerState st;
    float corr = 0.0f, corrVel = 0.0f, centre = 0.0f;
    bool hasCentre = false;
    int candidate = -1, heldNote = -1;
    float candidateTime = 0.0f, silentTime = 1.0f;
    float vibPhase = 0.0f, vibDrift = 0.0f, vibDriftTarget = 0.0f, vibDriftTimer = 0.0f;
    float motionS = 0.0f;   // Motion offset (semitones), glided
    std::uint32_t vibCounter = 0;
};

} // namespace lodestone::dsp
