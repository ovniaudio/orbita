// Unit-tests del sistema de presets de M5 (FactoryPresets, PresetManager, ABState, Program Change).
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "PluginProcessor.h"
#include "presets/FactoryPresets.h"
#include "presets/PresetManager.h"
#include "presets/ABState.h"

static float rawVal (PluginProcessor& p, const char* id)
{
    return p.apvts.getRawParameterValue (id)->load();
}

//==================================================================================================
// Task 1 — FactoryPresets
//==================================================================================================
TEST_CASE ("FactoryPresets: 40 presets, 15 Production + 25 Sound Design", "[preset]")
{
    const auto& all = orbita::factoryPresets();
    REQUIRE (all.size() == 40);
    int prod = 0, sd = 0;
    for (const auto& p : all)
    {
        REQUIRE (juce::String (p.name).isNotEmpty());
        (p.category == orbita::Category::Production) ? ++prod : ++sd;
    }
    REQUIRE (prod == 15);
    REQUIRE (sd  == 25);
}

TEST_CASE ("FactoryPresets: todos los IDs existen en el APVTS", "[preset]")
{
    PluginProcessor proc;
    for (const auto& preset : orbita::factoryPresets())
        for (const auto& pp : preset.params)
        {
            INFO ("preset=" << preset.name << " id=" << pp.id);
            REQUIRE (proc.apvts.getParameter (pp.id) != nullptr);
        }
}

//==================================================================================================
// Task 2 — PresetManager: aplicar fábrica + navegar
//==================================================================================================
TEST_CASE ("PresetManager: aplicar fábrica setea params y resetea el resto a default", "[preset]")
{
    PluginProcessor proc;
    orbita::PresetManager pm (proc.apvts);

    // ensuciar el estado
    proc.apvts.getParameter ("doppler")->setValueNotifyingHost (1.0f);
    proc.apvts.getParameter ("orbChaos")->setValueNotifyingHost (1.0f);

    // "Slow Drift" (index 0): doppler/chaos vuelven a 0 (default), radius a 58
    pm.applyFactory (0);
    REQUIRE (std::abs (rawVal (proc, "doppler"))        <  0.5f);
    REQUIRE (std::abs (rawVal (proc, "orbChaos"))       <  0.5f);
    REQUIRE (std::abs (rawVal (proc, "orbRadius") - 58.0f) < 0.5f);
    REQUIRE (pm.current().name == juce::String ("Slow Drift"));
}

TEST_CASE ("PresetManager: next/prev recorre y envuelve", "[preset]")
{
    PluginProcessor proc;
    orbita::PresetManager pm (proc.apvts);

    pm.applyFactory (0);
    pm.next();
    REQUIRE (pm.current().name == juce::String (orbita::factoryPresets()[1].name));

    pm.applyFactory (pm.numFactory() - 1);
    pm.next();   // wrap al primero
    REQUIRE (pm.current().name == juce::String (orbita::factoryPresets()[0].name));

    pm.prev();   // wrap al último
    REQUIRE (pm.current().name == juce::String (orbita::factoryPresets()[(size_t) pm.numFactory() - 1].name));
}

//==================================================================================================
// Task 3 — User presets (save/load/delete round-trip)
//==================================================================================================
TEST_CASE ("PresetManager: guardar/cargar/borrar User preset (round-trip)", "[preset]")
{
    PluginProcessor proc;
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("orbita_test_presets_ZZ");
    tmp.deleteRecursively();
    orbita::PresetManager pm (proc.apvts, tmp);

    // estado distintivo: width = 88
    auto* wp = proc.apvts.getParameter ("width");
    wp->setValueNotifyingHost (wp->convertTo0to1 (88.0f));
    const juce::String nm = "TestUserPreset_ZZ";
    pm.saveUser (nm);

    // cambiar el estado, recargar el user
    wp->setValueNotifyingHost (0.0f);
    pm.rescanUser();
    juce::File f = pm.userDir().getChildFile (nm + ".preset");
    REQUIRE (f.existsAsFile());
    pm.applyUserFile (f);
    REQUIRE (std::abs (rawVal (proc, "width") - 88.0f) < 0.5f);
    REQUIRE (pm.current().isUser);

    // limpiar
    pm.deleteUser (f);
    REQUIRE (! f.existsAsFile());
    tmp.deleteRecursively();
}

//==================================================================================================
// Task 4 — Guard de salud de los 40: ninguno clippea (peak <= 1.0) ni da NaN/Inf
//==================================================================================================
TEST_CASE ("presetsafe: los 40 presets no clippean ni dan NaN", "[presetsafe]")
{
    PluginProcessor proc;
    orbita::PresetManager pm (proc.apvts);
    const double SR = 48000.0;
    const int    N  = 512;
    proc.prepareToPlay (SR, N);

    for (int i = 0; i < pm.numFactory(); ++i)
    {
        pm.applyFactory (i);
        float globalPeak = 0.0f;
        bool  allFinite  = true;

        for (int blk = 0; blk < 200; ++blk)   // ~2.1 s: varias vueltas en los presets rápidos (los riesgosos)
        {
            juce::AudioBuffer<float> buf (2, N);
            juce::MidiBuffer midi;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int n = 0; n < N; ++n)
                    d[n] = 0.9f * std::sin (juce::MathConstants<float>::twoPi * 180.0f
                                            * (float) (blk * N + n) / (float) SR);
            }
            proc.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getReadPointer (ch);
                for (int n = 0; n < N; ++n)
                {
                    if (! std::isfinite (d[n])) allFinite = false;
                    globalPeak = juce::jmax (globalPeak, std::abs (d[n]));
                }
            }
        }
        INFO ("preset=" << orbita::factoryPresets()[(size_t) i].name << " peak=" << globalPeak);
        REQUIRE (allFinite);
        REQUIRE (globalPeak <= 1.0f);   // nunca pasa 0 dBFS (output default 0 dB; la cadena anti-clip protege)
    }
}

//==================================================================================================
// Task 5 — Program Change: 40 programs aplican el preset de fábrica
//==================================================================================================
TEST_CASE ("Program Change MIDI: carga el preset de fábrica", "[pc]")
{
    PluginProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    REQUIRE (proc.getNumPrograms() == 1);   // NO exponemos programs (rompía la restauración de estado en VST3)

    int abIdx = -1;
    const auto& all = orbita::factoryPresets();
    for (int i = 0; i < (int) all.size(); ++i)
        if (juce::String (all[(size_t) i].name) == "Abduction") abIdx = i;
    REQUIRE (abIdx >= 0);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::programChange (1, abIdx), 0);
    proc.processBlock (buf, midi);

    // el applyFactory se difiere al message thread -> bombear el loop
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    REQUIRE (std::abs (rawVal (proc, "doppler") - 85.0f) < 0.5f);
}

//==================================================================================================
// Task 8 — ABState: comparador A/B
//==================================================================================================
TEST_CASE ("ABState: toggle alterna estados sin perder datos", "[ab]")
{
    PluginProcessor proc;
    orbita::ABState ab (proc.apvts);
    auto* wp = proc.apvts.getParameter ("width");

    wp->setValueNotifyingHost (wp->convertTo0to1 (80.0f));
    ab.toggle();   // guarda A(width=80), pasa a B (estado inicial)
    wp->setValueNotifyingHost (wp->convertTo0to1 (20.0f));
    ab.toggle();   // guarda B(width=20), vuelve a A(width=80)

    REQUIRE (std::abs (rawVal (proc, "width") - 80.0f) < 0.5f);
    REQUIRE (ab.activeSlot() == 'A');

    ab.toggle();   // vuelve a B(width=20)
    REQUIRE (std::abs (rawVal (proc, "width") - 20.0f) < 0.5f);
    REQUIRE (ab.activeSlot() == 'B');
}

//==================================================================================================
// Task 9 — D-27: `Left Pocket` suena a la izquierda y `Right Pocket` a la derecha.
//
// Bug preexistente (v0.2.1 y anteriores), encontrado midiendo en la ronda 0.3 (informe 23 §1e):
// los dos presets estaban cruzados. La convención de ORBIT es explícita —`SpatialEngine.h:42`:
// "0 = frente, + = CCW / izquierda"— y el test [fidelity] de ITD la confirma end-to-end
// (ITD > 0 ⇒ llega antes al oído izquierdo ⇒ azimut > 0 es la izquierda). `Left Pocket` seteaba
// orbFixedAz = −45 (derecha). Se mide el BALANCE real de la salida, no el número del preset:
// así el test cae igual si alguien invierte la convención del motor en vez del preset.
//==================================================================================================
static float presetBalanceDb (const char* presetName)
{
    PluginProcessor proc;
    orbita::PresetManager pm (proc.apvts);

    int idx = -1;
    const auto& all = orbita::factoryPresets();
    for (int i = 0; i < (int) all.size(); ++i)
        if (juce::String (all[(size_t) i].name) == juce::String (presetName)) idx = i;
    REQUIRE (idx >= 0);
    pm.applyFactory (idx);

    const double SR = 48000.0;
    const int    N  = 512;
    proc.setPlayConfigDetails (2, 2, SR, N);
    proc.prepareToPlay (SR, N);

    juce::Random rng (20260907);          // semilla fija: el test es determinista
    const int warmupBlocks = 48;          // ~0.5 s de asentamiento (crossfades, reflexiones, limiter)
    const int measureBlocks = 96;         // ~1.0 s de medición
    double sumL = 0.0, sumR = 0.0;
    juce::int64 n = 0;

    juce::AudioBuffer<float> buf (2, N);
    juce::MidiBuffer midi;
    for (int blk = 0; blk < warmupBlocks + measureBlocks; ++blk)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < N; ++i) d[i] = 0.25f * (rng.nextFloat() * 2.0f - 1.0f);
        }
        // mismo ruido en los dos canales: ORBIT suma a mono antes de espacializar
        buf.copyFrom (1, 0, buf, 0, 0, N);
        proc.processBlock (buf, midi);

        if (blk < warmupBlocks) continue;
        const auto* l = buf.getReadPointer (0);
        const auto* r = buf.getReadPointer (1);
        for (int i = 0; i < N; ++i) { sumL += (double) l[i] * l[i]; sumR += (double) r[i] * r[i]; }
        n += N;
    }

    const double rmsL = std::sqrt (sumL / (double) n);
    const double rmsR = std::sqrt (sumR / (double) n);
    REQUIRE (rmsL > 1.0e-6);
    REQUIRE (rmsR > 1.0e-6);
    return (float) (20.0 * std::log10 (rmsL / rmsR));   // > 0 ⇒ izquierda más fuerte
}

TEST_CASE ("FactoryPresets: Left/Right Pocket ponen la fuente en el lado que dice el nombre", "[preset]")
{
    const float left  = presetBalanceDb ("Left Pocket");
    const float right = presetBalanceDb ("Right Pocket");
    INFO ("balance L/R en dB (positivo = izquierda): Left Pocket=" << left << "  Right Pocket=" << right);
    REQUIRE (left  >  3.0f);   // "Left Pocket"  ⇒ izquierda más fuerte
    REQUIRE (right < -3.0f);   // "Right Pocket" ⇒ derecha  más fuerte
}
