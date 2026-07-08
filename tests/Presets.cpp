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
