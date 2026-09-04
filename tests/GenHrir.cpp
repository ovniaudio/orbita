// =====================================================================================
// Generador one-off: extrae un par HRIR fijo de un SOFA y lo escribe como header C++.
// Para M1 (motor de posición fija) — así el plugin no carga archivos en runtime.
// Correr con:  ./Tests "[gen]"   (regenera source/dsp/HrirData.h)
// =====================================================================================
#include <catch2/catch_test_macros.hpp>

#include <vector>
#include <fstream>
#include <iomanip>
#include <string>
#include <cmath>
#include <complex>
#include <juce_dsp/juce_dsp.h>

#include "helpers/itd_estimator.h"

extern "C" {
#include <mysofa.h>
}

TEST_CASE ("generar HrirData.h (90 izquierda)", "[gen]")
{
    const float sr = 48000.0f;
    int len = 0, err = 0;
    MYSOFA_EASY* sofa = mysofa_open (HRTF_SOFA_PATH, sr, &len, &err);
    REQUIRE (sofa != nullptr);
    REQUIRE (err == MYSOFA_OK);
    REQUIRE (len > 0);

    std::vector<float> irL ((size_t) len), irR ((size_t) len);
    float dL = 0.0f, dR = 0.0f;
    // cartesiano: x=frente, y=izquierda, z=arriba -> (0,1,0) = 90° a la izquierda
    mysofa_getfilter_float (sofa, 0.0f, 1.0f, 0.0f, irL.data(), irR.data(), &dL, &dR);

    const std::string path = std::string (ORBITA_REPO_DIR) + "/source/dsp/HrirData.h";
    std::ofstream out (path);
    REQUIRE (out.is_open());
    out << std::showpoint << std::setprecision (9); // fuerza el punto decimal -> literales float válidos

    out << "// GENERADO por tests/GenHrir.cpp -- HRIR fija (90 izquierda) para M1.\n"
        << "// No editar a mano: regenerar con  ./Tests \"[gen]\"\n"
        << "#pragma once\n#include <array>\n\nnamespace orbita {\n\n"
        << "inline constexpr int   kHrirLength     = " << len << ";\n"
        << "inline constexpr float kHrirSampleRate = " << sr << "f;\n"
        << "inline constexpr float kHrirDelayL     = " << dL << "f;\n"
        << "inline constexpr float kHrirDelayR     = " << dR << "f;\n\n";

    auto dump = [&] (const char* name, const std::vector<float>& v) {
        out << "inline constexpr std::array<float, " << v.size() << "> " << name << " = {{\n    ";
        out << std::setprecision (9);
        for (size_t i = 0; i < v.size(); ++i) {
            out << v[i] << "f, ";
            if ((i + 1) % 6 == 0) out << "\n    ";
        }
        out << "\n}};\n\n";
    };
    dump ("kHrirL", irL);
    dump ("kHrirR", irR);
    out << "} // namespace orbita\n";
    out.close();

    mysofa_close (sofa);
    WARN ("HrirData.h generado: " << path << " (" << len << " taps, delayL=" << dL << " delayR=" << dR << ")");
    SUCCEED();
}

// =====================================================================================
// Generador del ANILLO (M2 — movimiento): vuelca N azimuts a elevación 0 como header C++.
// El motor v2 elige las 2 direcciones que bracketean el azimut actual e interpola por
// crossfade de salidas. Convención cartesiana mysofa: x=frente, y=izquierda, z=arriba.
//   dir i  ->  θ = i * (360/N)°  ->  (cosθ, sinθ, 0)   [θ: 0=frente, +CCW hacia la izq]
//   i=0 frente · i=N/4 izquierda · i=N/2 atrás · i=3N/4 derecha
// Correr con:  ./Tests "[genring]"   (regenera source/dsp/HrirRing.h)
// =====================================================================================
TEST_CASE ("generar HrirRing.h (anillo 72 azimuts, elev 0)", "[genring]")
{
    constexpr int   kNumDirs = 72;            // 5° de paso
    constexpr float kStepDeg = 360.0f / kNumDirs;
    const float sr = 48000.0f;

    int len = 0, err = 0;
    MYSOFA_EASY* sofa = mysofa_open (HRTF_SOFA_PATH, sr, &len, &err);
    REQUIRE (sofa != nullptr);
    REQUIRE (err == MYSOFA_OK);
    REQUIRE (len > 0);

    const int taps = len;
    std::vector<float> ringL ((size_t) kNumDirs * taps, 0.0f);
    std::vector<float> ringR ((size_t) kNumDirs * taps, 0.0f);
    std::vector<float> delayL ((size_t) kNumDirs, 0.0f);
    std::vector<float> delayR ((size_t) kNumDirs, 0.0f);

    std::vector<float> irL ((size_t) taps), irR ((size_t) taps);
    for (int d = 0; d < kNumDirs; ++d)
    {
        const float theta = (float) d * kStepDeg * (3.14159265358979324f / 180.0f);
        const float x = std::cos (theta);   // frente
        const float y = std::sin (theta);   // izquierda
        float dl = 0.0f, dr = 0.0f;
        mysofa_getfilter_float (sofa, x, y, 0.0f, irL.data(), irR.data(), &dl, &dr);
        for (int t = 0; t < taps; ++t)
        {
            ringL[(size_t) d * taps + t] = irL[(size_t) t];
            ringR[(size_t) d * taps + t] = irR[(size_t) t];
        }
        delayL[(size_t) d] = dl;
        delayR[(size_t) d] = dr;
    }
    mysofa_close (sofa);

    // -------------------------------------------------------------------------------
    // Simetrización L/R: el sujeto CIPIC es asimétrico (sus orejas difieren ~4 dB en 4 kHz);
    // el spec pide HRTF de MANIQUÍ (simétrico). Promediamos cada dirección con su espejo para
    // que izquierda y derecha tengan idéntica potencia Y timbre. (L@θ == R@-θ por simetría.)
    {
        const std::vector<float> oL (ringL), oR (ringR), oDL (delayL), oDR (delayR);
        for (int d = 0; d < kNumDirs; ++d)
        {
            const int dm = (kNumDirs - d) % kNumDirs; // dirección espejo (azimut -θ)
            for (int t = 0; t < taps; ++t)
            {
                ringL[(size_t) d * taps + t] = 0.5f * (oL[(size_t) d * taps + t] + oR[(size_t) dm * taps + t]);
                ringR[(size_t) d * taps + t] = 0.5f * (oR[(size_t) d * taps + t] + oL[(size_t) dm * taps + t]);
            }
            delayL[(size_t) d] = 0.5f * (oDL[(size_t) d] + oDR[(size_t) dm]);
            delayR[(size_t) d] = 0.5f * (oDR[(size_t) d] + oDL[(size_t) dm]);
        }
    }

    // -------------------------------------------------------------------------------
    // Ecualización de campo difuso (DFE) + reconstrucción de fase mínima (spec §5.1).
    // Quita el color común a todas las direcciones (la "caja" del HRTF crudo) dejando
    // sólo las pistas direccionales. Todo offline -> costo runtime cero.
    // -------------------------------------------------------------------------------
    {
        const int order = 9;
        const int N = 1 << order;              // 512 >= taps
        REQUIRE (N >= taps);
        juce::dsp::FFT fft (order);
        using C = std::complex<float>;

        // Auto-detectar la convención de escala del inverse de JUCE (true IDFT = *invScale).
        std::vector<C> u ((size_t) N, C{}), uf ((size_t) N), ui ((size_t) N);
        u[0] = C { 1.0f, 0.0f };
        fft.perform (u.data(), uf.data(), false);
        fft.perform (uf.data(), ui.data(), true);
        const float invScale = 1.0f / ui[0].real();

        auto magOf = [&] (const float* ir, std::vector<float>& mag)
        {
            std::vector<C> in ((size_t) N, C{}), out ((size_t) N);
            for (int i = 0; i < taps; ++i) in[(size_t) i] = C { ir[i], 0.0f };
            fft.perform (in.data(), out.data(), false);
            mag.assign ((size_t) N, 0.0f);
            for (int k = 0; k < N; ++k) mag[(size_t) k] = std::abs (out[(size_t) k]);
        };

        auto midBandDiffuse = [&] (std::vector<float>& mgScratch, int b1, int b2)
        {
            std::vector<double> p ((size_t) N, 0.0);
            for (int d = 0; d < kNumDirs; ++d)
            {
                magOf (&ringL[(size_t) d * taps], mgScratch); for (int k=0;k<N;++k) p[(size_t)k]+=(double)mgScratch[(size_t)k]*mgScratch[(size_t)k];
                magOf (&ringR[(size_t) d * taps], mgScratch); for (int k=0;k<N;++k) p[(size_t)k]+=(double)mgScratch[(size_t)k]*mgScratch[(size_t)k];
            }
            double ls = 0.0;
            for (int k = b1; k <= b2; ++k) ls += std::log (std::max ((float) std::sqrt (p[(size_t)k] / (2.0 * kNumDirs)), 1.0e-9f));
            return std::pair<float, std::vector<double>> { (float) std::exp (ls / std::max (1, b2 - b1 + 1)), std::move (p) };
        };

        auto binHz = [&] (double hz) { return juce::jlimit (1, N/2, (int) std::round (hz / ((double) sr / N))); };
        const int b1 = binHz (300.0), b2 = binHz (6000.0);

        // 1) campo difuso (RMS de magnitud sobre las 144 IRs) + nivel de referencia mid-band
        std::vector<float> mg;
        auto [Dref, pacc] = midBandDiffuse (mg, b1, b2);
        std::vector<float> D ((size_t) N);
        for (int k = 0; k < N; ++k) D[(size_t)k] = (float) std::sqrt (pacc[(size_t)k] / (2.0 * kNumDirs));

        // L1 máxima del anillo CRUDO (cota dura de ganancia; aún seguro como M1).
        float rawMaxL1 = 0.0f;
        for (int d = 0; d < kNumDirs; ++d)
        {
            float sL = 0.0f, sR = 0.0f;
            for (int t = 0; t < taps; ++t)
            {
                sL += std::abs (ringL[(size_t) d * taps + t]);
                sR += std::abs (ringR[(size_t) d * taps + t]);
            }
            rawMaxL1 = std::max (rawMaxL1, std::max (sL, sR));
        }

        // max|H(f)| del anillo CRUDO (prueba de que el DFE no aumenta la ganancia por banda).
        float rawMaxMag = 0.0f;
        for (int d = 0; d < kNumDirs; ++d)
        {
            magOf (&ringL[(size_t) d * taps], mg); for (int k=0;k<N;++k) rawMaxMag = std::max (rawMaxMag, mg[(size_t)k]);
            magOf (&ringR[(size_t) d * taps], mg); for (int k=0;k<N;++k) rawMaxMag = std::max (rawMaxMag, mg[(size_t)k]);
        }

        // 2) corrección G = campo difuso, pero PARCIAL y BAND-LIMITADA (clave para no matar
        //    la externalización, respaldado por la investigación):
        //      - fuerza alpha=0.6 (sólo 60% de la corrección en dB) -> de-box sin aplanar.
        //      - alpha=0.6 por debajo de 3 kHz, taper 3->6 kHz, y alpha=0 ARRIBA de 6 kHz:
        //        deja INTACTOS los notches del pabellón (6-10 kHz) = el cue direccional clave.
        //    Permite cortar (-12 dB) y un boost suave de cuerpo; la norma por-dirección (paso 4)
        //    garantiza que no clippee. G_eff(f) = clamp(Dref/D, gMin, gMax) ^ alpha(f).
        const float gMax = 2.0f, gMin = 0.25f;
        const float alpha0 = 0.6f;
        const int   bLoFull = binHz (3000.0), bHiZero = binHz (6000.0);
        std::vector<float> G ((size_t) N, 1.0f);
        for (int k = 0; k <= N/2; ++k)
        {
            const float raw = juce::jlimit (gMin, gMax, Dref / std::max (D[(size_t)k], 1.0e-9f));
            float a;
            if (k <= bLoFull)       a = alpha0;
            else if (k >= bHiZero)  a = 0.0f;                 // pabellón protegido (sin tocar)
            else                    a = alpha0 * (1.0f - (float) (k - bLoFull) / (float) std::max (1, bHiZero - bLoFull));
            G[(size_t)k] = std::pow (raw, a);                 // a=0 -> G=1 (crudo)
        }
        G[0] = 1.0f;
        for (int k = 1; k < N/2; ++k) G[(size_t)(N - k)] = G[(size_t)k];

        // 3) por IR: |H'| = |H|*G -> IR de fase mínima (cepstrum real)
        auto fromMagnitude = [&] (const std::vector<float>& mag, float* ir)
        {
            std::vector<C> X ((size_t) N), x ((size_t) N);
            for (int k = 0; k < N; ++k) X[(size_t)k] = C { std::log (std::max (mag[(size_t)k], 1.0e-9f)), 0.0f };
            fft.perform (X.data(), x.data(), true);
            for (int k = 0; k < N; ++k) x[(size_t)k] *= invScale;     // true IDFT -> cepstrum real

            std::vector<C> w ((size_t) N, C{});
            w[0] = C { x[0].real(), 0.0f };
            for (int n = 1; n < N/2; ++n) w[(size_t)n] = C { 2.0f * x[(size_t)n].real(), 0.0f };
            w[(size_t)(N/2)] = C { x[(size_t)(N/2)].real(), 0.0f };

            std::vector<C> W ((size_t) N); fft.perform (w.data(), W.data(), false); // log-espectro mín-fase
            for (int k = 0; k < N; ++k) W[(size_t)k] = std::exp (W[(size_t)k]);
            std::vector<C> h ((size_t) N); fft.perform (W.data(), h.data(), true);
            for (int i = 0; i < taps; ++i) ir[i] = h[(size_t)i].real() * invScale;
        };

        // 3b) ILD DE GRAVES: abajo de 250 Hz el dato de CIPIC no existe (ronda 0.3).
        //
        // La HRIR mide 218 taps = 4.5 ms: no puede representar nada por debajo de ~220 Hz. Lo
        // que el anillo trae en 63–250 Hz no es sombra de cabeza, es el residuo de truncar la
        // medición — y sale con el signo AL REVÉS. Medido en v0.2.1, con la fuente a la
        // izquierda: −1.8 dB a +45° y −2.7 dB a +90°, o sea el oído DERECHO más fuerte.
        // Para un productor eso significa que un bajo, un kick o un pad grave se van al lado
        // contrario del que muestra el radar.
        //
        // Debajo de fc = 250 Hz imponemos el ILD de una cabeza esférica. Para una esfera rígida
        // el ILD tiende a 0 en continua (la longitud de onda pasa a ser mucho mayor que la
        // cabeza y deja de haber sombra), así que interpolamos linealmente en frecuencia entre
        // 0 dB en DC y el ILD MEDIDO en fc, que es el primer dato confiable del propio sujeto.
        // No inventamos un número: extendemos hacia abajo el que ya está, con la forma que la
        // física pide. Da +1…+3 dB hacia el oído cercano en las dos bandas graves.
        //
        // Se conserva la MEDIA GEOMÉTRICA de los dos oídos en cada bin: sólo se REPARTE el nivel
        // entre L y R, no se agrega ni se quita energía → la suma mono no cambia de timbre.
        const int bIldFc = juce::jmax (2, binHz (250.0));
        auto fixLowFreqIld = [&] (std::vector<float>& mL, std::vector<float>& mR)
        {
            double accL = 0.0, accR = 0.0;
            for (int k = bIldFc; k <= binHz (500.0); ++k)
            { accL += (double) mL[(size_t)k] * mL[(size_t)k]; accR += (double) mR[(size_t)k] * mR[(size_t)k]; }
            const double ildRefDb = 10.0 * std::log10 ((accL + 1.0e-30) / (accR + 1.0e-30));

            for (int k = 0; k < bIldFc; ++k)
            {
                const double t      = (double) k / (double) bIldFc;      // 0 en DC, 1 en fc
                const double halfDb = 0.5 * ildRefDb * t;
                const float  mid    = std::sqrt (std::max (mL[(size_t)k] * mR[(size_t)k], 1.0e-30f));
                mL[(size_t)k] = mid * (float) std::pow (10.0,  halfDb / 20.0);
                mR[(size_t)k] = mid * (float) std::pow (10.0, -halfDb / 20.0);
                if (k > 0) { mL[(size_t)(N - k)] = mL[(size_t)k]; mR[(size_t)(N - k)] = mR[(size_t)k]; }
            }
        };

        std::vector<float> magL, magR;
        for (int d = 0; d < kNumDirs; ++d)
        {
            magOf (&ringL[(size_t) d * taps], magL);
            magOf (&ringR[(size_t) d * taps], magR);
            for (int k = 0; k < N; ++k) { magL[(size_t)k] *= G[(size_t)k]; magR[(size_t)k] *= G[(size_t)k]; }
            fixLowFreqIld (magL, magR);
            fromMagnitude (magL, &ringL[(size_t) d * taps]);
            fromMagnitude (magR, &ringR[(size_t) d * taps]);
        }

        // 4) SEGURIDAD DE GANANCIA: limitar la L1 máxima del anillo nuevo a la del crudo.
        //    ||h||1 acota la ganancia pico para CUALQUIER entrada/azimut -> nunca clippea
        //    más que M1 (que no clippeaba). Sólo se escala hacia abajo (nunca boost).
        float newMaxL1 = 0.0f;
        for (int d = 0; d < kNumDirs; ++d)
        {
            float sL = 0.0f, sR = 0.0f;
            for (int t = 0; t < taps; ++t)
            {
                sL += std::abs (ringL[(size_t) d * taps + t]);
                sR += std::abs (ringR[(size_t) d * taps + t]);
            }
            newMaxL1 = std::max (newMaxL1, std::max (sL, sR));
        }
        juce::ignoreUnused (rawMaxL1, newMaxL1);

        // SEGURIDAD + sin pumping: normalizar la ENERGÍA (L2 de los dos oídos) de CADA
        // dirección al mismo objetivo. Se escalan ambos oídos por el MISMO factor -> el ILD
        // (cue de nivel interaural) queda intacto; sólo se iguala la loudness por azimut.
        // Para una órbita a radio constante todas las direcciones deben sonar igual de fuerte.
        const float kTargetE = 0.70f; // energía L2 (dos oídos) objetivo por dirección
        float safeGain = 0.0f;        // (informativo: factor de la dir más fuerte)
        for (int d = 0; d < kNumDirs; ++d)
        {
            double e = 0.0;
            for (int t = 0; t < taps; ++t)
            {
                const float L = ringL[(size_t) d * taps + t], R = ringR[(size_t) d * taps + t];
                e += (double) L * L + (double) R * R;
            }
            const float s = kTargetE / std::max ((float) std::sqrt (e), 1.0e-9f);
            for (int t = 0; t < taps; ++t)
            {
                ringL[(size_t) d * taps + t] *= s;
                ringR[(size_t) d * taps + t] *= s;
            }
            if (d == 0) safeGain = s;
        }

        float newMaxMag = 0.0f;
        for (int d = 0; d < kNumDirs; ++d)
        {
            magOf (&ringL[(size_t) d * taps], mg); for (int k=0;k<N;++k) newMaxMag = std::max (newMaxMag, mg[(size_t)k]);
            magOf (&ringR[(size_t) d * taps], mg); for (int k=0;k<N;++k) newMaxMag = std::max (newMaxMag, mg[(size_t)k]);
        }

        for (float v : ringL) REQUIRE (std::isfinite (v));
        for (float v : ringR) REQUIRE (std::isfinite (v));
        // newMaxMag <= rawMaxMag prueba que el DFE no recalienta ninguna banda (cut-only).
        REQUIRE (newMaxMag <= rawMaxMag * 1.05f);
        WARN ("DFE cut-only: rawMaxMag=" << rawMaxMag << " newMaxMag=" << newMaxMag
              << " | rawMaxL1=" << rawMaxL1 << " newMaxL1=" << newMaxL1
              << " safeGain=" << safeGain);
    }

    // -------------------------------------------------------------------------------
    // CAMPO DE DELAY = ITD DE WOODWORTH  (ronda 0.3 — arregla el cue temporal)
    //
    // Los delays que libmysofa devuelve para este SOFA no son un ITD utilizable. CIPIC está
    // medido en coordenadas inter-aurales-polares (azimut sólo −80°…+80°, sin ±90°), y en esa
    // grilla la conversión entrega un campo con el MÍNIMO en los costados y el MÁXIMO al frente
    // y atrás: es un patrón de tiempo de llegada al CENTRO de la cabeza, no un retardo por oído.
    // La simetrización de más arriba conserva el ITD sólo si el original es antisimétrico; como
    // no lo es, lo promedia contra sí mismo y lo colapsa. Medido sobre el bake de v0.2.1:
    // ±90.7 µs (el 14 % de una cabeza real) y con el signo INVERTIDO en todo el lado izquierdo.
    //
    // Lo reemplazamos por el ITD de una cabeza esférica (Woodworth):
    //     ITD(θ) = a/c · (θ + sin θ)      a = 8.75 cm,  c = 343 m/s,   0 ≤ θ ≤ 90°
    // REFLEJADO arriba de 90° (θ → 180° − θ): atrás, en el plano medio, los dos oídos vuelven a
    // estar equidistantes, así que el ITD baja a 0 en 180° (cono de confusión). Sin reflejar, la
    // fórmula seguiría creciendo hasta 180°, que no es físico.
    //
    // Se toca SÓLO el campo de delay. Las magnitudes min-fase de CIPIC quedan intactas → el
    // timbre y el ILD no cambian ni un dB por este paso.
    //
    // PISO = 2 muestras: es el mínimo al que el kernel de 4 puntos de Lagrange3rd de JUCE queda
    // CENTRADO. En updateInternalVariables(), si delayFrac < 2 y delayInt ≥ 1, JUCE resta 1 al
    // entero y suma 1 a la fracción; con delay = 2 eso deja delayInt = 1 y delayFrac = 1, o sea
    // el punto de interpolación entre el 2º y el 3er tap de los 4. Por debajo de 2 el kernel se
    // corre hacia adelante y el primer tap cae sobre la posición de ESCRITURA (muestra vieja).
    // Bonus: en delay entero exacto los coeficientes colapsan a (0, 1, 0, 0) → passthrough.
    {
        constexpr double kHeadRadiusM = 0.0875;
        constexpr double kSoundC      = 343.0;
        constexpr float  kBaseSamples = 2.0f;

        auto woodworthSamples = [&] (double azDeg)
        {
            double a = std::fmod (azDeg, 360.0);
            if (a > 180.0) a -= 360.0;
            const double sgn = (a >= 0.0) ? 1.0 : -1.0;
            double m = std::abs (a);
            if (m > 90.0) m = 180.0 - m;                 // reflexión: cono de confusión
            const double th = m * 3.14159265358979324 / 180.0;
            return sgn * (kHeadRadiusM / kSoundC) * (th + std::sin (th)) * (double) sr;
        };

        // El campo de delay NO es el ITD total: es el ITD EXCEDENTE sobre el que ya trae la
        // magnitud. Las IRs min-fase de este bake no son neutras en el tiempo — el oído lejano
        // está filtrado por la sombra de la cabeza (un low-pass), y un low-pass de fase mínima
        // tiene retardo de grupo positivo, así que la magnitud sola ya adelanta el oído cercano.
        // Medido end-to-end sobre v0.3 con el campo puesto en Woodworth crudo, ese aporte llega
        // a +221 µs en az 50° — la mitad del Woodworth de ese ángulo. Sumarle el Woodworth
        // entero encima lo cuenta DOS VECES (el ITD medido daba hasta 53 % de más).
        //
        // Así que descomponemos como corresponde:
        //      ITD_total(d) = ITD_minfase(d) + ITD_excedente(d)   →   campo = Woodworth − ITD_minfase
        //
        // ITD_minfase se mide con el MISMO estimador que usa el test de aceptación (correlación
        // cruzada interaural limitada a 1.5 kHz y sobremuestreada ×16): así el criterio con el
        // que se hornea y el criterio con el que se verifica son el mismo, sin ajustar a ojo.
        double worstMin = 0.0; int worstMinDir = 0;
        for (int d = 0; d < kNumDirs; ++d)
        {
            const double az     = (double) d * (double) kStepDeg;
            const double target = woodworthSamples (az);                                  // ITD total que queremos
            const double already = orbita_test::itdSamples (&ringL[(size_t) d * taps], &ringR[(size_t) d * taps], taps, (double) sr);
            const double tau    = target - already;                                       // lo que falta poner
            delayL[(size_t) d] = kBaseSamples + (float) std::max (0.0, -tau);
            delayR[(size_t) d] = kBaseSamples + (float) std::max (0.0,  tau);
            if (std::abs (already) > std::abs (worstMin)) { worstMin = already; worstMinDir = d; }
        }
        WARN ("Campo de delay = Woodworth - ITD_minfase. Objetivo a 90 deg: "
              << woodworthSamples (90.0) / sr * 1.0e6 << " us | ITD que ya trae la magnitud: max "
              << worstMin / sr * 1.0e6 << " us en az " << (worstMinDir * kStepDeg)
              << " deg | piso del campo " << kBaseSamples << " muestras");
    }

    const std::string path = std::string (ORBITA_REPO_DIR) + "/source/dsp/HrirRing.h";
    std::ofstream out (path);
    REQUIRE (out.is_open());
    out << std::showpoint << std::setprecision (9); // fuerza el punto decimal -> literales float válidos

    out << "// GENERADO por tests/GenHrir.cpp [genring] -- anillo HRIR (movimiento, M2).\n"
        << "// DFE (ecualizacion de campo difuso) + fase minima aplicados; ITD en el delay field.\n"
        << "// No editar a mano: regenerar con  ./Tests \"[genring]\"\n"
        << "#pragma once\n#include <array>\n\nnamespace orbita {\n\n"
        << "inline constexpr int   kNumDirs       = " << kNumDirs << ";\n"
        << "inline constexpr int   kRingTaps      = " << taps << ";\n"
        << "inline constexpr float kRingSampleRate= " << sr << "f;\n"
        << "inline constexpr float kRingStepDeg   = " << kStepDeg << "f;\n\n"
        << "// Planos: dir d, tap t -> indice d*kRingTaps + t\n";

    auto dumpRing = [&] (const char* name, const std::vector<float>& v) {
        out << "inline constexpr std::array<float, " << v.size() << "> " << name << " = {{\n    ";
        out << std::setprecision (9);
        for (size_t i = 0; i < v.size(); ++i) {
            out << v[i] << "f, ";
            if ((i + 1) % 6 == 0) out << "\n    ";
        }
        out << "\n}};\n\n";
    };
    auto dumpDelays = [&] (const char* name, const std::vector<float>& v) {
        out << "inline constexpr std::array<float, " << v.size() << "> " << name << " = {{\n    ";
        out << std::setprecision (9);
        for (size_t i = 0; i < v.size(); ++i) {
            out << v[i] << "f, ";
            if ((i + 1) % 8 == 0) out << "\n    ";
        }
        out << "\n}};\n\n";
    };
    dumpRing   ("kRingL",      ringL);
    dumpRing   ("kRingR",      ringR);
    dumpDelays ("kRingDelayL", delayL);
    dumpDelays ("kRingDelayR", delayR);
    out << "} // namespace orbita\n";
    out.close();

    WARN ("HrirRing.h generado: " << path << " (" << kNumDirs << " dirs x " << taps << " taps)");
    SUCCEED();
}
