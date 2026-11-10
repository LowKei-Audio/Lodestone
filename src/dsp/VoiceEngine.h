#pragma once
// Lodestone's voice engine.
//
//   input --> PitchDetector --> pitch track (time-stamped)
//         \-> ring buffer --> analysis marks (one per period)
//                              |
//      Tuner (per block) ----> lead / harmony / double voices: TD-PSOLA
//                              synthesis, each with its own pitch ratio,
//                              formant factor, delay, pan and level
//
// TD-PSOLA works period by period: two-period Hann grains are cut around
// each analysis mark and re-spaced at the new period. Because the grain's
// shape is kept, the voice's formants stay put (natural, no "chipmunk"); a
// grain read faster or slower moves the formants on purpose (Throat).
//
// Everything is lined up on the output timeline: the audio is delayed by the
// latency (Live: 3 ms, Studio: 15 ms) and the pitch track is looked up at
// the same moment, so corrections land on the notes they belong to. A grain
// is always cut from audio that already exists, so even Live mode keeps
// full, symmetric grains (it may reach back one period: inaudible).

#include "PitchDetector.h"
#include "Tuner.h"
#include "Module.h"

#include <vector>

namespace lodestone::dsp
{

inline constexpr int kNumHarmonies = 3;

struct HarmonyParams
{
    bool on = false;
    int interval = Ivl::thirdUp;
    float level = 0.6f;
    float pan = 0.0f;           // -1..1
    float formant = 0.0f;       // semitones
};

struct GritModeDef { const char* name; float ratio; };
inline constexpr GritModeDef kGritModes[] = { { "Sub", 0.5f }, { "Growl", 0.25f }, { "Buzz", 1.5f }, { "Metal", 2.5f } };
inline constexpr int kNumGritModes = 4;

struct EngineParams
{
    TunerParams tuner;
    int key = 0, scale = Scale::major;
    Mask customMask = kAllNotes;
    float refHz = 440.0f;
    int range = 1;
    float sensitivity = 0.5f;
    MidiMode midiMode = MidiMode::off;

    bool formantPreserve = true;
    float formant = 0.0f;       // lead throat, semitones
    float breath = 1.0f;        // unvoiced (breaths, consonants) level, 0..1.5
    float leadLevel = 1.0f;
    float grit = 0.0f;
    int gritMode = 0;

    std::array<HarmonyParams, kNumHarmonies> harmony {};
    float harmonyHumanize = 0.3f;

    bool doubleOn = false;
    float doubleLevel = 0.5f;
    float doubleSpread = 0.7f;
    float doubleDetune = 8.0f;  // cents
    float doubleDelayMs = 18.0f;

    std::uint32_t seed = 1;
};

// What the UI shows (copied out once per block).
struct EngineView
{
    bool voiced = false;
    float inputPitch = 0.0f, outputPitch = 0.0f, targetPitch = 0.0f, shift = 0.0f, flexWeight = 1.0f, clarity = 0.0f;
    int targetNote = 60;
    std::array<float, kNumHarmonies> harmonyPitch {};
    std::array<bool, kNumHarmonies> harmonyActive {};
};

class VoiceEngine
{
public:
    static constexpr int kBlock = 32;
    static constexpr int kMaxHeld = 16;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        detector.prepare (fs);
        tuner.prepare (fs);
        int r = 1;
        while (r < (int) (fs * 0.5)) r <<= 1;
        ringSize = r;
        mask = r - 1;
        in.assign ((size_t) r, 0.0f);
        energyRing.assign ((size_t) r, 0.0f);
        accLead.assign ((size_t) r, 0.0f);
        accL.assign ((size_t) r, 0.0f);
        accR.assign ((size_t) r, 0.0f);
        envAttack = 1.0f - std::exp (-1.0f / (0.004f * (float) fs));
        envRelease = 1.0f - std::exp (-1.0f / (0.012f * (float) fs));
        levelSmooth = 1.0f - std::exp (-1.0f / (0.02f * (float) fs));
        energyCoeff = std::exp (-1.0f / (0.0008f * (float) fs));
        bypassSmooth = 1.0f - std::exp (-1.0f / (0.015f * (float) fs));
        setLowLatency (lowLatency);
        reset();
    }

    void reset()
    {
        detector.reset();
        tuner.reset();
        std::fill (in.begin(), in.end(), 0.0f);
        std::fill (energyRing.begin(), energyRing.end(), 0.0f);
        std::fill (accLead.begin(), accLead.end(), 0.0f);
        std::fill (accL.begin(), accL.end(), 0.0f);
        std::fill (accR.begin(), accR.end(), 0.0f);
        n = 0;
        track.fill ({});
        lastTrackK = -1;
        lastVoicedPeriod = (float) (fs / 200.0);
        marks.fill ({});
        markCount = 0;
        nextMark = 0.0;
        energy = 0.0f;
        lastMarkVoiced = false;
        bypass = 0.0f;
        bypassWanted = false;
        for (auto& v : voices) v = {};
        shiftHist.fill (0.0f);
        lastShiftBucket = -1;
        voicedEnv = 0.0f;
        gritPhase = 0.0f;
        chroma.fill (0.0f);
        for (auto& h : harmonyInterval) h = 0.0f;
        for (auto& g : harmonyGate) g = 0.0f;
        for (auto& d : drift) d = {};
        view = {};
    }

    // Live (low latency) or Studio (lined up): sets the audio delay.
    void setLowLatency (bool live) noexcept
    {
        lowLatency = live;
        latency = std::max (16, (int) std::lround (fs * (live ? 0.003 : 0.015)));
    }
    int getLatency() const noexcept { return latency; }

    void setParams (const EngineParams& p) noexcept
    {
        params = p;
        detector.setRange (p.range);
        detector.setSensitivity (p.sensitivity);
    }

    // Held MIDI notes, lowest first, plus the most recent one (Exact mode).
    void setHeldNotes (const int* notes, int count, int newest) noexcept
    {
        heldCount = std::clamp (count, 0, kMaxHeld);
        for (int i = 0; i < heldCount; ++i) held[(size_t) i] = notes[i];
        newestNote = newest;
    }

    // Processes up to kBlock samples. in: mono input. Writes the processed
    // stereo signal and the equally delayed dry signal.
    void process (const float* input, float* outL, float* outR, float* dryOut, int count) noexcept
    {
        // 1. Input: ring, mark low-pass, detector.
        for (int i = 0; i < count; ++i)
        {
            const float x = input[i];
            in[(size_t) (n & mask)] = x;
            // Short-term energy (~0.8 ms): peaks at each glottal pulse.
            energy = x * x + energyCoeff * (energy - x * x);
            energyRing[(size_t) (n & mask)] = energy;
            ++n;
            if (detector.push (x)) writeTrack (detector.reading());
            placeMarks();
        }

        // 2. Control: tuner at the output time of this block.
        const std::int64_t tOut0 = n - count - latency;
        control (tOut0, count);

        // 3. Synthesis, sample by sample on the output timeline.
        const float ceilingGain = 1.0f;
        for (int i = 0; i < count; ++i)
        {
            const std::int64_t t = tOut0 + i;
            for (int v = 0; v < kNumVoices; ++v) emitGrains (v, t);

            const auto& tr = trackAt (t);
            const float target = tr.voiced ? 1.0f : 0.0f;
            voicedEnv += (target - voicedEnv) * (target > voicedEnv ? envAttack : envRelease);

            const size_t k = (size_t) (t & mask);
            const float dry = t >= 0 ? in[k] : 0.0f;
            float lead = accLead[k];
            float l = accL[k], r = accR[k];
            accLead[k] = accL[k] = accR[k] = 0.0f;

            // Grit: ring modulation locked to the output pitch.
            if (params.grit > 0.001f)
            {
                const float f0 = (float) fs / std::max (8.0f, lastOutPeriod);
                gritPhase += f0 * kGritModes[std::clamp (params.gritMode, 0, kNumGritModes - 1)].ratio / (float) fs;
                gritPhase -= std::floor (gritPhase);
                const float g = clamp01 (params.grit) * voicedEnv;
                // Power-neutral: the ring-modulated part is louder by the loss it causes.
                const float norm = 1.0f / std::sqrt ((1.0f - g) * (1.0f - g) + 1.28f * g * g);
                lead *= norm * (1.0f - g + g * 1.6f * std::sin (kTwoPi * gritPhase));
            }

            // Nothing to change on the lead: crossfade to the clean, delayed voice.
            bypass += ((bypassWanted ? 1.0f : 0.0f) - bypass) * bypassSmooth;
            const float tuned = lead + bypass * (dry - lead);
            const float leadOut = params.leadLevel * (voicedEnv * tuned + (1.0f - voicedEnv) * params.breath * dry);
            outL[i] = (leadOut + l) * ceilingGain;
            outR[i] = (leadOut + r) * ceilingGain;
            dryOut[i] = dry;
        }
    }

    const EngineView& getView() const noexcept { return view; }
    const std::array<float, 12>& getChroma() const noexcept { return chroma; }
    void clearChroma() noexcept { chroma.fill (0.0f); }
    const PitchDetector& getDetector() const noexcept { return detector; }

    // Allowed notes right now (scale, custom notes, or held MIDI notes).
    Mask allowedMask() const noexcept
    {
        if (params.midiMode == MidiMode::scale && heldCount > 0)
        {
            Mask m = 0;
            for (int i = 0; i < heldCount; ++i) m |= (Mask) (1u << (((held[(size_t) i] % 12) + 12) % 12));
            return m;
        }
        return scaleMask (params.key, params.scale, params.customMask);
    }

private:
    // ---------------------------------------------------------------- track
    struct TrackEntry { bool voiced = false; float period = 0.0f, pitch = 0.0f, clarity = 0.0f; };
    static constexpr int kTrackSize = 1024;

    void writeTrack (const PitchReading& r) noexcept
    {
        const int hop = detector.hopSamples();
        const std::int64_t kc = r.centre >= 0 ? r.centre / hop : 0;
        TrackEntry e;
        e.voiced = r.voiced;
        e.period = r.voiced ? r.period : 0.0f;
        e.pitch = r.voiced ? 69.0f + 12.0f * std::log2 (r.hz / std::max (1.0f, params.refHz)) : 0.0f;
        e.clarity = r.clarity;
        if (r.voiced) lastVoicedPeriod = r.period;
        if (lastTrackK < 0 || kc - lastTrackK > kTrackSize) lastTrackK = kc - 1;
        for (std::int64_t k = lastTrackK + 1; k <= kc; ++k) track[(size_t) (k & (kTrackSize - 1))] = e;
        lastTrackK = std::max (lastTrackK, kc);
    }

    const TrackEntry& trackAt (std::int64_t time) const noexcept
    {
        static const TrackEntry none {};
        if (lastTrackK < 0) return none;
        const int hop = detector.hopSamples();
        std::int64_t k = time >= 0 ? time / hop : 0;
        if (k > lastTrackK) k = lastTrackK;
        if (lastTrackK - k >= kTrackSize - 1) return none;
        return track[(size_t) (k & (kTrackSize - 1))];
    }

    // ---------------------------------------------------------------- marks
    struct Mark { std::int64_t pos = 0; float period = 0.0f; };
    static constexpr int kMarks = 512;

    void placeMarks() noexcept
    {
        for (;;)
        {
            const auto& tr = trackAt ((std::int64_t) nextMark);
            const float P = std::clamp (tr.voiced ? tr.period : lastVoicedPeriod, (float) (fs / 2000.0), (float) (fs / 25.0));
            // Pitch-synchronous marks sit on the energy peak of each pulse, so
            // every grain is centred on one pulse (its spectrum is then the
            // voice's smooth envelope: formants stay put, no missing
            // fundamental). The first mark of a phrase looks wide; after that a
            // narrow window keeps it locked to the same pulse every period.
            const bool locked = tr.voiced && lastMarkVoiced;
            const float search = tr.voiced ? (locked ? 0.15f : 0.5f) * P : 0.0f;
            if (nextMark + search + 2.0 >= (double) n) return;

            std::int64_t pos = (std::int64_t) std::llround (nextMark);
            if (tr.voiced)
            {
                std::int64_t best = pos;
                float bv = -1.0f;
                for (std::int64_t q = pos - (std::int64_t) search; q <= pos + (std::int64_t) search; ++q)
                {
                    const float v = energyRing[(size_t) (q & mask)];
                    if (v > bv) { bv = v; best = q; }
                }
                pos = best;
            }
            if (markCount > 0)
            {
                const auto& prev = marks[(size_t) ((markCount - 1) & (kMarks - 1))];
                const float lo = locked ? 0.7f : 0.5f;
                pos = std::clamp (pos, prev.pos + (std::int64_t) (lo * P), prev.pos + (std::int64_t) (1.5f * P) + 1);
            }
            lastMarkVoiced = tr.voiced;
            marks[(size_t) (markCount & (kMarks - 1))] = { pos, P };
            ++markCount;
            nextMark = (double) pos + P;
        }
    }

    // Nearest mark to `time` whose whole grain (pos +/- period) already exists.
    const Mark* pickMark (double time) const noexcept
    {
        const Mark* best = nullptr;
        double bestD = 1.0e18;
        const std::int64_t newest = n - 1;
        for (int k = 0; k < 256 && k < markCount; ++k)
        {
            const auto& m = marks[(size_t) ((markCount - 1 - k) & (kMarks - 1))];
            if ((double) m.pos + m.period + 2.0 > (double) newest) continue;
            const double d = std::abs ((double) m.pos - time);
            if (d < bestD) { bestD = d; best = &m; }
            else if ((double) m.pos < time) break;   // marks only get further away from here
        }
        return best;
    }

    // ---------------------------------------------------------------- voices
    enum { leadVoice = 0, harm1, harm2, harm3, dblA, dblB, kNumVoices };

    struct Voice
    {
        double next = 0.0;          // next synthesis mark (output timeline)
        bool active = false;
        float shift = 0.0f;         // semitones vs the input
        float formant = 1.0f;       // grain read rate
        float level = 0.0f;         // smoothed
        float targetLevel = 0.0f;
        float panL = 0.7071f, panR = 0.7071f;
        float delay = 0.0f;         // samples
        bool lead = false;
    };

    struct Drift { float value = 0.0f, target = 0.0f, timer = 0.0f; std::uint32_t count = 0; };

    void control (std::int64_t tOut, int count) noexcept
    {
        const float dt = (float) count / (float) fs;
        const auto& tr = trackAt (tOut);
        const Mask allowed = allowedMask();

        int exact = -1;
        if (params.midiMode == MidiMode::exact && heldCount > 0) exact = newestNote;

        const auto& ts = tuner.advance (params.tuner, tr.voiced, tr.pitch, allowed, exact, dt, params.seed);

        // Pitch-class histogram for key detection.
        if (tr.voiced && tr.clarity > 0.5f)
            chroma[(size_t) ((((int) std::lround (tr.pitch)) % 12 + 12) % 12)] += dt * tr.clarity;

        // Shift history (doubles and delayed harmonies look it up at their own time).
        const std::int64_t bucket = (tOut >= 0 ? tOut : 0) >> 5;
        for (std::int64_t b = std::max (lastShiftBucket + 1, bucket - kShiftHist + 1); b <= bucket; ++b)
            shiftHist[(size_t) (b & (kShiftHist - 1))] = ts.shift;
        lastShiftBucket = bucket;

        const float formantLead = std::pow (2.0f, params.formant / 12.0f);
        const float leadRatio = std::pow (2.0f, ts.shift / 12.0f);

        auto& lead = voices[leadVoice];
        lead.lead = true;
        lead.active = true;
        lead.shift = ts.shift;
        lead.formant = (params.formantPreserve ? 1.0f : leadRatio) * formantLead;
        lead.targetLevel = 1.0f;
        lead.delay = 0.0f;
        lastOutPeriod = (tr.voiced ? tr.period : lastVoicedPeriod) / std::max (0.1f, leadRatio);
        bypassWanted = std::abs (ts.shift) < 0.005f && std::abs (params.formant) < 0.01f && params.grit < 0.001f;

        // Harmonies: scale-step (or fixed) intervals from the target note, or
        // held MIDI notes in Harmony mode.
        const bool midiHarm = params.midiMode == MidiMode::harmony;
        const float glide = 1.0f - std::exp (-dt / 0.03f);
        for (int h = 0; h < kNumHarmonies; ++h)
        {
            const auto& hp = params.harmony[(size_t) h];
            auto& v = voices[(size_t) (harm1 + h)];
            bool on = hp.on;
            float interval = harmonyInterval[(size_t) h];
            if (midiHarm)
            {
                on = h < heldCount;
                if (on) interval = (float) (held[(size_t) h] - ts.targetNote);
            }
            else if (on)
                interval = (float) (harmonyNote (ts.targetNote, hp.interval, allowed) - ts.targetNote);

            const float wantGate = on && ts.voiced ? 1.0f : 0.0f;
            harmonyGate[(size_t) h] += (wantGate - harmonyGate[(size_t) h]) * (1.0f - std::exp (-dt / 0.02f));
            if (std::abs (interval - harmonyInterval[(size_t) h]) > 6.0f && harmonyGate[(size_t) h] < 0.05f)
                harmonyInterval[(size_t) h] = interval;   // first entry: no long glide
            else
                harmonyInterval[(size_t) h] += glide * (interval - harmonyInterval[(size_t) h]);

            const float d = advanceDrift (h, dt) * params.harmonyHumanize;
            v.active = on || harmonyGate[(size_t) h] > 0.001f;
            v.shift = harmonyInterval[(size_t) h] + 0.12f * d;   // + lead shift at its own time
            const float ratio = std::pow (2.0f, (ts.shift + v.shift) / 12.0f);
            v.formant = (params.formantPreserve ? 1.0f : ratio) * std::pow (2.0f, hp.formant / 12.0f);
            v.targetLevel = hp.level * harmonyGate[(size_t) h];
            setPan (v, hp.pan);
            v.delay = (float) fs * 0.001f * (4.0f + 14.0f * params.harmonyHumanize * (0.5f + 0.5f * d));

            view.harmonyPitch[(size_t) h] = ts.outputPitch + harmonyInterval[(size_t) h];
            view.harmonyActive[(size_t) h] = harmonyGate[(size_t) h] > 0.1f;
        }

        // Doubles: two takes, slightly early/late, a little sharp/flat, panned apart.
        for (int k = 0; k < 2; ++k)
        {
            auto& v = voices[(size_t) (dblA + k)];
            const float d = advanceDrift (kNumHarmonies + k, dt);
            const float sign = k == 0 ? -1.0f : 1.0f;
            v.active = params.doubleOn || v.level > 0.0005f;
            v.delay = (float) fs * 0.001f * std::max (1.0f, params.doubleDelayMs * (k == 0 ? 0.65f : 1.35f));
            v.shift = 0.01f * (sign * params.doubleDetune + 0.5f * params.doubleDetune * d);   // + lead shift at its own time
            v.formant = 1.0f;
            v.targetLevel = params.doubleOn ? params.doubleLevel : 0.0f;
            setPan (v, sign * params.doubleSpread);
        }

        // View for the UI.
        view.voiced = ts.voiced;
        view.inputPitch = ts.inputPitch;
        view.outputPitch = ts.outputPitch;
        view.targetPitch = ts.targetPitch;
        view.targetNote = ts.targetNote;
        view.shift = ts.shift;
        view.flexWeight = ts.flexWeight;
        view.clarity = tr.clarity;
    }

    float advanceDrift (int i, float dt) noexcept
    {
        auto& d = drift[(size_t) i];
        d.timer -= dt;
        if (d.timer <= 0.0f)
        {
            d.timer = 0.18f + 0.3f * hash01 (params.seed + 31u * (std::uint32_t) i, d.count);
            d.target = 2.0f * hash01 (params.seed ^ (0xD1F7u + (std::uint32_t) i), d.count++) - 1.0f;
        }
        d.value += (d.target - d.value) * (1.0f - std::exp (-dt / 0.15f));
        return d.value;
    }

    static void setPan (Voice& v, float pan) noexcept
    {
        const float a = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
        v.panL = std::cos (a);
        v.panR = std::sin (a);
    }

    float shiftAt (std::int64_t time) const noexcept
    {
        std::int64_t b = (time >= 0 ? time : 0) >> 5;
        if (b > lastShiftBucket) b = lastShiftBucket;
        if (lastShiftBucket - b >= kShiftHist - 1) b = lastShiftBucket - (kShiftHist - 2);
        return shiftHist[(size_t) (b & (kShiftHist - 1))];
    }

    void emitGrains (int vi, std::int64_t t) noexcept
    {
        auto& v = voices[(size_t) vi];
        v.level += (v.targetLevel - v.level) * levelSmooth;
        if (! v.active && v.level < 0.0005f) { v.next = (double) t; return; }
        if (v.next < (double) t - 4.0 * fs / 25.0) v.next = (double) t;   // after a reset or a long idle

        for (int guard = 0; guard < 8; ++guard)
        {
            const double srcTime = v.next - v.delay;
            float shift = v.shift;
            if (! v.lead) shift += shiftAt ((std::int64_t) srcTime);
            const auto* m = pickMark (srcTime);
            const float P = m != nullptr ? m->period : lastVoicedPeriod;
            // No shift: grains go back exactly where they were cut (phase-locked
            // to the input), so the lead stays in time with the dry signal.
            if (v.lead && m != nullptr && std::abs (shift) < 0.02f && std::abs (v.formant - 1.0f) < 1.0e-3f)
                v.next = (double) m->pos;
            const float hOut = P / std::max (0.25f, v.formant);
            if (v.next - hOut > (double) t) return;
            const float ratio = std::clamp (std::pow (2.0f, shift / 12.0f), 0.25f, 4.0f);
            const float spacing = P / ratio;

            if (m != nullptr && v.level > 0.0001f)
            {
                const float gain = v.level * std::clamp (std::sqrt (spacing / hOut), 0.3f, 1.4f)
                                 * (v.lead ? 1.0f : voicedEnv);
                writeGrain (*m, v, hOut, gain, t);
            }
            v.next += std::max (2.0f, spacing);
        }
    }

    void writeGrain (const Mark& m, const Voice& v, float hOut, float gain, std::int64_t t) noexcept
    {
        const std::int64_t first = std::max (t, (std::int64_t) std::ceil (v.next - hOut));
        const std::int64_t last = (std::int64_t) std::floor (v.next + hOut);
        const float inv = 1.0f / hOut;
        for (std::int64_t k = first; k <= last; ++k)
        {
            const float j = (float) ((double) k - v.next);
            const float w = 0.5f + 0.5f * std::cos (kPi * j * inv);
            const float x = sampleAt ((double) m.pos + (double) (j * v.formant));
            const float y = gain * w * x;
            const size_t idx = (size_t) (k & mask);
            if (v.lead) accLead[idx] += y;
            else { accL[idx] += y * v.panL; accR[idx] += y * v.panR; }
        }
    }

    // 4-point cubic (Catmull-Rom) read of the input ring.
    float sampleAt (double pos) const noexcept
    {
        const double fl = std::floor (pos);
        const float f = (float) (pos - fl);
        const auto i = (std::int64_t) fl;
        const float xm = in[(size_t) ((i - 1) & mask)], x0 = in[(size_t) (i & mask)];
        const float x1 = in[(size_t) ((i + 1) & mask)], x2 = in[(size_t) ((i + 2) & mask)];
        return x0 + 0.5f * f * (x1 - xm + f * (2.0f * xm - 5.0f * x0 + 4.0f * x1 - x2 + f * (3.0f * (x0 - x1) + x2 - xm)));
    }

    // ---------------------------------------------------------------- state
    double fs = 48000.0;
    EngineParams params;
    PitchDetector detector;
    Tuner tuner;
    int ringSize = 0, latency = 720;
    bool lowLatency = false;
    std::int64_t mask = 0, n = 0;
    std::vector<float> in, energyRing, accLead, accL, accR;

    std::array<TrackEntry, kTrackSize> track {};
    std::int64_t lastTrackK = -1;
    float lastVoicedPeriod = 240.0f, lastOutPeriod = 240.0f;

    std::array<Mark, kMarks> marks {};
    int markCount = 0;
    double nextMark = 0.0;

    std::array<Voice, kNumVoices> voices {};
    static constexpr int kShiftHist = 1024;
    std::array<float, kShiftHist> shiftHist {};
    std::int64_t lastShiftBucket = -1;

    std::array<float, kNumHarmonies> harmonyInterval {}, harmonyGate {};
    std::array<Drift, kNumHarmonies + 2> drift {};

    std::array<int, kMaxHeld> held {};
    int heldCount = 0, newestNote = -1;

    float voicedEnv = 0.0f, gritPhase = 0.0f, energy = 0.0f, energyCoeff = 0.98f;
    bool lastMarkVoiced = false, bypassWanted = false;
    float bypass = 0.0f, bypassSmooth = 0.002f;
    float envAttack = 0.02f, envRelease = 0.006f, levelSmooth = 0.002f;
    std::array<float, 12> chroma {};
    EngineView view;

};

} // namespace lodestone::dsp
