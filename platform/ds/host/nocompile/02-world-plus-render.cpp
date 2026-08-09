// `World + Render` must not compile.
//
// They are Q12.4 and Q19.12.  Adding them is adding sixteenths to
// four-thousand-and-ninety-sixths, and the result is meaningless in a way no
// runtime check would catch -- it is simply a number, four hundred times too
// large or too small depending on which way round it went.
#include "fixed.h"

kh::World add(kh::World a, kh::Render b) { return a + b; }
