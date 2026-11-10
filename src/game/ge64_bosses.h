#ifndef GE64_BOSSES_H
#define GE64_BOSSES_H

#include <PR/ultratypes.h>

#include "types.h"

struct GeBossPush {
    struct Object *obj; // object to shove, or NULL
    f32 force;          // multiplier on the weapon's force
};

void ge64_bosses_level_init(void);
s32 ge64_boss_on_hit(struct Object *obj, s16 shotYaw, f32 damage, f32 force, struct GeBossPush *push);
s32 ge64_take_pending_pound(struct Object *obj);
s32 ge64_pound_pending(struct Object *obj);

#endif // GE64_BOSSES_H
