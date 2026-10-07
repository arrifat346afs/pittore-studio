#pragma once
// Palette I/O: GPL, ASE, ACB (subset).
//
// GPL is the native line format; ASE parses top-level RGB/CMYK/LAB/Gray
// blocks; ACB parses CIELAB Adobe Color Books (CMYK entries report as
// unavailable).
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pittore::vector {

struct PaletteColor {
    std::string name;
    std::array<std::uint8_t, 4> rgba{0, 0, 0, 255};
};

struct Palette {
    std::string name;
    std::vector<PaletteColor> colors;
};

// Parse .gpl palette text.
Palette parseGpl(const std::string& text);
// Parse Adobe Swatch Exchange (binary big-endian).
Palette parseAse(const std::vector<std::uint8_t>& bytes);
// Parse Adobe Color Book (binary big-endian, LAB-first).
Palette parseAcb(const std::vector<std::uint8_t>& bytes);

// Serialize to .gpl text (lossless for display).
std::string writeGpl(const Palette& palette);

}  // namespace pittore::vector
