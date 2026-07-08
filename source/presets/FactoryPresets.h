#pragma once
#include <vector>

// Presets de fábrica de ORBIT (M5). Definidos en código = la fuente de verdad: legibles,
// versionables, cada uno se lee como lo que HACE. Valores en unidades del parámetro (los choices
// como índice). El PresetManager los aplica vía convertTo0to1 sobre el APVTS.
namespace orbita
{
enum class Category { Production, SoundDesign };

struct PresetParam { const char* id; float value; };
struct FactoryPreset { const char* name; Category category; std::vector<PresetParam> params; };

const std::vector<FactoryPreset>& factoryPresets();
}
