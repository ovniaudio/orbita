#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/Panel.h"
#include "ui/Fonts.h"
#include "ui/KnobLookAndFeel.h"
#include "ui/RadarView.h"
#include "ui/OutputMeter.h"
#include "ui/Controls.h"
#include <map>
#include <memory>

//==============================================================================
// UI nativa de ORBIT (rediseño orbit-a) — instrumento del sello OVNI. Header (marca OVNI + browser
// de presets + A/B + S·M·L) + radar de pozo profundo + bahía MOVEMENT (banda de modos + dos grupos
// de knobs rotulados: TRAYECTORIA / ESPACIO·TONO) + riel inferior de salida (MIX/OUTPUT/INPUT +
// MONO SAFE). Patrón Canvas + zoom propio. Inglés en los controles.
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::ChangeListener
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Resize S·M·L: zoom del editor entero (público — lo usan host/tests). NO persiste (eso lo hace el selector).
    void applyZoom (float zoomFactor);

    // Test-only: avanza la estela del radar n frames (shot real headless; el timer no corre sin ventana).
    void dbgPump (int n) noexcept { radar.dbgPump (n); }

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    // --- Resize de 3 tamaños fijos (S·M·L) ----------------------------------------------------
    // El zoom NO puede ir en el transform del AudioProcessorEditor (JUCE lo reserva para el DPI del
    // host). Va en un hijo interno 'content': todo el dibujo/layout/mouse ocurre en coords base
    // (baseW×baseH) y el transform de 'content' lo escala. host DPI × zoom se combinan solos.
    struct Canvas : juce::Component
    {
        PluginEditor& owner;
        explicit Canvas (PluginEditor& o) : owner (o) {}
        void paint     (juce::Graphics& g)         override { owner.paintCanvas (g); }
        void resized   ()                          override { owner.layoutCanvas (); }
        void mouseDown (const juce::MouseEvent& e) override { owner.canvasMouseDown (e); }
    };
    Canvas content { *this };

    void paintCanvas     (juce::Graphics&);          // todo el dibujo, en coords base
    void layoutCanvas    ();                         // todo el layout, en coords base
    void canvasMouseDown (const juce::MouseEvent&);  // clicks del header (browser/bypass + selector S·M·L)
    void setBaseSize     (int w, int h);             // guarda base + lee zoom persistido + aplica
    void setZoomPersist  (float zoomFactor);         // selector: aplica + guarda global del sello OVNI

    int   baseW = 960, baseH = 580;                  // tamaño lógico de diseño (M)
    float currentZoom = 1.0f;
    juce::Rectangle<int> zoomSZone, zoomMZone, zoomLZone;

    // Slider rotario que publica flags de feel (hovered/pressed/focused) → el KnobLookAndFeel pinta el
    // glow al pasar el mouse / apretar / enfocar, igual que los OvniKnob del resto del sello.
    struct GlowSlider : juce::Slider
    {
        void mouseEnter (const juce::MouseEvent& e) override { flag ("hovered", true);  juce::Slider::mouseEnter (e); }
        void mouseExit  (const juce::MouseEvent& e) override { flag ("hovered", false); flag ("pressed", false); juce::Slider::mouseExit (e); }
        void mouseDown  (const juce::MouseEvent& e) override { flag ("pressed", true);  juce::Slider::mouseDown (e); }
        void mouseUp    (const juce::MouseEvent& e) override { flag ("pressed", false); juce::Slider::mouseUp (e); }
        void focusGained (FocusChangeType c) override        { flag ("focused", true);  juce::Slider::focusGained (c); }
        void focusLost   (FocusChangeType c) override        { flag ("focused", false); juce::Slider::focusLost (c); }
        void flag (const juce::Identifier& id, bool v) { getProperties().set (id, v); repaint(); }
    };

    struct Knob
    {
        GlowSlider slider;
        std::unique_ptr<APVTS::SliderAttachment> att;
        juce::String name;
        juce::Colour hue;
        juce::String idx;                    // "01".."08" — índice mono del knob (mockup .kidx)
        juce::Rectangle<int> nameBounds;     // dónde dibujar el label centrado (en paint)
        juce::Rectangle<int> idxBounds;      // esquina sup-izq de la celda: índice mono sin pisar el nombre
    };

    Knob& addKnob (const juce::String& id, const juce::String& name, juce::Colour hue,
                   const juce::String& idx);
    void  placeKnob (const juce::String& id, juce::Rectangle<int> cell, int knobSize);

    // Riel inferior: faders horizontales con label arriba (nombre izq · valor der).
    struct Fader
    {
        juce::Slider slider;
        std::unique_ptr<APVTS::SliderAttachment> att;
        juce::String name;
        juce::String fmt;                    // "pct" | "db"
        bool bipolar = false;
        juce::Rectangle<int> labelBounds;    // fila de label (nombre/valor) sobre el track
    };
    Fader& addFader (const juce::String& id, const juce::String& name, const juce::String& fmt, bool bip);
    juce::String faderValueText (const Fader& f) const;

    // M5 — browser de presets en el header (zonas clickeables + menú por categoría)
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showPresetMenu();
    void showSaveDialog();
    juce::Rectangle<int> presetPrevZone, presetNameZone, presetNextZone, presetSaveZone, presetAbZone, bypassZone;

    PluginProcessor& processorRef;

    orbita::Panel             panel;     // fondo atmósfera (estático)
    orbita::KnobLookAndFeel   knobLAF;   // knob dimensional
    orbita::RailSliderLAF     railLAF;   // faders del riel
    orbita::RadarView         radar;     // visualizador
    orbita::OutputMeter       meter;     // readout + meter (footer)

    std::map<juce::String, std::unique_ptr<Knob>>  knobs;
    std::map<juce::String, std::unique_ptr<Fader>> faders;

    std::unique_ptr<orbita::SegControl>    rateSeg, dirSeg, shapeSeg, outSeg;
    std::unique_ptr<orbita::InPhaseButton> monoSafe;

    // rects de anclaje para labels/secciones que dibuja paint()
    juce::Rectangle<int> headerArea, bayArea, railArea, bayCap,
                         rateLbl, shapeLbl, dirLbl, outLbl, grp1Cap, grp2Cap;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
