// fixed.h -- the fixed-point scalar, and the reason this target is C++ at all.
//
// The DS has no FPU.  Every position, velocity, extent and transform in the
// engine is therefore an integer with an implied binary point, and the failure
// mode of that is silent: add a Q12.4 to a Q20.12 and you get a number, not a
// diagnostic.  Wrapping the representation in a type with no implicit
// conversions turns that entire class of bug into a compile error, which is
// worth a language change on its own.
//
// F is the number of FRACTIONAL bits, so the SNES build's Q12.4 is Fixed<4>.
//
// Two formats, and the distinction is load-bearing:
//
//   World  Fixed<4>   -- gameplay.  Identical semantics to the 65816 build, so
//                        the two simulations can be run against each other
//                        frame for frame (see docs/BEHAVIOUR.md §10).  Every
//                        tuning number in that document is a Fixed<4> raw
//                        value and can be pasted in as one.
//   Render Fixed<12>  -- the DS's own 1.19.12 matrix and vertex format.
//
// Conversion between them is explicit and happens at the rendering boundary
// only.  Do not be tempted to "simplify" by making gameplay 12-bit: the extra
// precision buys nothing a 16 px tile can see, and it costs the bit-exactness
// that makes the old build a usable oracle.
//
// Division is deliberately absent from the fast paths.  ARMv5TE has no divide
// instruction, so operator/ is a libgcc call costing tens of cycles; the SNES
// engine never divided either, and every ratio it needed was a shift.  Keep it
// that way and the port stays honest.

#pragma once

#include <stdint.h>

template <int F>
class Fixed {
public:
    // Deliberately not a converting constructor: Fixed<4> f = 3; must not
    // compile, because "3" is ambiguous between three pixels and three
    // sixteenths and the whole point of this type is to make you say which.
    constexpr Fixed() : v_(0) {}

    static constexpr Fixed fromRaw(int32_t raw) { return Fixed(raw, Raw{}); }
    static constexpr Fixed fromInt(int32_t whole) {
        return Fixed(whole << F, Raw{});
    }
    // Whole pixels plus a fraction of 1/2^F, spelled out at the call site.
    static constexpr Fixed fromParts(int32_t whole, int32_t frac) {
        return Fixed((whole << F) + frac, Raw{});
    }

    constexpr int32_t raw() const { return v_; }
    // Truncates toward negative infinity, which is what a tile lookup wants:
    // an arithmetic shift of a negative coordinate lands in the cell the
    // coordinate is actually inside, where a divide would round toward zero
    // and put -1 px in cell 0.
    constexpr int32_t toInt() const { return v_ >> F; }

    constexpr Fixed operator+(Fixed o) const { return fromRaw(v_ + o.v_); }
    constexpr Fixed operator-(Fixed o) const { return fromRaw(v_ - o.v_); }
    constexpr Fixed operator-() const { return fromRaw(-v_); }

    // 64-bit intermediate: Fixed<12> world coordinates reach 2^21, and their
    // product overflows int32 well before the shift brings it back down.
    // The ARM9 does this in one smull, so it is not worth a narrow version.
    constexpr Fixed operator*(Fixed o) const {
        return fromRaw(int32_t((int64_t(v_) * o.v_) >> F));
    }
    // Scaling by a plain count is not a fixed-point multiply and should not
    // pay for one.
    constexpr Fixed operator*(int32_t n) const { return fromRaw(v_ * n); }
    constexpr Fixed operator>>(int n) const { return fromRaw(v_ >> n); }
    constexpr Fixed operator<<(int n) const { return fromRaw(v_ << n); }

    constexpr Fixed& operator+=(Fixed o) { v_ += o.v_; return *this; }
    constexpr Fixed& operator-=(Fixed o) { v_ -= o.v_; return *this; }

    constexpr bool operator==(Fixed o) const { return v_ == o.v_; }
    constexpr bool operator!=(Fixed o) const { return v_ != o.v_; }
    constexpr bool operator< (Fixed o) const { return v_ <  o.v_; }
    constexpr bool operator<=(Fixed o) const { return v_ <= o.v_; }
    constexpr bool operator> (Fixed o) const { return v_ >  o.v_; }
    constexpr bool operator>=(Fixed o) const { return v_ >= o.v_; }

    constexpr Fixed abs() const { return fromRaw(v_ < 0 ? -v_ : v_); }

private:
    struct Raw {};
    constexpr Fixed(int32_t raw, Raw) : v_(raw) {}
    int32_t v_;
};

using World  = Fixed<4>;        // gameplay: Q12.4, as the 65816 build had it
using Render = Fixed<12>;       // the DS's 1.19.12 vertex and matrix format

// The only sanctioned crossing between the two, and it is a widening shift so
// it is exact.  Nothing converts the other way: rendering never feeds back
// into the simulation.
constexpr Render toRender(World w) { return Render::fromRaw(w.raw() << 8); }

// Geometry constants, straight from docs/BEHAVIOUR.md §1.  Tiles are 16 px, so
// a tile index is a shift of the whole part -- four bits of fraction plus four
// bits of pixel, which is why World::raw() >> 8 is a tile coordinate directly.
constexpr int TILE_PX      = 16;
constexpr int TILE_SHIFT   = 4;
constexpr int WORLD_TO_TILE_SHIFT = TILE_SHIFT + 4;

constexpr int32_t tileOf(World w) { return w.raw() >> WORLD_TO_TILE_SHIFT; }
constexpr World tileCentre(int32_t t) {
    return World::fromInt(t * TILE_PX + TILE_PX / 2);
}

static_assert(sizeof(World) == 4, "the whole engine indexes arrays of these");
static_assert(World::fromInt(16).raw() == 256, "Q12.4 parity with the SNES");
static_assert(tileOf(World::fromInt(-1)) == -1, "must floor, not truncate");
