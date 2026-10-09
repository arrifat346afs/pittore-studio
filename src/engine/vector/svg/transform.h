#pragma once
// Transform string to matrix. Composed left to right.
#include <string>
#include <string_view>

namespace pittore::svg {

// Affine as SVG matrix(a b c d e f).
struct Affine {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
};

Affine identity();
Affine multiply(const Affine& x, const Affine& y);
bool isIdentity(const Affine& m);

// Parse full transform list. Bad ops are skipped.
Affine parseTransform(std::string_view s);

// Minimal canonical emit.
std::string writeTransform(const Affine& m);

}  // namespace pittore::svg
