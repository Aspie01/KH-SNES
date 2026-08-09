// An ActType must not be usable as an integer without saying so.
//
// The numeric values are load-bearing -- the engine does range comparisons on
// them -- which is exactly why arithmetic on one has to be deliberate.  The
// range predicates in actor.h are how it is meant to be asked.
#include "actor.h"

int slot() { return kh::ActType::Log + 1; }
