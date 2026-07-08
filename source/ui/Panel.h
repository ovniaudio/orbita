#pragma once
#include "Theme.h"

namespace orbita
{
// Atmósfera del chasis (mockup orbit-a §buildAtmo, nivel GOLD): luz fría direccional arriba-izquierda,
// caída a negro abajo-derecha, topográficas orgánicas (mapa de otro planeta), barrido diagonal de
// vidrio, viñeta perimetral, scanlines, grano fino + bisel de chasis con corner-brackets. TODO se
// hornea UNA vez a una juce::Image a resolución FÍSICA (lógico × escala) → nítido en Retina. El editor
// la blittea con la transformación inversa. Estático: CPU ~0 por frame.
struct Panel
{
    juce::Image img;
    int   rw = 0, rh = 0;
    float rs = 0.0f;

    bool needsRender (int w, int h, float scale) const
    {
        return img.isNull() || rw != w || rh != h || std::abs (rs - scale) > 0.01f;
    }

    // ruido de valor suave (mismo perfil que el mockup: NSEED[256] + smoothstep) → topográficas orgánicas
    static float vnoise (const float* seed, float x)
    {
        const int i = (int) std::floor (x);
        const float f = x - (float) i;
        const float u = f * f * (3.0f - 2.0f * f);
        const float a = seed[i & 255], b = seed[(i + 1) & 255];
        return a * (1.0f - u) + b * u;
    }

    void render (int w, int h, float scale)
    {
        if (w <= 0 || h <= 0) return;
        rw = w; rh = h; rs = juce::jmax (1.0f, scale);
        const int pw = juce::jmax (1, juce::roundToInt ((float) w * rs));
        const int ph = juce::jmax (1, juce::roundToInt ((float) h * rs));

        img = juce::Image (juce::Image::ARGB, pw, ph, true);
        juce::Graphics g (img);

        const float fw = (float) w, fh = (float) h;
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        // tabla de ruido determinística para las topográficas
        float seed[256];
        { juce::Random rng (1337); for (auto& s : seed) s = rng.nextFloat() * 2.0f - 1.0f; }

        {
            juce::Graphics::ScopedSaveState ss (g);
            g.addTransform (juce::AffineTransform::scale (rs));   // dibujo en coords lógicas, render físico

            // base negro casi puro
            g.setColour (juce::Colour (0xff05070b));
            g.fillAll();

            // luz fría direccional entrando desde arriba-izquierda (mockup: radial 150,-80 r=1140)
            {
                juce::ColourGradient light (juce::Colour (0xff7ea0c6).withAlpha (0.105f),
                                            fw * 0.156f, -fh * 0.138f,
                                            juce::Colour (0xff141c28).withAlpha (0.0f),
                                            fw * 0.156f, -fh * 0.138f + fh * 1.965f, true);
                light.addColour (0.34, juce::Colour (0xff6082aa).withAlpha (0.048f));
                light.addColour (0.68, juce::Colour (0xff28384e).withAlpha (0.012f));
                g.setGradientFill (light);
                g.fillAll();
            }

            // glow CIAN de familia (mockup #atmo .glow: radial rgba(94,231,240,0.075)) — el color
            // de ORBIT respira en el chasis, centrado alto sobre la bahía/cuerpo.
            {
                // misma geometría/alpha que el Panel de la familia (ui-kit): centro 0.5w/0.46h,
                // r = max(w,h)·0.52, alpha 0.090 con las mismas paradas → atmósfera idéntica al resto.
                const float gx = fw * 0.5f, gy = fh * 0.46f, gr = juce::jmax (fw, fh) * 0.52f;
                juce::ColourGradient glow (theme::cyan.withAlpha (0.090f), gx, gy,
                                           theme::cyan.withAlpha (0.0f), gx, gy - gr, true);
                glow.addColour (0.42, theme::cyan.withAlpha (0.034f));
                glow.addColour (0.74, theme::cyan.withAlpha (0.010f));
                g.setGradientFill (glow);
                g.fillAll();
            }

            // caída a negro casi puro abajo-derecha
            {
                juce::ColourGradient dark (juce::Colour (0xff030406).withAlpha (0.62f),
                                           fw * 0.95f, fh * 1.08f,
                                           juce::Colour (0xff030406).withAlpha (0.0f),
                                           fw * 0.95f, fh * 1.08f - fh * 1.69f, true);
                g.setGradientFill (dark);
                g.fillAll();
            }

            // topográficas orgánicas — anillos de "otro planeta" (alpha 0.018–0.032)
            auto topo = [&] (float cx, float cy, float baseR, int rings, float alpha, float squash)
            {
                for (int ri = 1; ri <= rings; ++ri)
                {
                    const float r0 = baseR * (float) ri / (float) rings;
                    juce::Path p;
                    const int SEG = 72;
                    for (int s = 0; s <= SEG; ++s)
                    {
                        const float th = (float) s / (float) SEG * twoPi;
                        const float rr = r0 * (1.0f
                            + 0.24f * vnoise (seed, std::cos (th) * 2.1f + (float) ri * 1.73f + cx * 0.011f)
                            + 0.15f * vnoise (seed, std::sin (th) * 3.3f + (float) ri * 0.91f + cy * 0.017f));
                        const float x = cx + std::cos (th) * rr;
                        const float y = cy + std::sin (th) * rr * squash;
                        if (s == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                    }
                    p.closeSubPath();
                    g.setColour (juce::Colour (0xff96bade).withAlpha (alpha));
                    g.strokePath (p, juce::PathStrokeType (1.0f));
                }
            };
            topo (fw * 0.77f, fh * 0.19f, fw * 0.31f, 6, 0.032f, 0.80f);
            topo (fw * 0.125f, fh * 0.86f, fw * 0.26f, 5, 0.026f, 0.86f);
            topo (fw * 0.52f, fh * 0.55f, fw * 0.625f, 7, 0.018f, 0.74f);

            // barrido diagonal de luz — reflejo de vidrio del panel
            {
                juce::ColourGradient sweep (juce::Colour (0xffc3deff).withAlpha (0.0f), 0.0f, 0.0f,
                                            juce::Colour (0xffc3deff).withAlpha (0.0f), fw, fh * 0.88f, false);
                sweep.addColour (0.45, juce::Colour (0xffc3deff).withAlpha (0.030f));
                sweep.addColour (0.52, juce::Colour (0xffc3deff).withAlpha (0.013f));
                g.setGradientFill (sweep);
                g.fillAll();
            }

            // viñeta perimetral
            {
                juce::ColourGradient vig (juce::Colour (0xff020305).withAlpha (0.0f), fw * 0.5f, fh * 0.5f,
                                          juce::Colour (0xff020305).withAlpha (0.52f),
                                          fw * 0.5f, fh * 0.5f - juce::jmax (fw, fh) * 0.70f, true);
                vig.addColour (0.7, juce::Colour (0xff020305).withAlpha (0.22f));
                g.setGradientFill (vig);
                g.fillAll();
            }

            // scanlines horizontales ultra-sutiles
            g.setColour (juce::Colours::black.withAlpha (0.028f));
            for (float y = 0.0f; y < fh; y += 3.0f)
                g.fillRect (0.0f, y, fw, 1.0f);
        }

        // grano fino: tile de ruido offscreen (en físico, overlay)
        {
            const int NT = 128;
            juce::Image nz (juce::Image::ARGB, NT, NT, false);
            { juce::Image::BitmapData bd (nz, juce::Image::BitmapData::writeOnly);
              juce::int64 s = 1337;
              for (int y = 0; y < NT; ++y)
                  for (int x = 0; x < NT; ++x)
                  {
                      s = (s * 16807) % 2147483647;
                      const auto v = (juce::uint8) (((double) s / 2147483647.0) * 255.0);
                      bd.setPixelColour (x, y, juce::Colour (v, v, v));
                  } }
            g.setTiledImageFill (nz, 0, 0, 0.045f);
            g.fillRect (0, 0, pw, ph);
        }
    }
};
}
