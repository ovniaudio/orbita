// Medidor OBJETIVO de imagen estéreo / fase de ÓRBITA (sin depender del Insight ni de capturas).
// Portado de PULSAR (~/ovni/plugins/pulsar/tests/StereoMeasure.cpp): MISMO ruido rosa, MISMA matemática
// de acumuladores -> los números son DIRECTAMENTE comparables con los de PULSAR (mismo motor binaural HRIR).
// Corre RUIDO ROSA mono por el PluginProcessor REAL de ÓRBITA e imprime los mismos números que un
// vectorscope/correlímetro pro:
//   CORR       = correlación de fase L/R: +1 = mono/correlacionado, ~0 = ancho/decorrelacionado, <0 = fuera de fase
//   WIDTH      = RMS(side)/RMS(mid): 0 = mono, ↑ = más ancho (>1 = el side domina)
//   BAL_dB     = balance de energía L vs R (≈0 = centrado; +=L más fuerte, -=R más fuerte)
//   MONOSUM_dB = nivel de la suma L+R vs el directo: 0 ≈ sin pérdida al monoficar, muy negativo = cancela
// Es DIAGNÓSTICO (no pass/fail): imprime líneas MEASURE[...] que el harness/Claude lee. REQUIRE sólo finitud.
//
// NO modifica el DSP de ÓRBITA: sólo instancia su processor, le setea params y mide.
// Rate=Fixed -> la fuente queda QUIETA en un azimut fijo: medimos el ANCHO/decorrelación del motor, no el barrido.
//   - Grupo A (azimut 0° = frente): aísla el knob WIDTH (a 0° L≈R por simetría -> sólo el M/S del width abre).
//   - Grupo B (azimut ±90° = costados): la decorrelación binaural REAL del HRIR (ITD + sombra de cabeza),
//     valida externalización y convención L/R (BAL_dB debe invertir signo entre izq/der).
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <PluginProcessor.h>

namespace
{
// Ruido rosa determinístico (Paul Kellet "economy") sobre un LCG con semilla fija (reproducible).
// IDÉNTICO al de PULSAR -> mismas muestras de entrada -> comparación justa entre plugins.
struct Pink
{
    std::uint32_t s = 0x13572468u;
    float b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0;
    float white() { s = s * 1664525u + 1013904223u; return ((float) (s >> 9) * (1.0f / 4194304.0f)) - 1.0f; }
    float next()
    {
        const float w = white();
        b0 = 0.99886f*b0 + w*0.0555179f; b1 = 0.99332f*b1 + w*0.0750759f;
        b2 = 0.96900f*b2 + w*0.1538520f; b3 = 0.86650f*b3 + w*0.3104856f;
        b4 = 0.55000f*b4 + w*0.5329522f; b5 = -0.7616f*b5 - w*0.0168980f;
        const float p = b0+b1+b2+b3+b4+b5+b6 + w*0.5362f; b6 = w*0.115926f;
        return p * 0.11f;   // ~[-1,1]
    }
};

// fixedAzDeg: ángulo fijo de la fuente (grados; 0 = frente, + = CCW/izquierda, como el motor). width01/inPhase: el knob.
void measure (PluginProcessor& proc, float fixedAzDeg, float width01, bool inPhase, const char* tag)
{
    const double SR = 48000.0; const int N = 512;
    auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
    // RATE=Fixed (índice 4/4 -> normalizado 1.0): fuente quieta en orbFixedAz -> medimos el ANCHO/decorrelación
    // del motor, no el barrido. Chaos/Doppler a 0 (sin jitter ni pitch), mix full wet, bypass off.
    // orbFixedAz: rango -180..180 -> normalizado = (deg+180)/360.
    // Lo demás queda en su DEFAULT VALIDADO: room=30%, orbRadius=60%, outMode=Phones (camino binaural HRIR),
    // output/inGain=0 dB (no afectan ratios CORR/WIDTH/BAL/MONOSUM).
    set ("orbRate", 1.0f); set ("orbChaos", 0.0f); set ("doppler", 0.0f);
    set ("mix", 1.0f); set ("bypass", 0.0f);
    set ("orbFixedAz", (fixedAzDeg + 180.0f) / 360.0f);
    set ("width", width01); set ("monoSafe", inPhase ? 1.0f : 0.0f);
    proc.prepareToPlay (SR, N);

    Pink pink;
    double sLL=0,sRR=0,sLR=0,sMid=0,sSide=0,sMono=0,pk=0; long cnt=0;
    for (int blk = 0; blk < 500; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int n = 0; n < N; ++n) { const float x = pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk < 100) continue;   // warmup (delays HRIR/reflexiones se asientan)
        const float* L = buf.getReadPointer (0); const float* R = buf.getReadPointer (1);
        for (int n = 0; n < N; ++n)
        {
            const double l = L[n], r = R[n];
            sLL += l*l; sRR += r*r; sLR += l*r;
            const double m = 0.5*(l+r), sd = 0.5*(l-r);
            sMid += m*m; sSide += sd*sd; sMono += (l+r)*(l+r); ++cnt;
            pk = std::fmax (pk, std::fmax (std::abs (l), std::abs (r)));   // pico de salida -> ¿clip? (limiter del motor: techo 0.85)
        }
    }
    const double corr   = sLR / (std::sqrt (sLL * sRR) + 1e-12);
    const double width  = std::sqrt (sSide / (sMid + 1e-12));
    const double balDB  = 10.0 * std::log10 ((sLL + 1e-12) / (sRR + 1e-12));
    const double rmsL   = std::sqrt (sLL / (double) cnt);
    const double rmsMono= std::sqrt (sMono / (double) cnt);
    const double monoDB = 20.0 * std::log10 ((rmsMono + 1e-12) / (2.0 * rmsL + 1e-12));
    std::printf ("MEASURE[%-20s] CORR=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f  PEAK=%.3f\n",
                 tag, corr, width, balDB, monoDB, pk);
    REQUIRE (std::isfinite (corr));
}
} // namespace

TEST_CASE ("ORBITA imagen estéreo / fase con ruido rosa (diagnóstico)", "[measure][orbita]")
{
    PluginProcessor proc;
    // --- Grupo A: knob WIDTH con la fuente al FRENTE (0°). Aísla el ancho del width + el escape IN PHASE. ---
    measure (proc, 0.0f, 0.0f, false, "W=0  @0deg");
    measure (proc, 0.0f, 0.5f, false, "W=50 @0deg");
    measure (proc, 0.0f, 1.0f, false, "W=100 @0deg");
    measure (proc, 0.0f, 0.5f, true,  "W=50 @0deg INPHASE");
    measure (proc, 0.0f, 1.0f, true,  "W=100 @0deg INPHASE");
    // --- Grupo B: decorrelación binaural del HRIR fuera de eje (width=50 natural). Externalización + L/R. ---
    measure (proc, +90.0f, 0.5f, false, "W=50 @+90deg L");
    measure (proc, -90.0f, 0.5f, false, "W=50 @-90deg R");
}
