#include "RadarView.h"

namespace orbita
{
//==============================================================================
RadarView::RadarView (PluginProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    startTimerHz (30);
}
RadarView::~RadarView() { stopTimer(); }

//==============================================================================
// Lógica pura del PATH por forma (testeable). az en grados, 0 = frente, + = izquierda (CCW).
float RadarView::shapeAzDeg (int shape, float phase01) noexcept
{
    const float tau = juce::MathConstants<float>::twoPi;
    switch (shape)
    {
        case kPendulum:     return  kSwingDeg * std::sin (phase01 * tau);            // hamaca al frente
        case kPendulumBack: return 180.0f + kSwingDeg * std::sin (phase01 * tau);    // hamaca atrás
        case kSpiral:       return phase01 * 360.0f * 3.0f;                          // vórtice (3 vueltas)
        default:            return phase01 * 360.0f;                                 // Circle / Ellipse
    }
}
float RadarView::shapeRadius01 (int shape, float phase01) noexcept
{
    if (shape == kSpiral) return juce::jmap (phase01, 0.0f, 1.0f, 0.35f, 1.0f);      // se enrosca afuera
    return 1.0f;
}

//==============================================================================
// Geometría: centro del pozo + radio del campo + semiejes del PATH.
//   RADIUS  -> tamaño del semieje R (la órbita CRECE/ACHICA claramente).
//   SPREAD  -> achatamiento front-back de la elipse (ay < ax).
//   DOPPLER -> NO toca la geometría base: la excentricidad del fly-by viene de la distancia por azimut
//              (dopplerDist01, igual que el motor) → el ghost y el punto coinciden, con el centro FIJO.
RadarView::Geom RadarView::geom (int w, int h) const
{
    const float cx = (float) w * 0.5f, cy = (float) h * 0.5f;
    const float maxR = (float) juce::jmin (w, h) * 0.45f;   // campo más grande: usa mejor el alto disponible
    const int   shape    = (int) get ("orbShape");
    const float radius01 = get ("orbRadius") * 0.01f;
    const float spread01 = get ("orbSpread") * 0.01f;

    // RADIUS: rango CONTENIDO (0.30..0.82 del campo) → con ds≤1 + estela + bloom el conjunto queda
    // dentro de maxR (ver bug 3). Sigue siendo un rango amplio (la órbita crece/achica claramente).
    const float R   = juce::jmap (radius01, 0.0f, 1.0f, maxR * 0.30f, maxR * 0.82f);
    // SPREAD achata el eje front-back (sólo Ellipse). El Doppler NO entra acá: va por la distancia.
    const float eccS = (shape == kEllipse) ? juce::jlimit (0.0f, 0.62f, 0.34f + spread01 * 0.28f) : 0.0f;
    const float ay   = R * (1.0f - eccS);
    return { cx, cy, maxR, R, ay };
}

// Distancia con fly-by Doppler — MISMA fórmula que OrbitBrain (DopplerTuning por defecto: maxEcc01=0.55,
// curve=2): A = 0.55·doppler²; dist = base − A·cos(az). Cerca al frente (az=0), lejos atrás (az=π).
float RadarView::dopplerDist01 (float radiusBase01, float dopp01, float azRad) noexcept
{
    const float d = juce::jlimit (0.0f, 1.0f, dopp01);
    const float A = 0.55f * d;                           // = maxEcc01 · doppler^curve (curve=1 LINEAL, coincide con OrbitBrain)
    return juce::jlimit (0.0f, 1.0f, radiusBase01 - A * std::cos (azRad));
}

// Mapea (azimut, distancia 0..1) → punto del campo. La distancia escala el radio: cerca (0) ≈ 30%
// (pegado al oyente) · lejos (1) = al semieje. Centro FIJO en (cx,cy): el Doppler NUNCA traslada el centro.
juce::Point<float> RadarView::mapOrbit (float cx, float cy, float ax, float ay,
                                        float azRad, float dist01) noexcept
{
    const float ds = 0.30f + 0.70f * juce::jlimit (0.0f, 1.0f, dist01);
    return { cx - ax * ds * std::sin (azRad),
             cy - ay * ds * std::cos (azRad) };
}

juce::Point<float> RadarView::orbitPoint (const Geom& gm, float azRad, float dist01) const
{
    return mapOrbit (gm.cx, gm.cy, gm.ax, gm.ay, azRad, dist01);
}

//==============================================================================
void RadarView::resized()
{
    buildBloomSprites();
    lastShape = -1;             // fuerza rebuild de la capa estática en paint (con la escala física)
    bgLayer = juce::Image();
}

// Bloom: sprite de glow apilado en 3 capas + corona, horneado por bucket de proximidad. Halo exterior
// amplio + halo medio + glow interno denso → núcleo. Cerca = más grande/brillante (el punto nunca se apaga).
void RadarView::buildBloomSprites()
{
    constexpr int kSize = 200;          // lienzo amplio: el halo exterior entra completo
    constexpr float c = kSize * 0.5f;
    for (int b = 0; b <= kProxBuckets; ++b)
    {
        const float prox = (float) b / (float) kProxBuckets;
        const float haloR = juce::jmap (prox, 22.0f, 52.0f);
        const float bri   = juce::jmap (prox, 0.72f, 1.0f);
        const float coreR = juce::jmap (prox, 2.6f, 6.6f);

        auto& img = bloomSprites[b];
        img = juce::Image (juce::Image::ARGB, kSize, kSize, true);
        juce::Graphics g (img);

        struct Layer { float r, a0, aMid, midPos; };
        const Layer layers[] = {
            { haloR * 1.85f, 0.22f * bri, 0.075f * bri, 0.5f },  // halo exterior amplio
            { haloR * 1.05f, 0.60f * bri, 0.24f  * bri, 0.4f },  // halo medio (denso)
            { haloR * 0.46f, 0.85f * bri, 0.0f,         1.0f },  // glow interno denso
        };
        for (const auto& L : layers)
        {
            juce::ColourGradient gl (theme::cyan.withAlpha (L.a0), c, c,
                                     theme::cyan.withAlpha (0.0f), c, c - L.r, true);
            if (L.aMid > 0.0f) gl.addColour (L.midPos, theme::cyan.withAlpha (L.aMid));
            g.setGradientFill (gl);
            g.fillEllipse (c - L.r, c - L.r, L.r * 2.0f, L.r * 2.0f);
        }
        // corona blanca AMPLIA calentando el núcleo (el corazón del estallido)
        {
            const float r = coreR * 3.2f;
            juce::ColourGradient gl (juce::Colour (0xffeffcff).withAlpha (0.72f * bri), c, c,
                                     juce::Colour (0xffeffcff).withAlpha (0.0f), c, c - r, true);
            gl.addColour (0.42, juce::Colour (0xffd6f6ff).withAlpha (0.34f * bri));
            g.setGradientFill (gl);
            g.fillEllipse (c - r, c - r, r * 2.0f, r * 2.0f);
        }
    }
}

const juce::Image& RadarView::bloomSpriteFor (float prox)
{
    const int b = juce::jlimit (0, kProxBuckets,
                                juce::roundToInt (juce::jlimit (0.0f, 1.0f, prox) * (float) kProxBuckets));
    return bloomSprites[b];
}

//==============================================================================
// Capa estática "B" limpia: pozo oscuro con profundidad sutil + UN anillo + 4 marcas cardinales +
// PATH-ghost + personita CHICA. Nada de polvo / scanlines pesadas / anillos múltiples / labels.
void RadarView::renderStatic (int w, int h, float scale)
{
    if (w <= 0 || h <= 0) return;
    bgScale = juce::jmax (1.0f, scale);
    const int pw = juce::jmax (1, juce::roundToInt ((float) w * bgScale));
    const int ph = juce::jmax (1, juce::roundToInt ((float) h * bgScale));
    bgLayer = juce::Image (juce::Image::ARGB, pw, ph, true);
    juce::Graphics g (bgLayer);
    g.addTransform (juce::AffineTransform::scale (bgScale));   // dibujo lógico, render físico (nítido)

    const auto gm = geom (w, h);
    const float cx = gm.cx, cy = gm.cy, R = gm.maxR;
    const float fw = (float) w, fh = (float) h;
    const float pi = juce::MathConstants<float>::pi;

    // base: cubre TODA la sección (negro casi puro)
    g.setColour (juce::Colour (0xff020304));
    g.fillRect (0, 0, w, h);

    // pozo: degradé MUY oscuro centro→borde, profundidad sutil (alcanza las esquinas, sin viñeta dura)
    {
        const float reach = std::hypot (juce::jmax (cx, fw - cx), juce::jmax (cy, fh - cy)) * 1.02f;
        juce::ColourGradient well (juce::Colour (0xff0a1320), cx, cy,
                                   juce::Colour (0xff020304), cx, cy - reach, true);
        well.addColour (0.30, juce::Colour (0xff070e19));
        well.addColour (0.62, juce::Colour (0xff04080f));
        g.setGradientFill (well);
        g.fillRect (0, 0, w, h);
    }

    // ambiente cian MUY tenue del fondo del pozo (capa base estática; el latido lo agrega el vivo)
    {
        juce::ColourGradient core (theme::cyan.withAlpha (0.040f), cx, cy,
                                   theme::cyan.withAlpha (0.0f), cx, cy - R * 0.62f, true);
        g.setGradientFill (core);
        g.fillRect (0, 0, w, h);
    }

    // UN solo anillo de referencia (el borde exterior del campo, ~maxR), cian muy tenue
    g.setColour (theme::cyan.withAlpha (0.11f));
    g.drawEllipse (cx - R, cy - R, R * 2.0f, R * 2.0f, 1.2f);

    // 4 marcas cardinales cortas sobre el anillo (arriba = FRENTE, derecha, abajo, izquierda)
    {
        g.setColour (theme::cyan.withAlpha (0.30f));
        const float tickOut = R * 1.045f, tickIn = R * 0.93f;
        const float ang[4] = { -pi * 0.5f, 0.0f, pi * 0.5f, pi };   // arriba, der, abajo, izq
        for (float a : ang)
        {
            const float ca = std::cos (a), sa = std::sin (a);
            // la marca de arriba (FRENTE) un poco más larga/brillante para leerse como referencia
            const bool isFront = (std::abs (a + pi * 0.5f) < 0.01f);
            g.setColour (theme::cyan.withAlpha (isFront ? 0.42f : 0.30f));
            const float in = isFront ? R * 0.90f : tickIn;
            g.drawLine (cx + ca * in, cy + sa * in, cx + ca * tickOut, cy + sa * tickOut,
                        isFront ? 1.8f : 1.4f);
        }
    }

    // PATH-ghost de la órbita según forma (guía tenue: glow + hairline). Dibuja EXACTAMENTE la órbita que
    // recorre el punto vivo: mismo azimut por forma + MISMA distancia con fly-by Doppler (dopplerDist01) +
    // mismo mapeo (orbitPoint). Así el punto vivo cae SIEMPRE sobre el ghost (bug 2). Reacciona a
    // Shape/Radius/Spread/Doppler/Dir. HEIGHT no eleva el ghost: el plano es la referencia compartida y la
    // elevación la muestra el indicador vivo (tallo + fuente) sobre este plano.
    {
        const int   shape    = (int) get ("orbShape");
        const int   dir      = (int) get ("orbDir");
        const float dirS     = (dir == 0) ? 1.0f : -1.0f;
        const float radius01 = get ("orbRadius") * 0.01f;
        const float dopp01   = get ("doppler")   * 0.01f;
        const int   N = 220;
        // construye el ghost para una base de radio dada (mismo azimut por forma + misma distancia con
        // fly-by + mismo mapeo que el punto vivo → el punto cae sobre el ghost).
        auto buildPath = [&] (float base)
        {
            juce::Path path;
            for (int i = 0; i <= N; ++i)
            {
                const float phase = (float) i / (float) N;
                const float az    = juce::degreesToRadians (shapeAzDeg (shape, phase)) * dirS;
                const float dist  = dopplerDist01 (juce::jlimit (0.0f, 1.0f, base), dopp01, az);
                const auto  pt    = orbitPoint (gm, az, dist);
                if (i == 0) path.startNewSubPath (pt.x, pt.y); else path.lineTo (pt.x, pt.y);
            }
            return path;
        };
        auto drawGhost = [&] (const juce::Path& p, float glowA, float hairA)
        {
            g.setColour (theme::cyan.withAlpha (glowA));
            g.strokePath (p, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (theme::cyan.withAlpha (hairA));
            g.strokePath (p, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        if (shape == kSpiral)
        {
            // Spiral: el radio RESPIRA ±depth alrededor de radius01 (vórtice acoplado a la rotación). El
            // ghost es la BANDA por la que viaja la fuente: anillos de envolvente in/out + el central tenue.
            // El punto vivo recorre la banda de forma predecible (no se "sale" de un único anillo).
            constexpr float kSpiralDepth = 0.35f;   // = OrbitBrain ShapeTuning.spiralDepth01
            drawGhost (buildPath (radius01 - kSpiralDepth), 0.05f, 0.14f);
            drawGhost (buildPath (radius01 + kSpiralDepth), 0.05f, 0.14f);
            drawGhost (buildPath (radius01),                0.04f, 0.08f);
        }
        else
        {
            drawGhost (buildPath (radius01), 0.055f, 0.20f);
        }
    }

    // ===== personita (LISTENER) CHICA al centro, vista cenital, mirando al FRENTE (arriba) =====
    //   cabeza (círculo lleno) + hombros (arco suave debajo) + UNA flecha (chevron) hacia arriba.
    //   Sin cruz, sin cono, sin orbe, sin label — la personita se explica sola.
    {
        const juce::Colour ink  { 0xffd7f7fb };    // cian muy claro (casi blanco)
        const juce::Colour glow = theme::cyan;
        const float headR = 7.5f;                  // cabeza CHICA (~7-8px de radio)
        const float headCy = cy - 1.0f;            // la cabeza apenas sobre el centro

        // halo cian suave detrás → separa la personita del pozo sin ensuciar
        {
            const float hr = headR * 3.2f;
            juce::ColourGradient ring (glow.withAlpha (0.14f), cx, headCy,
                                       glow.withAlpha (0.0f), cx, headCy - hr, true);
            ring.addColour (0.55, glow.withAlpha (0.04f));
            g.setGradientFill (ring);
            g.fillEllipse (cx - hr, headCy - hr, hr * 2.0f, hr * 2.0f);
        }

        // hombros: arco suave debajo de la cabeza (vista desde arriba). Trazo grueso, suave.
        {
            const float shY = headCy + headR + 4.5f;
            const float shW = headR * 2.4f;          // semiancho de los hombros
            juce::Path shoulders;
            shoulders.addCentredArc (cx, shY, shW, headR * 1.7f, 0.0f,
                                     -pi * 0.62f, pi * 0.62f, true);   // arco hacia arriba (los hombros)
            g.setColour (glow.withAlpha (0.42f));
            g.strokePath (shoulders, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved,
                                                                juce::PathStrokeType::rounded));
        }

        // cabeza: círculo LLENO (relleno tenue + corazón claro + aro nítido)
        g.setColour (glow.withAlpha (0.30f));
        g.fillEllipse (cx - headR, headCy - headR, headR * 2.0f, headR * 2.0f);
        g.setColour (ink.withAlpha (0.95f));
        g.fillEllipse (cx - headR * 0.62f, headCy - headR * 0.62f, headR * 1.24f, headR * 1.24f);
        g.setColour (ink.withAlpha (0.85f));
        g.drawEllipse (cx - headR, headCy - headR, headR * 2.0f, headR * 2.0f, 1.3f);

        // UNA flecha (chevron limpio) hacia arriba = orientación al FRENTE. Sobre la cabeza, fuera del aro.
        {
            const float tipY  = headCy - headR - 7.5f;     // punta de la flecha
            const float baseY = headCy - headR - 0.5f;     // base de las alas
            const float halfW = 5.0f;
            juce::Path chevron;
            chevron.startNewSubPath (cx - halfW, baseY);
            chevron.lineTo (cx,        tipY);
            chevron.lineTo (cx + halfW, baseY);
            g.setColour (ink.withAlpha (0.95f));
            g.strokePath (chevron, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                              juce::PathStrokeType::rounded));
        }
    }

    // sincroniza los "last" para que el timer no rehornee de más
    lastShape  = (int) get ("orbShape");
    lastSpread = get ("orbSpread");
    lastRadius = get ("orbRadius");
    lastHeight = get ("orbHeight");
    lastDopp   = get ("doppler");
    lastDir    = (int) get ("orbDir");
}

//==============================================================================
void RadarView::paint (juce::Graphics& g)
{
    // capa estática a resolución física (nítida en Retina) — rehornea si cambió la escala/tamaño
    const float scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    const int   pw    = juce::roundToInt ((float) getWidth() * juce::jmax (1.0f, scale));
    if (bgLayer.isNull() || std::abs (bgScale - scale) > 0.01f || bgLayer.getWidth() != pw)
        renderStatic (getWidth(), getHeight(), scale);
    if (bgLayer.isValid())
        g.drawImageTransformed (bgLayer, juce::AffineTransform::scale (1.0f / bgScale));

    const auto gm = geom (getWidth(), getHeight());
    const float cx = gm.cx, cy = gm.cy, maxR = gm.maxR;

    // glow central que RESPIRA (latido muy suave) — la "profundidad al centro" del pozo, viva.
    {
        const float breath = 0.5f + 0.5f * std::sin (breathPhase);    // 0..1
        const float a  = 0.05f + 0.035f * breath;
        const float rr = maxR * (0.42f + 0.06f * breath);
        juce::ColourGradient bg (theme::cyan.withAlpha (a), cx, cy,
                                 theme::cyan.withAlpha (0.0f), cx, cy - rr, true);
        g.setGradientFill (bg);
        g.fillEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f);
    }

    // ROOM (Espacio, magenta): halo de ambiente que CRECE claro con el Room → se ve el espacio
    const float room01 = juce::jlimit (0.0f, 1.0f, get ("room") * 0.01f);
    if (room01 > 0.01f)
    {
        const float rr = maxR * (0.55f + 1.05f * room01);             // crece más con Room
        juce::ColourGradient amb (theme::magenta.withAlpha (0.06f + 0.18f * room01), cx, cy,
                                  juce::Colours::transparentBlack, cx, cy - rr, true);
        amb.addColour (0.58, theme::magenta.withAlpha (0.05f * room01));
        g.setGradientFill (amb);
        g.fillEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f);
    }

    // CHAOS: el caos ya viene en uiAzimuth/uiDistance (el motor lo aplica al azimut y, vía dist=f(az), al
    // radio). El punto/estela lo reflejan SOLOS y siguen sobre la órbita → nervioso pero predecible. No se
    // agrega jitter extra acá (eso sacaba el punto de la órbita y ensuciaba la estela).

    // BUG 3: el punto + la estela + bloom quedan SIEMPRE dentro del campo (maxR). Clip de la capa VIVA al
    // círculo del radar. El latido central y el halo de Room quedan FUERA del clip a propósito (atmósfera
    // suave centrada que se desvanece, no "palitos" que se salen). Se restaura al salir de paint().
    juce::Graphics::ScopedSaveState liveClip (g);
    {
        juce::Path field;
        field.addEllipse (cx - maxR, cy - maxR, maxR * 2.0f, maxR * 2.0f);
        g.reduceClipRegion (field);
    }

    // estela — cola de cometa LIMPIA que SIGUE EL ARCO de la órbita. Entre muestras consecutivas
    // interpola el azimut por el camino MÁS CORTO + la distancia, y subdivide en tramos cortos. Así, a
    // SPEED alto (hasta 8 Hz → ~90°/frame) la estela traza el arco real en vez de cuerdas rectas (que
    // dibujaban una "estrella"/maraña). A SPEED bajo el delta es chico → 1 tramo (igual que antes).
    // Fade suave pow(t,1.35), triple pasada source-over (acumula luz → brilla sin quedar "fina").
    {
        juce::Graphics::ScopedSaveState ss (g);
        const float pi = juce::MathConstants<float>::pi;
        const float tau = juce::MathConstants<float>::twoPi;
        auto wrapPi = [pi, tau] (float d) noexcept            // delta de azimut → (-π, π] (camino corto)
        {
            d = std::fmod (d + pi, tau);
            if (d < 0.0f) d += tau;
            return d - pi;
        };
        bool  have = false;
        float pAz = 0.0f, pDist = 0.0f, pT = 0.0f;            // muestra previa (más nueva)
        juce::Point<float> a;
        for (int i = 0; i < kTrail; ++i)
        {
            const int   idx  = (trailPos - 1 - i + kTrail * 2) % kTrail;
            const float az   = trail[idx];
            const float dist = trailDist[idx];
            const float t    = (float) (kTrail - i) / (float) kTrail;   // 1 = cabeza, 0 = cola
            if (! have) { a = orbitPoint (gm, az, dist); pAz = az; pDist = dist; pT = t; have = true; continue; }

            const float d = wrapPi (az - pAz);                          // arco corto desde la cabeza hacia la cola
            const int   M = juce::jlimit (1, 16, (int) std::ceil (std::abs (d) / 0.26f));  // ~15° por tramo
            for (int s = 1; s <= M; ++s)
            {
                const float u   = (float) s / (float) M;
                const auto  b   = orbitPoint (gm, pAz + d * u, pDist + (dist - pDist) * u);
                const float st  = pT + (t - pT) * u;
                const float fade = std::pow (st, 1.35f);
                g.setColour (theme::cyan.withAlpha (fade * 0.15f));               // 1) glow ancho (aura)
                g.drawLine ({ a, b }, juce::jmap (st, 2.2f, 7.0f));
                g.setColour (theme::cyan.withAlpha (fade * 0.40f));               // 2) halo medio
                g.drawLine ({ a, b }, juce::jmap (st, 1.0f, 3.6f));
                g.setColour (juce::Colour (0xffd2f8ff).withAlpha (fade * 0.85f)); // 3) núcleo brillante
                g.drawLine ({ a, b }, juce::jmap (st, 0.5f, 2.0f));
                a = b;
            }
            pAz = az; pDist = dist; pT = t;
        }
    }

    // punto sonoro: posición de la cabeza (azimut + distancia reales del motor) + wobble de Chaos
    const int   cur  = (trailPos - 1 + kTrail) % kTrail;
    const float dist = juce::jlimit (0.0f, 1.0f, trailDist[cur]);
    const auto  hp   = orbitPoint (gm, trail[cur], dist);

    // DOPPLER: fly-by más marcado. La proximidad base es 1-dist; con Doppler EXAGERAMOS el contraste
    // cerca/lejos → cerca del frente la fuente revienta (grande/brillante), atrás se apaga (chica/tenue).
    const float dopp01    = juce::jlimit (0.0f, 1.0f, get ("doppler") * 0.01f);
    const float proxBase  = juce::jlimit (0.0f, 1.0f, 1.0f - dist);
    const float proxGamma = std::pow (proxBase, 1.0f + dopp01 * 1.6f);   // gamma curva el contraste
    const float prox      = juce::jlimit (0.0f, 1.0f, proxBase + dopp01 * (proxGamma - proxBase));

    // HEIGHT: elevación visible — la fuente se DESPLAZA vertical (arriba/abajo) según el signo, con un
    // tallo claro desde el plano hasta la fuente elevada y un "fantasma" en el plano (la proyección).
    const float height = get ("orbHeight") * 0.01f;          // -1..+1
    const float hLiftLive = -height * maxR * 0.26f;          // desplazamiento marcado (era 0.16)
    const auto  src = juce::Point<float> (hp.x, hp.y + hLiftLive);
    if (std::abs (height) > 0.01f)
    {
        // fantasma en el plano (dónde estaría sin elevación): aro tenue → da referencia del "suelo"
        g.setColour (theme::cyan.withAlpha (0.18f));
        g.drawEllipse (hp.x - 4.5f, hp.y - 4.5f, 9.0f, 9.0f, 1.2f);
        g.setColour (theme::cyan.withAlpha (0.55f));
        g.fillEllipse (hp.x - 1.8f, hp.y - 1.8f, 3.6f, 3.6f);
        // tallo brillante desde el plano hasta la fuente elevada
        g.setColour (theme::cyan.withAlpha (0.45f));
        g.drawLine (hp.x, hp.y, src.x, src.y, 1.6f);
        // chevroncito de dirección de la elevación (arriba si +, abajo si -)
        const float dirY = (height > 0.0f) ? -1.0f : 1.0f;
        const float ay0 = src.y - dirY * 6.5f;
        juce::Path tip;
        tip.startNewSubPath (src.x - 3.4f, ay0 + dirY * 3.4f);
        tip.lineTo (src.x,        ay0);
        tip.lineTo (src.x + 3.4f, ay0 + dirY * 3.4f);
        g.setColour (theme::cyan.withAlpha (0.7f));
        g.strokePath (tip, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // WIDTH (estéreo): la fuente se ENSANCHA como una mancha HORIZONTAL — claramente más ancha con Width.
    // Dos pasadas: una mancha amplia tenue (la "imagen estéreo") + un núcleo horizontal más denso.
    const float width01 = juce::jlimit (0.0f, 1.0f, get ("width") * 0.01f);
    const auto& sprite = bloomSpriteFor (prox);
    if (sprite.isValid() && width01 > 0.02f)
    {
        auto smear = [&] (float wMul, float hMul, float op)
        {
            const float bw = maxR * (0.10f + 0.40f * width01) * wMul;   // ancho HORIZONTAL sobrio (contenido)
            const float bh = maxR * (0.09f + 0.02f * width01) * hMul;   // alto casi fijo → "estéreo"
            const float sx = (bw * 2.0f) / (float) sprite.getWidth();
            const float sy = (bh * 2.0f) / (float) sprite.getHeight();
            g.setOpacity (op);
            g.drawImageTransformed (sprite, juce::AffineTransform::scale (sx, sy)
                                                .translated (src.x - bw, src.y - bh));
        };
        smear (1.20f, 1.10f, 0.22f + 0.18f * width01);   // mancha amplia tenue
        smear (0.78f, 0.85f, 0.26f + 0.26f * width01);   // núcleo horizontal más denso
        g.setOpacity (1.0f);
    }

    // bloom apilado (sprite cacheado por proximidad) + núcleo blanco. Doble pasada source-over =
    // acumulación de luz tipo 'lighter' → el bloom ESTALLA. Doppler ya curvó 'prox' (fly-by).
    if (sprite.isValid())
    {
        const float haloR = juce::jmap (prox, 22.0f, 52.0f);
        const float outR  = haloR * 1.85f;
        const float bri   = juce::jmap (prox, 0.72f, 1.0f);
        const float scl   = (outR * 2.0f) / (float) sprite.getWidth();
        const auto  xf    = juce::AffineTransform::scale (scl).translated (src.x - outR, src.y - outR);
        g.setOpacity (bri);
        g.drawImageTransformed (sprite, xf);                 // 1ª pasada: el cuerpo del glow
        g.setOpacity (0.55f * bri);
        g.drawImageTransformed (sprite, xf);                 // 2ª pasada: suma luz al centro (burst)
        g.setOpacity (1.0f);

        // núcleo: corona caliente + punto blanco puro → el corazón sobreexpuesto
        const float coreR = juce::jmap (prox, 2.6f, 6.6f);
        juce::ColourGradient hot (juce::Colour (0xffffffff).withAlpha (0.85f * bri), src.x, src.y,
                                  juce::Colour (0xffeffcff).withAlpha (0.0f), src.x, src.y - coreR * 2.4f, true);
        g.setGradientFill (hot);
        g.fillEllipse (src.x - coreR * 2.4f, src.y - coreR * 2.4f, coreR * 4.8f, coreR * 4.8f);
        g.setColour (juce::Colours::white.withAlpha (0.98f * bri));
        g.fillEllipse (src.x - coreR, src.y - coreR, coreR * 2.0f, coreR * 2.0f);
    }
}

//==============================================================================
void RadarView::timerCallback()
{
    if (! isShowing()) return;                 // CPU: no animar con la ventana cerrada

    const float az = proc.uiAzimuth.load (std::memory_order_relaxed);
    const float ds = proc.uiDistance.load (std::memory_order_relaxed);
    trail[trailPos]     = az;
    trailDist[trailPos] = ds;
    trailPos = (trailPos + 1) % kTrail;

    // latido del glow central: avanza siempre (mantiene la "profundidad que respira")
    breathPhase += 0.12f;
    if (breathPhase > juce::MathConstants<float>::twoPi) breathPhase -= juce::MathConstants<float>::twoPi;

    // rehornear la capa estática sólo si cambió forma/spread/radius/height/doppler/dir (afectan el PATH/geom)
    const int   shape  = (int) get ("orbShape");
    const int   dir    = (int) get ("orbDir");
    const float spread = get ("orbSpread"), radius = get ("orbRadius"), heightP = get ("orbHeight");
    const float doppP  = get ("doppler");
    bool rebuilt = false;
    if (bgScale > 0.0f && (shape != lastShape || dir != lastDir
                           || std::abs (spread - lastSpread) > 0.5f
                           || std::abs (radius - lastRadius) > 0.5f
                           || std::abs (heightP - lastHeight) > 0.5f
                           || std::abs (doppP - lastDopp) > 0.5f))
    {
        renderStatic (getWidth(), getHeight(), bgScale);
        rebuilt = true;
    }

    // CPU: si nada se mueve ni cambia (incluido Doppler vivo), una vez asentada la estela el frame sólo
    // varía por el latido — lo dejamos correr a baja frecuencia (no fijamos para no congelar el respiro).
    const float room = get ("room"), width = get ("width");
    const bool moved = std::abs (az - lastAz) > 1.0e-4f || std::abs (ds - lastDist) > 1.0e-4f;
    const bool dynCh = std::abs (room - lastRoom) > 0.5f || std::abs (width - lastWidth) > 0.5f
                       || std::abs (doppP - lastDopLive) > 0.5f;
    lastAz = az; lastDist = ds; lastRoom = room; lastWidth = width; lastDopLive = doppP;
    settleFrames = (moved || dynCh || rebuilt) ? 0 : (settleFrames + 1);
    // aun "asentado", repintamos 1 de cada 4 frames para que el glow central respire (CPU mínima)
    if (settleFrames > kTrail + 2 && (settleFrames % 4 != 0)) return;

    repaint();
}

void RadarView::setPosFromMouse (const juce::MouseEvent& e)
{
    const float maxR = (float) juce::jmin (getWidth(), getHeight()) * 0.45f;   // = geom(): el borde del campo = radio 1.0
    const float dx = e.position.x - (float) getWidth() * 0.5f;
    const float dy = e.position.y - (float) getHeight() * 0.5f;
    const float r  = std::sqrt (dx * dx + dy * dy) / juce::jmax (1.0f, maxR);
    if (auto* p = proc.apvts.getParameter ("orbRadius"))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, r));
    // En modo Fijo (orbRate == 4) el arrastre también fija el ángulo.
    if (((int) get ("orbRate")) == 4)
        if (auto* pa = proc.apvts.getParameter ("orbFixedAz"))
        {
            const float azDeg = juce::jlimit (-180.0f, 180.0f, juce::radiansToDegrees (std::atan2 (-dx, -dy)));
            pa->setValueNotifyingHost (juce::jmap (azDeg, -180.0f, 180.0f, 0.0f, 1.0f));
        }
}
}
