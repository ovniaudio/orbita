// Unit-tests de la UI (lógica pura testeable) + un snapshot del editor a PNG para verificación
// visual headless (sin ventana ni permisos de Screen Recording). El snapshot lo usamos en M4 para
// comparar contra el mockup; la lógica de paths/labels lleva aserciones de verdad.
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/RadarView.h"

// --- lógica pura del PATH del radar por forma ---------------------------------------------------
TEST_CASE ("RadarView: el path respeta la forma", "[ui]")
{
    using R = orbita::RadarView;
    REQUIRE (R::shapeIsPendulumFront (R::kPendulum));
    REQUIRE (R::shapeIsPendulumBack  (R::kPendulumBack));
    REQUIRE_FALSE (R::shapeIsPendulumFront (R::kCircle));

    // Circle: az = fase·360
    REQUIRE (std::abs (R::shapeAzDeg (R::kCircle, 0.25f) -  90.0f) < 0.01f);
    REQUIRE (std::abs (R::shapeAzDeg (R::kCircle, 0.50f) - 180.0f) < 0.01f);

    // Pendulum (front): |az| nunca supera el swing (hamaca al frente)
    for (float ph = 0.0f; ph <= 1.0f; ph += 0.05f)
        REQUIRE (std::abs (R::shapeAzDeg (R::kPendulum, ph)) <= R::kSwingDeg + 0.01f);

    // Pendulum Back: az ronda 180 ± swing (siempre por detrás)
    for (float ph = 0.0f; ph <= 1.0f; ph += 0.05f)
    {
        const float a = R::shapeAzDeg (R::kPendulumBack, ph);
        REQUIRE (a >= 180.0f - R::kSwingDeg - 0.01f);
        REQUIRE (a <= 180.0f + R::kSwingDeg + 0.01f);
    }

    // Spiral: el radio "respira" (de adentro hacia afuera); Circle = radio constante
    REQUIRE (R::shapeRadius01 (R::kSpiral, 0.0f) < R::shapeRadius01 (R::kSpiral, 1.0f));
    REQUIRE (std::abs (R::shapeRadius01 (R::kCircle, 0.3f) - 1.0f) < 0.01f);
}

// --- geometría reconciliada del radar (los 3 bugs del radar nuevo) ------------------------------
//  BUG 1: Doppler = fly-by adelante-atrás SIN traslación vertical de la órbita.
//  BUG 2: el punto vivo usa el MISMO mapeo+distancia que el ghost → cae sobre la órbita dibujada.
//  BUG 3: el punto queda contenido dentro de los semiejes (y, con R≤maxR·0.82, dentro de maxR).
TEST_CASE ("RadarView: geometría reconciliada (fly-by sin traslación, punto sobre la órbita)", "[ui]")
{
    using R = orbita::RadarView;
    const float pi = juce::MathConstants<float>::pi;

    // BUG 1 — dopplerDist01: doppler=0 → distancia constante (círculo): frente == atrás == base.
    REQUIRE (std::abs (R::dopplerDist01 (0.60f, 0.0f, 0.0f) - 0.60f) < 1e-5f);
    REQUIRE (std::abs (R::dopplerDist01 (0.60f, 0.0f, pi)  - 0.60f) < 1e-5f);
    // doppler>0 → más CERCA al frente (az=0) y más LEJOS atrás (az=π) que la base (fly-by real).
    REQUIRE (R::dopplerDist01 (0.60f, 1.0f, 0.0f) < 0.60f - 0.2f);   // frente se acerca claramente
    REQUIRE (R::dopplerDist01 (0.60f, 1.0f, pi)  > 0.60f);           // atrás se aleja
    // los costados (az=±90°, cos=0) NO cambian con doppler → la órbita no se corre de lado.
    REQUIRE (std::abs (R::dopplerDist01 (0.60f, 1.0f,  pi * 0.5f) - 0.60f) < 1e-5f);
    REQUIRE (std::abs (R::dopplerDist01 (0.60f, 1.0f, -pi * 0.5f) - 0.60f) < 1e-5f);

    // BUG 1 (mapeo) — centro de la órbita FIJO en el oyente: con cualquier doppler, los costados (az=±90°)
    // se mantienen sobre la horizontal del centro (y == cy) y simétricos en x. El bug viejo (doppFront) los
    // subía a TODOS (y < cy) → traslación de toda la órbita hacia arriba.
    const float cx = 200.0f, cy = 150.0f, ax = 100.0f, ay = 80.0f;
    for (float dp : { 0.0f, 0.5f, 1.0f })
    {
        const float dR = R::dopplerDist01 (0.60f, dp, pi * 0.5f);
        const auto  pR = R::mapOrbit (cx, cy, ax, ay,  pi * 0.5f, dR);
        const auto  pL = R::mapOrbit (cx, cy, ax, ay, -pi * 0.5f, dR);
        REQUIRE (std::abs (pR.y - cy) < 1e-3f);                       // costado derecho sobre la horizontal
        REQUIRE (std::abs (pL.y - cy) < 1e-3f);                       // costado izquierdo idem
        REQUIRE (std::abs ((pR.x - cx) + (pL.x - cx)) < 1e-3f);       // sin corrimiento lateral del centro
    }
    // frente (az=0) ARRIBA del centro, atrás (az=π) ABAJO — orientación correcta.
    REQUIRE (R::mapOrbit (cx, cy, ax, ay, 0.0f, 0.6f).y < cy);
    REQUIRE (R::mapOrbit (cx, cy, ax, ay, pi,   0.6f).y > cy);

    // BUG 2 — el ghost a doppler=0 se dibuja a ds = 0.30 + 0.70·radius01 (= donde está el punto vivo), NO
    // al borde del semieje. El bug viejo dibujaba el ghost a ds=1.0 (radio = ay) con el punto adentro (0.72).
    {
        const float radius01 = 0.60f;
        const float dsExpected = 0.30f + 0.70f * radius01;           // 0.72
        const float distGhost  = R::dopplerDist01 (radius01, 0.0f, 0.0f);   // = radius01
        const auto  pTop = R::mapOrbit (cx, cy, ax, ay, 0.0f, distGhost);
        REQUIRE (std::abs ((cy - pTop.y) - ay * dsExpected) < 1e-3f);       // ghost al 0.72·ay, no al borde
        REQUIRE ((cy - pTop.y) < ay - 1e-2f);                              // estrictamente dentro del semieje
    }

    // BUG 3 — contención: para cualquier (az, dist∈[0,1]) el punto cae dentro de los semiejes (|Δ|≤ax,ay).
    // Con R≤maxR·0.82 en geom(), esto garantiza punto (y núcleo) dentro de maxR; el clip recorta el bloom.
    for (float dp : { 0.0f, 1.0f })
        for (int k = 0; k <= 16; ++k)
        {
            const float az = juce::degreesToRadians ((float) k * 22.5f);
            const float d  = R::dopplerDist01 (1.0f, dp, az);        // radius al máximo
            const auto  p  = R::mapOrbit (cx, cy, ax, ay, az, d);
            REQUIRE (std::abs (p.x - cx) <= ax + 1e-3f);
            REQUIRE (std::abs (p.y - cy) <= ay + 1e-3f);
        }
}

// --- choices en inglés (IDs congelados; sólo el display) ----------------------------------------
TEST_CASE ("Choices: salida y rate en inglés", "[ui]")
{
    PluginProcessor proc;
    auto* outMode = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("outMode"));
    auto* orbRate = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("orbRate"));
    REQUIRE (outMode != nullptr);
    REQUIRE (orbRate != nullptr);
    REQUIRE (outMode->choices == juce::StringArray { "Phones", "Speakers" });
    REQUIRE (orbRate->choices.contains ("Fixed"));
    REQUIRE_FALSE (orbRate->choices.contains ("Fijo"));
}

// --- LED de clip (uiClip): apagado en reposo, encendido empujando --------------------------------
TEST_CASE ("LED de clip: uiClip reacciona al nivel", "[ui]")
{
    PluginProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto runBlocks = [&] (float amp, int blocks)
    {
        for (int k = 0; k < blocks; ++k)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int i = 0; i < 512; ++i)
                    d[i] = amp * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / 48000.0f);
            }
            proc.processBlock (buf, midi);
        }
    };

    // Reposo: silencio -> el limiter se recupera y no hay pico -> LED apagado.
    runBlocks (0.0f, 8);
    REQUIRE (proc.uiClip.load() < 0.05f);

    // Empujando: señal fuerte + OUTPUT al tope (+24 dB) -> clip de salida -> LED encendido.
    proc.apvts.getParameter ("output")->setValueNotifyingHost (1.0f);
    runBlocks (0.95f, 8);
    REQUIRE (proc.uiClip.load() > 0.5f);

    // Y vuelve a apagarse al soltar (silencio + OUTPUT a 0 dB).
    proc.apvts.getParameter ("output")->setValueNotifyingHost (0.5f); // 0 dB (centro de -24..+24)
    runBlocks (0.0f, 20);
    REQUIRE (proc.uiClip.load() < 0.05f);
}

// --- snapshot del editor a /tmp/orbita_ui.png (2x). Correr: ./build/Tests "[snap]" --------------
TEST_CASE ("UI: snapshot del editor a PNG", "[snap]")
{
    PluginProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 2.0f);
    REQUIRE (img.isValid());

    juce::File out ("/tmp/orbita_ui.png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
}

// Variantes para verificar que el radar REACCIONA a Width/Room. ./build/Tests "[snapvar]"
TEST_CASE ("UI: snapshot variantes (reactividad Width/Room)", "[snapvar]")
{
    auto render = [] (float width01, float room01, const char* path)
    {
        PluginProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        proc.apvts.getParameter ("width")->setValueNotifyingHost (width01);
        proc.apvts.getParameter ("room")->setValueNotifyingHost (room01);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 1.5f);
        juce::File out (path); out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat png; png.writeImageToStream (img, os);
    };
    render (0.0f, 0.0f,  "/tmp/orbita_lo.png");   // estéreo 0, room 0
    render (1.0f, 0.85f, "/tmp/orbita_hi.png");   // estéreo máx, room alto
    SUCCEED();
}

// Snapshot con el LED de clip ENCENDIDO (uiClip forzado). ./build/Tests "[snapled]" -> /tmp/orbita_led.png
TEST_CASE ("UI: snapshot con LED de clip encendido", "[snapled]")
{
    PluginProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.uiClip.store (0.9f);      // clip fuerte -> LED rojo
    proc.uiOutPeak.store (0.97f);  // pico coherente

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 2.0f);
    REQUIRE (img.isValid());
    juce::File out ("/tmp/orbita_led.png"); out.deleteFile();
    juce::FileOutputStream os (out);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
}

// Snapshot del editor en los 3 tamaños S/M/L (resize de 3 tamaños). ./build/Tests "[snapzoom]"
//   -> /tmp/orbita_zoom_{S,M,L}.png — el diseño se ve idéntico, sólo escalado.
TEST_CASE ("UI: snapshot S/M/L (resize de 3 tamaños)", "[snapzoom]")
{
    PluginProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* pe = dynamic_cast<PluginEditor*> (ed.get());
    REQUIRE (pe != nullptr);

    struct Case { float z; const char* path; int w; int h; };
    const Case cases[] = {
        { 0.8f,  "/tmp/orbita_zoom_S.png", 768,  464 },
        { 1.0f,  "/tmp/orbita_zoom_M.png", 960,  580 },
        { 1.25f, "/tmp/orbita_zoom_L.png", 1200, 725 },
    };
    for (auto& c : cases)
    {
        pe->applyZoom (c.z);
        REQUIRE (pe->getWidth()  == c.w);   // el editor toma exactamente el tamaño zoomeado (enteros)
        REQUIRE (pe->getHeight() == c.h);
        auto img = pe->createComponentSnapshot (pe->getLocalBounds(), false, 2.0f);
        REQUIRE (img.isValid());
        juce::File out (c.path); out.deleteFile();
        juce::FileOutputStream os (out);
        REQUIRE (os.openedOk());
        juce::PNGImageFormat png;
        REQUIRE (png.writeImageToStream (img, os));
    }
}

// Snapshot del browser de presets con un preset cargado. ./build/Tests "[snapbrowser]" -> /tmp/orbita_browser.png
TEST_CASE ("UI: snapshot del browser con un preset cargado", "[snapbrowser]")
{
    PluginProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.presets().applyFactory (15);   // carga "Abduction" -> el header lo muestra

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 2.0f);
    REQUIRE (img.isValid());
    juce::File out ("/tmp/orbita_browser.png"); out.deleteFile();
    juce::FileOutputStream os (out);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
}

// [shot4k][ui] — CAPTURA REAL 4x (3840x2320) del editor REAL de ORBIT en tamano M, estado VIVO: preset
// "Abduction" (orbita marcada, width/room evocativos) + interleave processBlock con dbgPump -> el punto
// SONORO recorre la orbita y deja una estela REAL antes del PNG (el timer no corre headless). Fuente
// unica de la foto de web/ficha (ORBIT en ingles). -> ovni/docs/redesign/real-shots/orbit.png
TEST_CASE ("shot4k: ORBIT editor real 4x -> real-shots/orbit.png", "[shot4k][ui]")
{
    PluginProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.presets().applyFactory (15);   // "Abduction": shape orbital, width 75, room 45, radius 35

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* pe = dynamic_cast<PluginEditor*> (ed.get());
    REQUIRE (pe != nullptr);
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());   // M base (960x580)

    int sampleN = 0;
    for (int blk = 0; blk < 90; ++blk)   // ~1 s: el LFO de la orbita barre un arco amplio -> estela rica
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int n = 0; n < 512; ++n)
                d[n] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (sampleN + n) / 48000.0f);
        }
        sampleN += 512;
        proc.processBlock (buf, midi);
        pe->dbgPump (1);   // un frame por bloque -> el punto avanza el mismo paso que el timer real
    }

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 4.0f);   // 4x (real 4K)
    REQUIRE (img.isValid());

    auto out2 = juce::File ("/Users/musik/PLUGINS/ovni/docs/redesign/real-shots/orbit.png");
    out2.getParentDirectory().createDirectory();
    out2.deleteFile();
    juce::FileOutputStream os2 (out2);
    REQUIRE (os2.openedOk());
    juce::PNGImageFormat png2;
    REQUIRE (png2.writeImageToStream (img, os2));
    os2.flush();
    REQUIRE (out2.getSize() > 0);
}

// [shotvar][ui] — DEMOSTRACIÓN de reactividad a los knobs del RadarView nuevo. Renderiza pares
// comparativos (knob bajo vs alto) del editor REAL a /tmp, con el punto sonoro recorriendo la órbita
// (mismo interleave processBlock+dbgPump que shot4k) → la diferencia se ve en el radar. Correr:
//   ./build/Tests "[shotvar]"   →  /tmp/orbita_var_*.png
TEST_CASE ("shotvar: reactividad del radar a los knobs", "[shotvar][ui]")
{
    // setea params (0..100 normalizado por el rango del propio param) + Free para que la órbita barra,
    // corre ~1 s de audio empujando la estela, y captura el editor real a 2.5x.
    auto render = [] (std::initializer_list<std::pair<const char*, float>> params, const char* path)
    {
        PluginProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        // base evocativa: Ellipse + Free (la fuente recorre un arco amplio)
        proc.apvts.getParameter ("orbShape")->setValueNotifyingHost (1.0f / 4.0f);   // Ellipse (idx 1 de 5)
        proc.apvts.getParameter ("orbRate") ->setValueNotifyingHost (3.0f / 4.0f);   // Free (idx 3 de 5)
        for (auto& kv : params)
            if (auto* p = proc.apvts.getParameter (kv.first))
                p->setValueNotifyingHost (kv.second);

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* pe = dynamic_cast<PluginEditor*> (ed.get());
        ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());

        int sampleN = 0;
        for (int blk = 0; blk < 90; ++blk)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int n = 0; n < 512; ++n)
                    d[n] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f
                                            * (float) (sampleN + n) / 48000.0f);
            }
            sampleN += 512;
            proc.processBlock (buf, midi);
            pe->dbgPump (1);
        }

        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 2.5f);
        juce::File out (path); out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat png; png.writeImageToStream (img, os);
        return img.isValid();
    };

    // RADIUS: órbita chica vs grande
    REQUIRE (render ({ {"orbRadius", 0.20f} }, "/tmp/orbita_var_radius_lo.png"));
    REQUIRE (render ({ {"orbRadius", 1.00f} }, "/tmp/orbita_var_radius_hi.png"));
    // ROOM (magenta): sin ambiente vs ambiente amplio
    REQUIRE (render ({ {"room", 0.00f} }, "/tmp/orbita_var_room_lo.png"));
    REQUIRE (render ({ {"room", 1.00f} }, "/tmp/orbita_var_room_hi.png"));
    // DOPPLER: elipse normal vs fly-by excéntrico + contraste cerca/lejos
    REQUIRE (render ({ {"doppler", 0.00f} }, "/tmp/orbita_var_doppler_lo.png"));
    REQUIRE (render ({ {"doppler", 1.00f} }, "/tmp/orbita_var_doppler_hi.png"));
    // CHAOS: trayectoria limpia vs nerviosa
    REQUIRE (render ({ {"orbChaos", 0.00f} }, "/tmp/orbita_var_chaos_lo.png"));
    REQUIRE (render ({ {"orbChaos", 1.00f} }, "/tmp/orbita_var_chaos_hi.png"));
    // WIDTH: fuente angosta vs ancha
    REQUIRE (render ({ {"width", 0.00f} }, "/tmp/orbita_var_width_lo.png"));
    REQUIRE (render ({ {"width", 1.00f} }, "/tmp/orbita_var_width_hi.png"));
    // HEIGHT (bipolar): abajo vs arriba
    REQUIRE (render ({ {"orbHeight", 0.10f} }, "/tmp/orbita_var_height_lo.png"));
    REQUIRE (render ({ {"orbHeight", 0.90f} }, "/tmp/orbita_var_height_hi.png"));
}
