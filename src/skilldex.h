#ifndef SKILLDEX_H
#define SKILLDEX_H

#include "obj_types.h"

namespace fallout {

enum SkilldexRC : int {
    SKILLDEX_RC_ERROR = -1,
    SKILLDEX_RC_CANCELED,
    SKILLDEX_RC_SNEAK,
    SKILLDEX_RC_LOCKPICK,
    SKILLDEX_RC_STEAL,
    SKILLDEX_RC_TRAPS,
    SKILLDEX_RC_FIRST_AID,
    SKILLDEX_RC_DOCTOR,
    SKILLDEX_RC_SCIENCE,
    SKILLDEX_RC_REPAIR,
    SKILLDEX_RC_COUNT,
};

// [target] - object the skill is for (action menu), the mobile UI shows
// the list next to it.
SkilldexRC skilldexOpen(Object* target = nullptr);
int skilldexGetWindow();

} // namespace fallout

#endif /* SKILLDEX_H */
