#pragma once
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <complex>
#include <vector>

namespace orbita_test
{
// =====================================================================================
// Estimador de ITD por correlación cruzada interaural, limitada en banda y sobremuestreada.
//
// Es el método estándar de la literatura (IACC-LP; cf. Katz & Noisternig 2014, comparativa
// de estimadores de ITD sobre HRIRs):
//   · LIMITADO A 1.5 kHz — arriba de ahí la longitud de onda es menor que la cabeza, la fase
//     interaural se vuelve ambigua y el pabellón mete retardo de grupo propio que no es ITD.
//     Es además la banda donde el ITD manda perceptualmente.
//   · SOBREMUESTREADO ×16 por zero-padding del espectro cruzado (= interpolación sinc exacta
//     en el dominio del lag) + refinamiento parabólico: resolución ~1 µs, dos órdenes por
//     debajo del JND de ITD (~20 µs).
//
// Vive acá y no copiado en cada test porque LO MISMO se usa en tres lugares y tienen que
// coincidir: el horneado del anillo (tests/GenHrir.cpp) lo usa para descontar el ITD que ya
// trae la magnitud min-fase, y los tests de aceptación lo usan para verificar el resultado.
// Si el criterio con el que se hornea y el criterio con el que se mide se separan, el bake
// queda ajustado contra un número que nadie verifica.
//
// Convención: devuelve el retardo de `r` respecto de `l`, en MUESTRAS.
//   > 0  ⇒  `r` llega DESPUÉS  ⇒  con (l = oído izq, r = oído der), fuente a la IZQUIERDA.
// =====================================================================================
inline double itdSamples (const float* l, const float* r, int len, double sampleRate,
                          double fMaxHz = 1500.0, int maxLagSamples = 64)
{
    using C = std::complex<float>;
    constexpr int kOrder = 12;                 // 4096 muestras de ventana
    constexpr int kN     = 1 << kOrder;
    constexpr int kUp    = 16;
    constexpr int kBigN  = kN * kUp;

    static juce::dsp::FFT fft    (kOrder);
    static juce::dsp::FFT fftBig (kOrder + 4);

    std::vector<C> a ((size_t) kN, C {}), b ((size_t) kN, C {});
    const int n = juce::jmin (len, kN);
    for (int i = 0; i < n; ++i) { a[(size_t) i] = C { l[i], 0.0f }; b[(size_t) i] = C { r[i], 0.0f }; }

    std::vector<C> A ((size_t) kN), B ((size_t) kN);
    fft.perform (a.data(), A.data(), false);
    fft.perform (b.data(), B.data(), false);

    // Espectro cruzado conj(A)·B, en cero arriba de fMax, colocado en un buffer ×16.
    const int maxBin = juce::jlimit (1, kN / 2 - 1, (int) std::floor (fMaxHz / (sampleRate / (double) kN)));
    std::vector<C> S ((size_t) kBigN, C {});
    for (int k = 1; k <= maxBin; ++k)
    {
        const C c = std::conj (A[(size_t) k]) * B[(size_t) k];
        S[(size_t) k]           = c;
        S[(size_t) (kBigN - k)] = std::conj (c);
    }
    std::vector<C> cc ((size_t) kBigN);
    fftBig.perform (S.data(), cc.data(), true);

    auto at = [&] (int i) { return cc[(size_t) ((i % kBigN + kBigN) % kBigN)].real(); };
    const int span = maxLagSamples * kUp;
    int   best  = 0;
    float bestV = -1.0e30f;
    for (int i = -span; i <= span; ++i)
        if (at (i) > bestV) { bestV = at (i); best = i; }

    const float ym = at (best - 1), y0 = at (best), yp = at (best + 1);
    const float den = ym - 2.0f * y0 + yp;
    const double frac = (std::abs (den) > 1.0e-20f) ? (double) (0.5f * (ym - yp) / den) : 0.0;
    return ((double) best + frac) / (double) kUp;
}

inline double itdMicros (const float* l, const float* r, int len, double sampleRate,
                         double fMaxHz = 1500.0)
{
    return itdSamples (l, r, len, sampleRate, fMaxHz) / sampleRate * 1.0e6;
}
} // namespace orbita_test
