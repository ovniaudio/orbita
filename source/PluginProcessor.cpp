#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PluginProcessor::PluginProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

PluginProcessor::~PluginProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    constexpr int v = 1; // versionHint — NO cambiar (rompe automatización guardada)

    auto pct = [] { return NormalisableRange<float> (0.0f, 100.0f, 0.1f); };

    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"mix", v},       "Mix",         pct(), 100.0f));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"width", v},     "Width",       pct(), 50.0f));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"doppler", v},   "Doppler",     pct(), 0.0f));
    // 'depth' y 'clarity' sacados (2026-06-07): estaban declarados pero NUNCA se leían = params muertos
    // que ensuciaban la automatización del DAW. Pre-release → momento OK para limpiarlos.
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"room", v},      "Room",        pct(), 30.0f));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"output", v},    "Output",      NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID{"outMode", v},   "Output Mode", StringArray{"Phones", "Speakers"}, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID{"orbShape", v},  "Shape",       StringArray{"Circle", "Ellipse", "Spiral", "Pendulum", "Pendulum Back"}, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID{"orbRate", v},   "Rate",        StringArray{"1/4", "1/2", "1 bar", "Free", "Fixed"}, 3));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"orbRadius", v}, "Radius",      pct(), 60.0f));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"orbHeight", v}, "Height",      NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"orbSpread", v}, "Spread",      pct(), 35.0f));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"orbChaos", v},  "Chaos",       pct(), 0.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID{"orbDir", v},    "Direction",   StringArray{"CW", "CCW"}, 0));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID{"bypass", v},    "Bypass",      false));
    // Agregado en M2 (append-only seguro): velocidad de la órbita en modo Free (vueltas/seg).
    // Skew 0.5 = recorrido parejo del knob (antes 0.3 amontonaba todo en lo lento).
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"orbFreeHz", v}, "Speed",
                NormalisableRange<float> (0.05f, 8.0f, 0.001f, 0.5f), 0.5f));
    // Agregado en M2 (append-only): EN FASE — fuerza graves a mono (mono-safe pase lo que pase).
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID{"monoSafe", v},  "Mono Safe", false));
    // Agregado en M3 (append-only): ángulo fijo de la fuente en modo Rate=Fijo (grados; backing del visualizador, sin knob).
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"orbFixedAz", v}, "Fixed Angle",
                NormalisableRange<float> (-180.0f, 180.0f, 0.1f), 0.0f));
    // Agregado en M4 (append-only): Input gain (drive, pre-motor). El 'output' (gain de salida) ya
    // existía parkeado desde M0 — ahora cableado (post-limiter). Ambos en dB, default 0 = unidad.
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"inGain", v}, "Input",
                NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));

    // Agregado (append-only): LOW CUT + HI CUT de la SALIDA — el par de filtros de tono del sello OVNI.
    // High-pass + low-pass sobre el buffer de salida (dry+wet ya espacializado), ANTES del output-gain.
    // % 0..100; default 0 = off (20 Hz / 20 kHz) → transparente, no cambia presets. Mapeo %→Hz log en
    // orbita::dsp::LowCut/HiCut::hzFor01. TPT modulación-safe (sin clicks), lineales (sin alias).
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"lowCut", v}, "Low Cut", pct(), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID{"hiCut", v},  "Hi Cut",  pct(), 0.0f));

    return layout;
}

//==============================================================================
const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double PluginProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

// NO exponemos los 40 presets como "programs" del host: hacerlo rompe la restauración de estado de
// los parámetros Bool en VST3 (pluginval). El Program Change MIDI se intercepta en processBlock.
int PluginProcessor::getNumPrograms()                              { return 1; }
int PluginProcessor::getCurrentProgram()                           { return 0; }
void PluginProcessor::setCurrentProgram (int index)                { juce::ignoreUnused (index); }
const juce::String PluginProcessor::getProgramName (int index)     { juce::ignoreUnused (index); return {}; }
void PluginProcessor::changeProgramName (int index, const juce::String& newName) { juce::ignoreUnused (index, newName); }

void PluginProcessor::handleAsyncUpdate()
{
    const int p = pendingProgram.exchange (-1, std::memory_order_relaxed);
    if (p >= 0) presetManager.applyFactory (p);   // corre en el message thread (RT-safe)
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = (juce::uint32) juce::jmax (1, getTotalNumOutputChannels());
    engine.prepare (spec);
    brain.prepare (sampleRate);
    lowCut.prepare (spec);
    hiCut.prepare (spec);
    prevInGain  = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("inGain")->load());
    prevOutGain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load());
}

void PluginProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // ORBIT es un espacializador binaural: la salida SIEMPRE es estéreo.
    // La entrada puede ser mono (punto sonoro) o estéreo (se suma a mono).
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::stereo())
        return false;

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Program Change MIDI -> cargar preset de fábrica (se difiere al message thread; RT-safe).
    for (const auto meta : midiMessages)
    {
        const auto m = meta.getMessage();
        if (m.isProgramChange())
        {
            const int prog = m.getProgramChangeNumber();
            if (prog >= 0 && prog < (int) orbita::factoryPresets().size())
            {
                pendingProgram.store (prog, std::memory_order_relaxed);
                triggerAsyncUpdate();
            }
        }
    }

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Limpia canales de salida sin entrada (evita basura antes de escribirlos)
    for (int i = numIn; i < numOut; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Bypass: pass-through (la salida es estéreo; si entró mono, duplicar a R)
    if (apvts.getRawParameterValue ("bypass")->load() >= 0.5f)
    {
        if (numOut > 1 && numIn == 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
        return;
    }

    // Input gain (drive) — rampeado por-bloque (sin zipper). Sólo en el camino activo, no en bypass.
    const float inG = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("inGain")->load());
    buffer.applyGainRamp (0, buffer.getNumSamples(), prevInGain, inG);
    prevInGain = inG;

    // mix (0..100 %) -> 0..1
    const float mix = apvts.getRawParameterValue ("mix")->load() * 0.01f;

    // Cerebro Órbitas: parámetros del APVTS -> trayectoria
    orbita::OrbitBrain::Params bp;
    bp.shape    = (int) apvts.getRawParameterValue ("orbShape")->load();
    bp.rate     = (int) apvts.getRawParameterValue ("orbRate")->load();
    bp.radius01 =       apvts.getRawParameterValue ("orbRadius")->load() * 0.01f;
    bp.height   =       apvts.getRawParameterValue ("orbHeight")->load() * 0.01f;
    bp.spread01 =       apvts.getRawParameterValue ("orbSpread")->load() * 0.01f;
    bp.chaos01  =       apvts.getRawParameterValue ("orbChaos")->load() * 0.01f;
    bp.dir      = (int) apvts.getRawParameterValue ("orbDir")->load();
    bp.freeHz   =       apvts.getRawParameterValue ("orbFreeHz")->load();
    bp.fixedAzRad = juce::degreesToRadians (apvts.getRawParameterValue ("orbFixedAz")->load());
    bp.doppler01  = apvts.getRawParameterValue ("doppler")->load() * 0.01f;
    brain.setParams (bp);

    // Transporte del host (BPM/ppq/playing) para sync; free-run si no toca
    orbita::TransportInfo t;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            t.isPlaying = pos->getIsPlaying();
            if (auto bpm = pos->getBpm())           t.bpm         = *bpm;
            if (auto ppq = pos->getPpqPosition())   t.ppqPosition = *ppq;
            if (auto ts  = pos->getTimeSignature()) t.timeSigNum  = (double) ts->numerator;
        }

    // El cerebro produce el azimut objetivo; el motor mueve la fuente sin clicks.
    const float room     = apvts.getRawParameterValue ("room")->load()  * 0.01f;
    const float width    = apvts.getRawParameterValue ("width")->load() * 0.01f;
    const bool  monoSafe = apvts.getRawParameterValue ("monoSafe")->load() >= 0.5f;
    const float radius   = apvts.getRawParameterValue ("orbRadius")->load() * 0.01f;
    const bool  speakers = apvts.getRawParameterValue ("outMode")->load() >= 0.5f; // 0=Auric, 1=Parlantes
    const float doppler  = apvts.getRawParameterValue ("doppler")->load() * 0.01f;
    const auto targetPos = brain.advance (buffer.getNumSamples(), t);
    engine.process (buffer, juce::jmax (1, numIn),
        orbita::SpatialEngine::EngineParams {
            .azimuthRad = targetPos.azimuth, .mix01 = mix, .room01 = room,
            .width01 = width, .monoSafe = monoSafe, .radius01 = radius, .speakerMode = speakers,
            .distance01 = targetPos.distance, .doppler01 = doppler,
            .elevation01 = bp.height });   // HEIGHT (-1..+1): cue espectral de elevación (arriba=aire, abajo=oscuro)

    // LOW CUT + HI CUT de la SALIDA (dry+wet ya espacializado), ANTES del output-gain. Cortan los graves/
    // agudos de TODO lo que sale → se oyen a cualquier MIX. Cadena low-cut -> hi-cut. TPT modulación-safe
    // (de-zipper interno, sin clicks), lineales (sin alias). BYPASS POR FILTRO: en default (0/0) NO corre
    // NINGÚN filtro → salida idéntica a la ya validada (limiter-safe).
    {
        const float lc01 = juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue ("lowCut")->load() * 0.01f);
        const float hc01 = juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue ("hiCut")->load()  * 0.01f);
        if (lc01 > 0.0f) { lowCut.setCutoffHz (orbita::dsp::LowCut::hzFor01 (lc01)); lowCut.process (buffer); }
        if (hc01 > 0.0f) { hiCut .setCutoffHz (orbita::dsp::HiCut ::hzFor01 (hc01)); hiCut .process (buffer); }
    }

    // Output gain (nivel final) — POST-limiter, rampeado: el usuario PUEDE subir el nivel (el medidor/
    // LED de clip avisa si pasa 0 dBFS). Default 0 dB = unidad -> no cambia nada de lo ya validado.
    const float outG = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load());
    buffer.applyGainRamp (0, buffer.getNumSamples(), prevOutGain, outG);
    prevOutGain = outG;

    // Publicar estado para el visualizador / meter (lock-free: audio escribe, UI lee).
    const float outPk = buffer.getMagnitude (0, buffer.getNumSamples());  // pico final (ambos canales), post output-gain
    uiAzimuth .store (targetPos.azimuth,   std::memory_order_relaxed);
    uiActive  .store (outPk > 1.0e-5f,     std::memory_order_relaxed);
    uiDistance.store (targetPos.distance,  std::memory_order_relaxed);
    uiOutPeak .store (outPk,               std::memory_order_relaxed);

    // LED de clip ("estás empujando, bajá"). Se toma la MAYOR de dos fuentes —
    //  (a) el limiter conteniendo picos internos (lastLimiterGain < 1 -> dB de reducción), y
    //  (b) el pico de salida final cerca/encima de 0 dBFS (si el OUTPUT subido clipea el host).
    const float limRedDb = -juce::Decibels::gainToDecibels (juce::jlimit (1.0e-4f, 1.0f, engine.lastLimiterGain()));
    const float limPush  = juce::jlimit (0.0f, 1.0f, limRedDb * 0.25f);                              // ~4 dB de reducción -> pleno
    const float peakPush = juce::jlimit (0.0f, 1.0f, juce::jmap (outPk, 0.95f, 1.0f, 0.0f, 1.0f));   // 0.95 -> 0 dBFS = 0 -> 1
    uiClip.store (juce::jmax (limPush, peakPush), std::memory_order_relaxed);
}

//==============================================================================
bool PluginProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", 1, nullptr); // para migraciones futuras
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            // const int stateVersion = (int) tree.getProperty ("stateVersion", 1); // ramificar acá al migrar
            apvts.replaceState (tree);
        }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
