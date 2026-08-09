// A TownStage must not be comparable to a DiveStage.
//
// §0.3: "enum class for state machines so a TownStage cannot be compared to a
// DiveStage".  Both are small integers counting from zero, and the town's
// T_BOSS is 5 where the dive's DIVE_S2_FIGHT is 5 -- so an unscoped enum would
// make that comparison true and silent.
#include "constants.h"

bool same() { return kh::TownStage::Boss == kh::DiveStage::S2Fight; }
