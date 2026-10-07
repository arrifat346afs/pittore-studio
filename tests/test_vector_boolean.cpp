// test_vector_boolean.cpp — the polygon boolean engine (union,
// intersection, difference, xor): overlapping, touching, contained and
// holed inputs, both fill rules. Areas are measured by shoelace over the
// output loops, so the checks verify geometry, not just non-emptiness.
#include <cmath>
#include <vector>

#include "engine/vector/boolean.h"
#include "test_util.h"

using namespace pittore::vector;

namespace {

BoolRing square(double x0, double y0, double x1, double y1) {
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

BoolRing triangle(double x0, double y0, double x1, double y1, double x2,
                  double y2) {
    return {{x0, y0}, {x1, y1}, {x2, y2}};
}

double totalArea(const std::vector<BoolRing>& loops) {
    // Signed sum: holes trace clockwise and subtract automatically.
    double a = 0.0;
    for (const BoolRing& r : loops) a += boolRingArea(r);
    return std::abs(a);
}

bool near(double a, double b, double tol = 0.05) {
    return std::abs(a - b) <= tol;
}

}  // namespace

int main() {
    // Overlapping squares: 10x10 at (0,0) and (5,5).
    const BoolRing a = square(0, 0, 10, 10);
    const BoolRing b = square(5, 5, 15, 15);
    CHECK(near(totalArea(booleanOp({a}, {b}, BoolOp::Union)), 175.0));
    CHECK(near(totalArea(booleanOp({a}, {b}, BoolOp::Intersection)), 25.0));
    CHECK(near(totalArea(booleanOp({a}, {b}, BoolOp::Difference)), 75.0));
    CHECK(near(totalArea(booleanOp({b}, {a}, BoolOp::Difference)), 75.0));
    // XOR overlap now traces outer + notch loops; drop the probe.
    CHECK(near(totalArea(booleanOp({a}, {b}, BoolOp::Xor)), 150.0));

    // Edge-aligned touch: shared edge must not glue or vanish wrongly.
    const BoolRing c = square(10, 0, 20, 10);
    CHECK(near(totalArea(booleanOp({a}, {c}, BoolOp::Union)), 200.0));
    CHECK(booleanOp({a}, {c}, BoolOp::Intersection).empty());
    CHECK(near(totalArea(booleanOp({a}, {c}, BoolOp::Difference)), 100.0));
    CHECK(near(totalArea(booleanOp({a}, {c}, BoolOp::Xor)), 200.0));

    // Identical inputs: union/intersection keep one, difference/xor vanish.
    CHECK(near(totalArea(booleanOp({a}, {a}, BoolOp::Union)), 100.0));
    CHECK(near(totalArea(booleanOp({a}, {a}, BoolOp::Intersection)), 100.0));
    CHECK(booleanOp({a}, {a}, BoolOp::Difference).empty());
    CHECK(booleanOp({a}, {a}, BoolOp::Xor).empty());

    // Containment: small square inside big (2,2,4,4 in 0,0,10,10).
    const BoolRing small = square(2, 2, 4, 4);
    CHECK(near(totalArea(booleanOp({a}, {small}, BoolOp::Union)), 100.0));
    CHECK(near(totalArea(booleanOp({a}, {small}, BoolOp::Intersection)), 4.0));
    CHECK(near(totalArea(booleanOp({a}, {small}, BoolOp::Difference)), 96.0));
    CHECK(near(totalArea(booleanOp({small}, {a}, BoolOp::Difference)), 0.0));

    // Disjoint: union sums, intersection/difference trivial.
    const BoolRing far = square(50, 50, 60, 60);
    CHECK(near(totalArea(booleanOp({a}, {far}, BoolOp::Union)), 200.0));
    CHECK(booleanOp({a}, {far}, BoolOp::Intersection).empty());
    CHECK(near(totalArea(booleanOp({a}, {far}, BoolOp::Difference)), 100.0));
    CHECK(near(totalArea(booleanOp({a}, {far}, BoolOp::Xor)), 200.0));

    // Triangle crossing a square: smoke + area bounds.
    const BoolRing tri = triangle(-5, 5, 20, -5, 20, 15);
    const double uArea = totalArea(booleanOp({a}, {tri}, BoolOp::Union));
    const double iArea = totalArea(booleanOp({a}, {tri}, BoolOp::Intersection));
    const double dArea = totalArea(booleanOp({a}, {tri}, BoolOp::Difference));
    CHECK(uArea > 100.0 && uArea < 400.0);
    CHECK(iArea > 0.0 && iArea < 100.0);
    CHECK(near(uArea, 100.0 + totalArea({tri}) - iArea, 1.0));
    CHECK(near(dArea, 100.0 - iArea, 0.5));

    // Even-odd hole (concentric squares, opposite windings): union keeps
    // the ring, intersection with the hole footprint is empty-ish.
    const BoolRing outer = square(0, 0, 10, 10);
    const BoolRing hole = square(3, 3, 7, 7);
    const double ringArea =
        totalArea(booleanOp({outer}, {hole}, BoolOp::Difference));
    CHECK(near(ringArea, 100.0 - 16.0));
    // A square strictly inside the hole punches nothing out of the ring.
    const BoolRing speck = square(4, 4, 5, 5);
    CHECK(near(totalArea(booleanOp({outer, hole}, {speck}, BoolOp::Difference,
                                   BoolFill::EvenOdd)),
               100.0 - 16.0));

    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
