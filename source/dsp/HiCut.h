#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// =====================================================================================
// source/dsp/HiCut.h — LOW-PASS estéreo (HI CUT) para la SALIDA de ORBIT. Oscurece/suaviza TODO lo
// que sale (dry+wet ya espacializados) cortando los agudos — el par del LowCut. Va sobre el buffer
// de salida, ANTES del output-gain (ver PluginProcessor::processBlock).
//
// Mismo criterio que LowCut.h: TPT State-Variable (Zavalishin) = MODULACIÓN-SAFE (mover el cutoff no
// genera zipper). 12 dB/oct (Butterworth). Lineal → no genera alias. Perilla 0..1 INVERTIDA (más
// perilla = más oscuro): 0 = 20 kHz (off/transparente), 1 = 1.5 kHz (máx cut). Default = 0 → off →
// no cambia presets. El cutoff se clampea a 0.45·SR (seguro a cualquier sample-rate).
// =====================================================================================
namespace orbita::dsp
{

class HiCut
{
public:
    static constexpr float kMinHz = 1500.0f;    // máximo cut (más oscuro)
    static constexpr float kMaxHz = 20000.0f;   // off (transparente a 48 kHz)

    // Perilla 0..1 -> Hz (log, invertida). 0 = 20 kHz (off), 1 = 1.5 kHz (máx cut).
    static float hzFor01 (float x01) noexcept
    {
        x01 = juce::jlimit (0.0f, 1.0f, x01);
        return kMaxHz * std::pow (kMinHz / kMaxHz, x01);   // x=0 → 20k ; x=1 → 1.5k
    }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sr = (float) spec.sampleRate;
        filt.prepare (spec);
        filt.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        filt.setResonance (0.7071f);                       // Butterworth: sin pico
        smHz.reset (spec.sampleRate, 0.03);                // 30 ms de de-zipper del cutoff
        const float off = juce::jmin (kMaxHz, 0.45f * sr);
        smHz.setCurrentAndTargetValue (off);
        filt.setCutoffFrequency (off);
    }

    void reset() noexcept { filt.reset(); }

    void setCutoffHz (float hz) noexcept
    {
        smHz.setTargetValue (juce::jlimit (kMinHz, juce::jmin (kMaxHz, 0.45f * sr), hz));
    }

    void process (juce::AudioBuffer<float>& buf) noexcept
    {
        filt.setCutoffFrequency (smHz.skip (buf.getNumSamples()));
        juce::dsp::AudioBlock<float> block (buf);
        filt.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

private:
    juce::dsp::StateVariableTPTFilter<float> filt;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smHz;
    float sr = 48000.0f;
};

} // namespace orbita::dsp
