#include "PluginEditor.h"

using namespace orbita;

namespace
{
    // Resize de 3 tamaños fijos (diseño aprobado): factores de zoom sobre el tamaño base.
    constexpr float kZoomS = 0.8f, kZoomM = 1.0f, kZoomL = 1.25f;
    constexpr const char* kZoomKey = "uiZoom";

    // Snap a uno de los 3 factores válidos (defensa ante valores corruptos del settings).
    float snapZoom (float z)
    {
        if (z < (kZoomS + kZoomM) * 0.5f) return kZoomS;
        if (z < (kZoomM + kZoomL) * 0.5f) return kZoomM;
        return kZoomL;
    }

    // Settings GLOBAL del sello OVNI: ~/Library/Application Support/OVNI/OVNI.settings, clave "uiZoom".
    // Compartido por TODOS los plugins del sello -> el tamaño elegido sincroniza el arranque de todos.
    juce::PropertiesFile& ovniUiSettings()
    {
        static juce::PropertiesFile file { [] {
            juce::PropertiesFile::Options o;
            o.applicationName     = "OVNI";
            o.filenameSuffix      = "settings";
            o.folderName          = "OVNI";
            o.osxLibrarySubFolder = "Application Support";
            return o;
        }() };
        return file;
    }

    // divider con rótulo (mockup .grpCap): hairline corto · LABEL // sub · hairline largo a la derecha.
    void groupCap (juce::Graphics& g, juce::Rectangle<int> a, const juce::String& main, const juce::String& sub)
    {
        auto f = fonts::mono (8.0f).withExtraKerningFactor (0.22f);
        g.setFont (f);
        auto strW = [&f] (const juce::String& s)
        {
            juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0.0f, 0.0f);
            return (float) ga.getBoundingBox (0, -1, true).getWidth();
        };
        const float cy = (float) a.getCentreY();
        float x = (float) a.getX();
        // hairline corto a la izquierda
        g.setColour (theme::line);
        g.drawLine (x, cy, x + 14.0f, cy, 1.0f);
        x += 14.0f + 9.0f;
        g.setColour (theme::fnt);
        g.drawText (main, juce::Rectangle<float> (x, (float) a.getY(), strW (main) + 4.0f, (float) a.getHeight()),
                    juce::Justification::centredLeft);
        x += strW (main) + 8.0f;
        g.setColour (theme::cyanD.withAlpha (0.62f));
        auto fs = fonts::mono (8.0f).withExtraKerningFactor (0.14f);
        g.setFont (fs);
        g.drawText (sub, juce::Rectangle<float> (x, (float) a.getY(), strW (sub) + 6.0f, (float) a.getHeight()),
                    juce::Justification::centredLeft);
        x += strW (sub) + 9.0f;
        // hairline largo (más tenue) hasta el borde
        g.setColour (theme::line.withAlpha (0.45f));
        g.drawLine (x, cy, (float) a.getRight(), cy, 1.0f);
    }
}

//==============================================================================
PluginEditor::Knob& PluginEditor::addKnob (const juce::String& id, const juce::String& name,
                                           juce::Colour hue, const juce::String& idx)
{
    auto k = std::make_unique<Knob>();
    k->name = name; k->hue = hue; k->idx = idx;
    auto& s = k->slider;
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setRotaryParameters (-2.3562f, 2.3562f, true);          // rango 270° (mockup: -135°..+135°)
    s.getProperties().set ("hue", (int) hue.getARGB());        // cian=Movimiento, magenta=Profundidad
    s.setLookAndFeel (&knobLAF);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 13);
    s.setColour (juce::Slider::textBoxTextColourId, theme::mut);
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);

    content.addAndMakeVisible (s);   // hijo del lienzo (escala con el zoom)
    k->att = std::make_unique<APVTS::SliderAttachment> (processorRef.apvts, id, s);
    auto& ref = *k;
    knobs[id] = std::move (k);
    return ref;
}

void PluginEditor::placeKnob (const juce::String& id, juce::Rectangle<int> cell, int knobSize)
{
    auto it = knobs.find (id);
    if (it == knobs.end()) return;
    auto& k = *it->second;
    const int tbH = k.slider.getTextBoxHeight();

    // índice mono anclado a la ESQUINA sup-izq de la celda (mockup .kidx{top:-1px;left:1px}):
    // box angosto y a la izquierda → nunca pisa el nombre (que va centrado).
    k.idxBounds = juce::Rectangle<int> (cell.getX() + 4, cell.getY(), 16, 10);

    // nombre CENTRADO en la celda, en su propia línea bajo el índice (mockup .kname sobre el knob).
    cell.removeFromTop (2);                    // aire para que el índice respire arriba
    k.nameBounds = cell.removeFromTop (13);

    auto sCell = cell.removeFromTop (knobSize + tbH);
    k.slider.setBounds (sCell.withSizeKeepingCentre (knobSize, knobSize + tbH));
}

//==============================================================================
PluginEditor::Fader& PluginEditor::addFader (const juce::String& id, const juce::String& name,
                                             const juce::String& fmt, bool bip)
{
    auto f = std::make_unique<Fader>();
    f->name = name; f->fmt = fmt; f->bipolar = bip;
    auto& s = f->slider;
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.getProperties().set ("bip", bip);
    s.setLookAndFeel (&railLAF);
    content.addAndMakeVisible (s);
    f->att = std::make_unique<APVTS::SliderAttachment> (processorRef.apvts, id, s);
    auto* fp = f.get();
    s.onValueChange = [this, fp] { content.repaint (fp->labelBounds); };   // valor en vivo
    auto& ref = *f;
    faders[id] = std::move (f);
    return ref;
}

juce::String PluginEditor::faderValueText (const Fader& f) const
{
    const float v = (float) f.slider.getValue();
    if (f.fmt == "db")
        return (v >= 0.0f ? "+" : juce::String::fromUTF8 ("\xe2\x88\x92")) + juce::String (std::abs (v), 1) + " dB";
    return juce::String (juce::roundToInt (v)) + "%";
}

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), radar (p), meter (p)
{
    addAndMakeVisible (content);   // el lienzo cubre el editor; lleva el transform de zoom

    content.addAndMakeVisible (radar);
    content.addAndMakeVisible (meter);

    // banda de modos (mockup #modeBlock): RATE (5) · SHAPE (5) · DIRECTION (2) · OUTPUT (2)
    rateSeg = std::make_unique<SegControl> (p, "orbRate",
                  juce::StringArray { "1/4", "1/2", "1 BAR", "FREE", "FIXED" });
    shapeSeg = std::make_unique<SegControl> (p, "orbShape", juce::StringArray {},
                  [] (juce::Graphics& g, juce::Rectangle<float> c, int idx, bool on)
                  { paintShapeIcon (g, c.reduced (c.getWidth() * 0.18f, c.getHeight() * 0.20f),
                                    idx, on ? theme::cyan : theme::mut); }, 5);
    dirSeg  = std::make_unique<SegControl> (p, "orbDir", juce::StringArray { "CW", "CCW" });
    outSeg  = std::make_unique<SegControl> (p, "outMode", juce::StringArray { "PHONES", "SPEAKERS" });
    content.addAndMakeVisible (*rateSeg);  content.addAndMakeVisible (*shapeSeg);
    content.addAndMakeVisible (*dirSeg);   content.addAndMakeVisible (*outSeg);

    // knobs — DOS grupos de 5, ALINEADOS y consistentes por hue:
    //   TRAYECTORIA (cian, 5): SPEED·WIDTH·RADIUS·HEIGHT·CHAOS (CHAOS = turbulencia del camino → movimiento)
    //   ESPACIO·TONO·FILTRO (magenta, 5): DOPPLER·ROOM·SPREAD·LOW·HI
    addKnob ("orbFreeHz", "SPEED",   theme::cyan,    "01");
    addKnob ("width",     "WIDTH",   theme::cyan,    "02");
    addKnob ("orbRadius", "RADIUS",  theme::cyan,    "03");
    addKnob ("orbHeight", "HEIGHT",  theme::cyan,    "04");
    addKnob ("orbChaos",  "CHAOS",   theme::cyan,    "05");
    addKnob ("doppler",   "DOPPLER", theme::magenta, "06");
    addKnob ("room",      "ROOM",    theme::magenta, "07");
    addKnob ("orbSpread", "SPREAD",  theme::magenta, "08");
    addKnob ("lowCut",    "LOW",     theme::magenta, "09");   // par de filtros de tono de la SALIDA
    addKnob ("hiCut",     "HI",      theme::magenta, "10");

    // riel inferior: MIX (uni) · OUTPUT (bip) · INPUT (bip) + MONO SAFE
    addFader ("mix",    "MIX",    "pct", false);
    addFader ("output", "OUTPUT", "db",  true);
    addFader ("inGain", "INPUT",  "db",  true);

    monoSafe = std::make_unique<InPhaseButton> (p);
    content.addAndMakeVisible (*monoSafe);

    processorRef.presets().addChangeListener (this);   // refrescar el nombre cuando cambia el preset

    setBaseSize (960, 580);   // M = 960×580; lee el zoom persistido y arranca en ese tamaño
}

PluginEditor::~PluginEditor()
{
    processorRef.presets().removeChangeListener (this);
    for (auto& kv : knobs)  kv.second->slider.setLookAndFeel (nullptr);
    for (auto& kv : faders) kv.second->slider.setLookAndFeel (nullptr);
}

//==============================================================================
// Resize S·M·L — el zoom va en el hijo 'content' (el transform del editor lo reserva el host para el DPI).
void PluginEditor::applyZoom (float z)
{
    currentZoom = z;
    content.setTransform (juce::AffineTransform::scale (z));
    setSize (juce::roundToInt ((float) baseW * z), juce::roundToInt ((float) baseH * z));   // dispara el resize del host
    repaint();
}

void PluginEditor::setBaseSize (int w, int h)
{
    baseW = w; baseH = h;
    applyZoom (snapZoom ((float) ovniUiSettings().getDoubleValue (kZoomKey, kZoomM)));   // arranca en el último elegido
}

void PluginEditor::setZoomPersist (float z)
{
    applyZoom (z);
    ovniUiSettings().setValue (kZoomKey, (double) z);   // global del sello -> sincroniza el arranque de los 6
    ovniUiSettings().saveIfNeeded();
    repaint();   // el selector del header refleja la celda activa
}

//==============================================================================
void PluginEditor::resized()
{
    // El editor sólo posiciona el lienzo en coords lógicas (base); el transform de 'content' lo escala
    // al tamaño físico (zoom). Todo el layout real ocurre en layoutCanvas() (coords base).
    content.setBounds (0, 0, baseW, baseH);
}

void PluginEditor::paint (juce::Graphics& g)
{
    // Respaldo: cubre cualquier borde por redondeo del zoom. El lienzo (content) pinta todo lo demás.
    g.fillAll (theme::bg0);
}

//==============================================================================
void PluginEditor::layoutCanvas()
{
    // coords BASE (no getLocalBounds(): eso sería el tamaño zoomeado)
    const int headerH = 48, bbarH = 22, radarW = 520;   // radar más ancho (era 472): el campo deja de estar
                                                        // limitado por el ancho y respira; la bahía sigue holgada
    auto r = juce::Rectangle<int> (0, 0, baseW, baseH);
    headerArea = r.removeFromTop (headerH);
    r.removeFromBottom (bbarH);                 // footer (bbar/meter); el meter ocupa esa franja

    {   // Cluster derecho (igual ORDEN que la familia, de afuera hacia adentro): [S·M·L] [A/B] [SAVE] [‹ nombre ›]
        auto hh = headerArea.reduced (14, 0);
        const int cellW = 24, gp = 2;
        auto sml = hh.removeFromRight (cellW * 3 + gp * 2).withSizeKeepingCentre (cellW * 3 + gp * 2, 22);
        zoomSZone = sml.removeFromLeft (cellW); sml.removeFromLeft (gp);
        zoomMZone = sml.removeFromLeft (cellW); sml.removeFromLeft (gp);
        zoomLZone = sml.removeFromLeft (cellW);
        hh.removeFromRight (14);
        presetAbZone = hh.removeFromRight (54).withSizeKeepingCentre (54, 24);
        hh.removeFromRight (12);
        presetSaveZone = hh.removeFromRight (50).withSizeKeepingCentre (50, 24);
        hh.removeFromRight (12);
        auto browser = hh.removeFromRight (188).withSizeKeepingCentre (188, 28);
        presetPrevZone = browser.removeFromLeft (28);
        presetNextZone = browser.removeFromRight (28);
        presetNameZone = browser;
    }
    // bypass (power) a la izquierda del header
    bypassZone = juce::Rectangle<int> (headerArea.getX() + 10, headerArea.getCentreY() - 15, 30, 30);

    // ===== cuerpo: radar (izq, alto completo) | bahía (der) =====
    auto body  = r;                              // ya sin header ni bbar
    auto left  = body.removeFromLeft (radarW);
    radar.setBounds (left);
    bayArea = body;                              // x=472..960

    // la bahía: mode block (arriba) · knob bay (medio) · rail (abajo)
    auto bay  = bayArea.reduced (16, 11);
    bayCap = bay.removeFromTop (16);
    bay.removeFromTop (10);

    // --- mode block (mockup #modeBlock) ---
    const int segH = 24, rowGap = 10, lblH = 12;
    auto modeBand = bay.removeFromTop (lblH + segH + rowGap + lblH + segH + rowGap + lblH + segH);
    {
        auto rateRow = modeBand.removeFromTop (lblH + segH);
        rateLbl = rateRow.removeFromTop (lblH);
        rateSeg->setBounds (rateRow);
        modeBand.removeFromTop (rowGap);

        auto shapeRow = modeBand.removeFromTop (lblH + segH);
        shapeLbl = shapeRow.removeFromTop (lblH);
        shapeSeg->setBounds (shapeRow);
        modeBand.removeFromTop (rowGap);

        auto dirOutRow = modeBand.removeFromTop (lblH + segH);
        const int colGap = 14;
        const int cw = (dirOutRow.getWidth() - colGap) / 2;
        auto dirCol = dirOutRow.removeFromLeft (cw);
        dirLbl = dirCol.removeFromTop (lblH);
        dirSeg->setBounds (dirCol);
        dirOutRow.removeFromLeft (colGap);
        auto outCol = dirOutRow;
        outLbl = outCol.removeFromTop (lblH);
        outSeg->setBounds (outCol);
    }

    bay.removeFromTop (14);

    // --- rail inferior: reservalo ANTES de repartir los knobs (queda anclado abajo) ---
    const int railH = 60;
    auto rail = bay.removeFromBottom (railH);
    railArea = rail;
    bay.removeFromBottom (10);

    // --- knob bay: DOS grupos de 5, en la MISMA grilla de 5 columnas (alineados 1:1) ---
    auto knobBay = bay;
    const int capH = 14, capGap = 9;
    // bloque del knob: aire(2) + nombre(13) + knob(46) + valor(13) = 74 (el aire separa índice/nombre)
    const int knobSize = 46, knobBlockH = 2 + 13 + knobSize + 13;
    const int groupH = capH + capGap + knobBlockH;   // 97 por grupo (divider + fila)

    auto place5 = [this, knobSize] (juce::Rectangle<int> row, const char* a, const char* b,
                                    const char* c, const char* d, const char* e)
    {
        const int cw = row.getWidth() / 5;
        placeKnob (a, row.removeFromLeft (cw), knobSize);
        placeKnob (b, row.removeFromLeft (cw), knobSize);
        placeKnob (c, row.removeFromLeft (cw), knobSize);
        placeKnob (d, row.removeFromLeft (cw), knobSize);
        placeKnob (e, row, knobSize);
    };

    // Las dos filas BAJAN un poco (más aire arriba) y van CERCA entre sí (grilla unida, no dos
    // bloques sueltos). El sobrante se concentra arriba; gap medio chico; un respiro mínimo abajo.
    const int gapMid = 16, gapBot = 8;
    const int gapTop = juce::jmax (8, knobBay.getHeight() - groupH * 2 - gapMid - gapBot);
    knobBay.removeFromTop (gapTop);

    grp1Cap = knobBay.removeFromTop (capH);
    knobBay.removeFromTop (capGap);
    place5 (knobBay.removeFromTop (knobBlockH), "orbFreeHz", "width", "orbRadius", "orbHeight", "orbChaos");

    knobBay.removeFromTop (gapMid);
    grp2Cap = knobBay.removeFromTop (capH);
    knobBay.removeFromTop (capGap);
    place5 (knobBay.removeFromTop (knobBlockH), "doppler", "room", "orbSpread", "lowCut", "hiCut");

    // --- rail: 3 faders + MONO SAFE, CENTRADOS verticalmente en el riel (no pegados arriba) ---
    {
        const int faderContentH = 34;   // label(14) + aire(4) + track(16)
        auto rr = railArea.withSizeKeepingCentre (railArea.getWidth(), juce::jmax (faderContentH, 46));
        // MONO SAFE más ancho que alto → el IN PHASE entra en su modo HORIZONTAL (LED + texto), igual que la familia.
        const int monoW = 104, faderGap = 16;
        auto monoCell = rr.removeFromRight (monoW);
        monoSafe->setBounds (monoCell.withSizeKeepingCentre (monoW, 46));
        rr.removeFromRight (faderGap);

        const int slotW = (rr.getWidth() - faderGap * 2) / 3;
        auto place = [&] (const char* id)
        {
            auto it = faders.find (id);
            if (it == faders.end()) return;
            auto& f = *it->second;
            auto cell = rr.removeFromLeft (slotW);
            cell.removeFromTop (juce::jmax (0, (cell.getHeight() - faderContentH) / 2));   // centrar
            f.labelBounds = cell.removeFromTop (14);
            cell.removeFromTop (4);
            f.slider.setBounds (cell.removeFromTop (16));
            rr.removeFromLeft (faderGap);
        };
        place ("mix"); place ("output"); place ("inGain");
    }

    // meter (footer bbar): franja inferior a todo el ancho de la bahía
    meter.setBounds (juce::Rectangle<int> (bayArea.getX(), baseH - bbarH, bayArea.getWidth(), bbarH));
}

//==============================================================================
void PluginEditor::paintCanvas (juce::Graphics& g)
{
    // fondo atmósfera — horneado a resolución FÍSICA (nítido en Retina), blit a escala.
    g.fillAll (theme::bg0);
    const float scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    if (panel.needsRender (baseW, baseH, scale))
        panel.render (baseW, baseH, scale);
    if (panel.img.isValid())
        g.drawImageTransformed (panel.img, juce::AffineTransform::scale (1.0f / panel.rs));

    // superficie elevada de la bahía (mockup #bay): wash frío MUY tenue que REVELA la atmósfera
    // (cian de familia + topográficas + luz radial) en vez de taparla. Tinte azul-cian arriba →
    // casi transparente → leve oscurecido al pie. Inset highlight superior = filo de cristal.
    {
        juce::ColourGradient cg (juce::Colour (0xffa0c0e0).withAlpha (0.040f),
                                 (float) bayArea.getX(), (float) bayArea.getY(),
                                 juce::Colour (0xff030406).withAlpha (0.18f),
                                 (float) bayArea.getX(), (float) bayArea.getBottom(), false);
        cg.addColour (0.30, juce::Colour (0xffa0c0e0).withAlpha (0.013f));
        g.setGradientFill (cg);
        g.fillRect (bayArea);

        // glow cian de familia entrando por el borde izquierdo de la bahía (continúa el pozo del radar)
        {
            juce::ColourGradient edge (theme::cyan.withAlpha (0.055f),
                                       (float) bayArea.getX(), (float) bayArea.getCentreY(),
                                       theme::cyan.withAlpha (0.0f),
                                       (float) bayArea.getX() + 150.0f, (float) bayArea.getCentreY(), false);
            g.setGradientFill (edge);
            g.fillRect (bayArea);
        }

        g.setColour (juce::Colour (0xffbee1ff).withAlpha (0.07f));
        g.drawHorizontalLine (bayArea.getY(), (float) bayArea.getX(), (float) baseW);   // inset highlight
        g.setColour (juce::Colour (0x4ca0c0e0));
        g.drawVerticalLine (bayArea.getX(), (float) bayArea.getY(), (float) baseH);     // filo de la bahía
    }
    // riel: rebaje que MANTIENE EL COLOR DE ARRIBA. Usa la MISMA progresión que la bahía (cian tenue
    // arriba → oscuro abajo), no un negro que desatura; así la atmósfera cian/teal sigue por el riel.
    // La atmósfera ya está pintada debajo (bayArea la cubre); esto sólo la hunde apenas y la tiñe.
    {
        const float top    = (float) railArea.getY() - 6.0f;
        const float rightW = (float) baseW - (float) bayArea.getX();
        const float h      = (float) baseH - top;
        juce::ColourGradient rg (juce::Colour (0xffa0c0e0).withAlpha (0.050f), 0.0f, top,
                                 theme::bg0.withAlpha (0.30f),                 0.0f, (float) baseH, false);
        rg.addColour (0.22, juce::Colour (0xffa0c0e0).withAlpha (0.012f));
        g.setGradientFill (rg);
        g.fillRect (juce::Rectangle<float> ((float) bayArea.getX(), top, rightW, h));

        // glow cian de familia entrando por el borde izquierdo (continúa el de la bahía → mismo color)
        const float midY = top + h * 0.5f;
        juce::ColourGradient edge (theme::cyan.withAlpha (0.050f), (float) bayArea.getX(),          midY,
                                   theme::cyan.withAlpha (0.0f),   (float) bayArea.getX() + 150.0f, midY, false);
        g.setGradientFill (edge);
        g.fillRect (juce::Rectangle<float> ((float) bayArea.getX(), top, rightW, h));

        g.setColour (theme::line.withAlpha (0.65f));
        g.drawHorizontalLine (railArea.getY() - 6, (float) bayArea.getX(), (float) baseW);
    }

    // ===== HEADER =====
    {
        auto h = headerArea.reduced (14, 0);
        // bypass: power button (cian = activo / rojo = bypasseado)
        {
            auto byp = bypassZone.toFloat();
            const bool bypassed = processorRef.apvts.getRawParameterValue ("bypass")->load() >= 0.5f;
            const auto bcol = bypassed ? theme::red : theme::cyan;
            if (! bypassed) { g.setColour (bcol.withAlpha (0.12f)); g.fillRoundedRectangle (byp, 3.0f); }
            g.setColour (theme::line); g.drawRoundedRectangle (byp, 3.0f, 1.0f);
            const auto bc = byp.getCentre();
            const float rr = 6.0f;
            juce::Path pw;
            pw.addCentredArc (bc.x, bc.y + 1.0f, rr, rr, 0.0f,
                              juce::degreesToRadians (40.0f), juce::degreesToRadians (320.0f), true);
            g.setColour (bcol);
            g.strokePath (pw, juce::PathStrokeType (1.6f));
            g.drawLine (bc.x, bc.y - rr + 1.0f, bc.x, bc.y + 1.5f, 1.6f);
        }

        // marca: OVNI · ORBIT · MOV·01
        int tx = bypassZone.getRight() + 14;
        g.setColour (theme::line);
        g.fillRect ((float) tx, (float) h.getY() + 14.0f, 1.0f, (float) h.getHeight() - 28.0f);
        tx += 12;
        g.setColour (juce::Colours::white);
        g.setFont (fonts::display (17.0f).withExtraKerningFactor (0.34f));
        g.drawText ("OVNI", tx, h.getY(), 70, headerArea.getHeight(), juce::Justification::centredLeft);
        tx += 64;
        g.setColour (theme::cyan);
        g.setFont (fonts::display (14.0f).withExtraKerningFactor (0.22f));
        g.drawText ("ORBIT", tx, h.getY(), 64, headerArea.getHeight(), juce::Justification::centredLeft);
        tx += 60;
        g.setColour (theme::fnt);
        g.setFont (fonts::mono (9.0f).withExtraKerningFactor (0.14f));
        auto movBox = juce::Rectangle<int> (tx, h.getCentreY() - 8, 40, 16);
        g.drawRoundedRectangle (movBox.toFloat(), 2.0f, 1.0f);
        g.drawText (juce::String::fromUTF8 ("MOV\xc2\xb7""01"), movBox, juce::Justification::centred);

        // selector S·M·L — UN grupo hairline + divisores internos; activo = cian sobre tinte (formato familia)
        {
            auto grp = zoomSZone.getUnion (zoomLZone);
            g.setColour (theme::lineSoft);
            g.drawRoundedRectangle (grp.toFloat(), 3.0f, 1.0f);
            g.fillRect (zoomMZone.getX(), grp.getY() + 5, 1, grp.getHeight() - 10);
            g.fillRect (zoomLZone.getX(), grp.getY() + 5, 1, grp.getHeight() - 10);
            const float zf[]            = { kZoomS, kZoomM, kZoomL };
            const char* const lbls[]    = { "S", "M", "L" };
            const juce::Rectangle<int> zones[] = { zoomSZone, zoomMZone, zoomLZone };
            g.setFont (fonts::mono (10.0f));
            for (int i = 0; i < 3; ++i)
            {
                const bool on = std::abs (currentZoom - zf[i]) < 0.01f;
                if (on) { g.setColour (theme::cyan.withAlpha (0.10f)); g.fillRect (zones[i].reduced (1)); }
                g.setColour (on ? theme::cyan : theme::mut);
                g.drawText (lbls[i], zones[i], juce::Justification::centred);
            }
        }

        // ‹ nombre › — SIN caja (cluster limpio, como la familia): chevrons mut + nombre
        g.setColour (theme::mut); g.setFont (fonts::mono (13.0f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x80\xb9"), presetPrevZone, juce::Justification::centred);
        g.drawText (juce::String::fromUTF8 ("\xe2\x80\xba"), presetNextZone, juce::Justification::centred);
        const auto pcur = processorRef.presets().current();
        juce::String pname = pcur.name.isEmpty() ? juce::String ("Init") : pcur.name;
        if (pcur.modified) pname += juce::String::fromUTF8 (" *");
        g.setColour (theme::txt); g.setFont (fonts::label (12.0f));
        g.drawText (pname, presetNameZone, juce::Justification::centred);

        // SAVE — sin caja, con kerning (como la familia)
        g.setColour (theme::mut); g.setFont (fonts::mono (10.0f).withExtraKerningFactor (0.10f));
        g.drawText ("SAVE", presetSaveZone, juce::Justification::centred);

        // separadores verticales entre clusters (como la familia)
        g.setColour (theme::lineSoft);
        for (const int sx : { presetSaveZone.getX() - 8, presetAbZone.getX() - 8, zoomSZone.getX() - 9 })
            g.fillRect (sx, headerArea.getCentreY() - 10, 1, 20);

        // A/B — grupo hairline; slot ACTIVO RELLENO cian con texto oscuro (formato de la familia)
        {
            auto ab = presetAbZone;
            g.setColour (theme::lineSoft); g.drawRoundedRectangle (ab.toFloat(), 3.0f, 1.0f);
            const bool onA = processorRef.ab().activeSlot() == 'A';
            auto aCell = ab.removeFromLeft (ab.getWidth() / 2);
            auto bCell = ab;
            g.setFont (fonts::mono (10.0f));
            g.setColour (theme::cyan);
            g.fillRoundedRectangle ((onA ? aCell : bCell).toFloat().reduced (1.0f), 2.0f);
            g.setColour (onA ? juce::Colour (0xff031014) : theme::mut);
            g.drawText ("A", aCell, juce::Justification::centred);
            g.setColour (onA ? theme::mut : juce::Colour (0xff031014));
            g.drawText ("B", bCell, juce::Justification::centred);
        }
    }
    g.setColour (theme::line);
    g.drawHorizontalLine (headerArea.getBottom(), 0.0f, (float) baseW);

    // ===== secCap de la bahía: "MOVEMENT // HRTF SPATIALIZER" =====
    {
        auto f = fonts::mono (8.0f).withExtraKerningFactor (0.26f);
        g.setFont (f);
        auto strW = [&f] (const juce::String& s)
        { juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0.0f, 0.0f);
          return (float) ga.getBoundingBox (0, -1, true).getWidth(); };
        int x = bayCap.getX() + 1;
        g.setColour (theme::fnt);
        g.drawText ("MOVEMENT", x, bayCap.getY(), 120, bayCap.getHeight(), juce::Justification::centredLeft);
        x += (int) strW ("MOVEMENT") + 8;
        g.setColour (theme::cyanD.withAlpha (0.7f));
        g.drawText ("// HRTF SPATIALIZER", x, bayCap.getY(), 200, bayCap.getHeight(), juce::Justification::centredLeft);
    }

    // ===== labels de la banda de modos (mockup .clab) =====
    auto clab = [&] (juce::Rectangle<int> a, const juce::String& s)
    {
        g.setColour (theme::fnt);
        g.setFont (fonts::mono (7.0f).withExtraKerningFactor (0.22f));
        g.drawText (s, a.withTrimmedLeft (1), juce::Justification::bottomLeft);
    };
    clab (rateLbl,  "RATE");
    clab (shapeLbl, "SHAPE");
    clab (dirLbl,   "DIRECTION");
    clab (outLbl,   "OUTPUT");

    // ===== group caps de los knobs (divider con rótulo) =====
    groupCap (g, grp1Cap, "TRAJECTORY", "// PATH");
    groupCap (g, grp2Cap, juce::String::fromUTF8 ("SPACE \xc2\xb7 TONE"), "// FILTER");

    // ===== knob name labels + idx =====
    for (auto& kv : knobs)
    {
        auto& k = *kv.second;
        // índice mono en la esquina sup-izq de la celda — dibujado ANTES del nombre y a la izquierda,
        // jamás se solapa con el nombre (que va centrado) ni con la celda vecina.
        g.setColour (theme::fnt);
        g.setFont (fonts::mono (7.0f).withExtraKerningFactor (0.06f));
        g.drawText (k.idx, k.idxBounds, juce::Justification::topLeft);
        // nombre centrado sobre el knob
        g.setColour (theme::txt.withAlpha (0.92f));
        g.setFont (fonts::mono (9.5f).withExtraKerningFactor (0.10f));
        g.drawText (k.name, k.nameBounds, juce::Justification::centred);
    }

    // ===== rail: labels de faders (nombre izq · valor der) =====
    for (auto& kv : faders)
    {
        auto& f = *kv.second;
        auto lb = f.labelBounds;
        g.setColour (theme::mut);
        g.setFont (fonts::mono (8.0f).withExtraKerningFactor (0.18f));
        g.drawText (f.name, lb, juce::Justification::bottomLeft);
        g.setColour (theme::txt);
        g.setFont (fonts::mono (8.5f));
        g.drawText (faderValueText (f), lb, juce::Justification::bottomRight);
    }
}

//==============================================================================
// M5 — browser de presets en el header + selector S·M·L. Las coords del evento llegan en espacio
// base (JUCE invierte el transform del lienzo).
void PluginEditor::canvasMouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if      (presetPrevZone.contains (p)) processorRef.presets().prev();
    else if (presetNextZone.contains (p)) processorRef.presets().next();
    else if (presetNameZone.contains (p)) showPresetMenu();
    else if (presetSaveZone.contains (p)) showSaveDialog();
    else if (zoomSZone.contains (p))      setZoomPersist (kZoomS);
    else if (zoomMZone.contains (p))      setZoomPersist (kZoomM);
    else if (zoomLZone.contains (p))      setZoomPersist (kZoomL);
    else if (presetAbZone.contains (p))
    {
        if (juce::ModifierKeys::getCurrentModifiers().isAltDown()) processorRef.ab().copyActiveToOther();
        else                                                       processorRef.ab().toggle();
        content.repaint (headerArea);
    }
    else if (bypassZone.contains (p))
    {
        if (auto* bp = processorRef.apvts.getParameter ("bypass"))
            bp->setValueNotifyingHost (bp->getValue() >= 0.5f ? 0.0f : 1.0f);
        content.repaint (headerArea);
    }
}

void PluginEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    content.repaint (headerArea);   // refrescar el nombre del preset (+ marca de modificado)
}

void PluginEditor::showPresetMenu()
{
    juce::PopupMenu prod, sd, user, root;
    const auto& all = orbita::factoryPresets();
    for (int i = 0; i < (int) all.size(); ++i)
    {
        auto& sub = (all[(size_t) i].category == orbita::Category::Production) ? prod : sd;
        sub.addItem (i + 1, all[(size_t) i].name);
    }
    int uid = 10001;
    for (auto& f : processorRef.presets().userPresets())
        user.addItem (uid++, f.getFileNameWithoutExtension());

    root.addSubMenu ("PRODUCTION", prod);
    root.addSubMenu ("SOUND DESIGN", sd);
    if (processorRef.presets().userPresets().size() > 0)
        root.addSubMenu ("USER", user);

    // ancla en coords de PANTALLA desde el lienzo (incluye el transform de zoom)
    root.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (content.localAreaToGlobal (presetNameZone)),
        [this] (int r)
        {
            if (r >= 1 && r <= (int) orbita::factoryPresets().size())
                processorRef.presets().applyFactory (r - 1);
            else if (r >= 10001)
            {
                auto users = processorRef.presets().userPresets();
                const int idx = r - 10001;
                if (idx >= 0 && idx < users.size()) processorRef.presets().applyUserFile (users[idx]);
            }
        });
}

void PluginEditor::showSaveDialog()
{
    auto* w = new juce::AlertWindow ("Save Preset", "Preset name:",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", "My Preset");
    w->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, w] (int res)
        {
            if (res == 1)
            {
                const auto nm = w->getTextEditorContents ("name").trim();
                if (nm.isNotEmpty()) processorRef.presets().saveUser (nm);
            }
        }), true);   // deleteWhenDismissed
}
