#pragma once
#include "MovementSource.h"

namespace orbita {

// Cerebro Órbitas (spec §6): trayectoria paramétrica del punto sonoro.
// M2 produce azimut(t): círculo/elipse, sync al tempo o free-run, dirección, caos.
// Parkeado a M3 (lo consume el motor luego): elevación (height) y distancia (radius).
class OrbitBrain : public MovementSource
{
public:
    enum Shape { Circle = 0, Ellipse = 1, Spiral = 2, Pendulum = 3, PendulumBack = 4 };
    enum Rate  { Quarter = 0, Half = 1, Bar = 2, Free = 3, Fixed = 4 };
    enum Dir   { CW = 0, CCW = 1 };

    struct Params
    {
        int   shape    = Ellipse;
        int   rate     = Half;
        float radius01 = 0.60f;  // 0..1   (parkeado M2)
        float height   = 0.0f;   // -1..1  (parkeado M2)
        float spread01 = 0.35f;  // 0..1   (M2: excentricidad de la elipse)
        float chaos01  = 0.0f;   // 0..1   (M2: caos OVNI — velocidad errática + bamboleo)
        int   dir      = CW;
        float freeHz   = 0.5f;   // vueltas/seg en modo Free (control de velocidad)
        float fixedAzRad = 0.0f;  // azimut fijo (rad) cuando rate==Fixed (M3)
        float doppler01  = 0.0f;  // 0..1 (M3 Doppler): excentricidad de distancia (fly-by)
    };

    void prepare (double sampleRate) override;
    void reset() override;
    void setParams (const Params& p) noexcept { params = p; }

    // Afinable (geometría del fly-by). Default = arranque sensato (afinable por oído en la medición).
    struct DopplerTuning { float maxEcc01 = 0.55f; float curve = 1.0f; }; // maxEcc01: excentricidad máx (unidades radius01); curve=1 (LINEAL): la perilla rinde parejo (antes curve=2 dejaba la mitad de abajo casi inaudible)
    void setDopplerTuning (const DopplerTuning& t) noexcept { dopplerTune = t; }

    // Afinable: trayectorias nuevas (M4). Spiral = vórtice (el radio respira lento, in/out);
    // Pendulum = hamaca (el azimut oscila ±swing, no rota). Defaults = arranque sensato (se afina por oído).
    struct ShapeTuning
    {
        float spiralDepth01    = 0.35f;       // amplitud del respirar del radio en Spiral (unidades radius01)
        float spiralTurns      = 4.0f;        // vueltas por ciclo IN/OUT del vórtice — ACOPLADO a la velocidad
                                              // de la órbita (un ciclo cada N vueltas) → se lee como espiral a
                                              // cualquier Speed (antes: LFO de Hz fijo, decorrelacionado)
        float pendulumSwingRad = 1.5707963f;  // ±amplitud del swing en Pendulum (90° por defecto)
    };
    void setShapeTuning (const ShapeTuning& t) noexcept { shapeTune = t; }

    SpatialTarget advance (int numSamples, const TransportInfo& transport) override;

private:
    double sampleRate = 48000.0;
    double phase      = 0.0;   // [0,1) vueltas — integrador free-run

    // Caos: dos ruidos suavizados deterministas (RT-safe). 1 = velocidad errática, 2 = bamboleo.
    unsigned int rngState = 0x9E3779B9u;
    float jitterCur = 0.0f, jitterCur2 = 0.0f;

    Params params;
    DopplerTuning dopplerTune;
    ShapeTuning   shapeTune;
    double spiralPhase = 0.0;   // fase lenta del vórtice (Spiral)

    float nextWhite() noexcept;   // [-1, 1]
};

} // namespace orbita
