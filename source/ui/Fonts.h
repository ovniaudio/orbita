#pragma once
#include <juce_graphics/juce_graphics.h>

// Familias del sello OVNI embebidas (spec M4 §5). Typefaces cacheadas; fallback a
// system si BinaryData faltara. Tres familias = excepción deliberada (el mono "alien").
namespace orbita::fonts
{
    juce::Font display (float height);  // Clash Grotesk Semibold — wordmark / display
    juce::Font body    (float height);  // General Sans Regular   — texto
    juce::Font label   (float height);  // General Sans Medium    — labels de control
    juce::Font mono    (float height);  // JetBrains Mono         — readouts / coordenadas
}
