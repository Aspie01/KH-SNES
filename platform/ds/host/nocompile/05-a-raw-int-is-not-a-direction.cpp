// A plain int must not pass where a Dir is wanted.
//
// dirVelX and dirVelY are indexed by facing, and the eight values are an order
// the velocity tables depend on.  Passing 2 because "east is two" is how a
// table and its index stop agreeing.
#include "constants.h"

kh::Dir east() { return 2; }
