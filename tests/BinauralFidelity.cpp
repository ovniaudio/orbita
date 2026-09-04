// =====================================================================================
// Test [fidelity] — FIDELIDAD BINAURAL medida END-TO-END sobre el PluginProcessor real.
//
// Por qué end-to-end y no sobre el header del anillo: lo que escucha el usuario es la
// cadena completa (anillo + campo de delay + near-field + width + air + limiter). Medir
// sólo `HrirRing.h` deja afuera todo lo que puede romper (o arreglar) el cue.
//
//   1. ITD vs Woodworth   — el cue temporal existe y vale lo que vale una cabeza.
//   2. ITD signo/monotonía— apunta al lado correcto y crece con el azimut.
//   3. ITD antisimétrico  — izquierda y derecha son espejo exacto.
//   4. ILD por banda      — el cue de nivel apunta al mismo lado en TODAS las bandas.
//   5. Latencia           — la reportada es la real, y NO depende de la perilla DOPPLER.
//   6. MIX intermedio     — mezclar seco+wet no peina (sin comb).
//   7. Cola declarada     — getTailLengthSeconds() cubre reflexiones + latencia.
//
// Convención de signo (la misma del anillo y del script de la auditoría):
//   ITD = retardo_R − retardo_L.  ITD > 0  ⇒  llega ANTES al oído izquierdo
//                                        ⇒  fuente a la IZQUIERDA (azimut > 0).
// =====================================================================================
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <vector>

#include <PluginProcessor.h>

namespace
{
constexpr double SR    = 48000.0;
constexpr int    BLOCK = 512;

// Woodworth: cabeza esférica de radio a, sonido a c m/s.
//   ITD(θ) = a/c · (θ + sin θ)   para 0 ≤ θ ≤ 90°
// Arriba de 90° se REFLEJA (θ → 180° − θ): la fuente vuelve a acercarse al plano medio y el
// ITD baja hasta 0 en 180° (cono de confusión). Sin reflejar, la fórmula seguiría creciendo
// hasta 180°, que no es físico — atrás, en el plano medio, los dos oídos son equidistantes.
constexpr double kHeadRadiusM = 0.0875;
constexpr double kSoundC      = 343.0;

double woodworthUs (double azDeg) noexcept
{
    double a = std::fmod (azDeg, 360.0);
    if (a > 180.0)  a -= 360.0;
    if (a < -180.0) a += 360.0;
    const double sign = (a >= 0.0) ? 1.0 : -1.0;
    double m = std::abs (a);
    if (m > 90.0) m = 180.0 - m;                       // reflexión: cono de confusión
    const double th = m * juce::MathConstants<double>::pi / 180.0;
    return sign * (kHeadRadiusM / kSoundC) * (th + std::sin (th)) * 1.0e6;
}

//======================================================================================
// Render offline del plugin completo (mismo patrón que el renderizador de la auditoría).
//======================================================================================
struct Render { std::vector<float> L, R; };

void setNorm (PluginProcessor& p, const char* id, float v01)
{ if (auto* q = p.apvts.getParameter (id)) q->setValueNotifyingHost (v01); }

void setVal (PluginProcessor& p, const char* id, float v)
{
    if (auto* q = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)))
        q->setValueNotifyingHost (q->convertTo0to1 (v));
}

Render run (const std::vector<float>& inL, const std::vector<float>& inR,
            const std::function<void (PluginProcessor&)>& setup, int* latencyOut = nullptr)
{
    PluginProcessor proc;
    setup (proc);
    proc.setPlayConfigDetails (2, 2, SR, BLOCK);
    proc.prepareToPlay (SR, BLOCK);
    if (latencyOut != nullptr) *latencyOut = proc.getLatencySamples();

    const int n = (int) inL.size();
    Render out; out.L.assign ((size_t) n, 0.0f); out.R.assign ((size_t) n, 0.0f);
    juce::AudioBuffer<float> buf (2, BLOCK);
    juce::MidiBuffer midi;
    for (int i = 0; i < n; i += BLOCK)
    {
        const int len = juce::jmin (BLOCK, n - i);
        buf.clear();
        for (int k = 0; k < len; ++k)
        {
            buf.setSample (0, k, inL[(size_t) (i + k)]);
            buf.setSample (1, k, inR[(size_t) (i + k)]);
        }
        proc.processBlock (buf, midi);
        for (int k = 0; k < len; ++k)
        {
            out.L[(size_t) (i + k)] = buf.getSample (0, k);
            out.R[(size_t) (i + k)] = buf.getSample (1, k);
        }
    }
    return out;
}

// Fuente puntual FIJA (sin órbita), seca del todo, sin sala: aísla el par HRIR.
void fixedSource (PluginProcessor& p, float azDeg, float dopplerPct = 0.0f, float mixPct = 100.0f)
{
    setVal  (p, "mix",       mixPct);
    setVal  (p, "room",      0.0f);
    setVal  (p, "width",     50.0f);
    setNorm (p, "orbRate",   1.0f);        // choice index 4 de 5 = "Fixed"
    setVal  (p, "orbFixedAz", azDeg);
    setVal  (p, "orbRadius", 60.0f);
    setVal  (p, "doppler",   dopplerPct);
    setVal  (p, "orbChaos",  0.0f);
    setVal  (p, "orbHeight", 0.0f);
}

//======================================================================================
// ITD por correlación cruzada interaural, sobremuestreada ×16 y limitada a 1.5 kHz.
//
// Por qué band-limitada: es el estimador estándar de la literatura (IACC-LP, cf. Katz &
// Noisternig 2014). Arriba de ~1.5 kHz la fase interaural es ambigua (la longitud de onda
// es menor que la cabeza) y el pabellón mete retardos de grupo propios que no son ITD. Es
// además la banda donde el ITD manda perceptualmente.
//
// Por qué ×16 sobremuestreada: 1/16 de muestra = 1.3 µs, dos órdenes por debajo del JND
// de ITD (~20 µs) y suficiente para el test de antisimetría (< 5 µs).
//
// Devuelve el ITD en MICROSEGUNDOS, con la convención ITD = retardo_R − retardo_L.
//======================================================================================
constexpr int kCorrOrder = 12;                 // 4096 muestras de ventana
constexpr int kCorrN     = 1 << kCorrOrder;
constexpr int kCorrUp    = 16;
constexpr int kCorrBigN  = kCorrN * kCorrUp;

double itdMicros (const std::vector<float>& l, const std::vector<float>& r, int from)
{
    using C = std::complex<float>;
    static juce::dsp::FFT fft    (kCorrOrder);
    static juce::dsp::FFT fftBig (kCorrOrder + 4);   // 65536

    std::vector<C> a ((size_t) kCorrN, C {}), b ((size_t) kCorrN, C {});
    for (int i = 0; i < kCorrN; ++i)
    {
        const size_t s = (size_t) (from + i);
        if (s < l.size()) { a[(size_t) i] = C { l[s], 0.0f }; b[(size_t) i] = C { r[s], 0.0f }; }
    }
    std::vector<C> A ((size_t) kCorrN), B ((size_t) kCorrN);
    fft.perform (a.data(), A.data(), false);
    fft.perform (b.data(), B.data(), false);

    // Espectro cruzado conj(A)·B, cero arriba de 1.5 kHz, insertado en un buffer ×16
    // (zero-padding en frecuencia = interpolación sinc exacta en el dominio del lag).
    const int kMaxBin = (int) std::floor (1500.0 / (SR / (double) kCorrN));
    std::vector<C> S ((size_t) kCorrBigN, C {});
    for (int k = 1; k <= kMaxBin; ++k)
    {
        const C c = std::conj (A[(size_t) k]) * B[(size_t) k];
        S[(size_t) k]                  = c;
        S[(size_t) (kCorrBigN - k)]    = std::conj (c);
    }
    std::vector<C> c ((size_t) kCorrBigN);
    fftBig.perform (S.data(), c.data(), true);

    // Buscar el pico dentro de ±64 muestras (el ITD físico máximo es ~31).
    const int span = 64 * kCorrUp;
    int   best = 0;
    float bestV = -1.0e30f;
    auto at = [&] (int i) { return c[(size_t) ((i % kCorrBigN + kCorrBigN) % kCorrBigN)].real(); };
    for (int i = -span; i <= span; ++i)
        if (at (i) > bestV) { bestV = at (i); best = i; }

    // Refinamiento parabólico sobre la grilla ×16.
    const float ym = at (best - 1), y0 = at (best), yp = at (best + 1);
    const float den = ym - 2.0f * y0 + yp;
    const double frac = (std::abs (den) > 1.0e-20f) ? (double) (0.5f * (ym - yp) / den) : 0.0;

    return ((double) best + frac) / (double) kCorrUp / SR * 1.0e6;
}

// Primer arribo: primera muestra que supera −40 dB del pico. Para un FIR de fase mínima
// (todo el anillo lo es) el primer arribo ES el retardo puro del camino — que es justo lo
// que un host tiene que compensar. El centroide de energía, en cambio, mide la dispersión
// del propio HRIR, que es coloración del filtro y no latencia.
double firstArrival (const std::vector<float>& v, int from, int len)
{
    double pk = 0.0;
    for (int i = 0; i < len; ++i) pk = juce::jmax (pk, (double) std::abs (v[(size_t) (from + i)]));
    if (pk < 1.0e-12) return -1.0;
    const double thr = pk * 0.01;      // −40 dB
    for (int i = 0; i < len; ++i)
        if (std::abs (v[(size_t) (from + i)]) >= thr) return (double) i;
    return -1.0;
}

// Energía en una banda [lo, hi) de un tramo, en dB.
double bandDb (const std::vector<float>& v, int from, double lo, double hi)
{
    using C = std::complex<float>;
    static juce::dsp::FFT fft (kCorrOrder);
    std::vector<C> in ((size_t) kCorrN, C {}), out ((size_t) kCorrN);
    for (int i = 0; i < kCorrN; ++i)
    {
        const size_t s = (size_t) (from + i);
        if (s < v.size()) in[(size_t) i] = C { v[s], 0.0f };
    }
    fft.perform (in.data(), out.data(), false);
    const double binHz = SR / (double) kCorrN;
    double acc = 0.0; int cnt = 0;
    for (int k = 1; k <= kCorrN / 2; ++k)
    {
        const double f = (double) k * binHz;
        if (f >= lo && f < hi) { acc += (double) std::norm (out[(size_t) k]); ++cnt; }
    }
    return 10.0 * std::log10 (acc / juce::jmax (1, cnt) + 1.0e-30);
}

//======================================================================================
// Barrido de los 72 azimuts (paso 5°). Caro: se calcula UNA vez y lo comparten los tests.
//======================================================================================
struct Sweep
{
    std::vector<double> azDeg, itdUs, woodUs;
    int latency = 0;
};

const Sweep& sweep()
{
    static const Sweep s = []
    {
        Sweep out;
        // Pre-roll de silencio: deja asentar los suavizados (near-field, width, air, Doppler)
        // antes del impulso. "Impulso a régimen", como pide el informe 21.
        constexpr int kPre = 12288;                 // 0.256 s
        constexpr int kLen = kPre + kCorrN + 2048;
        std::vector<float> imp ((size_t) kLen, 0.0f);
        imp[(size_t) kPre] = 1.0f;

        for (int i = 0; i < 72; ++i)
        {
            const double az = -180.0 + 5.0 * (double) i;   // −180 … +175
            int lat = 0;
            auto r = run (imp, imp, [&] (PluginProcessor& p) { fixedSource (p, (float) az); }, &lat);
            out.latency = lat;
            out.azDeg .push_back (az);
            out.itdUs .push_back (itdMicros (r.L, r.R, kPre));
            out.woodUs.push_back (woodworthUs (az));
        }
        return out;
    }();
    return s;
}

double itdAt (double azDeg)
{
    const auto& s = sweep();
    double a = std::fmod (azDeg + 180.0, 360.0); if (a < 0.0) a += 360.0;
    const int i = (int) std::lround (a / 5.0) % 72;
    return s.itdUs[(size_t) i];
}
} // namespace

//======================================================================================
// 1 · ITD vs Woodworth
//======================================================================================
TEST_CASE ("ORBIT fidelidad: ITD end-to-end vs Woodworth (72 azimuts)", "[fidelity]")
{
    const auto& s = sweep();
    std::printf ("\nFIDELITY[itd] az  medido_us  woodworth_us   error_us   error_%%\n");
    double worstRel = 0.0; double worstAz = 0.0;
    int fails = 0;
    for (size_t i = 0; i < s.azDeg.size(); ++i)
    {
        const double err = s.itdUs[i] - s.woodUs[i];
        const double rel = (std::abs (s.woodUs[i]) > 1.0e-9) ? 100.0 * std::abs (err) / std::abs (s.woodUs[i]) : 0.0;
        if (std::abs (s.woodUs[i]) > 1.0e-9 && rel > worstRel) { worstRel = rel; worstAz = s.azDeg[i]; }
        // Tolerancia: 15 % del Woodworth. Piso absoluto de 10 µs (≈ 0.5 muestra) porque
        // cerca del plano medio el 15 % de un valor casi nulo cae bajo la resolución del
        // estimador; 10 µs sigue estando 2× por debajo del JND de ITD (~20 µs).
        const double tol = juce::jmax (0.15 * std::abs (s.woodUs[i]), 10.0);
        const bool ok = std::abs (err) <= tol;
        if (! ok) ++fails;
        std::printf ("FIDELITY[itd] %+5.0f %10.1f %13.1f %10.1f %9.1f %s\n",
                     s.azDeg[i], s.itdUs[i], s.woodUs[i], err, rel, ok ? "" : "  <-- FUERA");
    }
    std::printf ("FIDELITY[itd] peor error relativo = %.1f %% en az %+.0f · fuera de tolerancia: %d/72\n",
                 worstRel, worstAz, fails);
    REQUIRE (fails == 0);
}

//======================================================================================
// 2 · ITD con signo correcto y monótono
//======================================================================================
TEST_CASE ("ORBIT fidelidad: ITD monotono y con signo correcto", "[fidelity]")
{
    const auto& s = sweep();

    // (a) signo: en TODO el lado izquierdo (0° < az < 180°) el ITD tiene que ser > 0.
    int badSign = 0;
    for (size_t i = 0; i < s.azDeg.size(); ++i)
        if (s.azDeg[i] > 0.0 && s.azDeg[i] < 180.0 && s.itdUs[i] <= 0.0)
        {
            ++badSign;
            std::printf ("FIDELITY[signo] az %+5.0f  ITD = %+.1f us  (deberia ser > 0)\n", s.azDeg[i], s.itdUs[i]);
        }

    // (b) monotonía creciente en (0°, 90°).
    int badMono = 0;
    double prev = 0.0;
    for (double az = 5.0; az <= 90.0; az += 5.0)
    {
        const double v = itdAt (az);
        if (v <= prev)
        {
            ++badMono;
            std::printf ("FIDELITY[mono] az %+5.0f  ITD = %+.1f us  no crece (previo %+.1f)\n", az, v, prev);
        }
        prev = v;
    }
    std::printf ("FIDELITY[signo] violaciones de signo = %d · de monotonia 0..90 = %d\n", badSign, badMono);
    REQUIRE (badSign == 0);
    REQUIRE (badMono == 0);
}

//======================================================================================
// 3 · ITD antisimétrico
//======================================================================================
TEST_CASE ("ORBIT fidelidad: ITD antisimetrico", "[fidelity]")
{
    double worst = 0.0, worstAz = 0.0;
    for (double az = 5.0; az < 180.0; az += 5.0)
    {
        const double d = std::abs (itdAt (az) + itdAt (-az));
        if (d > worst) { worst = d; worstAz = az; }
    }
    std::printf ("FIDELITY[antisim] peor |ITD(az)+ITD(-az)| = %.2f us en az %+.0f (limite 5.00)\n", worst, worstAz);
    REQUIRE (worst < 5.0);
}

//======================================================================================
// 4 · ILD por banda con el signo correcto
//======================================================================================
TEST_CASE ("ORBIT fidelidad: ILD por banda con signo correcto", "[fidelity]")
{
    struct Band { double lo, hi; const char* name; };
    const Band bands[] = {
        {   63.0,   125.0, "63-125"    }, {  125.0,   250.0, "125-250"   },
        {  250.0,   500.0, "250-500"   }, {  500.0,  1000.0, "500-1k"    },
        { 1000.0,  2000.0, "1k-2k"     }, { 2000.0,  4000.0, "2k-4k"     },
        { 4000.0,  8000.0, "4k-8k"     }, { 8000.0, 16000.0, "8k-16k"    },
    };

    constexpr int kPre = 12288;
    constexpr int kLen = kPre + kCorrN + 2048;
    std::vector<float> imp ((size_t) kLen, 0.0f);
    imp[(size_t) kPre] = 1.0f;

    int bad = 0;
    for (float az : { 45.0f, 90.0f })
    {
        auto r = run (imp, imp, [&] (PluginProcessor& p) { fixedSource (p, az); });
        std::printf ("FIDELITY[ild] az %+3.0f :", az);
        for (const auto& b : bands)
        {
            const double ild = bandDb (r.L, kPre, b.lo, b.hi) - bandDb (r.R, kPre, b.lo, b.hi);
            std::printf ("  %s:%+.1f%s", b.name, ild, ild > 0.0 ? "" : "(!)");
            if (ild <= 0.0) ++bad;
        }
        std::printf ("\n");
    }
    std::printf ("FIDELITY[ild] bandas con el signo invertido = %d (de 16)\n", bad);
    REQUIRE (bad == 0);
}

//======================================================================================
// 5 · Latencia reportada == medida, e INDEPENDIENTE de la perilla DOPPLER
//======================================================================================
TEST_CASE ("ORBIT fidelidad: latencia reportada == medida", "[fidelity]")
{
    constexpr int kPre = 12288;
    constexpr int kLen = kPre + 4096;
    std::vector<float> imp ((size_t) kLen, 0.0f);
    imp[(size_t) kPre] = 1.0f;

    // (a) el onset del wet coincide con la latencia reportada, para CUALQUIER Doppler.
    int badOnset = 0;
    std::vector<Render> byDop;
    for (float dop : { 0.0f, 25.0f, 50.0f, 100.0f })
    {
        int lat = 0;
        auto r = run (imp, imp, [&] (PluginProcessor& p) { fixedSource (p, 0.0f, dop); }, &lat);
        const double on = firstArrival (r.L, kPre, 4096);
        const double err = on - (double) lat;
        std::printf ("FIDELITY[lat] DOPPLER=%3.0f%%  reportada=%4d  medida=%7.2f  error=%+7.2f muestras\n",
                     dop, lat, on, err);
        if (! (std::abs (err) < 2.0)) ++badOnset;
        byDop.push_back (std::move (r));
    }

    // (b) invariancia exacta: con la fuente quieta el knob DOPPLER no cambia NADA.
    double worstDiff = 0.0;
    for (size_t k = 1; k < byDop.size(); ++k)
        for (size_t i = 0; i < byDop[k].L.size(); ++i)
            worstDiff = juce::jmax (worstDiff, (double) std::abs (byDop[k].L[i] - byDop[0].L[i]));
    std::printf ("FIDELITY[lat] maxima diferencia de forma de onda entre DOPPLER 0/25/50/100 = %.3e\n", worstDiff);

    // (c) con MIX=0 el seco sigue bit-exacto, retrasado por esa misma latencia.
    int lat0 = 0;
    std::uint32_t st = 0x2545F491u;
    auto white = [&st] { st = st * 1664525u + 1013904223u; return ((float) (st >> 9) * (1.0f / 4194304.0f)) - 1.0f; };
    std::vector<float> nz ((size_t) 32768);
    for (auto& v : nz) v = 0.25f * white();
    auto dry = run (nz, nz, [&] (PluginProcessor& p) { fixedSource (p, 33.0f, 50.0f, 0.0f); }, &lat0);
    double worstNull = 0.0;
    for (size_t n = (size_t) lat0 + 4096; n < nz.size(); ++n)
        worstNull = juce::jmax (worstNull, (double) std::abs (dry.L[n] - nz[n - (size_t) lat0]));
    std::printf ("FIDELITY[lat] MIX=0 null contra el seco retrasado %d muestras: peor error = %.3e (bit-exacto = 0)\n",
                 lat0, worstNull);

    REQUIRE (badOnset  == 0);
    REQUIRE (worstDiff < 1.0e-6);
    REQUIRE (worstNull == 0.0);
}

//======================================================================================
// 6 · MIX intermedio no peina
//======================================================================================
TEST_CASE ("ORBIT fidelidad: MIX intermedio no peina", "[fidelity]")
{
    // Ruido rosa determinista (Voss/McCartney de 3 polos, como el de la auditoría).
    const int n = (int) (SR * 4.0);
    std::vector<float> nz ((size_t) n);
    {
        juce::Random rnd (1234); float b0 = 0, b1 = 0, b2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const float w = rnd.nextFloat() * 2.0f - 1.0f;
            b0 = 0.99765f * b0 + w * 0.0990460f;
            b1 = 0.96300f * b1 + w * 0.2965164f;
            b2 = 0.57000f * b2 + w * 1.0526913f;
            nz[(size_t) i] = 0.15f * (b0 + b1 + b2 + w * 0.1848f);
        }
    }

    auto welch = [&] (const std::vector<float>& v)
    {
        using C = std::complex<float>;
        constexpr int order = 13, N = 1 << order;      // 8192 -> 5.86 Hz por bin
        juce::dsp::FFT fft (order);
        std::vector<double> p ((size_t) (N / 2 + 1), 0.0);
        std::vector<C> in ((size_t) N), out ((size_t) N);
        int frames = 0;
        for (int start = (int) SR; start + N <= (int) v.size(); start += N / 2)
        {
            for (int i = 0; i < N; ++i)
            {
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (N - 1));
                in[(size_t) i] = C { v[(size_t) (start + i)] * w, 0.0f };
            }
            fft.perform (in.data(), out.data(), false);
            for (int k = 0; k <= N / 2; ++k) p[(size_t) k] += (double) std::norm (out[(size_t) k]);
            ++frames;
        }
        for (auto& x : p) x /= juce::jmax (1, frames);
        return p;
    };

    // Azimut 0 (fuente al frente) es el PEOR caso para el comb: es donde el wet queda más
    // correlacionado con el seco (sin sombra de cabeza, L≈R), así que la interferencia entre
    // los dos caminos es la más profunda posible. A los costados el HRIR decorrela y el comb
    // se disimula — medirlo ahí sería elegir la condición favorable.
    auto r100 = run (nz, nz, [&] (PluginProcessor& p) { fixedSource (p, 0.0f, 50.0f, 100.0f); });
    auto r50  = run (nz, nz, [&] (PluginProcessor& p) { fixedSource (p, 0.0f, 50.0f,  50.0f); });
    const auto p100 = welch (r100.L);
    const auto p50  = welch (r50.L);

    constexpr int order = 13, N = 1 << order;
    const double binHz = SR / (double) N;
    std::vector<double> ratio ((size_t) (N / 2 + 1), 0.0);
    for (size_t k = 0; k < ratio.size(); ++k)
        ratio[k] = 10.0 * std::log10 ((p50[k] + 1.0e-30) / (p100[k] + 1.0e-30));

    // Un comb son notches ESTRECHOS: se detectan contra la tendencia LOCAL (±1/6 de octava),
    // no contra el promedio global — así un desnivel ancho y legítimo no cuenta como comb.
    double worst = 0.0, worstHz = 0.0;
    const int kLo = (int) std::ceil (30.0 / binHz), kHi = (int) std::floor (1000.0 / binHz);
    for (int k = kLo; k <= kHi; ++k)
    {
        const double f = (double) k * binHz;
        const int a = juce::jmax (kLo, (int) std::floor (f / std::pow (2.0, 1.0 / 6.0) / binHz));
        const int b = juce::jmin ((int) ratio.size() - 1, (int) std::ceil (f * std::pow (2.0, 1.0 / 6.0) / binHz));
        double sum = 0.0; int cnt = 0;
        for (int j = a; j <= b; ++j) { sum += ratio[(size_t) j]; ++cnt; }
        const double dip = (sum / juce::jmax (1, cnt)) - ratio[(size_t) k];
        if (dip > worst) { worst = dip; worstHz = f; }
    }
    // Diagnóstico extra (sin aserción): rizado crudo pico-a-valle de la razón bajo 1 kHz.
    // Es el número que se ve en un analizador y hace visible la mejora aunque el test ya
    // pasara antes del arreglo.
    double lo = 1.0e30, hi = -1.0e30;
    for (int k = kLo; k <= kHi; ++k) { lo = juce::jmin (lo, ratio[(size_t) k]); hi = juce::jmax (hi, ratio[(size_t) k]); }
    std::printf ("FIDELITY[comb] MIX=50 vs MIX=100 (DOPPLER=50, az 0): notch mas profundo = %.2f dB en %.0f Hz (limite 6.00)"
                 " · rizado crudo p-v bajo 1 kHz = %.2f dB\n", worst, worstHz, hi - lo);
    REQUIRE (worst < 6.0);
}

//======================================================================================
// 7 · Cola declarada
//======================================================================================
TEST_CASE ("ORBIT fidelidad: cola declarada", "[fidelity]")
{
    PluginProcessor proc;
    proc.prepareToPlay (SR, BLOCK);
    const double tail  = proc.getTailLengthSeconds();
    const double floor = 0.095 + (double) proc.getLatencySamples() / SR;   // reflexiones + latencia
    std::printf ("FIDELITY[tail] getTailLengthSeconds() = %.6f s · minimo = %.6f s (0.095 + %d/%.0f)\n",
                 tail, floor, proc.getLatencySamples(), SR);
    REQUIRE (tail >= floor);
}
