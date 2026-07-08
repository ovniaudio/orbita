// Test [quality] — la señal NUNCA pierde calidad (batería UAD-grade del sello, portada de
// shared/template/tests/QualityStub.h del repo ovni; acá standalone porque ÓRBITA no linkea ese harness).
//   1. PISO DE SILENCIO: tras cargar y callar, la salida muere (sin cola zombie/self-noise/denormals).
//   2. DC OFFSET: un seno (sin DC) no genera DC.
//   3. NULL DEL DRY (MIX=0): con mix en 0 la salida ES el dry (alineado por la latencia reportada).
//   4. LINEALIDAD: −20 dB de entrada = exactamente −20 dB de salida wet (sin saturación oculta).
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>
#include <PluginProcessor.h>

namespace
{
constexpr double SR = 48000.0;
constexpr int    N  = 512;

void setP (PluginProcessor& proc, const char* id, float v01)
{
    if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
}

struct WhiteQ
{
    std::uint32_t s = 0x2545F491u;
    float next() noexcept { s = s * 1664525u + 1013904223u; return ((float) (s >> 9) * (1.0f / 4194304.0f)) - 1.0f; }
};

double dB (double x) noexcept { return 20.0 * std::log10 (x + 1.0e-30); }
} // namespace

TEST_CASE ("ORBIT: calidad de senal (piso/DC/null mix0/linealidad)", "[quality]")
{
    // ── 1. PISO DE SILENCIO ─────────────────────────────────────────────────────────────────────
    double floorDb = 0.0;
    {
        PluginProcessor proc;
        setP (proc, "mix", 1.0f);
        proc.prepareToPlay (SR, N);
        WhiteQ w;
        for (int blk = 0; blk < (int) std::ceil (0.3 * SR / N); ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int i = 0; i < N; ++i) { const float x = 0.4f * w.next(); buf.setSample (0, i, x); buf.setSample (1, i, x); }
            proc.processBlock (buf, midi);
        }
        const int tailBlocks = (int) std::ceil (10.0 * SR / N);
        const int measFrom   = tailBlocks - (int) std::ceil (1.0 * SR / N);
        double acc = 0.0; long cnt = 0;
        for (int blk = 0; blk < tailBlocks; ++blk)
        {
            juce::AudioBuffer<float> z (2, N); juce::MidiBuffer midi; z.clear();
            proc.processBlock (z, midi);
            if (blk >= measFrom)
                for (int ch = 0; ch < 2; ++ch)
                { auto* d = z.getReadPointer (ch); for (int i = 0; i < N; ++i) { acc += (double) d[i] * d[i]; ++cnt; } }
        }
        floorDb = dB (std::sqrt (acc / juce::jmax (1L, cnt)));
    }

    // ── 2. DC OFFSET ────────────────────────────────────────────────────────────────────────────
    double dcDb = 0.0;
    {
        PluginProcessor proc;
        setP (proc, "mix", 1.0f);
        proc.prepareToPlay (SR, N);
        const int total = (int) std::ceil (5.0 * SR / N);
        const int from  = (int) std::ceil (2.0 * SR / N);
        double sum = 0.0; long cnt = 0; long g = 0;
        for (int blk = 0; blk < total; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int i = 0; i < N; ++i, ++g)
            {
                const float x = 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 468.75 * (double) g / SR);
                buf.setSample (0, i, x); buf.setSample (1, i, x);
            }
            proc.processBlock (buf, midi);
            if (blk >= from)
                for (int ch = 0; ch < 2; ++ch)
                { auto* d = buf.getReadPointer (ch); for (int i = 0; i < N; ++i) { sum += (double) d[i]; ++cnt; } }
        }
        dcDb = dB (std::abs (sum / juce::jmax (1L, cnt)) / 0.25);
    }

    // ── 3. NULL DEL DRY (MIX=0) ─────────────────────────────────────────────────────────────────
    double nullDb = 0.0; int latency = 0;
    {
        PluginProcessor proc;
        setP (proc, "mix", 0.0f);
        proc.prepareToPlay (SR, N);
        latency = proc.getLatencySamples();
        WhiteQ w;
        const int total = (int) std::ceil (4.0 * SR / N);
        std::vector<float> inHist, outHist;
        inHist.reserve ((size_t) total * N); outHist.reserve ((size_t) total * N);
        for (int blk = 0; blk < total; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int i = 0; i < N; ++i) { const float x = 0.25f * w.next(); buf.setSample (0, i, x); buf.setSample (1, i, x); inHist.push_back (x); }
            proc.processBlock (buf, midi);
            auto* d = buf.getReadPointer (0);
            for (int i = 0; i < N; ++i) outHist.push_back (d[i]);
        }
        const long from = (long) std::ceil (1.5 * SR);
        double res = 0.0, ref = 0.0; long cnt = 0;
        for (long n = from; n < (long) outHist.size(); ++n)
        {
            const long m = n - (long) latency;
            if (m < 0 || m >= (long) inHist.size()) continue;
            const double e = (double) outHist[(size_t) n] - (double) inHist[(size_t) m];
            res += e * e; ref += (double) inHist[(size_t) m] * inHist[(size_t) m]; ++cnt;
        }
        nullDb = dB (std::sqrt (res / juce::jmax (1L, cnt)) / (std::sqrt (ref / juce::jmax (1L, cnt)) + 1.0e-30));
    }

    // ── 4. LINEALIDAD ───────────────────────────────────────────────────────────────────────────
    double linDeltaDb = 0.0;
    {
        auto wetRms = [] (float amp) -> double
        {
            PluginProcessor proc;
            setP (proc, "mix", 1.0f);
            proc.prepareToPlay (SR, N);
            WhiteQ w;
            const int total = (int) std::ceil (4.0 * SR / N);
            const int from  = (int) std::ceil (1.5 * SR / N);
            double acc = 0.0; long cnt = 0;
            for (int blk = 0; blk < total; ++blk)
            {
                juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
                for (int i = 0; i < N; ++i) { const float x = amp * w.next(); buf.setSample (0, i, x); buf.setSample (1, i, x); }
                proc.processBlock (buf, midi);
                if (blk >= from)
                    for (int ch = 0; ch < 2; ++ch)
                    { auto* d = buf.getReadPointer (ch); for (int i = 0; i < N; ++i) { acc += (double) d[i] * d[i]; ++cnt; } }
            }
            return std::sqrt (acc / juce::jmax (1L, cnt));
        };
        linDeltaDb = dB (wetRms (0.05f)) - dB (wetRms (0.005f));
    }

    std::printf ("QUALITY[orbit] floor=%.1f dBFS  DC=%.1f dB  null(mix0)=%.1f dB (lat=%d)  lin=%.2f dB (esp. 20.00)\n",
                 floorDb, dcDb, nullDb, latency, linDeltaDb);

    REQUIRE (std::isfinite (floorDb));
    REQUIRE (floorDb < -80.0);
    REQUIRE (dcDb    < -60.0);
    REQUIRE (nullDb  < -60.0);
    REQUIRE (std::abs (linDeltaDb - 20.0) < 0.5);
}
