#pragma once
#include "../PluginProcessor.h"
#include "Theme.h"
#include "Fonts.h"

namespace orbita
{
// Radar "B" limpio (rediseño aprobado): pozo MUY oscuro con profundidad sutil al centro (un glow cian
// tenue que respira) — SIN polvo estelar, SIN scanlines pesadas. UN solo anillo de referencia (~maxR,
// cian ~0.11) + 4 marcas cardinales cortas (~0.30; la de arriba = FRENTE). Una personita CHICA al centro
// (cabeza llena + hombros + UNA flecha hacia arriba — sin cruz/cono/label). El PATH-ghost de la órbita
// (Shape/Radius/Spread/Height/Doppler/Dir) sigue horneado en la capa estática. Todo eso = capa estática
// cacheada a píxel FÍSICO (Retina), se rehace sólo al cambiar tamaño/escala/forma/knob-de-forma.
//
// En VIVO (por frame, sin mutación en paint): el glow central que respira, la estela-cometa LIMPIA
// (fade pow(t,1.35), núcleo + halo) y la fuente (bloom; cerca = más grande/brillante). La reactividad a
// los knobs es VISIBLE pero SOBRIA: el punto vivo cae SIEMPRE sobre la órbita dibujada y queda DENTRO
// del campo (maxR). Se lee directa de los params del APVTS:
//   RADIUS  -> la elipse/órbita crece o se achica (geom; rango contenido para no desbordar).
//   WIDTH   -> el bloom de la fuente se ensancha HORIZONTAL (sobrio).
//   DOPPLER -> fly-by SUTIL adelante-atrás: cerca al FRENTE (az=0), lejos ATRÁS (az=π). La elipse se
//              vuelve excéntrica front-back con el CENTRO FIJO en el oyente — NO traslada la órbita.
//   ROOM    -> halo de ambiente MAGENTA alrededor del campo que crece con el valor.
//   HEIGHT  -> elevación: la fuente se desplaza vertical (indicador vivo: tallo + fuente sobre el plano).
//   CHAOS   -> jitter del motor (ya viene en uiAzimuth); el punto se ve nervioso PERO sigue la órbita.
//   SPREAD  -> achatamiento front-back de la elipse (geom).
//   SPEED   -> velocidad angular de la órbita (ya viene del motor vía uiAzimuth).
// La posición viene de uiAzimuth/uiDistance (atomics lock-free; el DSP ya aplica doppler/chaos/speed).
// reduced-motion: un frame coherente. No pinta con la ventana cerrada (CPU).
class RadarView : public juce::Component, private juce::Timer
{
public:
    explicit RadarView (PluginProcessor& p);
    ~RadarView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override { setPosFromMouse (e); }
    void mouseDrag (const juce::MouseEvent& e) override { setPosFromMouse (e); }

    // Test-only: avanza la estela n frames a mano (headless; el timer no corre sin ventana). Empuja
    // uiAzimuth/uiDistance al buffer igual que timerCallback -> el shot real muestra la estela poblada.
    void dbgPump (int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            trail[trailPos]     = proc.uiAzimuth.load (std::memory_order_relaxed);
            trailDist[trailPos] = proc.uiDistance.load (std::memory_order_relaxed);
            trailPos = (trailPos + 1) % kTrail;
        }
    }

    // --- lógica pura del PATH por forma (testeable sin UI) -------------------------------------
    static constexpr int kCircle = 0, kEllipse = 1, kSpiral = 2, kPendulum = 3, kPendulumBack = 4;
    static bool  shapeIsPendulumFront (int s) noexcept { return s == kPendulum; }
    static bool  shapeIsPendulumBack  (int s) noexcept { return s == kPendulumBack; }
    static constexpr float kSwingDeg = 90.0f;   // amplitud del péndulo (coincide con OrbitBrain)

    // Azimut (grados, 0 = frente, + = CCW/izquierda) que dibuja el PATH en la fase 0..1.
    static float shapeAzDeg (int shape, float phase01) noexcept;
    // Radio relativo (0..1 del semieje) que dibuja el PATH en la fase — el Spiral "respira".
    static float shapeRadius01 (int shape, float phase01) noexcept;

    // Distancia 0..1 con fly-by Doppler — MISMA fórmula que OrbitBrain (DopplerTuning por defecto:
    // maxEcc01=0.55, curve=1 LINEAL): A = 0.55·doppler; dist = base − A·cos(az). az=0 (frente) → más CERCA;
    // az=π (atrás) → más LEJOS; doppler=0 → dist == base (círculo, sin excentricidad).
    static float dopplerDist01 (float radiusBase01, float dopp01, float azRad) noexcept;

    // Mapea (azimut, distancia 0..1) → punto del campo. Centro FIJO en (cx,cy): el Doppler sólo cambia la
    // distancia por azimut (elipse excéntrica front-back), NUNCA corre el centro (sin traslación vertical).
    static juce::Point<float> mapOrbit (float cx, float cy, float ax, float ay,
                                        float azRad, float dist01) noexcept;

private:
    void timerCallback() override;
    void setPosFromMouse (const juce::MouseEvent& e);
    void renderStatic (int w, int h, float scale);
    void buildBloomSprites();
    const juce::Image& bloomSpriteFor (float prox);
    float get (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    // geometría compartida estática↔vivo: el PATH-ghost y el punto vivo comparten centro/semiejes y usan
    // EXACTAMENTE el mismo mapeo (mapOrbit) + la misma distancia con fly-by (dopplerDist01) → el punto
    // recorre la órbita dibujada de forma predecible. El centro queda SIEMPRE en el oyente (cx,cy).
    struct Geom { float cx, cy, maxR, ax, ay; };
    Geom geom (int w, int h) const;
    juce::Point<float> orbitPoint (const Geom& gm, float azRad, float dist01) const;

    PluginProcessor& proc;
    juce::Image bgLayer;                 // capa estática cacheada (resolución FÍSICA)
    float bgScale = 0.0f;                // escala con la que se horneó bgLayer
    int   lastShape = -1;
    float lastSpread = -1.0f, lastRadius = -1.0f, lastHeight = -1.0e9f, lastDopp = -1.0f;
    int   lastDir = -1;

    // bloom: sprites de glow horneados por bucket de proximidad (3 capas apiladas + corona).
    static constexpr int kProxBuckets = 6;
    juce::Image bloomSprites[kProxBuckets + 1];

    // CPU: pausa el repaint cuando todo está quieto (fuente sin moverse + halos sin cambiar) y la
    // estela ya colapsó. El glow central que respira mantiene vivo un latido suave (no fija el frame).
    float lastAz = 1.0e9f, lastDist = 1.0e9f, lastRoom = -1.0f, lastWidth = -1.0f, lastDopLive = -1.0f;
    int   settleFrames = 0;
    float breathPhase = 0.0f;            // fase del latido del glow central (0..twoPi)

    static constexpr int kTrail = 44;
    float trail[kTrail] = {};
    float trailDist[kTrail] = {};
    int   trailPos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RadarView)
};
}
