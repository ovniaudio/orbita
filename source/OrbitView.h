#pragma once

#include "PluginProcessor.h"

//==============================================================================
// Visualizador cenital animado (héroe, spec §8): oyente al centro, la órbita y el
// punto sonoro con estela girando en vivo. Lee la posición del motor (lock-free) y
// la forma/tamaño de la órbita del APVTS.
class OrbitView : public juce::Component, private juce::Timer
{
public:
    explicit OrbitView (PluginProcessor& p) : proc (p)
    {
        setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
        startTimerHz (30);
    }
    ~OrbitView() override { stopTimer(); }

    // Inverso del dibujo (px = cx - ax·sin az, py = cy - ay·cos az): offset del mouse -> ángulo en grados.
    // 0 = frente, + = izquierda, - = derecha. Estático y puro (testeable sin UI).
    static float azDegFromOffset (float dx, float dy) noexcept
    {
        return juce::radiansToDegrees (std::atan2 (-dx, -dy));
    }

    // Interactivo: arrastrar fija la DISTANCIA (radio). En modo Fijo, también fija el ángulo.
    void mouseDown (const juce::MouseEvent& e) override { setPosFromMouse (e); }
    void mouseDrag (const juce::MouseEvent& e) override { setPosFromMouse (e); }

    void paint (juce::Graphics& g) override
    {
        const auto b   = getLocalBounds().toFloat().reduced (8.0f);
        const float cx = b.getCentreX();
        const float cy = b.getCentreY();
        const float maxR = juce::jmin (b.getWidth(), b.getHeight()) * 0.46f;

        const juce::Colour cyan (0xff22d3ee), soft (0xff7dd3fc), faint (0xff223247);

        // tamaño/forma de la órbita desde el APVTS
        const float radius01 = get ("orbRadius") * 0.01f;
        const float spread01 = get ("orbSpread") * 0.01f;
        const bool  ellipse  = ((int) get ("orbShape")) == 1; // 0 Círculo, 1 Elipse, 2 Espiral
        const float R   = juce::jmap (radius01, 0.0f, 1.0f, maxR * 0.42f, maxR);
        const float ecc = ellipse ? spread01 * 0.55f : 0.0f;
        const float ax  = R;                 // semieje izquierda-derecha
        const float ay  = R * (1.0f - ecc);  // semieje frente-atrás

        // anillos de distancia
        for (int i = 1; i <= 3; ++i)
        {
            const float t = (float) i / 3.0f;
            g.setColour (faint.withAlpha (0.5f));
            g.drawEllipse (cx - maxR * t, cy - maxR * t, maxR * 2 * t, maxR * 2 * t, 1.0f);
        }
        // ticks de azimut (cada 30°)
        for (int d = 0; d < 360; d += 30)
        {
            const float a = juce::degreesToRadians ((float) d);
            const float r0 = maxR * 0.96f, r1 = maxR * 1.02f;
            g.setColour (faint);
            g.drawLine (cx - r0 * std::sin (a), cy - r0 * std::cos (a),
                        cx - r1 * std::sin (a), cy - r1 * std::cos (a), 1.0f);
        }

        // path de la órbita
        juce::Path orbit;
        orbit.addEllipse (cx - ax, cy - ay, ax * 2.0f, ay * 2.0f);
        g.setColour (cyan.withAlpha (0.40f));
        g.strokePath (orbit, juce::PathStrokeType (1.5f));

        // estela (posiciones recientes; el radio sigue la distancia instantanea = fly-by)
        for (int i = 0; i < kTrail; ++i)
        {
            const int idx = (trailPos - 1 - i + kTrail * 2) % kTrail;
            const float a  = trail[idx];
            const float ds = (radius01 > 1.0e-3f) ? juce::jlimit (0.15f, 1.6f, trailDist[idx] / radius01) : 1.0f;
            const float alpha = (1.0f - (float) i / kTrail) * 0.5f;
            const float sz = juce::jmap ((float) i, 0.0f, (float) kTrail, 9.0f, 2.0f);
            const float x = cx - ax * ds * std::sin (a);
            const float y = cy - ay * ds * std::cos (a);
            g.setColour (cyan.withAlpha (alpha));
            g.fillEllipse (x - sz * 0.5f, y - sz * 0.5f, sz, sz);
        }

        // punto sonoro (actual), a la distancia instantanea (el fly-by lo acerca/aleja del centro)
        const int   cur = (trailPos - 1 + kTrail) % kTrail;
        const float az  = trail[cur];
        const float dsC = (radius01 > 1.0e-3f) ? juce::jlimit (0.15f, 1.6f, trailDist[cur] / radius01) : 1.0f;
        const float px = cx - ax * dsC * std::sin (az);
        const float py = cy - ay * dsC * std::cos (az);
        g.setColour (cyan.withAlpha (0.25f));
        g.fillEllipse (px - 11.0f, py - 11.0f, 22.0f, 22.0f);
        g.setColour (juce::Colour (0xffe8feff));
        g.fillEllipse (px - 5.5f, py - 5.5f, 11.0f, 11.0f);

        // oyente (centro)
        g.setColour (soft);
        g.drawEllipse (cx - 8.0f, cy - 8.0f, 16.0f, 16.0f, 1.8f);
        g.fillEllipse (cx - 2.0f, cy - 9.5f, 4.0f, 4.0f); // "nariz" = frente

        // readout AZ
        int deg = (int) std::round (juce::radiansToDegrees (az));
        deg = ((deg % 360) + 360) % 360; if (deg > 180) deg -= 360; // -180..180
        const char* side = (deg == 0 ? "FRENTE" : (std::abs (deg) == 180 ? "ATRAS" : (deg > 0 ? "IZQ" : "DER")));
        g.setColour (juce::Colour (0xff8aa2b8));
        g.setFont (juce::Font (12.0f));
        g.drawText (juce::String (std::abs (deg)) + juce::String::fromUTF8 ("\xc2\xb0 ") + side,
                    getLocalBounds().removeFromBottom (20), juce::Justification::centred);
    }

private:
    void timerCallback() override
    {
        trail[trailPos]     = proc.uiAzimuth.load (std::memory_order_relaxed);
        trailDist[trailPos] = proc.uiDistance.load (std::memory_order_relaxed);
        trailPos = (trailPos + 1) % kTrail;
        repaint();
    }

    void setPosFromMouse (const juce::MouseEvent& e)
    {
        const auto b = getLocalBounds().toFloat().reduced (8.0f);
        const float maxR = juce::jmin (b.getWidth(), b.getHeight()) * 0.46f;
        const float dx = e.position.x - b.getCentreX(), dy = e.position.y - b.getCentreY();
        const float r  = std::sqrt (dx * dx + dy * dy) / juce::jmax (1.0f, maxR);
        if (auto* p = proc.apvts.getParameter ("orbRadius"))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, r));
        // En modo Fijo (orbRate == 4) el arrastre también fija el ángulo.
        if (((int) get ("orbRate")) == 4)
            if (auto* pa = proc.apvts.getParameter ("orbFixedAz"))
            {
                const float azDeg = juce::jlimit (-180.0f, 180.0f, azDegFromOffset (dx, dy));
                pa->setValueNotifyingHost (juce::jmap (azDeg, -180.0f, 180.0f, 0.0f, 1.0f));
            }
    }

    float get (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    PluginProcessor& proc;
    static constexpr int kTrail = 40;
    float trail[kTrail] = {};
    float trailDist[kTrail] = {};
    int   trailPos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrbitView)
};
