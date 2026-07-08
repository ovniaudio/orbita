#pragma once
#include <juce_graphics/juce_graphics.h>

// Tokens visuales del sello OVNI (spec M4 §5). Negro casi puro; dos hues que cortan
// (cian = Movimiento, magenta = Profundidad/Espacio); ámbar/rojo = caución/clip.
namespace orbita::theme
{
    // superficies (Joaquín: "más negro")
    inline const juce::Colour bg0   { 0xff030406 };   // fondo casi puro
    inline const juce::Colour bg1   { 0xff060709 };
    inline const juce::Colour surf  { 0xff0a0c11 };   // superficie elevada (paneles)
    inline const juce::Colour line  { 0x26a0c0e0 };   // hairline ~15% alpha
    inline const juce::Colour lineSoft { 0x12a0c0e0 };// hairline tenue (grupos A/B · S·M·L · separadores) — igual que la familia

    // acentos semánticos
    inline const juce::Colour cyan   { 0xff5ee7f0 };  // Movimiento
    inline const juce::Colour cyanD  { 0xff27c3d2 };
    inline const juce::Colour magenta{ 0xffc98bff };  // Profundidad / Espacio
    inline const juce::Colour magD   { 0xff9d63d6 };
    inline const juce::Colour amber  { 0xffffb44d };  // caución (meter)
    inline const juce::Colour red    { 0xffff5733 };  // clip

    // texto
    inline const juce::Colour txt   { 0xffeaf1f8 };   // primario
    inline const juce::Colour mut   { 0xff8595aa };   // secundario
    inline const juce::Colour fnt   { 0xff566576 };   // terciario / footer

    // tamaños (lógicos)
    inline constexpr int padIn   = 16;
    inline constexpr int headerH = 48;
    inline constexpr int railH   = 56;

    // estados de feel (glow al hover/press/foco) — mismos valores que el ui-kit de la familia
    namespace state
    {
        inline constexpr float hoverGlow = 0.16f;
        inline constexpr float pressGlow = 0.26f;
        inline constexpr float focusRing = 0.55f;
        inline constexpr float fineHint  = 0.85f;
    }
}
