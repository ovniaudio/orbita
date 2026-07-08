#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// =====================================================================================
// source/dsp/LowCut.h — HIGH-PASS estéreo para la SALIDA de ORBIT. Saca los graves de TODO lo
// que sale (dry+wet ya espacializados) → limpia el lodo del bajo sin tocar la imagen binaural.
// Va sobre el buffer de salida, ANTES del output-gain (ver PluginProcessor::processBlock).
//
// TPT State-Variable (Zavalishin) = MODULACIÓN-SAFE: cambiar el cutoff en vivo NO genera zipper ni
// transitorios (la razón de existir de la topología TPT). 12 dB/oct (Butterworth, sin pico). Rango
// 20 Hz (≈apagado) .. 500 Hz, logarítmico. Default = 20 Hz → transparente, NO cambia presets.
// Lineal → no genera alias. Gemelo del par de los plugins del sello OVNI (shared/dsp/LowCut.h).
// =====================================================================================
namespace orbita::dsp
{

class LowCut
{
public:
    static constexpr float kMinHz = 20.0f;    // piso = prácticamente apagado (solo quita subsónico/DC)
    static constexpr float kMaxHz = 500.0f;   // techo útil para limpiar los graves

    // Mapa de la perilla 0..1 -> Hz (logarítmico, octava-uniforme). 0 = 20 Hz (off), 1 = 500 Hz.
    static float hzFor01 (float x01) noexcept
    {
        x01 = juce::jlimit (0.0f, 1.0f, x01);
        return kMinHz * std::pow (kMaxHz / kMinHz, x01);
    }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        filt.prepare (spec);
        filt.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        filt.setResonance (0.7071f);                       // Butterworth: máxima planicie, sin pico
        smHz.reset (spec.sampleRate, 0.03);                // 30 ms de de-zipper del cutoff
        smHz.setCurrentAndTargetValue (kMinHz);
        filt.setCutoffFrequency (kMinHz);
    }

    void reset() noexcept { filt.reset(); }

    void setCutoffHz (float hz) noexcept { smHz.setTargetValue (juce::jlimit (kMinHz, kMaxHz, hz)); }

    // Filtra in-place (estéreo). Suaviza el cutoff por bloque; el TPT no clickea al modular.
    void process (juce::AudioBuffer<float>& buf) noexcept
    {
        filt.setCutoffFrequency (smHz.skip (buf.getNumSamples()));
        juce::dsp::AudioBlock<float> block (buf);
        filt.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

private:
    juce::dsp::StateVariableTPTFilter<float> filt;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smHz;
};

} // namespace orbita::dsp
