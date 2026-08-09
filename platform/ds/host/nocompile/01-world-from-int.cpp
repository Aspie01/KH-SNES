// `World w = 3;` must not compile.
//
// Q12.4 means 3 is three sixteenths of a pixel, and nobody who writes this
// means that -- they mean three pixels, which is World::fromInt(3).  An
// implicit constructor would make the whole fixed-point type a suggestion.
#include "fixed.h"

kh::World w = 3;
