#include "KnobLookAndFeel.h"

namespace orbita
{
namespace
{
    // Hornea el cuerpo "glass" oscuro a una Image cuadrada de D px FÍSICOS — IDÉNTICO al del resto del
    // sello (ovni::ui::KnobLookAndFeel): disco de vidrio limpio, SIN anillo de ticks ni bisel torneado.
    // Estático (no depende del valor ni del hue) -> se cachea y se reusa por tamaño.
    void renderBody (juce::Image& img, int D)
    {
        img = juce::Image (juce::Image::ARGB, D, D, true);
        juce::Graphics g (img);
        const float fd    = (float) D;
        const float c     = fd * 0.5f;
        const float bodyR = fd * 0.33f;   // r33 — igual que la familia

        // sombra suave (asienta el cuerpo en el chasis)
        {
            juce::Path sh;
            sh.addEllipse (c - bodyR, c - bodyR + fd * 0.02f, bodyR * 2.0f, bodyR * 2.0f);
            juce::DropShadow ds (juce::Colours::black.withAlpha (0.45f),
                                 juce::roundToInt (fd * 0.05f),
                                 { 0, juce::roundToInt (fd * 0.015f) });
            ds.drawForPath (g, sh);
        }

        // cuerpo glass: radial con la luz arriba-izquierda (#1b222d → #0d1118 → #05070b)
        juce::ColourGradient body (juce::Colour (0xff1b222d), c - bodyR * 0.24f, c - bodyR * 0.40f,
                                   juce::Colour (0xff05070b), c - bodyR * 0.24f, c - bodyR * 0.40f + bodyR * 1.85f,
                                   true);
        body.addColour (0.55, juce::Colour (0xff0d1118));
        g.setGradientFill (body);
        g.fillEllipse (c - bodyR, c - bodyR, bodyR * 2.0f, bodyR * 2.0f);

        // hairline del borde (rgba(160,192,224,0.16))
        g.setColour (juce::Colour (0x29a0c0e0));
        g.drawEllipse (c - bodyR, c - bodyR, bodyR * 2.0f, bodyR * 2.0f, juce::jmax (1.0f, fd * 0.01f));

        // reflejo interno: arco fino arriba-izquierda (rgba(190,225,255,0.10))
        {
            juce::Path hi;
            hi.addCentredArc (c, c, bodyR * 0.91f, bodyR * 0.91f, 0.0f, -2.35f, -0.85f, true);
            g.setColour (juce::Colour (0x1abee1ff));
            g.strokePath (hi, juce::PathStrokeType (fd * 0.014f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }
    }
}

const juce::Image& KnobLookAndFeel::bodyFor (int D)
{
    auto it = bodyCache.find (D);
    if (it == bodyCache.end())
    {
        juce::Image im;
        renderBody (im, D);
        it = bodyCache.emplace (D, std::move (im)).first;
    }
    return it->second;
}

void KnobLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                        float pos, float a0, float a1, juce::Slider& s)
{
    const juce::Colour hue { (juce::uint32) (int) s.getProperties()
                                .getWithDefault ("hue", (int) theme::cyan.getARGB()) };

    const int   logicalD = juce::jmin (w, h);
    const float cx = (float) x + (float) w * 0.5f;
    const float cy = (float) y + (float) h * 0.5f;

    // cuerpo + ticks: blit del cache a resolución física (crisp en Retina)
    const float scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    const int   physD = juce::jmax (8, juce::roundToInt ((float) logicalD * scale));
    const auto& body  = bodyFor (physD);
    const float inv   = (float) logicalD / (float) physD;
    g.drawImageTransformed (body, juce::AffineTransform::scale (inv)
                                    .translated (cx - (float) logicalD * 0.5f,
                                                 cy - (float) logicalD * 0.5f));

    const float fd  = (float) logicalD;
    const float ang = a0 + pos * (a1 - a0);

    // track del rango — hairline NEUTRA (igual que la familia: el color de familia queda para el ARCO
    // DE VALOR, que corta más al no competir con el track).
    juce::Path track;
    track.addCentredArc (cx, cy, fd * 0.44f, fd * 0.44f, 0.0f, a0, a1, true);
    g.setColour (juce::Colour (0x21a0c0e0));
    g.strokePath (track, juce::PathStrokeType (fd * 0.030f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // arco de valor: halo tenue + núcleo sólido — el color = hue
    juce::Path val;
    val.addCentredArc (cx, cy, fd * 0.44f, fd * 0.44f, 0.0f, a0, ang, true);
    g.setColour (hue.withAlpha (0.30f));
    g.strokePath (val, juce::PathStrokeType (fd * 0.068f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (hue);
    g.strokePath (val, juce::PathStrokeType (fd * 0.030f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // puntero: línea clara del cuerpo hacia el borde (r14→r27) — igual que la familia, SIN punto central.
    const float pr0 = fd * 0.14f, pr1 = fd * 0.27f;
    const float sx = cx + pr0 * std::sin (ang), sy = cy - pr0 * std::cos (ang);
    const float ex = cx + pr1 * std::sin (ang), ey = cy - pr1 * std::cos (ang);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawLine (sx, sy, ex, ey, fd * 0.05f);
    g.setColour (juce::Colour (0xffeaf1f8));
    g.drawLine (sx, sy, ex, ey, fd * 0.034f);

    // ---- ESTADOS (feel de la familia) — el slider publica flags en getProperties(); acá se leen y se
    //      dibujan glows ENCIMA (sólo alpha, compositor-friendly). Sin flags = sin glow (slider pelado).
    auto& props = s.getProperties();
    const bool hovered = props.getWithDefault ("hovered", false);
    const bool pressed = props.getWithDefault ("pressed", false);
    const bool focused = props.getWithDefault ("focused", false);

    if (hovered || pressed)
    {
        const float ga = pressed ? theme::state::pressGlow : theme::state::hoverGlow;
        g.setColour (hue.withAlpha (ga * 1.8f));
        g.strokePath (val, juce::PathStrokeType (fd * 0.10f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        const float rimR = fd * 0.34f;   // velo radial sobre el cuerpo: "se enciende" bajo el cursor
        juce::ColourGradient glow (hue.withAlpha (ga * 0.5f), cx, cy,
                                   hue.withAlpha (0.0f), cx, cy - rimR * 1.15f, true);
        g.setGradientFill (glow);
        g.fillEllipse (cx - rimR * 1.15f, cy - rimR * 1.15f, rimR * 2.3f, rimR * 2.3f);
    }

    if (focused)   // foco de teclado: anillo fino por fuera del cuerpo
    {
        const float fr = fd * 0.46f;
        g.setColour (hue.withAlpha (theme::state::focusRing));
        g.drawEllipse (cx - fr, cy - fr, fr * 2.0f, fr * 2.0f, fd * 0.012f);
    }
}
}
