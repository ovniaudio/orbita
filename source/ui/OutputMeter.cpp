#include "OutputMeter.h"

namespace orbita
{
// LED de clip: apagado = puntito gris; encendido = ámbar (empujando) -> rojo (clip), con glow.
static void drawClipLed (juce::Graphics& g, juce::Rectangle<float> cell, float clip)
{
    const float d = 7.0f;
    const float on = juce::jlimit (0.0f, 1.0f, clip);
    juce::Rectangle<float> led (0.0f, 0.0f, d, d);
    led.setCentre (cell.getCentre());

    const juce::Colour onCol = (on < 0.5f) ? theme::amber
                                           : theme::amber.interpolatedWith (theme::red, (on - 0.5f) * 2.0f);
    if (on > 0.02f)
    {
        g.setColour (onCol.withAlpha (0.35f * on));
        g.fillEllipse (led.expanded (3.5f * on));
    }
    g.setColour (juce::Colour (0xff20262e).interpolatedWith (onCol, juce::jlimit (0.0f, 1.0f, on * 2.0f)));
    g.fillEllipse (led);
    g.setColour (theme::line.withAlpha (0.6f));
    g.drawEllipse (led, 0.7f);
}

OutputMeter::OutputMeter (PluginProcessor& p) : proc (p) { startTimerHz (30); }
OutputMeter::~OutputMeter() { stopTimer(); }

void OutputMeter::timerCallback()
{
    if (! isShowing()) return;
    const float pk = proc.uiOutPeak.load (std::memory_order_relaxed);
    peakSm = juce::jmax (pk, peakSm * 0.82f);   // attack inmediato, release suave
    const float cl = proc.uiClip.load (std::memory_order_relaxed);
    clipSm = juce::jmax (cl, clipSm * 0.90f);   // el LED destella y cae suave
    // CPU: sólo repintar si el meter o el LED cambiaron (en silencio sostenido no repinta).
    if (std::abs (peakSm - lastDrawnPeak) < 1.0e-4f && std::abs (clipSm - lastDrawnClip) < 1.0e-4f)
        return;
    lastDrawnPeak = peakSm; lastDrawnClip = clipSm;
    repaint();
}

// Bottom bar (mockup #bbar): franja fina de estado — dot + "MOV·01" + peak meter inline + LED de clip
// + sig "TRANSMISIÓN ESTABLE". El meter lee uiOutPeak; el LED lee uiClip (pico final post-output).
void OutputMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    // fondo del bbar (mockup: gradiente sutil + filo superior)
    juce::ColourGradient bg (juce::Colour (0xff0a0d12).withAlpha (0.6f), 0.0f, b.getY(),
                             juce::Colours::transparentBlack, 0.0f, b.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRect (b);
    g.setColour (theme::line.withAlpha (0.5f));
    g.drawHorizontalLine ((int) b.getY(), b.getX(), b.getRight());

    auto row = b.reduced (12.0f, 0.0f);

    const float peakDb = juce::Decibels::gainToDecibels (juce::jmax (1.0e-4f, peakSm));
    const float clip   = juce::jmax (clipSm, proc.uiClip.load (std::memory_order_relaxed));
    const bool  live   = peakSm > 1.0e-3f;

    // dot de estado (vivo = cian)
    auto dotCell = row.removeFromLeft (8.0f);
    g.setColour (live ? theme::cyan : theme::fnt.withAlpha (0.5f));
    g.fillEllipse (dotCell.withSizeKeepingCentre (5.0f, 5.0f));
    row.removeFromLeft (7.0f);

    // designación
    g.setColour (live ? theme::mut : theme::fnt);
    g.setFont (fonts::mono (9.0f).withExtraKerningFactor (0.12f));
    g.drawText (juce::String::fromUTF8 ("MOV\xc2\xb7""01"), row.removeFromLeft (44.0f).toNearestInt(),
                juce::Justification::centredLeft);

    // sig a la derecha
    g.setColour (theme::fnt.withAlpha (0.55f));
    g.setFont (fonts::mono (9.0f).withExtraKerningFactor (0.2f));
    g.drawText (juce::String ("SIGNAL STABLE"),
                row.removeFromRight (150.0f).toNearestInt(), juce::Justification::centredRight);

    // LED de clip + dB readout justo antes del sig
    row.removeFromRight (8.0f);
    g.setColour ((peakDb > -1.0f) ? theme::red : theme::mut);
    g.setFont (fonts::mono (9.0f));
    const juce::String val = live ? (juce::String (peakDb, 1) + " dB") : juce::String ("-inf");
    g.drawText (val, row.removeFromRight (58.0f).toNearestInt(), juce::Justification::centredRight);
    drawClipLed (g, row.removeFromRight (16.0f), clip);

    // peak meter inline (lo que queda en el medio)
    row.removeFromLeft (8.0f);
    auto bar = row.reduced (0.0f, row.getHeight() * 0.5f - 3.0f);
    if (bar.getWidth() > 20.0f)
    {
        g.setColour (juce::Colour (0xff0a0d12));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (theme::line.withAlpha (0.5f));
        g.drawRoundedRectangle (bar, 2.0f, 0.7f);
        const float w01 = juce::jlimit (0.0f, 1.0f, juce::jmap (peakDb, -48.0f, 0.0f, 0.0f, 1.0f));
        if (w01 > 0.001f)
        {
            juce::ColourGradient grad (theme::cyanD, bar.getX(), 0.0f, theme::red, bar.getRight(), 0.0f, false);
            grad.addColour (0.62, theme::cyan);
            grad.addColour (0.84, theme::amber);
            g.setGradientFill (grad);
            g.saveState();
            g.reduceClipRegion (bar.withWidth (bar.getWidth() * w01).getSmallestIntegerContainer());
            g.fillRoundedRectangle (bar, 2.0f);
            g.restoreState();
        }
    }
}
}
