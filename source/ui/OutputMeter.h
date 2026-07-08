#pragma once
#include "../PluginProcessor.h"
#include "Theme.h"
#include "Fonts.h"

namespace orbita
{
// Readout + meter de salida (spec §7, mockup): "OUTPUT" + dB + barra horizontal cian→ámbar→rojo
// con ticks + footer perf. Lee el atomic uiOutPeak (pico final). El LED de clip se agrega en Task 8.
class OutputMeter : public juce::Component, private juce::Timer
{
public:
    explicit OutputMeter (PluginProcessor& p);
    ~OutputMeter() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    PluginProcessor& proc;
    float peakSm = 0.0f;     // pico suavizado (ataque inmediato, caída lenta)
    float clipSm = 0.0f;     // LED de clip suavizado (destella y cae suave -> visible)
    float lastDrawnPeak = -1.0f, lastDrawnClip = -1.0f;  // CPU: no repintar si nada cambió (silencio)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputMeter)
};
}
