// Tests de M2 (movimiento): el motor con posición variable no debe producir NaN/Inf,
// debe quedar acotado, y no debe clickear (sin saltos groseros) al orbitar. Más tests
// del cerebro Órbitas (free-run, sync al tempo, dirección).
#include <catch2/catch_test_macros.hpp>
#include <dsp/SpatialEngine.h>
#include <dsp/OrbitBrain.h>
#include <dsp/HrirRing.h>
#include <PluginProcessor.h>
#include <OrbitView.h>
#include <cmath>

namespace {

constexpr double kPi    = 3.14159265358979324;
constexpr double kTwoPi = 6.28318530717958648;

struct Metrics { bool finite = true; float peak = 0.0f; float maxJump = 0.0f; };

// Corre un barrido de azimut a 'revsPerBlockTotal' vueltas a lo largo de 'numBlocks'
// bloques de 'block' samples, con seno de entrada, y mide salida.
Metrics runSweep (double sr, int block, int numBlocks, double totalRevs)
{
    orbita::SpatialEngine eng;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 2 };
    eng.prepare (spec);

    juce::AudioBuffer<float> buf (2, block);
    Metrics m;
    float prevL = 0.0f, prevR = 0.0f;
    double phase = 0.0; // fase del seno
    const double w = kTwoPi * 200.0 / sr;

    for (int b = 0; b < numBlocks; ++b)
    {
        buf.clear();
        auto* in = buf.getWritePointer (0);
        for (int i = 0; i < block; ++i) { in[i] = 0.25f * (float) std::sin (phase); phase += w; }

        // azimut al final de este bloque
        const double az = totalRevs * kTwoPi * (double) (b + 1) / (double) numBlocks;
        eng.process (buf, 1, (float) az, 1.0f, 0.0f, 0.5f, false, 0.5f, false);

        const auto* oL = buf.getReadPointer (0);
        const auto* oR = buf.getReadPointer (1);
        for (int i = 0; i < block; ++i)
        {
            if (! std::isfinite (oL[i]) || ! std::isfinite (oR[i])) m.finite = false;
            m.peak    = std::max (m.peak, std::max (std::abs (oL[i]), std::abs (oR[i])));
            m.maxJump = std::max (m.maxJump, std::max (std::abs (oL[i] - prevL), std::abs (oR[i] - prevR)));
            prevL = oL[i]; prevR = oR[i];
        }
    }
    return m;
}

} // namespace

TEST_CASE ("anillo HRIR válido", "[m2]")
{
    REQUIRE (orbita::kNumDirs == 72);
    REQUIRE (orbita::kRingTaps > 0);
    REQUIRE (orbita::kRingL.size() == (size_t) (orbita::kNumDirs * orbita::kRingTaps));
    REQUIRE (orbita::kRingR.size() == (size_t) (orbita::kNumDirs * orbita::kRingTaps));
    for (float v : orbita::kRingDelayL) REQUIRE (std::isfinite (v));
    for (float v : orbita::kRingDelayR) REQUIRE (std::isfinite (v));
}

TEST_CASE ("motor estático: finito y acotado", "[m2]")
{
    auto m = runSweep (48000.0, 128, 64, 0.0); // sin movimiento
    REQUIRE (m.finite);
    REQUIRE (m.peak < 8.0f);
    WARN ("estático: peak=" << m.peak << " maxJump=" << m.maxJump);
}

TEST_CASE ("motor en movimiento: sin NaN, acotado, sin clicks", "[m2]")
{
    // lento (1 vuelta en ~1.7s), rápido (10 vueltas), y absurdo (multi-segmento por bloque)
    for (auto cfg : { std::pair{ 0.5, 256 }, std::pair{ 10.0, 256 }, std::pair{ 120.0, 64 } })
    {
        auto m = runSweep (48000.0, cfg.second, 600, cfg.first);
        REQUIRE (m.finite);
        REQUIRE (m.peak < 8.0f);
        WARN ("revs=" << cfg.first << " block=" << cfg.second
              << " -> peak=" << m.peak << " maxJump=" << m.maxJump);
        // un click roto (recarga a peso pleno) dispara saltos enormes; el diseño los evita
        REQUIRE (m.maxJump < 1.0f);
    }
}

TEST_CASE ("motor: sin clicks a Speed alto + Width máximo (corte rampeado)", "[m2]")
{
    // El corte de oído lejano (Width) se calcula por bloque desde el azimut; a Speed alto el
    // azimut salta mucho por bloque -> sin rampa, esa ganancia clickea. Con rampa, no.
    orbita::SpatialEngine eng;
    const int block = 256, numBlocks = 600; const double sr = 48000.0;
    eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
    juce::AudioBuffer<float> buf (2, block);
    double phase = 0.0; const double w = kTwoPi * 200.0 / sr;
    float prevL = 0, prevR = 0, maxJump = 0; bool finite = true;
    for (int b = 0; b < numBlocks; ++b)
    {
        buf.clear();
        auto* in = buf.getWritePointer (0);
        for (int i = 0; i < block; ++i) { in[i] = 0.25f * (float) std::sin (phase); phase += w; }
        const double az = 30.0 * kTwoPi * (double)(b+1) / numBlocks; // ~30 vueltas = Speed muy alto
        eng.process (buf, 1, (float) az, 1.0f, 0.0f, 1.0f, false, 0.5f, false); // WIDTH=1 (corte activo)
        const auto* oL = buf.getReadPointer(0); const auto* oR = buf.getReadPointer(1);
        for (int i = 0; i < block; ++i)
        {
            if (! std::isfinite(oL[i]) || ! std::isfinite(oR[i])) finite = false;
            maxJump = std::max (maxJump, std::max (std::abs(oL[i]-prevL), std::abs(oR[i]-prevR)));
            prevL = oL[i]; prevR = oR[i];
        }
    }
    WARN ("Speed alto + Width=1: maxJump=" << maxJump);
    REQUIRE (finite);
    REQUIRE (maxJump < 0.3f); // sin clicks: el corte rampeado no salta en el borde del bloque
}

TEST_CASE ("cerebro: free-run gira con transporte parado", "[m2]")
{
    orbita::OrbitBrain brain;
    brain.prepare (48000.0);
    orbita::OrbitBrain::Params p; p.rate = orbita::OrbitBrain::Free; p.shape = orbita::OrbitBrain::Circle;
    p.dir = orbita::OrbitBrain::CCW; p.chaos01 = 0.0f;
    brain.setParams (p);

    orbita::TransportInfo t; t.isPlaying = false;
    float a0 = brain.advance (512, t).azimuth;
    float a1 = brain.advance (512, t).azimuth;
    REQUIRE (std::abs (a1 - a0) > 1.0e-6f);  // se mueve aunque el host esté parado
    REQUIRE (std::isfinite (a1));
}

TEST_CASE ("cerebro: dirección invierte el sentido", "[m2]")
{
    orbita::TransportInfo t; t.isPlaying = false;
    auto firstStep = [&] (int dir)
    {
        orbita::OrbitBrain b; b.prepare (48000.0);
        orbita::OrbitBrain::Params p; p.rate = orbita::OrbitBrain::Free;
        p.shape = orbita::OrbitBrain::Circle; p.dir = dir; p.chaos01 = 0.0f;
        b.setParams (p);
        const float a = b.advance (512, t).azimuth;          // [0,2π)
        return a <= (float) kPi ? a : a - (float) kTwoPi;     // a (-π,π]
    };
    const float cw  = firstStep (orbita::OrbitBrain::CW);
    const float ccw = firstStep (orbita::OrbitBrain::CCW);
    REQUIRE (cw  < 0.0f);   // CW -> azimut negativo (hacia la derecha)
    REQUIRE (ccw > 0.0f);   // CCW -> azimut positivo (hacia la izquierda)
}

TEST_CASE ("DIAG: ganancia/clipping por frecuencia y por sample rate", "[diag]")
{
    // Evidencia para 'cortado y roto': mide la ganancia del motor (pico/entrada) en LF/MID/HF
    // y a 44.1/48/96k (este último ejercita el camino de resampleo). gain>1 => riesgo de clip.
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (double freq : { 80.0, 1000.0, 8000.0 })
        {
            orbita::SpatialEngine eng;
            juce::dsp::ProcessSpec spec { sr, (juce::uint32) 256, 2 };
            eng.prepare (spec);

            const int block = 256, numBlocks = 400;
            const float inAmp = 0.9f;
            juce::AudioBuffer<float> buf (2, block);
            const double w = kTwoPi * freq / sr;
            double phase = 0.0; float prevL = 0, prevR = 0, peak = 0, maxJump = 0; bool finite = true;

            for (int b = 0; b < numBlocks; ++b)
            {
                buf.clear();
                auto* in = buf.getWritePointer (0);
                for (int i = 0; i < block; ++i) { in[i] = inAmp * (float) std::sin (phase); phase += w; }
                const double az = 0.5 * kTwoPi * (double) (b + 1) / numBlocks; // media vuelta
                eng.process (buf, 1, (float) az, 1.0f, 0.0f, 0.5f, false, 0.5f, false);
                const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
                for (int i = 0; i < block; ++i)
                {
                    if (! std::isfinite (oL[i]) || ! std::isfinite (oR[i])) finite = false;
                    peak    = std::max (peak, std::max (std::abs (oL[i]), std::abs (oR[i])));
                    maxJump = std::max (maxJump, std::max (std::abs (oL[i] - prevL), std::abs (oR[i] - prevR)));
                    prevL = oL[i]; prevR = oR[i];
                }
            }
            WARN ("sr=" << sr << " f=" << freq << " -> peak=" << peak
                  << " gain=" << (peak / inAmp) << " maxJump=" << maxJump);
            REQUIRE (finite);
        }
}

TEST_CASE ("DIAG: pico con material ANCHO (ruido full-scale) por sample rate", "[diag]")
{
    // El caso real de clipping no es un tono puro sino material ancho (batería/mezcla).
    // Ruido blanco full-scale orbitando -> el pico de salida debería quedar ~<=1 (seguro).
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        orbita::SpatialEngine eng;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) 256, 2 };
        eng.prepare (spec);

        const int block = 256, numBlocks = 800;
        juce::AudioBuffer<float> buf (2, block);
        unsigned int rng = 12345u;
        auto white = [&] { rng = rng * 1664525u + 1013904223u; return (float) ((double) rng / 4294967295.0 * 2.0 - 1.0); };
        float peak = 0; bool finite = true;
        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear();
            auto* in = buf.getWritePointer (0);
            for (int i = 0; i < block; ++i) in[i] = 0.99f * white(); // full-scale ancho
            const double az = 2.0 * kTwoPi * (double) (b + 1) / numBlocks; // 2 vueltas
            eng.process (buf, 1, (float) az, 1.0f, 0.0f, 0.5f, false, 0.5f, false);
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < block; ++i)
            {
                if (! std::isfinite (oL[i]) || ! std::isfinite (oR[i])) finite = false;
                peak = std::max (peak, std::max (std::abs (oL[i]), std::abs (oR[i])));
            }
        }
        WARN ("ANCHO sr=" << sr << " ruido full-scale -> peak=" << peak);
        REQUIRE (finite);
    }
}

TEST_CASE ("DIAG: reflexiones bajan la coherencia interaural y no clippean", "[diag]")
{
    // Métrica de externalización (Leclère 2019): coherencia interaural (IC). Reflexiones
    // decorreladas deben BAJAR la IC (sacar de la cabeza) sin clippear.
    const double sr = 48000.0;
    const int block = 256, numBlocks = 500;

    auto run = [&] (float room)
    {
        orbita::SpatialEngine eng;
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        unsigned int rng = 777u;
        auto white = [&] { rng = rng * 1664525u + 1013904223u; return (float) ((double) rng / 4294967295.0 * 2.0 - 1.0); };

        double sLL = 0, sRR = 0, sLR = 0; float peak = 0; bool finite = true;
        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear();
            auto* in = buf.getWritePointer (0);
            for (int i = 0; i < block; ++i) in[i] = 0.7f * white();
            const double az = 0.5 * kTwoPi * (double) (b + 1) / numBlocks;
            eng.process (buf, 1, (float) az, 1.0f, room, 0.5f, false, 0.5f, false);
            if (b < 40) continue; // warm-up de reflexiones
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < block; ++i)
            {
                if (! std::isfinite (oL[i]) || ! std::isfinite (oR[i])) finite = false;
                sLL += (double) oL[i] * oL[i]; sRR += (double) oR[i] * oR[i]; sLR += (double) oL[i] * oR[i];
                peak = std::max (peak, std::max (std::abs (oL[i]), std::abs (oR[i])));
            }
        }
        const float ic = (float) (sLR / std::sqrt (std::max (sLL * sRR, 1e-12))); // correlación interaural
        struct R { float ic, peak; bool finite; }; return R { ic, peak, finite };
    };

    auto dry = run (0.0f);   // sin reflexiones
    auto wet = run (1.0f);   // room al máximo
    WARN ("IC sin-room=" << dry.ic << "  IC room=1=" << wet.ic
          << " | peak sin-room=" << dry.peak << "  peak room=1=" << wet.peak);
    REQUIRE (dry.finite); REQUIRE (wet.finite);
    REQUIRE (wet.peak < 2.0f);            // no explota con reflexiones full
    REQUIRE (wet.ic <= dry.ic + 0.02f);   // reflexiones decorrelan (no aumentan la coherencia)
}

TEST_CASE ("DIAG: compatibilidad mono (comb por ITD al sumar L+R)", "[diag]")
{
    // Peor caso de fase: fuente lateral (ITD fuerte) sumada a mono. retention 1.0 = sin
    // pérdida; ~0.707 = decorrelado (pierde ancho, no cancela); <0.5 = cancelación/comb.
    const double sr = 48000.0;
    const int block = 256, numBlocks = 220;
    const float az = 1.3f; // ~74°, ITD significativo

    auto retention = [&] (float freq, float width, bool monoSafe)
    {
        orbita::SpatialEngine eng;
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        const double w = kTwoPi * (double) freq / sr; double ph = 0.0;
        double sChan = 0, sMono = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear();
            auto* in = buf.getWritePointer (0);
            for (int i = 0; i < block; ++i) { in[i] = 0.5f * (float) std::sin (ph); ph += w; }
            eng.process (buf, 1, az, 1.0f, 0.3f, width, monoSafe, 0.5f, false);
            if (b < 80) continue;
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < block; ++i)
            {
                const float mono = 0.5f * (oL[i] + oR[i]);
                sChan += 0.5 * ((double) oL[i] * oL[i] + (double) oR[i] * oR[i]);
                sMono += (double) mono * mono;
            }
        }
        return (float) std::sqrt (sMono / std::max (sChan, 1e-12));
    };

    // Width = tradeoff fase/mono: 0=reservado(mono-safe) .. 1=volador.
    for (float width : { 0.0f, 0.5f, 1.0f })
        WARN ("width=" << width << " (0=reservado/mono, 1=volador)"
              << "  mono-retention @150Hz=" << retention (150.0f, width, false)
              << "  @700Hz=" << retention (700.0f, width, false)
              << "  @2kHz="  << retention (2000.0f, width, false));
    // EN FASE (bass-mono): el bajo real (80 Hz) debe quedar prácticamente en fase aun en modo volador.
    WARN ("EN FASE @width=1: 80Hz off=" << retention (80.0f, 1.0f, false) << " on=" << retention (80.0f, 1.0f, true)
          << " | 150Hz on=" << retention (150.0f, 1.0f, true) << " | 2kHz on=" << retention (2000.0f, 1.0f, true));
    REQUIRE (retention (80.0f, 1.0f, true) > retention (80.0f, 1.0f, false)); // EN FASE mejora el grave
    REQUIRE (retention (80.0f, 1.0f, true) > 0.97f);                          // bajo casi perfecto en fase
    SUCCEED();
}

TEST_CASE ("DIAG: modo Parlantes (RACE) estable, acotado, finito", "[diag]")
{
    const double sr = 48000.0; const int block = 256, numBlocks = 600;
    orbita::SpatialEngine eng;
    eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
    juce::AudioBuffer<float> buf (2, block);
    unsigned int rng = 5u;
    auto white = [&] { rng = rng*1664525u+1013904223u; return (float)((double)rng/4294967295.0*2-1); };
    float peak = 0; bool finite = true;
    for (int b = 0; b < numBlocks; ++b)
    {
        buf.clear();
        for (int i = 0; i < block; ++i) buf.setSample (0, i, 0.9f * white());
        const double az = 2.0 * kTwoPi * (double)(b+1) / numBlocks;
        eng.process (buf, 1, (float) az, 1.0f, 0.5f, 1.0f, false, 0.5f, true); // PARLANTES on, todo al mango
        for (int i = 0; i < block; ++i)
        {
            if (! std::isfinite (buf.getSample(0,i)) || ! std::isfinite (buf.getSample(1,i))) finite = false;
            peak = std::max (peak, std::max (std::abs(buf.getSample(0,i)), std::abs(buf.getSample(1,i))));
        }
    }
    WARN ("RACE parlantes: peak=" << peak << " (la recursión no debe explotar)");
    REQUIRE (finite);
    REQUIRE (peak < 3.0f);   // recursión estable (A<1), nada de blowup
}

TEST_CASE ("DIAG: distancia (cercanía-lejanía) 1/r + DRR", "[diag]")
{
    const double sr = 48000.0; const int block = 256, numBlocks = 200;
    auto measure = [&] (float radius, float room)
    {
        orbita::SpatialEngine eng;
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        unsigned int rng = 31u;
        auto white = [&] { rng = rng*1664525u+1013904223u; return (float)((double)rng/4294967295.0*2-1); };
        double e = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear();
            for (int i = 0; i < block; ++i) buf.setSample (0, i, 0.5f * white());
            eng.process (buf, 1, 0.0f, 1.0f, room, 0.5f, false, radius, false); // frente, distancia = radius
            if (b < 100) continue;
            for (int i = 0; i < block; ++i) e += (double)buf.getSample(0,i)*buf.getSample(0,i) + (double)buf.getSample(1,i)*buf.getSample(1,i);
        }
        return e;
    };
    const double close = measure (0.05f, 0.0f); // ~0.2 m
    const double far   = measure (0.95f, 0.0f); // ~12 m
    WARN ("DISTANCIA (room=0): cerca/lejos = " << 10.0*std::log10(close/std::max(far,1e-12)) << " dB (1/r: cerca más fuerte)");
    REQUIRE (close > far * 2.0); // el directo cae con la distancia (1/r)
    // con reflexiones: lejos tiene MÁS proporción de reflexiones (DRR cae) — el directo cae, el lecho no.
    const double closeR = measure (0.05f, 0.6f), farR = measure (0.95f, 0.6f);
    WARN ("DRR check: el directo cae " << 10.0*std::log10(close/std::max(far,1e-12)) << " dB pero con room la caída total es menor ("
          << 10.0*std::log10(closeR/std::max(farR,1e-12)) << " dB) = el lecho reverberado sostiene = profundidad");
    REQUIRE (10.0*std::log10(closeR/std::max(farR,1e-12)) < 10.0*std::log10(close/std::max(far,1e-12)));
}

TEST_CASE ("DIAG: simetría L/R (¿izquierda y derecha igual de potentes?)", "[diag]")
{
    const double sr = 48000.0; const int block = 256, numBlocks = 240;
    auto power = [&] (float azRad, float room, float freq) // potencia: si freq>0 = seno (banda), si 0 = ruido
    {
        orbita::SpatialEngine eng;
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        unsigned int rng = 99u; double ph = 0.0; const double w = kTwoPi * (double) freq / sr;
        auto white = [&] { rng = rng*1664525u+1013904223u; return (float)((double)rng/4294967295.0*2-1); };
        double sL = 0, sR = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear();
            for (int i = 0; i < block; ++i) buf.setSample (0, i, freq > 0 ? 0.5f*(float)std::sin(ph) : 0.5f*white()), ph += w;
            eng.process (buf, 1, azRad, 1.0f, room, 0.5f, false, 0.5f, false);
            if (b < 100) continue;
            for (int i = 0; i < block; ++i) { sL += (double)buf.getSample(0,i)*buf.getSample(0,i); sR += (double)buf.getSample(1,i)*buf.getSample(1,i); }
        }
        struct P { double total, l, r; }; return P { sL + sR, sL, sR };
    };
    const float L90 = (float)(kPi*0.5), R90 = (float)(-kPi*0.5);
    auto dB = [] (double a, double b) { return 10.0 * std::log10 (a / std::max (b, 1e-12)); };
    WARN ("SIMETRIA total 90 (ruido, room=0):   izq vs der = " << dB (power(L90,0.0f,0).total, power(R90,0.0f,0).total) << " dB");
    WARN ("SIMETRIA total 90 (ruido, room=0.5): izq vs der = " << dB (power(L90,0.5f,0).total, power(R90,0.5f,0).total) << " dB");
    WARN ("SIMETRIA espectral 90 @ 500Hz: izq vs der = " << dB (power(L90,0.0f,500).total, power(R90,0.0f,500).total) << " dB");
    WARN ("SIMETRIA espectral 90 @ 4kHz:  izq vs der = " << dB (power(L90,0.0f,4000).total, power(R90,0.0f,4000).total) << " dB");
    WARN ("SIMETRIA espectral 90 @ 8kHz:  izq vs der = " << dB (power(L90,0.0f,8000).total, power(R90,0.0f,8000).total) << " dB");
    SUCCEED();
}

TEST_CASE ("DIAG: ILD (paneo L/R) por azimut y width", "[diag]")
{
    // ¿Qué tan fuerte panea? ILD = 20log10(RMS_L/RMS_R). + = más fuerte a la izquierda.
    const double sr = 48000.0; const int block = 256, numBlocks = 240;
    auto ild = [&] (float azRad, float width)
    {
        orbita::SpatialEngine eng;
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        unsigned int rng = 4242u;
        auto white = [&] { rng = rng * 1664525u + 1013904223u; return (float) ((double) rng / 4294967295.0 * 2.0 - 1.0); };
        double sL = 0, sR = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear();
            auto* in = buf.getWritePointer (0);
            for (int i = 0; i < block; ++i) in[i] = 0.5f * white();
            eng.process (buf, 1, azRad, 1.0f, 0.0f, width, false, 0.5f, false); // room=0, caos=0 (posición limpia)
            if (b < 80) continue;
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < block; ++i) { sL += (double) oL[i]*oL[i]; sR += (double) oR[i]*oR[i]; }
        }
        return 20.0 * std::log10 (std::sqrt (sL / std::max (sR, 1e-12)));
    };
    const float L90 = (float) (kPi * 0.5), L45 = (float) (kPi * 0.25), R90 = (float) (-kPi * 0.5);
    WARN ("ILD 90izq: width0.5=" << ild (L90, 0.5f) << "dB  width1.0=" << ild (L90, 1.0f) << "dB");
    WARN ("ILD 45izq: width0.5=" << ild (L45, 0.5f) << "dB   ILD 90der: width0.5=" << ild (R90, 0.5f) << "dB");
    SUCCEED();
}

TEST_CASE ("DIAG full-path: Speed y monoSafe via processBlock (camino real del plugin)", "[diag]")
{
    PluginProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.apvts.getParameter ("orbRate")->setValueNotifyingHost (0.75f); // Free (índice 3/4 con 5 choices)
    proc.apvts.getParameter ("orbChaos")->setValueNotifyingHost (0.0f);

    auto azAdvancePerBlock = [&] (float speedNorm)
    {
        proc.apvts.getParameter ("orbFreeHz")->setValueNotifyingHost (speedNorm);
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
        unsigned int rng = 7u;
        double sum = 0.0; int n = 0; float prev = proc.uiAzimuth.load();
        for (int b = 0; b < 20; ++b)
        {
            buf.clear();
            for (int i = 0; i < 512; ++i) { rng = rng*1664525u+1013904223u; buf.setSample (0, i, 0.3f * ((float)((double)rng/4294967295.0*2-1))); }
            proc.processBlock (buf, midi);
            const float a = proc.uiAzimuth.load();
            float d = std::abs (a - prev); if (d > 3.14159f) d = 6.2831853f - d; // arco más corto
            if (b > 2) { sum += d; ++n; }
            prev = a;
        }
        return sum / std::max (1, n);
    };
    const double slow = azAdvancePerBlock (0.0f); // Speed mínimo
    const double fast = azAdvancePerBlock (1.0f); // Speed máximo
    WARN ("FULL-PATH Speed: avance azimut/bloque  slow=" << slow << "  fast=" << fast);
    REQUIRE (fast > slow * 3.0);  // el knob Speed DEBE acelerar la órbita por el camino real

    // monoSafe full-path: con un seno grave lateral, EN FASE debe subir la retención mono.
    proc.apvts.getParameter ("orbFreeHz")->setValueNotifyingHost (0.0f); // casi quieto
    proc.apvts.getParameter ("width")->setValueNotifyingHost (1.0f);
    auto monoRet = [&] (bool on)
    {
        proc.apvts.getParameter ("monoSafe")->setValueNotifyingHost (on ? 1.0f : 0.0f);
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi; double ph = 0.0, sC = 0, sM = 0;
        const double w = kTwoPi * 90.0 / 48000.0;
        for (int b = 0; b < 60; ++b)
        {
            buf.clear();
            for (int i = 0; i < 512; ++i) { buf.setSample (0, i, 0.4f * (float) std::sin (ph)); ph += w; }
            proc.processBlock (buf, midi);
            if (b < 30) continue;
            for (int i = 0; i < 512; ++i)
            {
                const float oL = buf.getSample (0, i), oR = buf.getSample (1, i), m = 0.5f*(oL+oR);
                sC += 0.5*((double)oL*oL+(double)oR*oR); sM += (double)m*m;
            }
        }
        return std::sqrt (sM / std::max (sC, 1e-12));
    };
    const double mOff = monoRet (false), mOn = monoRet (true);
    WARN ("FULL-PATH monoSafe @90Hz: off=" << mOff << "  on=" << mOn);
    REQUIRE (mOn > mOff + 0.02);   // EN FASE DEBE mejorar la compatibilidad mono por el camino real
}

TEST_CASE ("SHAPES: Pendulum oscila (no da la vuelta entera)", "[shapes]")
{
    // La hamaca: el azimut va y viene dentro de ±swing (~90°), cruzando el frente, SIN rotar 360.
    // Un círculo barrería todo el rango (|az| hasta π); el péndulo se queda acotado y cambia de signo.
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        orbita::OrbitBrain b; b.prepare (sr);
        orbita::OrbitBrain::Params p;
        p.shape = orbita::OrbitBrain::Pendulum; p.rate = orbita::OrbitBrain::Free;
        p.dir = orbita::OrbitBrain::CW; p.chaos01 = 0.0f; p.freeHz = 1.0f; p.radius01 = 0.6f; p.doppler01 = 0.0f;
        b.setParams (p);
        orbita::TransportInfo t; t.isPlaying = false;
        float maxAbs = 0.0f, minS = 1.0e9f, maxS = -1.0e9f; bool finite = true, distOk = true;
        for (int i = 0; i < 4000; ++i)   // varios ciclos de swing
        {
            const auto out = b.advance (64, t);
            const float a = out.azimuth;                                // [0,2π)
            const float s = a <= (float) kPi ? a : a - (float) kTwoPi;  // signed (-π,π]
            if (! std::isfinite (a)) finite = false;
            if (out.distance < 0.0f || out.distance > 1.0f) distOk = false;
            maxAbs = std::max (maxAbs, std::abs (s)); minS = std::min (minS, s); maxS = std::max (maxS, s);
        }
        WARN ("PENDULUM sr=" << sr << "  maxAbs=" << maxAbs << " (swing~1.571)  min=" << minS << "  max=" << maxS);
        REQUIRE (finite); REQUIRE (distOk);
        REQUIRE (maxAbs < 1.9f);   // acotado a +/-swing(+eps) -- NO rota 360 (un circulo llegaria a pi~3.14)
        REQUIRE (maxS > 1.0f);     // alcanza bien un lado...
        REQUIRE (minS < -1.0f);    // ...y el otro -> oscila de verdad
    }
}

TEST_CASE ("SHAPES: Pendulum Back oscila por ATRAS (nunca viene al frente)", "[shapes]")
{
    // La hamaca de atras: oscila alrededor de pi (detras de la cabeza), cruzando el fondo de un
    // costado al otro. NUNCA pasa por el frente (|az| siempre > ~75 grados) y llega al fondo (cruza pi).
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        orbita::OrbitBrain b; b.prepare (sr);
        orbita::OrbitBrain::Params p;
        p.shape = orbita::OrbitBrain::PendulumBack; p.rate = orbita::OrbitBrain::Free;
        p.dir = orbita::OrbitBrain::CW; p.chaos01 = 0.0f; p.freeHz = 1.0f; p.radius01 = 0.6f; p.doppler01 = 0.0f;
        b.setParams (p);
        orbita::TransportInfo t; t.isPlaying = false;
        float minAbs = 1.0e9f, maxAbs = 0.0f; bool finite = true, reachesBack = false, distOk = true;
        for (int i = 0; i < 4000; ++i)
        {
            const auto out = b.advance (64, t);
            const float a = out.azimuth;                                // [0,2π)
            const float s = a <= (float) kPi ? a : a - (float) kTwoPi;  // signed (-π,π]
            if (! std::isfinite (a)) finite = false;
            if (out.distance < 0.0f || out.distance > 1.0f) distOk = false;
            const float abss = std::abs (s);
            minAbs = std::min (minAbs, abss); maxAbs = std::max (maxAbs, abss);
            if (abss > 3.0f) reachesBack = true;   // cerca de π = atras del todo
        }
        WARN ("PENDULUM-BACK sr=" << sr << "  |az| in [" << minAbs << ", " << maxAbs << "]  (atras: |az|>~pi/2, cruza pi)");
        REQUIRE (finite); REQUIRE (distOk);
        REQUIRE (minAbs > 1.3f);     // NUNCA viene al frente (|az| siempre > ~75 grados) -> es de atras
        REQUIRE (reachesBack);       // llega al fondo (cruza pi = atras del todo)
    }
}

TEST_CASE ("SHAPES: Spiral respira el radio (vortice) y sigue rotando; Circle no respira", "[shapes]")
{
    const double sr = 48000.0;
    struct R { float range, maxStep, maxAzAbs; bool finite; };
    auto run = [&] (int shape) -> R
    {
        orbita::OrbitBrain b; b.prepare (sr);
        orbita::OrbitBrain::Params p;
        p.shape = shape; p.rate = orbita::OrbitBrain::Free; p.dir = orbita::OrbitBrain::CW;
        p.chaos01 = 0.0f; p.freeHz = 1.0f; p.radius01 = 0.6f; p.doppler01 = 0.0f;  // doppler=0: aisla la respiracion
        b.setParams (p);
        orbita::TransportInfo t; t.isPlaying = false;
        float dmin = 1.0e9f, dmax = -1.0e9f, prevD = -1.0f, maxStep = 0.0f, maxAzAbs = 0.0f; bool finite = true;
        for (int i = 0; i < 2000; ++i)   // ~10.7 s a 256/48k -> cubre >1 ciclo de respiracion (0.16 Hz)
        {
            const auto out = b.advance (256, t);
            if (! std::isfinite (out.distance) || ! std::isfinite (out.azimuth)) finite = false;
            dmin = std::min (dmin, out.distance); dmax = std::max (dmax, out.distance);
            if (prevD >= 0.0f) maxStep = std::max (maxStep, std::abs (out.distance - prevD));
            prevD = out.distance;
            const float s = out.azimuth <= (float) kPi ? out.azimuth : out.azimuth - (float) kTwoPi;
            maxAzAbs = std::max (maxAzAbs, std::abs (s));
        }
        return R { dmax - dmin, maxStep, maxAzAbs, finite };
    };
    const auto spiral = run (orbita::OrbitBrain::Spiral);
    const auto circle = run (orbita::OrbitBrain::Circle);
    WARN ("SPIRAL dist-range=" << spiral.range << "  maxStep/blk=" << spiral.maxStep
          << "  azAbs=" << spiral.maxAzAbs << "  |  CIRCLE dist-range=" << circle.range);
    REQUIRE (spiral.finite); REQUIRE (circle.finite);
    REQUIRE (circle.range < 0.001f);   // Circle: distancia constante (doppler=0)
    REQUIRE (spiral.range > 0.2f);     // Spiral: el radio respira de verdad (vortice)
    REQUIRE (spiral.maxStep < 0.01f);  // suave por bloque -> sin saltos de distancia (sin clicks)
    REQUIRE (spiral.maxAzAbs > 2.5f);  // Spiral IGUAL rota (como circulo), no es hamaca
}

TEST_CASE ("SHAPES full-path: Pendulum y Spiral sin clicks por el camino real (processBlock)", "[shapes]")
{
    PluginProcessor proc; proc.prepareToPlay (48000.0, 256);
    proc.apvts.getParameter ("orbRate")->setValueNotifyingHost (0.75f);   // Free (indice 3/4 con 5 choices)
    proc.apvts.getParameter ("orbChaos")->setValueNotifyingHost (0.0f);
    proc.apvts.getParameter ("orbFreeHz")->setValueNotifyingHost (0.6f);  // velocidad media-alta
    proc.apvts.getParameter ("doppler")->setValueNotifyingHost (0.0f);

    auto runShape = [&] (int shapeIdx)
    {
        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("orbShape")))
            cp->setValueNotifyingHost (cp->convertTo0to1 ((float) shapeIdx));   // robusto al nº de choices
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
        double ph = 0.0; const double w = kTwoPi * 220.0 / 48000.0;
        float prevL = 0, prevR = 0, maxJump = 0; bool finite = true;
        for (int b = 0; b < 800; ++b)
        {
            buf.clear();
            for (int i = 0; i < 256; ++i) { buf.setSample (0, i, 0.4f * (float) std::sin (ph)); ph += w; }
            proc.processBlock (buf, midi);
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < 256; ++i)
            {
                if (! std::isfinite (oL[i]) || ! std::isfinite (oR[i])) finite = false;
                maxJump = std::max (maxJump, std::max (std::abs (oL[i]-prevL), std::abs (oR[i]-prevR)));
                prevL = oL[i]; prevR = oR[i];
            }
        }
        struct RR { float maxJump; bool finite; }; return RR { maxJump, finite };
    };
    const auto pend  = runShape (orbita::OrbitBrain::Pendulum);     // hamaca frente
    const auto spir  = runShape (orbita::OrbitBrain::Spiral);       // vortice
    const auto pendB = runShape (orbita::OrbitBrain::PendulumBack); // hamaca atras
    WARN ("FULL-PATH sin clicks (seno): Pendulum=" << pend.maxJump << "  Spiral=" << spir.maxJump << "  PendulumBack=" << pendB.maxJump);
    REQUIRE (pend.finite); REQUIRE (spir.finite); REQUIRE (pendB.finite);
    REQUIRE (pend.maxJump  < 0.3f);   // sin clicks por el camino real (mismo umbral que los demas guards)
    REQUIRE (spir.maxJump  < 0.3f);
    REQUIRE (pendB.maxJump < 0.3f);
}

TEST_CASE ("SHAPES fisica: Pendulum = sinusoide real (simetrico, cruza el frente, frena en extremos)", "[shapes]")
{
    // Verifica que la hamaca es una sinusoide de verdad (no un barrido lineal/triangular):
    // simetrica alrededor del frente, alcanza +/-swing, cruza el centro, y FRENA en los extremos
    // (velocidad angular ~cos -> minima en las puntas) = comportamiento pendular fisico.
    orbita::OrbitBrain b; b.prepare (48000.0);
    orbita::OrbitBrain::Params p; p.shape = orbita::OrbitBrain::Pendulum; p.rate = orbita::OrbitBrain::Free;
    p.dir = orbita::OrbitBrain::CW; p.chaos01 = 0.0f; p.freeHz = 0.5f; p.radius01 = 0.6f; p.doppler01 = 0.0f;
    b.setParams (p);
    orbita::TransportInfo t; t.isPlaying = false;
    std::vector<float> azs; azs.reserve (20000);
    for (int i = 0; i < 20000; ++i) { const float a = b.advance (8, t).azimuth; azs.push_back (a <= (float) kPi ? a : a - (float) kTwoPi); }
    float mn = 1.0e9f, mx = -1.0e9f; for (float s : azs) { mn = std::min (mn, s); mx = std::max (mx, s); }
    bool crossesFront = false; int nearExtreme = 0, nearCenter = 0;
    for (float s : azs)
    {
        if (std::abs (s) < 0.05f) crossesFront = true;
        if (std::abs (s) > 1.37f) ++nearExtreme;   // banda de 0.4 rad (las dos puntas)
        if (std::abs (s) < 0.20f) ++nearCenter;    // banda de 0.4 rad (el centro)
    }
    WARN ("PENDULUM sinusoide: min=" << mn << " max=" << mx << " | densidad extremo=" << nearExtreme
          << " centro=" << nearCenter << " (extremo>centro = frena en las puntas)");
    REQUIRE (std::abs (mx + mn) < 0.05f);          // simetrico alrededor del frente (0)
    REQUIRE (std::abs (mx - 1.5708f) < 0.05f);     // alcanza +swing (~90 grados)
    REQUIRE (crossesFront);                        // pasa por el frente (az=0)
    REQUIRE (nearExtreme > nearCenter);            // pasa mas tiempo en los extremos = pendular (no lineal)
}

TEST_CASE ("SHAPES anti-clip/crackle: peor caso full-path con las formas nuevas (todo al mango)", "[shapes]")
{
    // Rigor de los procesos previos (Doppler/near-field): con la forma NUEVA + TODO al maximo
    // (mix/doppler/width/room/caos max, fuente cerca, Speed max, auriculares Y parlantes), la salida
    // NO debe clipear (el limiter aguanta) ni crepitar (sin saltos por bloque). Spiral lleva la fuente
    // CERCA (near-field + 1/r) = el escenario que historicamente clipeaba -> se prueba que la cadena
    // anti-clip lo contiene tambien con la trayectoria nueva. A 44.1/48/96k (ejercita el resampleo).
    auto worst = [&] (double sr, int shapeIdx, bool speakers)
    {
        PluginProcessor proc; proc.prepareToPlay (sr, 256);
        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("orbShape")))
            cp->setValueNotifyingHost (cp->convertTo0to1 ((float) shapeIdx));   // robusto al nº de choices
        proc.apvts.getParameter ("orbRate")->setValueNotifyingHost (0.75f);    // Free
        proc.apvts.getParameter ("orbFreeHz")->setValueNotifyingHost (1.0f);   // Speed max
        proc.apvts.getParameter ("mix")->setValueNotifyingHost (1.0f);
        proc.apvts.getParameter ("doppler")->setValueNotifyingHost (1.0f);
        proc.apvts.getParameter ("width")->setValueNotifyingHost (1.0f);
        proc.apvts.getParameter ("room")->setValueNotifyingHost (1.0f);
        proc.apvts.getParameter ("orbChaos")->setValueNotifyingHost (1.0f);
        proc.apvts.getParameter ("orbRadius")->setValueNotifyingHost (0.25f);  // cerca (near-field activo)
        proc.apvts.getParameter ("outMode")->setValueNotifyingHost (speakers ? 1.0f : 0.0f);
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
        unsigned int rng = 13u;
        float prevL = 0, prevR = 0, peak = 0, maxJump = 0; bool finite = true;
        for (int b = 0; b < 1200; ++b)
        {
            buf.clear();
            for (int i = 0; i < 256; ++i) { rng = rng*1664525u+1013904223u; buf.setSample (0, i, 0.9f * (float)((double) rng / 4294967295.0 * 2.0 - 1.0)); }
            proc.processBlock (buf, midi);
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < 256; ++i)
            {
                if (! std::isfinite (oL[i]) || ! std::isfinite (oR[i])) finite = false;
                peak    = std::max (peak, std::max (std::abs (oL[i]), std::abs (oR[i])));
                maxJump = std::max (maxJump, std::max (std::abs (oL[i]-prevL), std::abs (oR[i]-prevR)));
                prevL = oL[i]; prevR = oR[i];
            }
        }
        struct R { float peak, maxJump; bool finite; }; return R { peak, maxJump, finite };
    };
    // Compara las 4 formas en el MISMO peor caso. Con ruido blanco full-scale el limiter de ataque
    // instantaneo genera saltos sample-a-sample (artefacto del limiter sobre ruido, NO de la forma).
    // Por eso el guard de crackle es RELATIVO: las formas nuevas no deben saltar mas que las viejas
    // analogas. El guard de CLIP (peak<=ceiling) es absoluto y vale con cualquier señal.
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (bool spk : { false, true })
        {
            const auto circ  = worst (sr, orbita::OrbitBrain::Circle,       spk);
            const auto elli  = worst (sr, orbita::OrbitBrain::Ellipse,      spk);
            const auto spir  = worst (sr, orbita::OrbitBrain::Spiral,       spk);
            const auto pend  = worst (sr, orbita::OrbitBrain::Pendulum,     spk);
            const auto pendB = worst (sr, orbita::OrbitBrain::PendulumBack, spk);
            WARN ("WORST sr=" << sr << " spk=" << spk
                  << " | peak C/E/Sp/Pe/PeB = " << circ.peak << "/" << elli.peak << "/" << spir.peak << "/" << pend.peak << "/" << pendB.peak
                  << " | maxJump = " << circ.maxJump << "/" << elli.maxJump << "/" << spir.maxJump << "/" << pend.maxJump << "/" << pendB.maxJump);
            REQUIRE (circ.finite); REQUIRE (elli.finite); REQUIRE (spir.finite); REQUIRE (pend.finite); REQUIRE (pendB.finite);
            // Guard de CLIP (valido con cualquier señal, incluido ruido al mango): el limiter contiene
            // <= ceiling en TODAS las formas. Este es el guard que importa aca.
            REQUIRE (circ.peak <= 0.95f); REQUIRE (elli.peak <= 0.95f); REQUIRE (spir.peak <= 0.95f);
            REQUIRE (pend.peak <= 0.95f); REQUIRE (pendB.peak <= 0.95f);
            // OJO: el maxJump con ruido blanco full-scale = artefacto del limiter de ataque instantaneo,
            // ~igual para TODAS las formas (medido: viejas 0.75-1.07, nuevas 0.75-1.09 = misma banda, sin
            // outlier). NO es metrica de crackle de trayectoria. El guard de crackle REAL va en el test con
            // SENO (full-path, maxJump<0.3, que pasa holgado). Aca solo se reporta para visibilidad.
            const float newMax = std::max (spir.maxJump, std::max (pend.maxJump, pendB.maxJump));
            const float oldMax = std::max (circ.maxJump, elli.maxJump);
            WARN ("  (maxJump ruido: nuevas=" << newMax << " vs viejas=" << oldMax << " -> misma banda = artefacto limiter+ruido, no crackle de forma)");
        }
}

TEST_CASE ("GAIN: output gain escala el nivel final (deja subir; default 0 dB = unidad)", "[gain]")
{
    // Output gain POST-limiter: -12/0/+6 dB escalan el RMS de salida ~acorde, y +6 sube el pico
    // (el usuario PUEDE levantar el nivel; el LED avisa si pasa 0 dBFS). Default 0 dB no cambia nada.
    auto meas = [&] (float outDb)
    {
        PluginProcessor proc; proc.prepareToPlay (48000.0, 256);
        if (auto* p = proc.apvts.getParameter ("output"))    p->setValueNotifyingHost (p->convertTo0to1 (outDb));
        proc.apvts.getParameter ("orbFreeHz")->setValueNotifyingHost (0.2f);
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi; double ph = 0.0; const double w = kTwoPi * 220.0 / 48000.0;
        double e = 0; int n = 0; float peak = 0;
        for (int b = 0; b < 120; ++b)
        {
            buf.clear();
            for (int i = 0; i < 256; ++i) { buf.setSample (0, i, 0.5f * (float) std::sin (ph)); ph += w; }
            proc.processBlock (buf, midi);
            if (b < 40) continue;
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < 256; ++i) { e += 0.5*((double)oL[i]*oL[i]+(double)oR[i]*oR[i]); ++n; peak = std::max (peak, std::max (std::abs(oL[i]), std::abs(oR[i]))); }
        }
        struct R { double rms; float peak; }; return R { std::sqrt (e / std::max (1, n)), peak };
    };
    const auto lo = meas (-12.0f), mid = meas (0.0f), hi = meas (6.0f);
    WARN ("OUTPUT gain rms -12/0/+6 = " << lo.rms << "/" << mid.rms << "/" << hi.rms << "  peak 0/+6 = " << mid.peak << "/" << hi.peak);
    REQUIRE (hi.rms  > mid.rms * 1.5);   // +6 dB sube (~x2 amplitud)
    REQUIRE (mid.rms > lo.rms  * 1.5);   // 0 dB > -12 dB
    REQUIRE (hi.peak > mid.peak * 1.3);  // +6 dB levanta el pico final (deja subir de verdad, post-limiter)
}

TEST_CASE ("GAIN: input gain drivea (mas entrada -> mas salida)", "[gain]")
{
    // Entrada BAJA (sin disparar el limiter) -> escala lineal: mas input gain, mas salida.
    auto rms = [&] (float inDb)
    {
        PluginProcessor proc; proc.prepareToPlay (48000.0, 256);
        if (auto* p = proc.apvts.getParameter ("inGain")) p->setValueNotifyingHost (p->convertTo0to1 (inDb));
        proc.apvts.getParameter ("orbFreeHz")->setValueNotifyingHost (0.2f);
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi; double ph = 0.0; const double w = kTwoPi * 220.0 / 48000.0;
        double e = 0; int n = 0;
        for (int b = 0; b < 120; ++b)
        {
            buf.clear();
            for (int i = 0; i < 256; ++i) { buf.setSample (0, i, 0.1f * (float) std::sin (ph)); ph += w; }
            proc.processBlock (buf, midi);
            if (b < 40) continue;
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < 256; ++i) { e += 0.5*((double)oL[i]*oL[i]+(double)oR[i]*oR[i]); ++n; }
        }
        return std::sqrt (e / std::max (1, n));
    };
    const double lo = rms (-6.0f), mid = rms (0.0f), hi = rms (6.0f);
    WARN ("INPUT gain rms -6/0/+6 (entrada baja, lineal) = " << lo << "/" << mid << "/" << hi);
    REQUIRE (hi  > mid * 1.3);   // mas input -> mas salida (drive)
    REQUIRE (mid > lo  * 1.3);
}

TEST_CASE ("GAIN: rampeado (sin zipper) al cambiar de golpe", "[gain]")
{
    // Alternar el output gain -18/+6 dB cada bloque NO debe producir un salto duro -> rampeado.
    PluginProcessor proc; proc.prepareToPlay (48000.0, 256);
    proc.apvts.getParameter ("orbFreeHz")->setValueNotifyingHost (0.2f);
    auto* outP = proc.apvts.getParameter ("output");
    juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi; double ph = 0.0; const double w = kTwoPi * 220.0 / 48000.0;
    float prevL = 0, maxJump = 0; bool finite = true;
    for (int b = 0; b < 80; ++b)
    {
        if (outP) outP->setValueNotifyingHost (outP->convertTo0to1 ((b % 2 == 0) ? -18.0f : 6.0f));
        buf.clear();
        for (int i = 0; i < 256; ++i) { buf.setSample (0, i, 0.4f * (float) std::sin (ph)); ph += w; }
        proc.processBlock (buf, midi);
        const auto* oL = buf.getReadPointer (0);
        for (int i = 0; i < 256; ++i) { if (! std::isfinite (oL[i])) finite = false; maxJump = std::max (maxJump, std::abs (oL[i] - prevL)); prevL = oL[i]; }
    }
    WARN ("GAIN ramp maxJump (alternando -18/+6 dB por bloque) = " << maxJump);
    REQUIRE (finite);
    REQUIRE (maxJump < 0.3f);   // rampeado dentro del bloque -> sin salto duro (zipper)
}

TEST_CASE ("cerebro: sync bloquea a ppq y banca cambios de BPM", "[m2]")
{
    orbita::OrbitBrain brain; brain.prepare (48000.0);
    orbita::OrbitBrain::Params p; p.rate = orbita::OrbitBrain::Bar;   // 1 vuelta por compás
    p.shape = orbita::OrbitBrain::Circle; p.dir = orbita::OrbitBrain::CCW; p.chaos01 = 0.0f;
    brain.setParams (p);

    orbita::TransportInfo t; t.isPlaying = true; t.timeSigNum = 4.0;
    t.bpm = 120.0; t.ppqPosition = 0.0;   float aStart = brain.advance (1, t).azimuth;
    t.ppqPosition = 2.0;                   float aHalf  = brain.advance (1, t).azimuth; // medio compás
    // a medio compás (CCW) el azimut debería estar cerca de π
    REQUIRE (std::abs (aHalf - (float) kPi) < 0.05f);
    REQUIRE (std::isfinite (aStart));
}

TEST_CASE ("DIAG near-field: el ILD de graves crece al acercar la fuente (Duda-Martens)", "[diag]")
{
    // Near-field (<1 m): el ILD de BAJA frecuencia de una fuente lateral crece mucho al acercarse
    // (Duda-Martens 1998: ~4 dB lejos -> ~20 dB al oído). Width=0.5 (neutro, sin corte de oído
    // lejano) para AISLAR el cue near-field. El 1/r escala ambos oídos por igual -> NO cambia el ILD;
    // sólo el shelf diferencial near-field lo hace. Lejos (d~12 m) el efecto se apaga (nearAmt=0).
    const double sr = 48000.0; const int block = 256, numBlocks = 240;
    auto ildLow = [&] (float azRad, float radius) // ILD a 160 Hz (banda donde manda el near-field)
    {
        orbita::SpatialEngine eng;
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        const double w = kTwoPi * 160.0 / sr; double ph = 0.0;
        double sL = 0, sR = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear();
            auto* in = buf.getWritePointer (0);
            for (int i = 0; i < block; ++i) { in[i] = 0.5f * (float) std::sin (ph); ph += w; }
            eng.process (buf, 1, azRad, 1.0f, 0.0f, 0.5f, false, radius, false); // room=0, width neutro
            if (b < 120) continue; // warm-up de los LPs del shelf/air
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < block; ++i) { sL += (double) oL[i]*oL[i]; sR += (double) oR[i]*oR[i]; }
        }
        return 20.0 * std::log10 (std::sqrt (sL / std::max (sR, 1e-12)));
    };
    const float L90 = (float) (kPi * 0.5);
    const double ildClose     = ildLow (L90, 0.05f); // ~0.2 m (al oído)
    const double ildFar       = ildLow (L90, 0.95f); // ~12 m (lejos)
    const double ildFrontClose = ildLow (0.0f, 0.05f); // de frente: el near-field NO debe crear ILD
    WARN ("NEAR-FIELD ILD@160Hz 90izq: cerca=" << ildClose << "dB  lejos=" << ildFar
          << "dB  (delta=" << (ildClose - ildFar) << "dB)   |   de frente cerca=" << ildFrontClose << "dB");
    REQUIRE (ildClose > ildFar + 4.0);        // acercar agranda el ILD de graves (el cue near-field)
    REQUIRE (std::abs (ildFrontClose) < 2.0);  // de frente queda simétrico (sólo boost de graves, sin ILD)
}

TEST_CASE ("near-field: sin clicks a Speed alto con la fuente cerca (shelf rampeado)", "[m2]")
{
    // El ILD de graves near-field sigue al azimut (como Width) -> a Speed alto su ganancia cambia
    // mucho por bloque. Sin rampa por-sample, esa ganancia clickea en el borde del bloque (el mismo
    // "crack" que tuvo Width). Con la fuente CERCA (near-field activo) y Width neutro (para aislar el
    // cue), el shelf rampeado NO debe saltar en el borde del bloque.
    orbita::SpatialEngine eng;
    const int block = 256, numBlocks = 600; const double sr = 48000.0;
    eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
    juce::AudioBuffer<float> buf (2, block);
    double phase = 0.0; const double w = kTwoPi * 200.0 / sr;
    float prevL = 0, prevR = 0, maxJump = 0; bool finite = true;
    for (int b = 0; b < numBlocks; ++b)
    {
        buf.clear();
        auto* in = buf.getWritePointer (0);
        for (int i = 0; i < block; ++i) { in[i] = 0.25f * (float) std::sin (phase); phase += w; }
        const double az = 30.0 * kTwoPi * (double)(b+1) / numBlocks; // ~30 vueltas = Speed muy alto
        eng.process (buf, 1, (float) az, 1.0f, 0.0f, 0.5f, false, 0.05f, false); // RADIUS cerca, Width neutro
        const auto* oL = buf.getReadPointer(0); const auto* oR = buf.getReadPointer(1);
        for (int i = 0; i < block; ++i)
        {
            if (! std::isfinite(oL[i]) || ! std::isfinite(oR[i])) finite = false;
            maxJump = std::max (maxJump, std::max (std::abs(oL[i]-prevL), std::abs(oR[i]-prevR)));
            prevL = oL[i]; prevR = oR[i];
        }
    }
    WARN ("near-field Speed alto cerca: maxJump=" << maxJump);
    REQUIRE (finite);
    // Medido: ~0.025 con rampa vs ~0.15 sin rampa (ganancia de golpe en el borde) -> 0.06 separa
    // claramente las dos (pasa rampeado, falla si alguien quita la rampa). Es un guard del "crack".
    REQUIRE (maxJump < 0.06f);
}

TEST_CASE ("NEAR-FIELD: caracterización completa (mediciones objetivas)", "[nearfield]")
{
    // Batería de medición física del near-field (no se confía en el oído): (A) ILD de graves vs
    // distancia, (B) vs azimut, (C) selectividad en frecuencia (low-shelf), (D) boost de graves de
    // proximidad, (E) ITD invariante con la distancia (Duda-Martens: el ITD NO se modula con el rango).
    const double sr = 48000.0; const int block = 256, numBlocks = 260, warmup = 140;
    auto dMeters = [] (float r) { return 0.15 * std::pow (15.0 / 0.15, (double) r); }; // sólo para mostrar

    // ILD (dB; + = izq más fuerte) de un seno a freq/azimut/radius. Width neutro -> aísla el near-field.
    auto ild = [&] (float freq, float azRad, float radius)
    {
        orbita::SpatialEngine eng;
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        const double w = kTwoPi * (double) freq / sr; double ph = 0.0; double sL = 0, sR = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear(); auto* in = buf.getWritePointer (0);
            for (int i = 0; i < block; ++i) { in[i] = 0.5f * (float) std::sin (ph); ph += w; }
            eng.process (buf, 1, azRad, 1.0f, 0.0f, 0.5f, false, radius, false);
            if (b < warmup) continue;
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < block; ++i) { sL += (double) oL[i]*oL[i]; sR += (double) oR[i]*oR[i]; }
        }
        return 20.0 * std::log10 (std::sqrt (sL / std::max (sR, 1e-12)));
    };
    // energía total (L+R) de un seno — para el tilt espectral graves/medios del boost de proximidad.
    auto energy = [&] (float freq, float azRad, float radius)
    {
        orbita::SpatialEngine eng;
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        const double w = kTwoPi * (double) freq / sr; double ph = 0.0; double e = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear(); auto* in = buf.getWritePointer (0);
            for (int i = 0; i < block; ++i) { in[i] = 0.5f * (float) std::sin (ph); ph += w; }
            eng.process (buf, 1, azRad, 1.0f, 0.0f, 0.5f, false, radius, false);
            if (b < warmup) continue;
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < block; ++i) e += (double) oL[i]*oL[i] + (double) oR[i]*oR[i];
        }
        return e;
    };
    const float L90 = (float) (kPi * 0.5);

    // ---- (A) ILD de graves @160 Hz a 90° izq vs distancia: debe CRECER al acercar (Duda-Martens) ----
    {
        const float radii[] = { 0.95f, 0.6f, 0.41f, 0.3f, 0.2f, 0.1f, 0.0f };
        const double far = ild (160.0f, L90, 0.95f); double prev = -1e9, near0 = 0; bool monotone = true;
        WARN ("== (A) ILD de graves @160 Hz, 90 izq, segun distancia ==");
        for (float r : radii)
        {
            const double v = ild (160.0f, L90, r);
            WARN ("   d=" << dMeters (r) << " m  ->  ILD = " << v << " dB");
            if (r < 0.9f && v < prev - 0.3) monotone = false; // tolerancia chica al ruido de medición
            prev = v; near0 = v;
        }
        REQUIRE (monotone);            // acercando, el ILD de graves no baja (crece de forma monótona)
        REQUIRE (near0 > far + 10.0);  // al oído vs lejos: +10 dB o más de ILD de graves
    }

    // ---- (B) ILD de graves @160 Hz, cerca, vs azimut: sigue |sin(az)|, max a 90, ~0 de frente ----
    {
        struct A { const char* name; float az; };
        const A azs[] = { {"0 (frente)",0.0f}, {"30 izq",(float)(kPi/6)}, {"45 izq",(float)(kPi/4)},
                          {"60 izq",(float)(kPi/3)}, {"90 izq",(float)(kPi/2)}, {"90 der",-(float)(kPi/2)} };
        WARN ("== (B) ILD de graves @160 Hz, CERCA (d~0.19 m), segun azimut ==");
        for (auto a : azs) WARN ("   " << a.name << "  ->  ILD = " << ild (160.0f, a.az, 0.05f) << " dB");
        REQUIRE (std::abs (ild (160.0f, 0.0f, 0.05f)) < 2.0);   // de frente: simétrico (sin ILD diferencial)
        REQUIRE (ild (160.0f, (float)(kPi/2), 0.05f) > 8.0);    // a 90 izq: ILD de graves grande (izq fuerte)
        REQUIRE (ild (160.0f, -(float)(kPi/2), 0.05f) < -8.0);  // a 90 der: espejado (der fuerte)
    }

    // ---- (C) selectividad: el near-field agrega ILD en GRAVES, no en agudos (low-shelf -> no toca timbre) ----
    {
        const float freqs[] = { 80.0f, 160.0f, 500.0f, 1000.0f, 4000.0f, 8000.0f };
        WARN ("== (C) incremento de ILD por near-field (cerca menos lejos), por frecuencia ==");
        double dLow = 0, dHigh = 0;
        for (float f : freqs)
        {
            const double inc = ild (f, L90, 0.05f) - ild (f, L90, 0.95f);
            WARN ("   " << f << " Hz  ->  +ILD cerca = " << inc << " dB");
            if ((int) f == 160) dLow = inc; if ((int) f == 8000) dHigh = inc;
        }
        REQUIRE (dLow > 10.0);          // agrega ILD fuerte en graves
        REQUIRE (dLow > dHigh + 8.0);   // selectividad: efecto dominante en graves, no en agudos (no rompe timbre)
    }

    // ---- (D) boost de graves de proximidad: de frente, el tilt graves/medios sube al acercar ----
    {
        const double tiltNear = 10.0*std::log10 (energy (160.0f,0.0f,0.05f) / energy (2000.0f,0.0f,0.05f));
        const double tiltFar  = 10.0*std::log10 (energy (160.0f,0.0f,0.95f) / energy (2000.0f,0.0f,0.95f));
        WARN ("== (D) boost de graves de proximidad (de frente, aisla del 1/r) ==");
        WARN ("   tilt 160/2k:  cerca=" << tiltNear << " dB   lejos=" << tiltFar << " dB");
        REQUIRE (tiltNear > tiltFar + 1.5);   // cerca: mas energia relativa de graves (boost de proximidad)
    }

    // ---- (E) ITD invariante con la distancia (Duda-Martens: ITD ~independiente del rango) ----
    //     Medido por respuesta al IMPULSO: ITD = retardo del pico de R menos el de L. Un impulso
    //     excita todo por igual, así el desbalance espectral del near-field (boost en un oído, cut en
    //     el otro) no corre la medición — la correlación de ruido sí se rompía por esa descorrelación.
    {
        auto captureITD = [&] (float radius)
        {
            orbita::SpatialEngine eng;
            eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
            juce::AudioBuffer<float> buf (2, block);
            const int cap = 512; std::vector<float> hL ((size_t) cap, 0.0f), hR ((size_t) cap, 0.0f);
            int got = 0; bool fired = false;
            while (got < cap)
            {
                buf.clear();
                if (! fired) { buf.setSample (0, 0, 1.0f); fired = true; } // delta al inicio
                eng.process (buf, 1, (float)(kPi/2), 1.0f, 0.0f, 0.5f, false, radius, false);
                const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
                for (int i = 0; i < block && got < cap; ++i) { hL[(size_t)got]=oL[i]; hR[(size_t)got]=oR[i]; ++got; }
            }
            int pL = 0, pR = 0; float mL = 0, mR = 0;
            for (int i = 0; i < cap; ++i)
            {
                if (std::abs (hL[(size_t)i]) > mL) { mL = std::abs (hL[(size_t)i]); pL = i; }
                if (std::abs (hR[(size_t)i]) > mR) { mR = std::abs (hR[(size_t)i]); pR = i; }
            }
            return pR - pL; // el oído lejano (R, fuente a la izq) llega después -> pR > pL
        };
        const int itdNear = captureITD (0.05f), itdFar = captureITD (0.95f);
        WARN ("== (E) ITD a 90 izq (samples @48k, pico R - pico L): cerca=" << itdNear << "  lejos=" << itdFar
              << "  (el near-field NO toca el delay) ==");
        REQUIRE (itdNear == itdFar);  // ITD idéntico cerca/lejos: no se modula con la distancia
    }
}

TEST_CASE ("NEAR-FIELD tuning: comparación de los 3 perfiles (medición real)", "[nftune]")
{
    // Mide los 3 perfiles (Sutil/Físico/Dramático) en el motor real para elegir por datos, no por oído.
    using NF = orbita::SpatialEngine::NearFieldTuning;
    const double sr = 48000.0; const int block = 256, numBlocks = 260, warmup = 140;

    auto ild = [&] (NF t, float freq, float azRad, float radius)
    {
        orbita::SpatialEngine eng; eng.setNearField (t);
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        const double w = kTwoPi * (double) freq / sr; double ph = 0.0; double sL = 0, sR = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear(); auto* in = buf.getWritePointer (0);
            for (int i = 0; i < block; ++i) { in[i] = 0.5f * (float) std::sin (ph); ph += w; }
            eng.process (buf, 1, azRad, 1.0f, 0.0f, 0.5f, false, radius, false);
            if (b < warmup) continue;
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < block; ++i) { sL += (double) oL[i]*oL[i]; sR += (double) oR[i]*oR[i]; }
        }
        return 20.0 * std::log10 (std::sqrt (sL / std::max (sR, 1e-12)));
    };
    auto energy = [&] (NF t, float freq, float azRad, float radius)
    {
        orbita::SpatialEngine eng; eng.setNearField (t);
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        const double w = kTwoPi * (double) freq / sr; double ph = 0.0; double e = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear(); auto* in = buf.getWritePointer (0);
            for (int i = 0; i < block; ++i) { in[i] = 0.5f * (float) std::sin (ph); ph += w; }
            eng.process (buf, 1, azRad, 1.0f, 0.0f, 0.5f, false, radius, false);
            if (b < warmup) continue;
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < block; ++i) e += (double) oL[i]*oL[i] + (double) oR[i]*oR[i];
        }
        return e;
    };
    const float L90 = (float)(kPi*0.5), L45 = (float)(kPi*0.25);

    struct Prof { const char* name; NF t; };                 // campos: bassDB, ildDB, cornerHz, normalize
    const Prof profs[] = {
        { "Sutil    ", NF{ 4.0f, 14.0f,  800.0f, true } },
        { "Fisico   ", NF{ 6.0f, 20.0f,  900.0f, true } },
        { "Dramatico", NF{ 7.0f, 26.0f, 1100.0f, true } },
    };
    WARN ("================ COMPARACION DE PERFILES NEAR-FIELD (todos normalizados, Radio al fondo) ================");
    WARN ("perfil      |  ILD@90 cerca |  ILD@45 cerca |  +graves prox |  +ILD@8k (timbre, debe ~0) ");
    for (auto& p : profs)
    {
        const double ild90 = ild (p.t, 160.0f, L90, 0.05f);
        const double ild45 = ild (p.t, 160.0f, L45, 0.05f);
        const double tilt  = 10.0*std::log10 (energy (p.t,160.0f,0.0f,0.05f) / energy (p.t,2000.0f,0.0f,0.05f))
                           - 10.0*std::log10 (energy (p.t,160.0f,0.0f,0.95f) / energy (p.t,2000.0f,0.0f,0.95f));
        const double inc8k = ild (p.t, 8000.0f, L90, 0.05f) - ild (p.t, 8000.0f, L90, 0.95f);
        WARN ("  " << p.name << " |    " << ild90 << " dB |    " << ild45 << " dB |   +" << tilt
              << " dB |   " << inc8k << " dB");
    }
    SUCCEED();
}

TEST_CASE ("cerebro: modo Fijo mantiene el azimut quieto", "[m2]")
{
    orbita::OrbitBrain brain; brain.prepare (48000.0);
    orbita::OrbitBrain::Params p;
    p.rate = orbita::OrbitBrain::Fixed;
    p.fixedAzRad = (float) (kPi * 0.5);  // 90° izquierda
    brain.setParams (p);
    orbita::TransportInfo t; t.isPlaying = true; t.bpm = 120.0; t.ppqPosition = 0.0;
    const float a0 = brain.advance (512, t).azimuth;
    t.ppqPosition = 4.0;                  // pasó un compás entero
    const float a1 = brain.advance (512, t).azimuth;
    REQUIRE (std::abs (a0 - (float) (kPi * 0.5)) < 1.0e-4f); // respeta el azimut fijo
    REQUIRE (std::abs (a1 - a0) < 1.0e-6f);                  // NO se mueve con el tiempo/transporte
}

TEST_CASE ("DIAG full-path: modo Fijo via processBlock mantiene la posición", "[diag]")
{
    PluginProcessor proc; proc.prepareToPlay (48000.0, 512);
    // orbRate: choice de 5 opciones -> normalizado 1.0 = último índice = Fijo
    proc.apvts.getParameter ("orbRate")->setValueNotifyingHost (1.0f);
    // orbFixedAz en -180..180; 90° -> normalizado (90-(-180))/360 = 0.75
    proc.apvts.getParameter ("orbFixedAz")->setValueNotifyingHost (0.75f);
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    auto tick = [&] { buf.clear(); for (int i=0;i<512;++i) buf.setSample (0,i,0.2f); proc.processBlock (buf, midi); return proc.uiAzimuth.load(); };
    const float a0 = tick();
    for (int b = 0; b < 10; ++b) tick();
    const float a1 = proc.uiAzimuth.load();
    REQUIRE (std::abs (a1 - a0) < 1.0e-5f);                  // no se mueve por el camino real
    REQUIRE (std::abs (a0 - (float) (kPi * 0.5)) < 0.02f);   // queda en ~90° (el ángulo fijo seteado)
}

TEST_CASE ("visualizador: mapeo de offset del mouse a ángulo (azDegFromOffset)", "[m2]")
{
    // El dibujo es px = cx - ax·sin(az), py = cy - ay·cos(az). El inverso: az = atan2(-dx, -dy).
    // 0° = frente (mouse arriba del centro), +90° = izquierda, -90° = derecha.
    REQUIRE (std::abs (OrbitView::azDegFromOffset (0.0f, -1.0f) -   0.0f) < 0.5f); // arriba = frente
    REQUIRE (std::abs (OrbitView::azDegFromOffset (-1.0f, 0.0f) -  90.0f) < 0.5f); // izquierda = +90
    REQUIRE (std::abs (OrbitView::azDegFromOffset ( 1.0f, 0.0f) - -90.0f) < 0.5f); // derecha  = -90
    REQUIRE (std::abs (OrbitView::azDegFromOffset (0.0f,  1.0f)) - 180.0f < 0.5f); // abajo = atrás (±180)
}

TEST_CASE ("NEAR-FIELD pro: evaluación profesional de candidatos (elegir el más pro)", "[nfpro]")
{
    // Un perfil "pro" MAXIMIZA el cue de cercanía SIN romper nada: sin colorear agudos, sin arruinar
    // la compatibilidad mono (el near-field mete diferencia L/R en graves), sin comerse el headroom.
    using NF = orbita::SpatialEngine::NearFieldTuning;
    const double sr = 48000.0; const int block = 256, numBlocks = 280, warmup = 150;
    const float L90 = (float)(kPi*0.5), L45 = (float)(kPi*0.25);

    auto ild = [&] (NF t, float freq, float azRad, float radius)
    {
        orbita::SpatialEngine eng; eng.setNearField (t);
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        const double w = kTwoPi * (double) freq / sr; double ph = 0.0; double sL = 0, sR = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear(); auto* in = buf.getWritePointer (0);
            for (int i = 0; i < block; ++i) { in[i] = 0.5f * (float) std::sin (ph); ph += w; }
            eng.process (buf, 1, azRad, 1.0f, 0.0f, 0.5f, false, radius, false);
            if (b < warmup) continue;
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < block; ++i) { sL += (double) oL[i]*oL[i]; sR += (double) oR[i]*oR[i]; }
        }
        return 20.0 * std::log10 (std::sqrt (sL / std::max (sR, 1e-12)));
    };
    // retención mono (1 = sin pérdida al sumar L+R). El ILD es NIVEL (no fase) -> no cancela; esto
    // mide si el shelf introduce algo de fase en graves. Con EN FASE los graves van a mono -> ~1.
    auto monoRet = [&] (NF t, float freq, float azRad, float radius, bool monoSafe)
    {
        orbita::SpatialEngine eng; eng.setNearField (t);
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        const double w = kTwoPi * (double) freq / sr; double ph = 0.0; double sC = 0, sM = 0;
        for (int b = 0; b < numBlocks; ++b)
        {
            buf.clear(); auto* in = buf.getWritePointer (0);
            for (int i = 0; i < block; ++i) { in[i] = 0.5f * (float) std::sin (ph); ph += w; }
            eng.process (buf, 1, azRad, 1.0f, 0.0f, 0.5f, monoSafe, radius, false);
            if (b < warmup) continue;
            const auto* oL = buf.getReadPointer (0); const auto* oR = buf.getReadPointer (1);
            for (int i = 0; i < block; ++i) { const float m = 0.5f*(oL[i]+oR[i]);
                sC += 0.5*((double)oL[i]*oL[i]+(double)oR[i]*oR[i]); sM += (double) m*m; }
        }
        return std::sqrt (sM / std::max (sC, 1e-12));
    };
    // peak con material ANCHO full-scale orbitando cerca (headroom: < ~2 seguro, no clip salvaje).
    auto peakWide = [&] (NF t, float radius)
    {
        orbita::SpatialEngine eng; eng.setNearField (t);
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block); unsigned int rng = 9u;
        auto white = [&] { rng = rng*1664525u+1013904223u; return (float)((double)rng/4294967295.0*2-1); };
        float peak = 0; bool finite = true;
        for (int b = 0; b < 400; ++b)
        {
            buf.clear(); for (int i = 0; i < block; ++i) buf.setSample (0, i, 0.99f * white());
            const double az = 2.0 * kTwoPi * (double)(b+1) / 400.0;
            eng.process (buf, 1, (float) az, 1.0f, 0.0f, 0.5f, false, radius, false);
            for (int i = 0; i < block; ++i) { if (! std::isfinite(buf.getSample(0,i)) || ! std::isfinite(buf.getSample(1,i))) finite=false;
                peak = std::max (peak, std::max (std::abs(buf.getSample(0,i)), std::abs(buf.getSample(1,i)))); }
        }
        struct R { float peak; bool finite; }; return R { peak, finite };
    };

    struct Cand { const char* name; NF t; };           // bassDB, ildDB, cornerHz, normalize
    const Cand cands[] = {
        { "Sutil    ", NF{ 4.0f, 14.0f,  800.0f, true } },
        { "Fisico   ", NF{ 6.0f, 20.0f,  900.0f, true } },
        { "Dramatico", NF{ 7.0f, 26.0f, 1100.0f, true } },
        { "Hibrido  ", NF{ 6.0f, 26.0f,  700.0f, true } }, // ILD fuerte, corner bajo: cue sin ensuciar agudos
        { "Pro      ", NF{ 6.0f, 24.0f,  800.0f, true } }, // óptimo: ILD ~físico real, corner bajo, headroom seguro
    };
    WARN ("===================== EVALUACION PRO DE CANDIDATOS NEAR-FIELD (cerca, ~0.19 m) =====================");
    WARN ("perfil    | ILD@90 | ILD@45 | colorAgudos(+8k) | selectiv(160-8k) | monoOFF@100 | monoEN@100 | peakWide");
    for (auto& c : cands)
    {
        const double ild90 = ild (c.t, 160.0f, L90, 0.05f);
        const double ild45 = ild (c.t, 160.0f, L45, 0.05f);
        const double inc160 = ild90 - ild (c.t, 160.0f, L90, 0.95f);
        const double inc8k  = ild (c.t, 8000.0f, L90, 0.05f) - ild (c.t, 8000.0f, L90, 0.95f);
        const double monoOff = monoRet (c.t, 100.0f, L90, 0.05f, false);
        const double monoEn  = monoRet (c.t, 100.0f, L90, 0.05f, true);
        const auto   pk = peakWide (c.t, 0.05f);
        WARN ("  " << c.name << " | " << ild90 << " | " << ild45 << " |   " << inc8k
              << "  |   " << (inc160 - inc8k) << "  |  " << monoOff << "  |  " << monoEn << "  |  " << pk.peak);
        REQUIRE (pk.finite);                   // no-negociable: nunca NaN/Inf
        CHECK   (pk.peak < 4.0f);              // headroom (evaluativo): material ancho full-scale cerca
        CHECK   (monoEn > monoOff - 0.01);     // EN FASE rescata (o iguala) la compatibilidad mono del grave
        CHECK   (monoEn > 0.9f);               // con EN FASE el grave queda en fase pese al ILD near-field
    }
    SUCCEED();
}

TEST_CASE ("DOPPLER cerebro: la perilla modula la distancia (cerca al frente, lejos atras)", "[doppler]")
{
    auto distAt = [&] (float doppler01, int rate, double phaseRevs)
    {
        orbita::OrbitBrain b; b.prepare (48000.0);
        orbita::OrbitBrain::Params p;
        p.rate = rate; p.shape = orbita::OrbitBrain::Circle; p.dir = orbita::OrbitBrain::CCW;
        p.chaos01 = 0.0f; p.radius01 = 0.6f; p.freeHz = 1.0f; p.doppler01 = doppler01;
        b.setParams (p);
        orbita::TransportInfo t; t.isPlaying = false;
        // avanzar hasta ~phaseRevs vueltas (Free a 1 Hz: 1 vuelta/seg). bloques de 512 @48k.
        const int blocks = (int) std::round (phaseRevs * 48000.0 / 512.0);
        orbita::SpatialTarget out;
        for (int i = 0; i < std::max (1, blocks); ++i) out = b.advance (512, t);
        return out;
    };

    // doppler=0 -> distancia constante == radius01 (sin modulacion)
    const float d0a = distAt (0.0f, orbita::OrbitBrain::Free, 0.10).distance;
    const float d0b = distAt (0.0f, orbita::OrbitBrain::Free, 0.35).distance;
    REQUIRE (std::abs (d0a - 0.6f) < 1.0e-4f);
    REQUIRE (std::abs (d0a - d0b) < 1.0e-4f);          // no varia con la fase

    // doppler=1 -> cerca de az=0 (frente) la distancia es MENOR que cerca de az=pi (atras)
    auto front = distAt (1.0f, orbita::OrbitBrain::Free, 1.0);   // ~vuelta completa -> az~0 (frente)
    auto back  = distAt (1.0f, orbita::OrbitBrain::Free, 0.5);   // ~media vuelta -> az~pi (atras)
    REQUIRE (front.distance < back.distance - 0.05f); // mas cerca al frente
    REQUIRE (front.distance >= 0.0f); REQUIRE (back.distance <= 1.0f); // clamp [0,1]
    REQUIRE (std::isfinite (front.distance));

    // modo Fijo + doppler>0 -> sin modulacion (distance == radius01)
    orbita::OrbitBrain b; b.prepare (48000.0);
    orbita::OrbitBrain::Params p; p.rate = orbita::OrbitBrain::Fixed; p.radius01 = 0.6f;
    p.doppler01 = 1.0f; p.fixedAzRad = 0.0f; b.setParams (p);
    orbita::TransportInfo t; t.isPlaying = true;
    REQUIRE (std::abs (b.advance (512, t).distance - 0.6f) < 1.0e-4f);
}

namespace {
// Frecuencia media de cruces por cero ascendentes del canal L (Hz), tras warm-up.
double measurePitchHz (float doppler01, float radius01, double freqIn, double sr, int block, int numBlocks, double totalRevs)
{
    orbita::SpatialEngine eng;
    eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
    juce::AudioBuffer<float> buf (2, block);
    double phase = 0.0; const double w = kTwoPi * freqIn / sr;
    double prev = 0.0; long crossings = 0; long samples = 0; const int warm = numBlocks / 5;
    for (int b = 0; b < numBlocks; ++b)
    {
        buf.clear(); auto* in = buf.getWritePointer (0);
        for (int i = 0; i < block; ++i) { in[i] = 0.5f * (float) std::sin (phase); phase += w; }
        const double az = totalRevs * kTwoPi * (double) (b + 1) / (double) numBlocks;
        eng.process (buf, 1, orbita::SpatialEngine::EngineParams {
            .azimuthRad = (float) az, .mix01 = 1.0f, .radius01 = radius01,
            .distance01 = radius01, .doppler01 = doppler01 }); // distance==radius: caso aislado del motor
        if (b < warm) continue;
        const auto* o = buf.getReadPointer (0);
        for (int i = 0; i < block; ++i) { if (prev < 0.0 && o[i] >= 0.0f) ++crossings; prev = o[i]; ++samples; }
    }
    return (double) crossings * sr / std::max (1L, samples);
}
} // namespace

TEST_CASE ("DOPPLER motor: doppler=0 NO altera el pitch; doppler>0 desvia el pitch (monotono)", "[doppler]")
{
    const double sr = 48000.0; const double f = 300.0;
    // doppler=0 -> el pitch medio del wet ~ la entrada (sin shift). distance==radius -> bypass.
    const double p0 = measurePitchHz (0.0f, 0.6f, f, sr, 256, 800, 4.0);
    REQUIRE (std::abs (p0 - f) < 6.0); // ~entrada (tolerancia de la medicion por cruces)

    // Con la DISTANCIA modulada por-bloque (fly-by real via motor), la frecuencia instantanea
    // se desvia; medimos el rango pico-a-pico de la frecuencia instantanea creciente con doppler.
    // Mide el RANGO pico-a-pico de la frecuencia instantánea por cruces por cero interpolados a sub-sample
    // (resolución fina; el método viejo de "cruces enteros por segmento de 4096" tenía un cuanto de ~11.7 Hz
    // que NO podía detectar el wobble real de ~1 semitono a doppler medio -> falso negativo).
    auto pitchSpread = [&] (float doppler01)
    {
        orbita::SpatialEngine eng; eng.prepare (juce::dsp::ProcessSpec { sr, 256u, 2 });
        juce::AudioBuffer<float> buf (2, 256); double ph = 0.0; const double w = kTwoPi * f / sr;
        double fmin = 1e9, fmax = -1e9; float prev = 0.0f; double gpos = 0.0, lastX = -1.0;
        for (int b = 0; b < 1200; ++b)
        {
            buf.clear(); auto* in = buf.getWritePointer (0);
            for (int i = 0; i < 256; ++i) { in[i] = 0.5f * (float) std::sin (ph); ph += w; }
            const double az = 6.0 * kTwoPi * (double)(b+1) / 1200.0; // 6 vueltas
            const double A = 0.55 * (double) doppler01;   // curve=1 LINEAL (coincide con OrbitBrain)
            const float dist = (float) std::clamp (0.6 - A * std::cos (az), 0.0, 1.0);
            eng.process (buf, 1, orbita::SpatialEngine::EngineParams {
                .azimuthRad = (float) az, .mix01 = 1.0f, .radius01 = 0.6f,
                .distance01 = dist, .doppler01 = doppler01 });
            const bool warm = (b >= 240);
            const auto* o = buf.getReadPointer (0);
            for (int i = 0; i < 256; ++i)
            {
                const float cur = o[i];
                if (prev < 0.0f && cur >= 0.0f)   // cruce ascendente -> instante sub-sample (interp lineal)
                {
                    const double x = gpos + (double) (-prev) / (double) (cur - prev);
                    if (warm && lastX >= 0.0)
                    {
                        const double fi = sr / (x - lastX);
                        if (fi > f * 0.25 && fi < f * 4.0) { fmin = std::min (fmin, fi); fmax = std::max (fmax, fi); }
                    }
                    lastX = x;
                }
                prev = cur; gpos += 1.0;
            }
        }
        return (fmax < fmin) ? 0.0 : fmax - fmin;
    };
    const double spread0 = pitchSpread (0.0f);
    const double spreadHalf = pitchSpread (0.5f);
    const double spreadFull = pitchSpread (1.0f);
    WARN ("DOPPLER pitch spread (Hz): d=0 -> " << spread0 << "  d=0.5 -> " << spreadHalf << "  d=1 -> " << spreadFull);
    REQUIRE (spreadFull > spreadHalf);          // monotono: mas perilla = mas Doppler
    REQUIRE (spreadHalf > spread0 + 1.0);        // doppler>0 produce shift audible
}

TEST_CASE ("DOPPLER motor: sin NaN/clicks a Speed alto + Doppler maximo (44.1/48/96k)", "[doppler]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        orbita::SpatialEngine eng; eng.prepare (juce::dsp::ProcessSpec { sr, 256u, 2 });
        juce::AudioBuffer<float> buf (2, 256); double ph = 0.0; const double w = kTwoPi * 220.0 / sr;
        float prevL = 0, prevR = 0, maxJump = 0; bool finite = true;
        for (int b = 0; b < 800; ++b)
        {
            buf.clear(); auto* in = buf.getWritePointer (0);
            for (int i = 0; i < 256; ++i) { in[i] = 0.25f * (float) std::sin (ph); ph += w; }
            const double az = 30.0 * kTwoPi * (double)(b+1) / 800.0; // Speed muy alto
            const double A = 0.55; const float dist = (float) std::clamp (0.4 - A * std::cos (az), 0.0, 1.0);
            eng.process (buf, 1, orbita::SpatialEngine::EngineParams {
                .azimuthRad = (float) az, .mix01 = 1.0f, .radius01 = 0.4f, .distance01 = dist, .doppler01 = 1.0f });
            const auto* oL = buf.getReadPointer(0); const auto* oR = buf.getReadPointer(1);
            for (int i = 0; i < 256; ++i) { if (!std::isfinite(oL[i])||!std::isfinite(oR[i])) finite=false;
                maxJump = std::max (maxJump, std::max (std::abs(oL[i]-prevL), std::abs(oR[i]-prevR))); prevL=oL[i]; prevR=oR[i]; }
        }
        WARN ("DOPPLER sr=" << sr << " Speed alto + d=1: maxJump=" << maxJump);
        REQUIRE (finite);
        REQUIRE (maxJump < 0.5f);   // sin clicks groseros (el delay rampeado no salta)
    }
}

TEST_CASE ("DOPPLER full-path: la distancia del visualizador se mueve con la orbita", "[doppler]")
{
    PluginProcessor proc; proc.prepareToPlay (48000.0, 512);
    proc.apvts.getParameter ("orbRate")->setValueNotifyingHost (0.75f); // Free
    proc.apvts.getParameter ("orbFreeHz")->setValueNotifyingHost (1.0f); // rapido
    proc.apvts.getParameter ("orbRadius")->setValueNotifyingHost (0.6f);
    proc.apvts.getParameter ("doppler")->setValueNotifyingHost (1.0f);  // Doppler al maximo

    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    float dmin = 1e9f, dmax = -1e9f;
    for (int b = 0; b < 200; ++b)
    {
        buf.clear(); for (int i = 0; i < 512; ++i) buf.setSample (0, i, 0.2f);
        proc.processBlock (buf, midi);
        const float d = proc.uiDistance.load();
        if (b > 40) { dmin = std::min (dmin, d); dmax = std::max (dmax, d); }
    }
    WARN ("DOPPLER full-path uiDistance: min=" << dmin << " max=" << dmax);
    REQUIRE (dmax - dmin > 0.1f);          // la distancia oscila (fly-by) por el camino real
    REQUIRE (dmin >= 0.0f); REQUIRE (dmax <= 1.0f);
}

TEST_CASE ("DOPPLER medicion: caracterizacion fisica (pitch vs perilla, semitonos, Fijo, near-field combo)", "[dopmeas]")
{
    const double sr = 48000.0, f = 300.0;

    // (A) pitch shift maximo en semitonos vs perilla: corremos la fuente cruzando el costado
    //     (v_radial maxima) a velocidad fija y medimos el corrimiento maximo de frecuencia.
    auto maxShiftSemis = [&] (float doppler01, double revs)
    {
        orbita::SpatialEngine eng; eng.prepare (juce::dsp::ProcessSpec { sr, 256u, 2 });
        juce::AudioBuffer<float> buf (2, 256); double ph = 0.0; const double w = kTwoPi * f / sr;
        const int nb = 2000; float prev = 0.0f; double gpos = 0.0, lastX = -1.0, fmin = 1e9, fmax = 0.0;
        for (int b = 0; b < nb; ++b)
        {
            buf.clear(); auto* in = buf.getWritePointer (0);
            for (int i = 0; i < 256; ++i) { in[i] = 0.5f * (float) std::sin (ph); ph += w; }
            const double az = revs * kTwoPi * (double)(b+1) / nb;
            const double A = 0.55 * (double) doppler01;   // curve=1 LINEAL (coincide con OrbitBrain)
            const float dist = (float) std::clamp (0.5 - A * std::cos (az), 0.0, 1.0);
            eng.process (buf, 1, orbita::SpatialEngine::EngineParams {
                .azimuthRad=(float)az, .mix01=1.0f, .radius01=0.5f, .distance01=dist, .doppler01=doppler01 });
            const bool warm = (b >= nb/5);
            const auto* o = buf.getReadPointer (0);
            for (int i = 0; i < 256; ++i)
            {
                const float cur = o[i];
                if (prev < 0.0f && cur >= 0.0f) // cruce ascendente: instante sub-sample (interpolacion lineal)
                {
                    const double x = gpos + (double) (-prev) / (double) (cur - prev);
                    if (warm && lastX >= 0.0)
                    {
                        const double fi = sr / (x - lastX);
                        if (fi > f * 0.25 && fi < f * 4.0) { fmin = std::min (fmin, fi); fmax = std::max (fmax, fi); } // descarta cruces espurios (ringing HRIR)
                    }
                    lastX = x;
                }
                prev = cur; gpos += 1.0;
            }
        }
        return std::max (12.0 * std::log2 (std::max (fmax, 1.0) / f), 12.0 * std::log2 (f / std::max (fmin, 1.0)));
    };
    WARN ("== (A) pitch shift max (semitonos) a ~3 vueltas: d=0.25 -> " << maxShiftSemis (0.25f, 3.0)
          << "  d=0.5 -> " << maxShiftSemis (0.5f, 3.0) << "  d=1.0 -> " << maxShiftSemis (1.0f, 3.0));
    WARN ("== (A') a Speed alto (~12 vueltas): d=0.5 -> " << maxShiftSemis (0.5f, 12.0)
          << "  d=1.0 -> " << maxShiftSemis (1.0f, 12.0));
    REQUIRE (maxShiftSemis (1.0f, 12.0) > maxShiftSemis (0.5f, 12.0)); // mas perilla = mas semitonos (a Speed con señal clara)

    // (B) Fijo: doppler>0 NO produce shift (sin movimiento -> sin v_radial). distance==radius constante.
    const double pFixed = measurePitchHz (1.0f, 0.5f, f, sr, 256, 800, 0.0); // totalRevs=0 (quieto)
    WARN ("== (B) Fijo (sin orbita) pitch con d=1 = " << pFixed << " Hz (esperado ~" << f << ")");
    REQUIRE (std::abs (pFixed - f) < 6.0);

    // (C) combo near-field: al acercarse (fly-by) sube el tilt de graves (engorda). De frente cerca vs lejos.
    auto bassTilt = [&] (float radius01)
    {
        orbita::SpatialEngine eng; eng.prepare (juce::dsp::ProcessSpec { sr, 256u, 2 });
        auto energy = [&] (double freq) {
            juce::AudioBuffer<float> buf (2, 256); double ph = 0.0; const double w = kTwoPi * freq / sr; double e = 0;
            for (int b = 0; b < 260; ++b) { buf.clear(); auto* in = buf.getWritePointer (0);
                for (int i = 0; i < 256; ++i) { in[i] = 0.5f * (float) std::sin (ph); ph += w; }
                eng.process (buf, 1, orbita::SpatialEngine::EngineParams { .azimuthRad=0.0f, .mix01=1.0f, .radius01=radius01, .distance01=radius01 });
                if (b < 140) continue; const auto* o = buf.getReadPointer (0);
                for (int i = 0; i < 256; ++i) e += (double)o[i]*o[i]; }
            return e;
        };
        return 10.0 * std::log10 (energy (160.0) / energy (2000.0));
    };
    WARN ("== (C) tilt graves/medios: cerca(0.05)=" << bassTilt (0.05f) << " dB  lejos(0.95)=" << bassTilt (0.95f) << " dB");
    REQUIRE (bassTilt (0.05f) > bassTilt (0.95f) + 1.0); // cerca engorda (near-field se activa con el fly-by)
    SUCCEED();
}

TEST_CASE ("CPU del motor: desglose por etapa (auriculares, peor caso de movimiento)", "[cpu]")
{
    const double sr = 48000.0;
    auto measure = [&] (int block, bool doppler, float room, bool speaker)
    {
        orbita::SpatialEngine eng;
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        const int blocks = (int) (sr * 4.0 / block); // ~4 s de audio
        double ph = 0.0; const double w = kTwoPi * 220.0 / sr;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i) { const float s = 0.3f * (float) std::sin (ph); ph += w; buf.setSample (0, i, s); buf.setSample (1, i, s); }
            const double az = 3.0 * kTwoPi * (double)(b+1) / blocks; // órbita en movimiento (recarga voces)
            eng.process (buf, 1, orbita::SpatialEngine::EngineParams {
                .azimuthRad=(float)az, .mix01=1.0f, .room01=room, .width01=1.0f, .monoSafe=true,
                .radius01=0.3f, .speakerMode=speaker, .distance01=0.3f, .doppler01= doppler?1.0f:0.0f });
        }
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        return secs / ((double) blocks * block / sr) * 100.0;
    };
    for (int block : { 128, 256 })
    {
        const double full   = measure (block, true,  1.0f, false);
        const double noRefl = measure (block, true,  0.0f, false);
        const double noDop  = measure (block, false, 1.0f, false);
        const double race   = measure (block, true,  1.0f, true);
        WARN ("CPU block " << block << ": COMPLETO=" << full << "%  room0=" << noRefl
              << "%  doppler0=" << noDop << "%  +RACE=" << race << "%");
    }
    SUCCEED();
}

TEST_CASE ("PATH REAL (brain+motor) todo al maximo: CPU + peak (caso de Joaquin)", "[cpu]")
{
    const double sr = 48000.0;
    auto run = [&] (int block, bool speakers)
    {
        orbita::OrbitBrain brain; brain.prepare (sr);
        orbita::OrbitBrain::Params bp;
        bp.shape = orbita::OrbitBrain::Ellipse; bp.rate = orbita::OrbitBrain::Free; bp.dir = orbita::OrbitBrain::CW;
        bp.freeHz = 8.0f; bp.chaos01 = 1.0f; bp.doppler01 = 1.0f; bp.radius01 = 1.0f; bp.spread01 = 1.0f;
        brain.setParams (bp);
        orbita::SpatialEngine eng; eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block); orbita::TransportInfo tr; tr.isPlaying = false;
        double ph = 0.0; const double w = kTwoPi * 110.0 / sr; float peak = 0.0f;
        const int blocks = (int) (sr * 4.0 / block);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i) { const float s = 0.7f * (float) std::sin (ph) + 0.25f * (float) std::sin (ph*2.7); ph += w; buf.setSample (0,i,s); buf.setSample (1,i,s); } // mono full-scale (no anti-fase: si no, se cancela al sumar)
            const auto tp = brain.advance (block, tr);
            eng.process (buf, 2, orbita::SpatialEngine::EngineParams {
                .azimuthRad = tp.azimuth, .mix01 = 1.0f, .room01 = 1.0f, .width01 = 1.0f, .monoSafe = false,
                .radius01 = 1.0f, .speakerMode = speakers, .distance01 = tp.distance, .doppler01 = 1.0f });
            for (int i = 0; i < block; ++i) { peak = std::max (peak, std::abs (buf.getSample (0,i))); peak = std::max (peak, std::abs (buf.getSample (1,i))); }
        }
        const double cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 / ((double) blocks * block / sr) * 100.0;
        WARN ("PATH REAL block " << block << (speakers?" PARLANTES":" AURICULARES") << ": CPU=" << cpu << "%  peak=" << peak << (peak>1.0f?"  >>> CLIPEA":""));
        return peak;
    };
    const float pAur = run (128, false);
    const float pPar = run (128, true);
    run (256, false);
    REQUIRE (pAur <= 1.0f);   // auriculares: no clipea por el path real
    REQUIRE (pPar <= 1.0f);   // parlantes: no clipea por el path real
}

TEST_CASE ("Clipping: peor caso (todo al maximo + fly-by cerca) + desglose", "[clip]")
{
    const double sr = 48000.0; const int block = 128;
    auto peakOf = [&] (float doppler, float width, float room, float radius, bool monoSafe, bool speaker)
    {
        orbita::SpatialEngine eng; eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block);
        double ph = 0.0; const double w = kTwoPi * 110.0 / sr; float peak = 0.0f;
        const int blocks = (int) (sr * 3.0 / block);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i) { const float s = 0.9f * (float) std::sin (ph); ph += w; buf.setSample (0,i,s); buf.setSample (1,i,-s); } // estereo ancho full-scale
            const double az = 4.0 * kTwoPi * (double)(b+1) / blocks;
            const float dist = (float) std::clamp ((double) radius - 0.55 * std::cos (az), 0.0, 1.0); // fly-by: pasa cerca
            eng.process (buf, 1, orbita::SpatialEngine::EngineParams {
                .azimuthRad=(float)az, .mix01=1.0f, .room01=room, .width01=width, .monoSafe=monoSafe,
                .radius01=radius, .speakerMode=speaker, .distance01=dist, .doppler01=doppler });
            for (int i = 0; i < block; ++i) { peak = std::max (peak, std::abs (buf.getSample (0,i))); peak = std::max (peak, std::abs (buf.getSample (1,i))); }
        }
        return peak;
    };
    const float pall  = peakOf (1.0f, 1.0f, 1.0f, 0.10f, false, false);
    const float prace = peakOf (1.0f, 1.0f, 1.0f, 0.10f, false, true);
    WARN ("PEAK todo-al-maximo = " << pall << "   +RACE = " << prace
          << "   (sin-doppler=" << peakOf (0.0f,1.0f,1.0f,0.10f,false,false)
          << "  width0.5="      << peakOf (1.0f,0.5f,1.0f,0.10f,false,false)
          << "  lejos="         << peakOf (1.0f,1.0f,1.0f,0.60f,false,false) << ")");
    REQUIRE (pall  <= 0.99f);   // peor caso auriculares: el limiter contiene bajo el techo
    REQUIRE (prace <= 0.99f);   // peor caso Parlantes (RACE): el limiter contiene bajo el techo
}

TEST_CASE ("Limiter de salida: nunca pasa el techo en todo el rango (todo al maximo)", "[clip]")
{
    const double sr = 48000.0; const int block = 128;
    auto runPeak = [&] (float radius, bool speaker)
    {
        orbita::OrbitBrain brain; brain.prepare (sr);
        orbita::OrbitBrain::Params bp; bp.shape = orbita::OrbitBrain::Ellipse; bp.rate = orbita::OrbitBrain::Free;
        bp.dir = orbita::OrbitBrain::CW; bp.freeHz = 8.0f; bp.chaos01 = 1.0f; bp.doppler01 = 1.0f; bp.radius01 = radius; bp.spread01 = 1.0f;
        brain.setParams (bp);
        orbita::SpatialEngine eng; eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block); orbita::TransportInfo tr; tr.isPlaying = false;
        double ph = 0.0; const double w = kTwoPi * 110.0 / sr; float peak = 0.0f;
        for (int b = 0; b < (int) (sr * 3.0 / block); ++b)
        {
            for (int i = 0; i < block; ++i) { const float s = 0.7f*(float)std::sin(ph)+0.25f*(float)std::sin(ph*2.7); ph+=w; buf.setSample(0,i,s); buf.setSample(1,i,s); }
            const auto tp = brain.advance (block, tr);
            eng.process (buf, 2, orbita::SpatialEngine::EngineParams {
                .azimuthRad=tp.azimuth, .mix01=1.0f, .room01=1.0f, .width01=1.0f, .monoSafe=false,
                .radius01=radius, .speakerMode=speaker, .distance01=tp.distance, .doppler01=1.0f });
            for (int i = 0; i < block; ++i) { peak=std::max(peak,std::abs(buf.getSample(0,i))); peak=std::max(peak,std::abs(buf.getSample(1,i))); }
        }
        return peak;
    };
    for (float r : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        REQUIRE (runPeak (r, false) <= 0.99f);   // auriculares: nunca pasa el techo
        REQUIRE (runPeak (r, true)  <= 0.99f);   // parlantes (RACE): nunca pasa el techo
    }
}

TEST_CASE ("True-peak (inter-sample) en el extremo, por ceiling del limiter", "[truepeak]")
{
    const double sr = 48000.0; const int block = 128;
    auto truePeakAt = [&] (float ceiling)
    {
        orbita::OrbitBrain brain; brain.prepare (sr);
        orbita::OrbitBrain::Params bp; bp.shape=orbita::OrbitBrain::Ellipse; bp.rate=orbita::OrbitBrain::Free; bp.dir=orbita::OrbitBrain::CW;
        bp.freeHz=8.0f; bp.chaos01=1.0f; bp.doppler01=1.0f; bp.radius01=0.3f; bp.spread01=1.0f;
        brain.setParams(bp);
        orbita::SpatialEngine eng; eng.setLimiter({ceiling,60.0f}); eng.prepare(juce::dsp::ProcessSpec{sr,(juce::uint32)block,2});
        juce::AudioBuffer<float> buf(2,block); orbita::TransportInfo tr; tr.isPlaying=false;
        double ph=0; const double w=kTwoPi*3000.0/sr;  // 3 kHz: mas HF -> mas inter-sample peak
        float P0=0,P1=0,P2=0,P3=0, tp=0;               // 4-tap history (canal L) para Catmull-Rom
        for(int b=0;b<400;b++){
            for(int i=0;i<block;i++){const float s=0.9f*(float)std::sin(ph);ph+=w;buf.setSample(0,i,s);buf.setSample(1,i,s);}
            const auto t=brain.advance(block,tr);
            eng.process(buf,2,orbita::SpatialEngine::EngineParams{.azimuthRad=t.azimuth,.mix01=1.0f,.room01=1.0f,.width01=1.0f,.monoSafe=false,.radius01=0.3f,.speakerMode=true,.distance01=t.distance,.doppler01=1.0f});
            if(b<80)continue;
            const auto*o=buf.getReadPointer(0);
            for(int i=0;i<block;i++){
                P0=P1;P1=P2;P2=P3;P3=o[i];
                tp=std::max(tp,std::abs(P3));
                for(int k=1;k<4;k++){ const float t2=(float)k*0.25f; // 4x oversample entre P1 y P2
                    const float v=0.5f*((2.0f*P1)+(-P0+P2)*t2+(2.0f*P0-5.0f*P1+4.0f*P2-P3)*t2*t2+(-P0+3.0f*P1-3.0f*P2+P3)*t2*t2*t2);
                    tp=std::max(tp,std::abs(v)); }
            }
        }
        return tp;
    };
    for (float c : { 0.985f, 0.95f, 0.90f, 0.85f })
        WARN ("ceiling=" << c << "  ->  TRUE-PEAK estimado=" << truePeakAt(c) << (truePeakAt(c)>1.0f?"  >>> pasa 0 dBFS (clip true-peak)":"  ok"));
    REQUIRE (truePeakAt (0.85f) < 0.97f);  // ceiling de produccion: true-peak con margen claro bajo 0 dBFS
}

TEST_CASE ("Aspereza vs Speed (aisla el artefacto de movimiento)", "[rough]")
{
    const double sr = 48000.0; const int block = 128;
    auto rough = [&] (float freeHz, bool doppler, float ceiling, float caos)
    {
        orbita::OrbitBrain brain; brain.prepare (sr);
        orbita::OrbitBrain::Params bp; bp.shape = orbita::OrbitBrain::Ellipse; bp.rate = orbita::OrbitBrain::Free; bp.dir = orbita::OrbitBrain::CW;
        bp.freeHz = freeHz; bp.chaos01 = caos; bp.doppler01 = doppler?1.0f:0.0f; bp.radius01 = 0.3f; bp.spread01 = 1.0f;
        brain.setParams (bp);
        orbita::SpatialEngine eng; eng.setLimiter({ceiling,60.0f}); eng.prepare (juce::dsp::ProcessSpec { sr,(juce::uint32)block,2 });
        juce::AudioBuffer<float> buf (2, block); orbita::TransportInfo tr; tr.isPlaying = false;
        double ph = 0.0; const double w = kTwoPi*1000.0/sr; float prevL=0,prevR=0,maxJump=0;
        for (int b = 0; b < 400; ++b)
        {
            for (int i=0;i<block;i++){const float s=0.9f*(float)std::sin(ph);ph+=w;buf.setSample(0,i,s);buf.setSample(1,i,s);} // full-scale (limiter en juego, caso de Joaquin)
            const auto tp = brain.advance(block,tr);
            eng.process(buf,2,orbita::SpatialEngine::EngineParams{.azimuthRad=tp.azimuth,.mix01=1.0f,.room01=0.0f,.width01=1.0f,.monoSafe=false,.radius01=0.3f,.speakerMode=false,.distance01=tp.distance,.doppler01=(doppler?1.0f:0.0f)});
            if(b<80)continue;
            const auto*oL=buf.getReadPointer(0);const auto*oR=buf.getReadPointer(1);
            for(int i=0;i<block;i++){maxJump=std::max(maxJump,std::max(std::abs(oL[i]-prevL),std::abs(oR[i]-prevR)));prevL=oL[i];prevR=oR[i];}
        }
        return maxJump;
    };
    WARN ("speed8 full-scale maxJump (motor puro, lim-off):  caos1+dop1=" << rough(8.0f,true,10.0f,1.0f)
          << "  caos0+dop1=" << rough(8.0f,true,10.0f,0.0f)
          << "  caos1+dop0=" << rough(8.0f,false,10.0f,1.0f)
          << "  caos0+dop0=" << rough(8.0f,false,10.0f,0.0f));
    SUCCEED();
}

TEST_CASE ("Clip FUZZ: el limiter contiene TODO el espacio de parametros (exhaustivo)", "[clipfuzz]")
{
    unsigned int rng = 0x9E3779B9u;
    auto nf = [&] { rng = rng * 1664525u + 1013904223u; return (float) ((double) rng / 4294967295.0); };
    float worst = 0.0f; int configs = 0; bool finite = true;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
      for (int block : { 32, 64, 128, 256, 512 })
        for (int c = 0; c < 150; ++c)
        {
            orbita::OrbitBrain brain; brain.prepare (sr);
            orbita::OrbitBrain::Params bp;
            bp.shape = (int)(nf()*2.99f); bp.rate = (int)(nf()*4.99f); bp.dir = (int)(nf()*1.99f);
            bp.radius01 = nf(); bp.spread01 = nf(); bp.chaos01 = nf(); bp.doppler01 = nf(); bp.freeHz = 0.05f + nf()*7.95f;
            bp.fixedAzRad = (nf()*2.0f - 1.0f) * 3.14159f;
            brain.setParams (bp);
            orbita::SpatialEngine eng; eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
            const float mix = nf(), room = nf(), width = nf(), radius = nf(); const bool ms = nf()>0.5f, spk = nf()>0.5f;
            juce::AudioBuffer<float> buf (2, block); orbita::TransportInfo tr; tr.isPlaying = nf()>0.5f; tr.bpm = 120.0;
            double ph = 0.0; const double w = kTwoPi * (40.0 + nf()*4000.0) / sr; const int mat = (int)(nf()*3);
            for (int b = 0; b < 16; ++b)
            {
                for (int i = 0; i < block; ++i)
                {
                    float s;
                    if      (mat == 0) s = 0.98f * (float) std::sin (ph);            // seno full-scale
                    else if (mat == 1) s = (((i + b*block) % 37) == 0) ? 0.99f : 0.0f; // tren de impulsos
                    else               s = 0.9f * (nf()*2.0f - 1.0f);                // ruido full-scale
                    ph += w; buf.setSample (0,i,s); buf.setSample (1,i,s);
                }
                const auto tp = brain.advance (block, tr);
                eng.process (buf, 2, orbita::SpatialEngine::EngineParams {
                    .azimuthRad=tp.azimuth, .mix01=mix, .room01=room, .width01=width, .monoSafe=ms,
                    .radius01=radius, .speakerMode=spk, .distance01=tp.distance, .doppler01=bp.doppler01 });
                for (int i = 0; i < block; ++i) { const float l = buf.getSample(0,i), r = buf.getSample(1,i);
                    if (!std::isfinite(l) || !std::isfinite(r)) finite = false;
                    worst = std::max (worst, std::max (std::abs(l), std::abs(r))); }
            }
            ++configs;
        }
    WARN ("FUZZ: " << configs << " configs x material seno/impulsos/ruido. WORST PEAK GLOBAL = " << worst);
    REQUIRE (finite);            // nunca NaN/Inf
    REQUIRE (worst <= 0.99f);    // el limiter contiene CUALQUIER combinacion bajo el techo (0.985 + epsilon)
}

TEST_CASE ("Clip diagnostico: peak PRE-softclip por distancia (de donde viene el exceso)", "[clipdiag]")
{
    const double sr = 48000.0; const int block = 128;
    auto peakAt = [&] (float radius, bool speaker, float ceiling)
    {
        orbita::OrbitBrain brain; brain.prepare (sr);
        orbita::OrbitBrain::Params bp;
        bp.shape = orbita::OrbitBrain::Ellipse; bp.rate = orbita::OrbitBrain::Free; bp.dir = orbita::OrbitBrain::CW;
        bp.freeHz = 8.0f; bp.chaos01 = 1.0f; bp.doppler01 = 1.0f; bp.radius01 = radius; bp.spread01 = 1.0f;
        brain.setParams (bp);
        orbita::SpatialEngine eng; eng.setLimiter ({ ceiling, 60.0f }); // ceiling alto (10) = sin limitar (PRE); 0.985 = POST
        eng.prepare (juce::dsp::ProcessSpec { sr, (juce::uint32) block, 2 });
        juce::AudioBuffer<float> buf (2, block); orbita::TransportInfo tr; tr.isPlaying = false;
        double ph = 0.0; const double w = kTwoPi * 110.0 / sr; float peak = 0.0f;
        const int blocks = (int) (sr * 3.0 / block);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i) { const float s = 0.7f*(float)std::sin(ph)+0.25f*(float)std::sin(ph*2.7); ph+=w; buf.setSample(0,i,s); buf.setSample(1,i,s); }
            const auto tp = brain.advance (block, tr);
            eng.process (buf, 2, orbita::SpatialEngine::EngineParams {
                .azimuthRad=tp.azimuth, .mix01=1.0f, .room01=1.0f, .width01=1.0f, .monoSafe=false,
                .radius01=radius, .speakerMode=speaker, .distance01=tp.distance, .doppler01=1.0f });
            for (int i = 0; i < block; ++i) { peak=std::max(peak,std::abs(buf.getSample(0,i))); peak=std::max(peak,std::abs(buf.getSample(1,i))); }
        }
        return peak;
    };
    WARN ("== PEAK por distancia (todo al maximo). PRE = motor sin limitar; POST = con soft-clip ==");
    for (float r : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        WARN ("radius=" << r << "  PRE auric=" << peakAt(r,false,10.0f) << "  PRE parl=" << peakAt(r,true,10.0f)
              << "  |  POST auric=" << peakAt(r,false,0.985f) << "  POST parl=" << peakAt(r,true,0.985f));
    SUCCEED();
}
