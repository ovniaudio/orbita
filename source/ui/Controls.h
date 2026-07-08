#pragma once
#include "../PluginProcessor.h"
#include "Theme.h"
#include "Fonts.h"
#include <functional>

namespace orbita
{
//================================================================================================
// Glyph de marca OVNI (portal): órbita inclinada con gradiente cian→violeta + núcleo blanco + satélite.
inline void paintOvniMark (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto c = r.getCentre();
    const float s = juce::jmin (r.getWidth(), r.getHeight());

    juce::Path orb;
    orb.addEllipse (c.x - s * 0.42f, c.y - s * 0.18f, s * 0.84f, s * 0.36f);
    juce::Path ring;
    juce::PathStrokeType (s * 0.095f).createStrokedPath (ring, orb);
    ring.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (-22.0f), c.x, c.y));
    juce::ColourGradient gg (theme::cyan, c.x - s * 0.4f, c.y - s * 0.4f,
                             juce::Colour (0xffc084fc), c.x + s * 0.4f, c.y + s * 0.4f, false);
    g.setGradientFill (gg);
    g.fillPath (ring);

    g.setColour (juce::Colours::white);
    g.fillEllipse (c.x - s * 0.135f, c.y - s * 0.135f, s * 0.27f, s * 0.27f);
    g.setColour (theme::cyan);
    g.fillEllipse (c.x + s * 0.27f, c.y - s * 0.33f, s * 0.15f, s * 0.15f);
}

// Ícono de trayectoria por índice (Circle/Ellipse/Spiral/Pendulum/Pendulum Back).
inline void paintShapeIcon (juce::Graphics& g, juce::Rectangle<float> r, int idx, juce::Colour col)
{
    g.setColour (col);
    const auto c = r.getCentre();
    const float s = juce::jmin (r.getWidth(), r.getHeight());
    const float t = juce::jmax (1.2f, s * 0.085f);
    switch (idx)
    {
        case 0: g.drawEllipse (c.x - s*0.34f, c.y - s*0.34f, s*0.68f, s*0.68f, t); break;          // Circle
        case 1: g.drawEllipse (c.x - s*0.42f, c.y - s*0.25f, s*0.84f, s*0.50f, t); break;          // Ellipse
        case 2: {                                                                                   // Spiral
            juce::Path p; const int N = 64;
            for (int i = 0; i <= N; ++i) { const float ph=(float)i/N, a=ph*juce::MathConstants<float>::twoPi*1.85f, rr=s*0.36f*(0.18f+0.82f*ph);
                const float x=c.x+rr*std::sin(a), y=c.y-rr*std::cos(a); if(i==0)p.startNewSubPath(x,y); else p.lineTo(x,y); }
            g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); break; }
        case 3: {                                                                                   // Pendulum (front)
            g.fillEllipse (c.x - s*0.05f, c.y - s*0.36f, s*0.10f, s*0.10f);
            g.drawLine (c.x, c.y - s*0.29f, c.x, c.y + s*0.12f, t*0.8f);
            g.fillEllipse (c.x - s*0.12f, c.y + s*0.06f, s*0.24f, s*0.24f);
            juce::Path arc; arc.addCentredArc (c.x, c.y - s*0.29f, s*0.40f, s*0.40f, 0.0f, -0.85f, 0.85f, true);
            g.strokePath (arc, juce::PathStrokeType (t*0.6f)); break; }
        case 4: {                                                                                   // Pendulum Back
            g.fillEllipse (c.x - s*0.05f, c.y + s*0.26f, s*0.10f, s*0.10f);
            g.drawLine (c.x, c.y + s*0.29f, c.x, c.y - s*0.12f, t*0.8f);
            g.fillEllipse (c.x - s*0.12f, c.y - s*0.30f, s*0.24f, s*0.24f);
            juce::Path arc; arc.addCentredArc (c.x, c.y + s*0.29f, s*0.40f, s*0.40f, 0.0f,
                                               juce::MathConstants<float>::pi - 0.85f, juce::MathConstants<float>::pi + 0.85f, true);
            g.strokePath (arc, juce::PathStrokeType (t*0.6f)); break; }
        default: break;
    }
}

// Flecha circular de dirección. cw=true → ⟳ (horario), false → ⟲ (antihorario).
inline void paintDirIcon (juce::Graphics& g, juce::Rectangle<float> r, bool cw, juce::Colour col)
{
    g.setColour (col);
    const auto c = r.getCentre();
    const float s = juce::jmin (r.getWidth(), r.getHeight());
    const float rr = s * 0.26f, t = juce::jmax (1.3f, s * 0.078f);
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, rr, rr, 0.0f, cw ? 0.5f : -0.5f, cw ? 5.2f : -5.2f, true);
    g.strokePath (arc, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // punta de flecha al final del arco
    const float ea = cw ? 5.2f : -5.2f;
    const float ex = c.x + rr * std::sin (ea), ey = c.y - rr * std::cos (ea);
    const float dir = cw ? 1.0f : -1.0f;
    const float tang = ea + dir * juce::MathConstants<float>::halfPi;
    juce::Path head;
    const float hx = std::sin (tang), hy = -std::cos (tang);
    const float bx = std::sin (ea), by = -std::cos (ea);
    head.addTriangle (ex, ey,
                      ex - hx * s*0.16f + bx * s*0.10f, ey - hy * s*0.16f + by * s*0.10f,
                      ex - hx * s*0.16f - bx * s*0.10f, ey - hy * s*0.16f - by * s*0.10f);
    g.fillPath (head);
}

//================================================================================================
// Control segmentado ligado a un AudioParameterChoice. Texto o íconos (via drawSeg). Sondea el
// param (12 Hz) para reflejar automatización. Click setea el valor.
class SegControl : public juce::Component, private juce::Timer
{
public:
    using DrawSeg = std::function<void (juce::Graphics&, juce::Rectangle<float>, int idx, bool on)>;

    SegControl (PluginProcessor& p, juce::String paramID, juce::StringArray segLabels,
                DrawSeg customDraw = {}, int iconCount = 0)
        : proc (p), id (std::move (paramID)), labels (std::move (segLabels)), draw (std::move (customDraw))
    {
        count = labels.isEmpty() ? juce::jmax (1, iconCount) : labels.size();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        refresh();
        startTimerHz (12);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int seg = juce::jlimit (0, count - 1, (int) (e.position.x / (getWidth() / (float) count)));
        if (auto* pr = proc.apvts.getParameter (id))
            pr->setValueNotifyingHost (count > 1 ? (float) seg / (float) (count - 1) : 0.0f);
        selected = seg;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        // Contenedor SIN marco: apenas un velo de superficie para que el riel "exista" sin enmarcar.
        // Sin borde, sin inset duro — el texto respira (estilo Ableton / TE).
        g.setColour (juce::Colour (0xff0a0c11).withAlpha (0.45f));
        g.fillRoundedRectangle (b, 5.0f);

        // fuente uniforme GRANDE (sin divisores internos no hay línea que pisar)
        const float fontH = juce::jlimit (11.0f, 14.0f, b.getHeight() * 0.42f);

        for (int i = 0; i < count; ++i)
        {
            // límites enteros = tiles parejos sin gaps, texto centrado exacto
            const int x0 = juce::roundToInt (b.getWidth() * (float) i / (float) count);
            const int x1 = juce::roundToInt (b.getWidth() * (float) (i + 1) / (float) count);
            auto cell = juce::Rectangle<float> (b.getX() + (float) x0, b.getY(), (float) (x1 - x0), b.getHeight());
            const bool on = (i == selected);
            const bool hov = (i == hovered);

            // ACTIVO = realce SUAVE, sin pastilla ni borde: glow centrado + tinte muy tenue.
            // El estado se lee por el BRILLO DEL TEXTO + el halo de hue, no por un marco.
            if (on)
            {
                auto sel = cell.reduced (2.5f);
                const float rad = 5.0f;

                // 1) glow EXTERIOR de 3 capas (halo difuso que insinúa el activo sin marco) — igual
                //    que el SegControl del ui-kit de la familia (paintSelected).
                for (int k = 3; k >= 1; --k)
                {
                    g.setColour (theme::cyan.withAlpha (0.022f + (hov ? 0.012f : 0.0f)));
                    g.fillRoundedRectangle (sel.expanded ((float) k * 1.8f), rad + (float) k * 1.4f);
                }

                // 2) tinte plano de baja alpha (sin gradiente glassy, sin sheen, sin borde)
                g.setColour (theme::cyan.withAlpha (hov ? 0.15f : 0.12f));
                g.fillRoundedRectangle (sel, rad);
            }
            // HOVER = susurro de tinte, sin borde
            else if (hov)
            {
                auto sel = cell.reduced (2.5f);
                g.setColour (theme::cyan.withAlpha (0.05f));
                g.fillRoundedRectangle (sel, 5.0f);
            }

            if (draw)
                draw (g, cell, i, on);
            else
            {
                // Activo = texto brillante (blanco cálido cian). Inactivo = apagado, sin caja.
                g.setColour (on ? juce::Colour (0xffd6fbff)
                                : (hov ? theme::txt.withAlpha (0.75f) : theme::mut));
                g.setFont (fonts::mono (fontH));
                g.drawText (labels[i], cell.toNearestInt(), juce::Justification::centred);
            }

            // (sin divisores internos ni borde de contenedor: limpio, suave, minimal)
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { updateHover (e.position.x); }
    void mouseEnter (const juce::MouseEvent& e) override { updateHover (e.position.x); }
    void mouseExit  (const juce::MouseEvent&)   override { if (hovered != -1) { hovered = -1; repaint(); } }

private:
    void timerCallback() override { refresh(); }
    void refresh()
    {
        const int s = juce::jlimit (0, count - 1, (int) std::round (proc.apvts.getRawParameterValue (id)->load()));
        if (s != selected) { selected = s; repaint(); }
    }
    void updateHover (float px)
    {
        const int h = juce::jlimit (0, count - 1, (int) (px / (getWidth() / (float) count)));
        if (h != hovered) { hovered = h; repaint(); }
    }

    PluginProcessor& proc;
    juce::String id;
    juce::StringArray labels;
    DrawSeg draw;
    int count = 1, selected = 0, hovered = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegControl)
};

//================================================================================================
// IN PHASE / MONO-SAFE — MISMA identidad que el resto del sello (ovni::ui::ToggleButton): LED a la
// izquierda + "IN PHASE" (main) / "MONO SAFE" (sub). Horizontal cuando la celda es más ancha que alta
// (caso de ÓRBITA), vertical (ø apilado) si fuera más alta. Ligado a "monoSafe". Hue de familia = cian.
class InPhaseButton : public juce::Component, private juce::Timer
{
public:
    explicit InPhaseButton (PluginProcessor& p, juce::Colour familyHue = theme::cyan)
        : proc (p), hue (familyHue)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        on = proc.apvts.getRawParameterValue ("monoSafe")->load() >= 0.5f;
        startTimerHz (12);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        if (auto* pr = proc.apvts.getParameter ("monoSafe"))
        {
            on = ! on;
            pr->setValueNotifyingHost (on ? 1.0f : 0.0f);
        }
        pressed = true;
        repaint();
    }
    void mouseUp   (const juce::MouseEvent&) override { pressed = false; repaint(); }
    void mouseEnter(const juce::MouseEvent&) override { hovered = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovered = false; pressed = false; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        const float hoverBoost = hovered ? (pressed ? 0.18f : 0.10f) : 0.0f;

        // ---- modo VERTICAL (celda más alta que ancha): símbolo ø arriba + main + sub apilados ----
        if (b.getHeight() > b.getWidth())
        {
            if (hovered) { g.setColour (hue.withAlpha (hoverBoost * 0.5f)); g.fillRoundedRectangle (b, 4.0f); }
            auto col = b.reduced (2.0f, 4.0f);
            auto sym = col.removeFromTop (col.getHeight() * 0.46f).withSizeKeepingCentre (22.0f, 22.0f);
            if (on)
            {
                g.setColour (hue.withAlpha (0.16f + hoverBoost)); g.fillEllipse (sym.expanded (4.0f));
                g.setColour (hue.withAlpha (0.10f + hoverBoost)); g.fillEllipse (sym.expanded (1.0f));
                g.setColour (hue);
            }
            else g.setColour (theme::fnt);
            g.setFont (fonts::mono (12.0f));
            g.drawText (juce::String::fromUTF8 ("\xc3\xb8"), sym.toNearestInt(), juce::Justification::centred);
            g.setColour (on ? juce::Colour (0xffeaffff) : theme::mut);
            g.setFont (fonts::mono (9.0f));
            g.drawText ("IN PHASE", col.removeFromTop (col.getHeight() * 0.55f).toNearestInt(), juce::Justification::centred);
            g.setColour (on ? hue : theme::fnt);
            g.setFont (fonts::mono (7.0f));
            g.drawText ("MONO SAFE", col.toNearestInt(), juce::Justification::centred);
            return;
        }

        // ---- modo HORIZONTAL (lo normal en ÓRBITA): LED a la izquierda + IN PHASE / MONO SAFE ----
        if (on)
        {
            g.setColour (hue.withAlpha (0.05f + hoverBoost * 0.3f)); g.fillRoundedRectangle (b.expanded (2.0f), 7.0f);
            g.setColour (hue.withAlpha (0.12f + hoverBoost * 0.3f)); g.fillRoundedRectangle (b, 6.0f);
        }
        else
        {
            g.setColour (juce::Colour (0x33060709)); g.fillRoundedRectangle (b, 6.0f);
            if (hovered) { g.setColour (hue.withAlpha (0.06f + hoverBoost)); g.fillRoundedRectangle (b, 6.0f); }
        }
        auto led = juce::Rectangle<float> (b.getX() + 13.0f, b.getCentreY() - 5.5f, 11.0f, 11.0f);
        if (on)
        {
            g.setColour (hue); g.fillEllipse (led);
            g.setColour (juce::Colours::white.withAlpha (0.85f)); g.fillEllipse (led.reduced (3.2f));
        }
        else { g.setColour (juce::Colour (0xff1b2430)); g.fillEllipse (led); }

        auto tx = b.withTrimmedLeft (34.0f).reduced (0.0f, 4.0f);
        g.setColour (on ? juce::Colour (0xffeaffff) : theme::mut);
        g.setFont (fonts::mono (10.5f));
        g.drawText ("IN PHASE", tx.removeFromTop (tx.getHeight() * 0.56f).toNearestInt(), juce::Justification::bottomLeft);
        g.setColour (on ? hue : theme::fnt);
        g.setFont (fonts::mono (7.5f));
        g.drawText ("MONO SAFE", tx.toNearestInt(), juce::Justification::topLeft);
    }
private:
    void timerCallback() override
    {
        const bool s = proc.apvts.getRawParameterValue ("monoSafe")->load() >= 0.5f;
        if (s != on) { on = s; repaint(); }
    }
    PluginProcessor& proc;
    juce::Colour hue;
    bool on = false, hovered = false, pressed = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InPhaseButton)
};

//================================================================================================
// LookAndFeel del riel de salida (mockup .fader): track inset oscuro + fill cian con glow + handle
// fino cian. Bipolar (OUTPUT/INPUT): el fill nace del centro hacia el handle + tick central. La
// propiedad "bip" del slider (set por el editor) lo activa.
struct RailSliderLAF : juce::LookAndFeel_V4
{
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        const bool bip = (bool) s.getProperties().getWithDefault ("bip", false);
        auto track = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 8.0f,
                                             (float) w, 16.0f);
        g.setColour (juce::Colour (0xe60a141d)); g.fillRoundedRectangle (track, 3.0f);   // inset oscuro
        g.setColour (theme::line);               g.drawRoundedRectangle (track, 3.0f, 1.0f);

        const float cxTrack = track.getCentreX();
        if (bip)
        {
            const float lo = juce::jmin (cxTrack, pos), hi = juce::jmax (cxTrack, pos);
            if (hi - lo > 0.5f)
            {
                juce::ColourGradient gg (theme::cyan.withAlpha (0.16f), track.getX(), 0.0f,
                                         theme::cyan.withAlpha (0.30f), track.getRight(), 0.0f, false);
                g.setGradientFill (gg);
                g.saveState();
                g.reduceClipRegion (juce::Rectangle<float> (lo, track.getY(), hi - lo, track.getHeight())
                                        .getSmallestIntegerContainer());
                g.fillRoundedRectangle (track, 3.0f);
                g.restoreState();
            }
            g.setColour (juce::Colour (0x2ea0c0e0));      // tick central
            g.drawVerticalLine ((int) cxTrack, track.getY(), track.getBottom());
        }
        else
        {
            const float fillW = juce::jmax (0.0f, pos - track.getX());
            if (fillW > 0.5f)
            {
                juce::ColourGradient gg (theme::cyan.withAlpha (0.16f), track.getX(), 0.0f,
                                         theme::cyan.withAlpha (0.30f), track.getRight(), 0.0f, false);
                g.setGradientFill (gg);
                g.saveState();
                g.reduceClipRegion (track.withWidth (fillW).getSmallestIntegerContainer());
                g.fillRoundedRectangle (track, 3.0f);
                g.restoreState();
            }
        }

        // handle: barra fina cian con glow (mockup .fhandle)
        auto hd = juce::Rectangle<float> (pos - 1.5f, track.getY() - 1.0f, 3.0f, track.getHeight() + 2.0f);
        g.setColour (theme::cyan.withAlpha (0.5f));
        g.fillRoundedRectangle (hd.expanded (1.5f, 0.0f), 2.0f);     // glow
        g.setColour (theme::cyan);
        g.fillRoundedRectangle (hd, 2.0f);
    }
};
}
