#pragma once

namespace orbita {

// Posición objetivo del punto sonoro (contrato del spec §3).
// azimuth: rad, 0 = frente, + = CCW (hacia la izquierda).
// elevation/distance: normalizados; PARKEADOS en M2 (los usa el motor en M3).
struct SpatialTarget
{
    float azimuth   = 0.0f;
    float elevation = 0.0f;
    float distance  = 1.0f;
};

// Estado de transporte del host (lo llena el processor desde AudioPlayHead).
struct TransportInfo
{
    bool   isPlaying   = false;
    double bpm         = 120.0;
    double ppqPosition = 0.0;
    double timeSigNum  = 4.0;   // numerador del compás (p. ej. 4 en 4/4)
};

// Seam de extensibilidad: el v1 trae Órbitas; v1.1+ (Audio-reactivo, Secuenciador,
// Trayectorias, Física) implementan este mismo contrato.
class MovementSource
{
public:
    virtual ~MovementSource() = default;

    virtual void prepare (double sampleRate) = 0;
    virtual void reset() = 0;

    // Avanza numSamples de tiempo y devuelve el objetivo al FINAL del bloque.
    // El motor interpola desde su posición anterior hasta este objetivo (sin zipper).
    virtual SpatialTarget advance (int numSamples, const TransportInfo& transport) = 0;
};

} // namespace orbita
