#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#if (MSVC)
#include "ipps.h"
#endif

#include "dsp/SpatialEngine.h"
#include "dsp/OrbitBrain.h"
#include "dsp/LowCut.h"
#include "dsp/HiCut.h"
#include "presets/PresetManager.h"
#include "presets/ABState.h"

class PluginProcessor : public juce::AudioProcessor,
                        private juce::AsyncUpdater
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Parámetros (IDs congelados — NO renombrar/eliminar; sólo agregar al final)
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // Posición en vivo para el visualizador (lock-free: audio escribe, UI lee).
    std::atomic<float> uiAzimuth { 0.0f };   // rad, 0 = frente, + = CCW (izquierda)
    std::atomic<bool>  uiActive  { false };  // hubo audio en el último bloque
    std::atomic<float> uiDistance { 0.6f };  // distancia instantanea 0..1 (fly-by; el visualizador acerca/aleja el punto)
    std::atomic<float> uiOutPeak  { 0.0f };  // pico de salida final (post output-gain) -> meter + LED de clip (M4)
    std::atomic<float> uiClip     { 0.0f };  // intensidad de "estás empujando" 0..1 (limiter reduciendo O pico cerca de 0 dBFS) -> LED de clip (M4)

    orbita::PresetManager& presets() { return presetManager; }   // M5: browser de presets (lo usa el editor)
    orbita::ABState&       ab()      { return abState; }         // M5: comparador A/B

private:
    orbita::SpatialEngine engine;
    orbita::OrbitBrain    brain;

    // LOW CUT (high-pass TPT) + HI CUT (low-pass TPT) de la SALIDA: filtran TODO lo que sale (dry+wet
    // ya espacializado), ANTES del output-gain. Default 0 → 20 Hz / 20 kHz → transparente (no cambia
    // presets). TPT modulación-safe (sin clicks), lineales (sin alias). Ver dsp/LowCut.h, dsp/HiCut.h.
    orbita::dsp::LowCut lowCut;
    orbita::dsp::HiCut  hiCut;
    orbita::PresetManager presetManager { apvts };   // M5: fábrica + user + navegación
    orbita::ABState       abState      { apvts };    // M5: comparador A/B

    // Program Change MIDI -> preset (diferido al message thread, RT-safe). NO exponemos programs al
    // host: hacerlo rompía la restauración de estado de los params Bool en VST3 (visto en pluginval).
    void handleAsyncUpdate() override;
    std::atomic<int> pendingProgram { -1 };
    float prevInGain  = 1.0f;   // suavizado por-bloque del Input gain (anti-zipper)
    float prevOutGain = 1.0f;   // suavizado por-bloque del Output gain (anti-zipper)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
