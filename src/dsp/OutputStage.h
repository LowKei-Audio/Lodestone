#pragma once
// Final stage, after Space:
//
//   wet -> Bass Mono -> Width (M/S)
//   out = dry + Mix * (wet - dry)       (or Delta: only what Lodestone adds)
//   -> Output gain -> Safety limiter (instant attack, -0.3 dBFS ceiling)
//
// Sound changes (presets, Dice, A/B, host loads): setTransition (true) fades
// the wet part out to the aligned dry voice (S-curve, kFadeMs); once it has
// been fully dry for kHoldMs, readyForSwap() says the new settings may be
// taken; setTransition (false) fades the new sound back in.

#include "Module.h"

namespace lodestone::dsp
{

struct OutputParams
{
    float width = 1.0f;        // 0..2
    float bassMonoHz = 20.0f;  // 20 = off .. 500
    float mix = 1.0f;
    bool delta = false;        // listen to the difference only
    float outGain = 1.0f;      // linear
    bool limiter = true;
};

class OutputStage
{
public:
    static constexpr float kCeiling = 0.9661f;   // -0.3 dBFS
    static constexpr float kFadeMs = 20.0f, kHoldMs = 10.0f;

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        for (auto& x : bassSplit) x.prepare (sampleRate);
        for (auto* s : { &widthS, &mixS, &gainS, &deltaS })
            s->prepare (sampleRate, 25.0f);
        release = std::exp (-1.0f / (0.12f * fs));
        fadeStep = 1.0f / (0.001f * kFadeMs * fs);
        holdSamples = (int) (0.001f * kHoldMs * fs);
        primed = false;
        transition = 0.0f;
        toDry = false;
        held = 0;
        reset();
    }

    void reset()
    {
        for (auto& x : bassSplit) x.reset();
        env = 0.0f;
        gr = 1.0f;
    }

    void setParams (const OutputParams& p) noexcept
    {
        params = p;
        widthS.setTarget (std::clamp (p.width, 0.0f, 2.0f));
        mixS.setTarget (clamp01 (p.mix));
        gainS.setTarget (p.outGain);
        deltaS.setTarget (p.delta ? 1.0f : 0.0f);
        for (auto& x : bassSplit) x.setCutoff (std::clamp (p.bassMonoHz, 20.0f, 500.0f));
        if (! primed)
        {
            for (auto* s : { &widthS, &mixS, &gainS, &deltaS }) s->snapTo (s->getTarget());
            primed = true;
        }
    }

    void setTransition (bool dry) noexcept { toDry = dry; if (dry) held = 0; }
    float getTransition() const noexcept { return transition; }   // 0 = normal, 1 = fully dry
    bool readyForSwap() const noexcept   { return transition >= 1.0f && held >= holdSamples; }
    // At the swap (fully dry, so unheard): the wet-side smoothers jump to the new settings.
    // Output gain keeps gliding: it also scales the dry voice that is playing.
    void snapWet() noexcept { for (auto* s : { &widthS, &mixS, &deltaS }) s->snapTo (s->getTarget()); }

    // Peak gain reduction of the last block (1 = none), for the UI meter.
    float gainReduction() const noexcept { return gr; }

    void process (const float* dryL, const float* dryR, float* L, float* R, int n) noexcept
    {
        const bool bassMono = params.bassMonoHz > 21.0f;
        float minGain = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];

            if (bassMono)
            {
                const float ll = bassSplit[0].lowpass (l), rl = bassSplit[1].lowpass (r);
                const float m = 0.5f * (ll + rl);
                l = l - ll + m;
                r = r - rl + m;
            }

            const float w = widthS.next();
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * w;
            l = mid + side;
            r = mid - side;

            // Sound-change fade: the wet part's share follows an S-curve.
            if (toDry) { if (transition < 1.0f) transition = std::min (1.0f, transition + fadeStep); else ++held; }
            else if (transition > 0.0f) transition = std::max (0.0f, transition - fadeStep);
            const float keep = transition > 0.0f ? 0.5f + 0.5f * std::cos (kPi * transition) : 1.0f;

            const float mix = mixS.next() * keep, d = deltaS.next(), g = gainS.next();
            const float outL = dryL[i] + mix * (l - dryL[i]);
            const float outR = dryR[i] + mix * (r - dryR[i]);
            const float deltaL = mix * (l - dryL[i]);
            const float deltaR = mix * (r - dryR[i]);
            float yl = g * lerp (outL, deltaL, d);
            float yr = g * lerp (outR, deltaR, d);

            if (params.limiter)
            {
                const float peak = std::max (std::abs (yl), std::abs (yr));
                env = std::max (peak, env * release);
                const float gain = env > kCeiling ? kCeiling / env : 1.0f;
                minGain = std::min (minGain, gain);
                yl = std::clamp (yl * gain, -kCeiling, kCeiling);
                yr = std::clamp (yr * gain, -kCeiling, kCeiling);
            }
            L[i] = std::isfinite (yl) ? yl : 0.0f;
            R[i] = std::isfinite (yr) ? yr : 0.0f;
        }
        gr = minGain;
    }

private:
    float fs = 48000.0f, release = 0.999f, env = 0.0f, gr = 1.0f;
    float transition = 0.0f, fadeStep = 0.001f;
    int held = 0, holdSamples = 480;
    bool toDry = false;
    OutputParams params;
    LinkwitzRiley bassSplit[2];
    Smoother widthS, mixS, gainS, deltaS;
    bool primed = false;
};

} // namespace lodestone::dsp
