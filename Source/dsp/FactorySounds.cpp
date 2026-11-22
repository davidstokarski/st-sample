// Built-in drum sounds, synthesised (so ST Sample plays out of the box, and the factory kits need no files).
// Everything is deterministic: the same name always renders the same audio.
#include "Sample.h"

namespace stsample::factory
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    struct Noise
    {
        explicit Noise (juce::int64 seed) : rng (seed) {}
        float next() noexcept { return rng.nextFloat() * 2.0f - 1.0f; }
        juce::Random rng;
    };

    /** RBJ biquad (direct form I), enough for shaping noise and metal. */
    struct Filter
    {
        enum Type { lowPass, highPass, bandPass };
        void set (Type type, double sr, double hz, double q)
        {
            const double w = twoPi * juce::jlimit (10.0, sr * 0.45, hz) / sr;
            const double cw = std::cos (w), sw = std::sin (w), alpha = sw / (2.0 * q);
            double b0 = 0, b1 = 0, b2 = 0;
            const double a0 = 1.0 + alpha, a1 = -2.0 * cw, a2 = 1.0 - alpha;
            if (type == lowPass)       { b0 = (1.0 - cw) * 0.5; b1 = 1.0 - cw; b2 = b0; }
            else if (type == highPass) { b0 = (1.0 + cw) * 0.5; b1 = -(1.0 + cw); b2 = b0; }
            else                       { b0 = alpha; b1 = 0.0; b2 = -alpha; }
            c[0] = b0 / a0; c[1] = b1 / a0; c[2] = b2 / a0; c[3] = a1 / a0; c[4] = a2 / a0;
        }
        float process (float x) noexcept
        {
            const double y = c[0] * x + c[1] * x1 + c[2] * x2 - c[3] * y1 - c[4] * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return (float) y;
        }
        double c[5] {}, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    };

    float env (double t, double attack, double decay) noexcept
    {
        const double a = attack > 0.0 ? juce::jmin (1.0, t / attack) : 1.0;
        return (float) (a * std::exp (-t / decay));
    }

    /** Six detuned square waves (the classic metallic hat / cymbal source). */
    struct Metal
    {
        explicit Metal (double baseScale = 1.0)
        {
            const double f[] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
            for (int i = 0; i < 6; ++i) freq[i] = f[i] * baseScale;
        }
        float next (double sr) noexcept
        {
            float s = 0.0f;
            for (int i = 0; i < 6; ++i)
            {
                phase[i] += freq[i] / sr;
                phase[i] -= std::floor (phase[i]);
                s += phase[i] < 0.5 ? 1.0f : -1.0f;
            }
            return s / 6.0f;
        }
        double freq[6] {}, phase[6] { 0.1, 0.37, 0.61, 0.83, 0.29, 0.52 };
    };

    using Gen = std::function<void (juce::AudioBuffer<float>&, double sr)>;

    struct Sound { const char* name; double seconds; int channels; Gen gen; };

    void kick (juce::AudioBuffer<float>& b, double sr, double base, double sweep, double sweepTime,
               double decay, double drive, double click)
    {
        double phase = 0.0;
        Noise n (11);
        Filter hp; hp.set (Filter::highPass, sr, 1800.0, 0.7);
        auto* d = b.getWritePointer (0);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double f = base + sweep * std::exp (-t / sweepTime);
            phase += f / sr;
            float s = (float) std::sin (twoPi * phase) * env (t, 0.0008, decay);
            s = std::tanh ((float) drive * s) / std::tanh ((float) drive);
            s += click * hp.process (n.next()) * env (t, 0.0, 0.0025);
            d[i] = s;
        }
    }

    void tom (juce::AudioBuffer<float>& b, double sr, double base)
    {
        double phase = 0.0, phase2 = 0.0;
        Noise n (23);
        Filter lp; lp.set (Filter::lowPass, sr, 2400.0, 0.7);
        auto* d = b.getWritePointer (0);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double f = base * (1.0 + 0.55 * std::exp (-t / 0.045));
            phase += f / sr;
            phase2 += f * 1.52 / sr;
            float s = (float) std::sin (twoPi * phase) * env (t, 0.001, 0.32);
            s += 0.25f * (float) std::sin (twoPi * phase2) * env (t, 0.001, 0.09);
            s += 0.22f * lp.process (n.next()) * env (t, 0.0, 0.03);
            d[i] = std::tanh (1.4f * s);
        }
    }

    void hat (juce::AudioBuffer<float>& b, double sr, double attack, double decay, double tone)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            Metal m (1.0 + 0.004 * c);
            Noise n (31 + c);
            Filter bp; bp.set (Filter::bandPass, sr, 10000.0 * tone, 0.9);
            Filter hp; hp.set (Filter::highPass, sr, 7000.0 * tone, 0.7);
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                const float src = 0.7f * m.next (sr) + 0.45f * n.next();
                d[i] = hp.process (bp.process (src)) * env (t, attack, decay);
            }
        }
    }

    void snare (juce::AudioBuffer<float>& b, double sr, double tone, double toneDecay, double noiseAmt,
                double noiseDecay, double noiseHz, juce::int64 seed)
    {
        double p1 = 0.0, p2 = 0.0;
        Noise n (seed);
        Filter hp; hp.set (Filter::highPass, sr, noiseHz, 0.7);
        Filter lp; lp.set (Filter::lowPass, sr, 9000.0, 0.7);
        auto* d = b.getWritePointer (0);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double f = tone * (1.0 + 0.35 * std::exp (-t / 0.012));
            p1 += f / sr;
            p2 += f * 1.86 / sr;
            float s = (float) (0.75 * std::sin (twoPi * p1) + 0.4 * std::sin (twoPi * p2)) * env (t, 0.0006, toneDecay);
            s += (float) noiseAmt * lp.process (hp.process (n.next())) * env (t, 0.0008, noiseDecay);
            d[i] = std::tanh (1.6f * s);
        }
    }

    const std::vector<Sound>& sounds()
    {
        static const std::vector<Sound> list {
            { "Kick Deep", 0.95, 1, [] (auto& b, double sr) { kick (b, sr, 44.0, 130.0, 0.032, 0.42, 1.6, 0.35); } },
            { "Kick Punch", 0.5, 1, [] (auto& b, double sr) { kick (b, sr, 56.0, 190.0, 0.018, 0.16, 2.8, 0.6); } },
            { "Kick Sub", 1.4, 1, [] (auto& b, double sr) { kick (b, sr, 41.0, 40.0, 0.05, 0.7, 1.15, 0.0); } },
            { "Kick Click", 0.08, 1, [] (auto& b, double sr)
              {
                  Noise n (5);
                  Filter bp; bp.set (Filter::bandPass, sr, 3600.0, 1.1);
                  auto* d = b.getWritePointer (0);
                  double ph = 0.0;
                  for (int i = 0; i < b.getNumSamples(); ++i)
                  {
                      const double t = i / sr;
                      ph += 1250.0 / sr;
                      d[i] = 1.6f * bp.process (n.next()) * env (t, 0.0002, 0.006)
                           + 0.8f * (float) std::sin (twoPi * ph) * env (t, 0.0, 0.0035);
                  }
              } },
            { "Snare Crack", 0.4, 1, [] (auto& b, double sr) { snare (b, sr, 196.0, 0.055, 0.9, 0.11, 1600.0, 41); } },
            { "Snare Body", 0.45, 1, [] (auto& b, double sr) { snare (b, sr, 172.0, 0.13, 0.35, 0.08, 700.0, 43); } },
            { "Snare Rattle", 0.55, 1, [] (auto& b, double sr)
              {
                  Noise n (47);
                  Filter bp; bp.set (Filter::bandPass, sr, 5200.0, 0.6);
                  Filter hp; hp.set (Filter::highPass, sr, 2200.0, 0.7);
                  auto* d = b.getWritePointer (0);
                  for (int i = 0; i < b.getNumSamples(); ++i)
                  {
                      const double t = i / sr;
                      d[i] = 1.8f * hp.process (bp.process (n.next())) * env (t, 0.002, 0.16);
                  }
              } },
            { "Clap", 0.55, 2, [] (auto& b, double sr)
              {
                  for (int c = 0; c < b.getNumChannels(); ++c)
                  {
                      Noise n (53 + c);
                      Filter bp; bp.set (Filter::bandPass, sr, 1250.0, 1.3);
                      Filter hp; hp.set (Filter::highPass, sr, 600.0, 0.7);
                      auto* d = b.getWritePointer (c);
                      const double bursts[] = { 0.0, 0.0105, 0.0215, 0.031 };
                      for (int i = 0; i < b.getNumSamples(); ++i)
                      {
                          const double t = i / sr;
                          float e = 0.0f;
                          for (double s : bursts)
                              if (t >= s) e = juce::jmax (e, env (t - s, 0.0004, 0.0065));
                          if (t >= 0.031) e = juce::jmax (e, 0.75f * env (t - 0.031, 0.0, 0.13));
                          d[i] = 2.4f * hp.process (bp.process (n.next())) * e;
                      }
                  }
              } },
            { "Rim", 0.14, 1, [] (auto& b, double sr)
              {
                  Noise n (61);
                  Filter hp; hp.set (Filter::highPass, sr, 3000.0, 0.7);
                  auto* d = b.getWritePointer (0);
                  double p1 = 0.0, p2 = 0.0;
                  for (int i = 0; i < b.getNumSamples(); ++i)
                  {
                      const double t = i / sr;
                      p1 += 1720.0 / sr;
                      p2 += 520.0 / sr;
                      d[i] = (float) (0.7 * std::sin (twoPi * p1) + 0.5 * std::sin (twoPi * p2)) * env (t, 0.0, 0.011)
                           + 0.6f * hp.process (n.next()) * env (t, 0.0, 0.003);
                  }
              } },
            { "Closed Hat", 0.16, 2, [] (auto& b, double sr) { hat (b, sr, 0.0005, 0.032, 1.0); } },
            { "Pedal Hat", 0.26, 2, [] (auto& b, double sr) { hat (b, sr, 0.006, 0.06, 0.85); } },
            { "Open Hat", 1.1, 2, [] (auto& b, double sr) { hat (b, sr, 0.001, 0.34, 0.95); } },
            { "Tom Low", 0.85, 1, [] (auto& b, double sr) { tom (b, sr, 88.0); } },
            { "Tom Mid", 0.75, 1, [] (auto& b, double sr) { tom (b, sr, 124.0); } },
            { "Tom High", 0.65, 1, [] (auto& b, double sr) { tom (b, sr, 168.0); } },
            { "Crash", 2.6, 2, [] (auto& b, double sr)
              {
                  for (int c = 0; c < b.getNumChannels(); ++c)
                  {
                      Metal m (0.92 + 0.01 * c);
                      Noise n (71 + c);
                      Filter hp; hp.set (Filter::highPass, sr, 3800.0, 0.6);
                      Filter lp; lp.set (Filter::lowPass, sr, 15000.0, 0.7);
                      auto* d = b.getWritePointer (c);
                      for (int i = 0; i < b.getNumSamples(); ++i)
                      {
                          const double t = i / sr;
                          const float e = 0.55f * env (t, 0.0015, 0.85) + 0.45f * env (t, 0.0005, 0.09);
                          d[i] = lp.process (hp.process (0.45f * m.next (sr) + 0.7f * n.next())) * e;
                      }
                  }
              } },
            { "Ride", 2.4, 2, [] (auto& b, double sr)
              {
                  const double partials[] = { 523.0, 1187.0, 1708.0, 2391.0, 3423.0, 4870.0 };
                  for (int c = 0; c < b.getNumChannels(); ++c)
                  {
                      Noise n (83 + c);
                      Filter hp; hp.set (Filter::highPass, sr, 5200.0, 0.7);
                      double ph[6] {};
                      auto* d = b.getWritePointer (c);
                      for (int i = 0; i < b.getNumSamples(); ++i)
                      {
                          const double t = i / sr;
                          float bell = 0.0f;
                          for (int k = 0; k < 6; ++k)
                          {
                              ph[k] += partials[k] * (1.0 + 0.0015 * c) / sr;
                              bell += (float) std::sin (twoPi * ph[k]) / (1.0f + 0.6f * (float) k);
                          }
                          const float wash = hp.process (n.next());
                          d[i] = 0.22f * bell * env (t, 0.0005, 0.9) + 0.55f * wash * env (t, 0.0005, 0.07)
                               + 0.22f * wash * env (t, 0.003, 0.75);
                      }
                  }
              } },
            { "Shaker", 0.18, 2, [] (auto& b, double sr)
              {
                  for (int c = 0; c < b.getNumChannels(); ++c)
                  {
                      Noise n (91 + c);
                      Filter bp; bp.set (Filter::bandPass, sr, 6500.0, 0.8);
                      auto* d = b.getWritePointer (c);
                      for (int i = 0; i < b.getNumSamples(); ++i)
                          d[i] = 1.5f * bp.process (n.next()) * env (i / sr, 0.018, 0.045);
                  }
              } },
            { "Cowbell", 0.4, 1, [] (auto& b, double sr)
              {
                  Filter bp; bp.set (Filter::bandPass, sr, 2600.0, 0.9);
                  auto* d = b.getWritePointer (0);
                  double p1 = 0.0, p2 = 0.25;
                  for (int i = 0; i < b.getNumSamples(); ++i)
                  {
                      const double t = i / sr;
                      p1 += 587.0 / sr; p1 -= std::floor (p1);
                      p2 += 845.0 / sr; p2 -= std::floor (p2);
                      const float sq = (p1 < 0.5 ? 0.5f : -0.5f) + (p2 < 0.5 ? 0.5f : -0.5f);
                      d[i] = bp.process (sq) * (0.75f * env (t, 0.0, 0.07) + 0.25f * env (t, 0.0, 0.25));
                  }
              } },
        };
        return list;
    }
}

const juce::StringArray& names()
{
    static const juce::StringArray n = []
    {
        juce::StringArray a;
        for (auto& s : sounds()) a.add (s.name);
        return a;
    }();
    return n;
}

juce::AudioBuffer<float> render (const juce::String& name, double sr)
{
    for (auto& s : sounds())
    {
        if (name != s.name) continue;
        juce::AudioBuffer<float> b (s.channels, juce::roundToInt (s.seconds * sr));
        b.clear();
        s.gen (b, sr);

        // -1 dBFS peak, 4 ms fade at the very end
        float peak = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
            peak = juce::jmax (peak, b.getMagnitude (c, 0, b.getNumSamples()));
        if (peak > 0.0f) b.applyGain (0.891f / peak);
        const int fade = juce::jmin (b.getNumSamples(), (int) (0.004 * sr));
        b.applyGainRamp (b.getNumSamples() - fade, fade, 1.0f, 0.0f);
        return b;
    }
    return {};
}

} // namespace stsample::factory
