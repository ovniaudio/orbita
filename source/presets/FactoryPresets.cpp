#include "FactoryPresets.h"

// Códigos de choice (índices): orbShape {Circle 0, Ellipse 1, Spiral 2, Pendulum 3, Pendulum Back 4};
// orbRate {1/4 0, 1/2 1, 1 bar 2, Free 3, Fixed 4}; outMode {Phones 0, Speakers 1}; monoSafe 0/1.
// Sólo se listan los parámetros que definen el preset; el resto el PresetManager lo lleva a su default.
namespace orbita
{
const std::vector<FactoryPreset>& factoryPresets()
{
    using C = Category;
    static const std::vector<FactoryPreset> presets = {
        // ===== PRODUCTION (15) — movimiento musical, usable en mezcla =====
        { "Slow Drift",    C::Production,  {{"orbShape",0},{"orbRate",3},{"orbFreeHz",0.15f},{"orbRadius",58},{"width",58},{"room",35}} },
        { "Pad Breath",    C::Production,  {{"orbShape",1},{"orbRate",3},{"orbFreeHz",0.10f},{"orbRadius",62},{"orbSpread",45},{"width",60},{"doppler",8},{"room",45}} },
        { "Wide Hold",     C::Production,  {{"orbShape",0},{"orbRate",3},{"orbFreeHz",0.20f},{"orbRadius",55},{"width",66},{"room",30}} },
        { "Bar Sway",      C::Production,  {{"orbShape",0},{"orbRate",2},{"orbRadius",60},{"width",56},{"room",30}} },
        { "Half Roll",     C::Production,  {{"orbShape",1},{"orbRate",1},{"orbRadius",55},{"orbSpread",40},{"width",52},{"room",25}} },
        { "Quarter Pulse", C::Production,  {{"orbShape",0},{"orbRate",0},{"orbRadius",50},{"width",48},{"room",20}} },
        { "Left Pocket",   C::Production,  {{"orbRate",4},{"orbFixedAz",-45},{"orbRadius",55},{"width",50},{"room",28}} },
        { "Right Pocket",  C::Production,  {{"orbRate",4},{"orbFixedAz",45},{"orbRadius",55},{"width",50},{"room",28}} },
        { "Up Close",      C::Production,  {{"orbRate",4},{"orbFixedAz",0},{"orbRadius",20},{"width",45},{"room",18}} },
        { "Behind",        C::Production,  {{"orbRate",4},{"orbFixedAz",180},{"orbRadius",70},{"width",55},{"room",40}} },
        { "Safe Wide",     C::Production,  {{"orbShape",0},{"orbRate",3},{"orbFreeHz",0.12f},{"orbRadius",58},{"width",70},{"room",25},{"monoSafe",1}} },
        { "Club Safe",     C::Production,  {{"orbRate",4},{"orbFixedAz",0},{"orbRadius",50},{"width",66},{"room",20},{"monoSafe",1}} },
        { "Broadcast",     C::Production,  {{"orbShape",1},{"orbRate",3},{"orbFreeHz",0.08f},{"orbRadius",60},{"width",64},{"room",22},{"monoSafe",1}} },
        { "Subtle Glue",   C::Production,  {{"orbShape",0},{"orbRate",3},{"orbFreeHz",0.08f},{"orbRadius",60},{"width",60},{"room",18}} },
        { "Stereo Lift",   C::Production,  {{"orbShape",1},{"orbRate",3},{"orbFreeHz",0.10f},{"orbRadius",58},{"orbSpread",30},{"width",62},{"room",20}} },

        // ===== SOUND DESIGN (25) — extremos OVNI =====
        { "Abduction",        C::SoundDesign, {{"orbShape",1},{"orbRate",3},{"orbFreeHz",1.5f},{"orbRadius",35},{"width",75},{"doppler",85},{"room",45},{"orbSpread",50}} },
        { "Close Pass",       C::SoundDesign, {{"orbShape",0},{"orbRate",3},{"orbFreeHz",1.0f},{"orbRadius",25},{"width",70},{"doppler",95},{"room",35}} },
        { "Warp Pass",        C::SoundDesign, {{"orbShape",1},{"orbRate",3},{"orbFreeHz",3.0f},{"orbRadius",45},{"width",72},{"doppler",80},{"orbChaos",20},{"room",40}} },
        { "Flyby",            C::SoundDesign, {{"orbShape",0},{"orbRate",3},{"orbFreeHz",2.0f},{"orbRadius",40},{"width",68},{"doppler",70},{"room",38}} },
        { "Tempo Flyby",      C::SoundDesign, {{"orbShape",1},{"orbRate",1},{"orbRadius",35},{"width",70},{"doppler",75},{"room",40}} },
        { "Swarm",            C::SoundDesign, {{"orbShape",0},{"orbRate",3},{"orbFreeHz",2.0f},{"orbRadius",50},{"width",70},{"orbChaos",80},{"room",50}} },
        { "Poltergeist",      C::SoundDesign, {{"orbShape",1},{"orbRate",3},{"orbFreeHz",3.0f},{"orbRadius",45},{"width",72},{"orbChaos",85},{"room",60}} },
        { "Insectoid",        C::SoundDesign, {{"orbShape",0},{"orbRate",3},{"orbFreeHz",6.0f},{"orbRadius",55},{"width",60},{"orbChaos",60},{"room",30}} },
        { "UFO Erratic",      C::SoundDesign, {{"orbShape",1},{"orbRate",3},{"orbFreeHz",1.5f},{"orbRadius",50},{"width",68},{"doppler",40},{"orbChaos",70},{"room",45}} },
        { "Glitch Orbit",     C::SoundDesign, {{"orbShape",0},{"orbRate",3},{"orbFreeHz",4.0f},{"orbRadius",45},{"width",65},{"orbChaos",90},{"room",35}} },
        { "Vortex",           C::SoundDesign, {{"orbShape",2},{"orbRate",3},{"orbFreeHz",1.0f},{"orbRadius",60},{"width",70},{"orbSpread",60},{"room",50}} },
        { "Black Hole",       C::SoundDesign, {{"orbShape",2},{"orbRate",3},{"orbFreeHz",0.5f},{"orbRadius",70},{"width",68},{"doppler",60},{"room",65}} },
        { "Maelstrom",        C::SoundDesign, {{"orbShape",2},{"orbRate",3},{"orbFreeHz",2.0f},{"orbRadius",55},{"width",72},{"orbChaos",40},{"room",45}} },
        { "Descent",          C::SoundDesign, {{"orbShape",2},{"orbRate",2},{"orbRadius",60},{"doppler",50},{"room",55}} },
        { "Pendulum Sweep",   C::SoundDesign, {{"orbShape",3},{"orbRate",3},{"orbFreeHz",0.8f},{"orbRadius",55},{"width",70},{"room",40}} },
        { "Behind Swing",     C::SoundDesign, {{"orbShape",4},{"orbRate",3},{"orbFreeHz",0.6f},{"orbRadius",60},{"width",66},{"room",50}} },
        { "Haunt",            C::SoundDesign, {{"orbShape",4},{"orbRate",3},{"orbFreeHz",0.4f},{"orbRadius",58},{"width",68},{"orbChaos",30},{"room",65}} },
        { "Tempo Pendulum",   C::SoundDesign, {{"orbShape",3},{"orbRate",1},{"orbRadius",55},{"width",68},{"doppler",30},{"room",42}} },
        { "Rhythmic Teleport",C::SoundDesign, {{"orbShape",0},{"orbRate",0},{"orbRadius",40},{"width",80},{"doppler",70},{"room",40}} },
        { "Beat Flyby",       C::SoundDesign, {{"orbShape",1},{"orbRate",1},{"orbRadius",30},{"width",72},{"doppler",85},{"room",38}} },
        { "Strobe",           C::SoundDesign, {{"orbShape",0},{"orbRate",0},{"orbRadius",45},{"width",75},{"orbChaos",50},{"room",35}} },
        { "Glitch Sync",      C::SoundDesign, {{"orbShape",1},{"orbRate",0},{"orbRadius",45},{"width",70},{"doppler",40},{"orbChaos",80},{"room",35}} },
        { "Deep Space",       C::SoundDesign, {{"orbShape",0},{"orbRate",3},{"orbFreeHz",0.3f},{"orbRadius",80},{"width",60},{"doppler",20},{"room",90}} },
        { "Cathedral",        C::SoundDesign, {{"orbShape",1},{"orbRate",3},{"orbFreeHz",0.5f},{"orbRadius",70},{"width",62},{"doppler",30},{"room",85}} },
        { "Speaker Trip",     C::SoundDesign, {{"orbShape",0},{"orbRate",3},{"orbFreeHz",1.0f},{"orbRadius",55},{"width",70},{"doppler",40},{"room",50},{"outMode",1}} },
    };
    return presets;
}
}
