#include <ultra64.h>

#include "sm64.h"
#include "types.h"
#include "macros.h"
#include "object_fields.h"
#include "object_constants.h"
#include "behavior_data.h"
#include "engine/math_util.h"
#include "audio/external.h"
#include "ge64_bosses.h"
#include "interaction.h"
#include "memory.h"
#include "object_helpers.h"
#include "object_list_processor.h"

// Shots that bosses and other "attack this a specific way" objects need special handling for.
// Damage accumulates per object; at a threshold the object's own vanilla response is triggered
// (thrown, ground pounded...), so its health, cutscenes and star spawns all run as normal.

#define GE64_MAX_BOSS_DAMAGE 6

struct GeBossDamage {
    struct Object *obj;
    const BehaviorScript *bhv;
    f32 damage;
};

static struct GeBossDamage sGeBossDamage[GE64_MAX_BOSS_DAMAGE];
static struct Object *sGePendingPound;

#define GE64_KING_BOBOMB_STAGGER 8.0f  // eight PP7 shots per "throw"
#define GE64_KING_WHOMP_POUND    6.0f  // shots into his back per "ground pound"
#define GE64_WHOMP_POUND         2.0f
#define GE64_WOODEN_POST_POUND   2.0f
#define GE64_BOWSER_STAGGER     10.0f  // halved when shot from behind

void ge64_bosses_level_init(void) {
    s32 i;

    for (i = 0; i < GE64_MAX_BOSS_DAMAGE; i++) {
        sGeBossDamage[i].obj = NULL;
    }
    sGePendingPound = NULL;
}

static f32 *ge64_boss_damage(struct Object *obj) {
    struct GeBossDamage *slot = NULL;
    s32 i;

    for (i = 0; i < GE64_MAX_BOSS_DAMAGE; i++) {
        if (sGeBossDamage[i].obj == obj && sGeBossDamage[i].bhv == obj->behavior) {
            return &sGeBossDamage[i].damage;
        }
        if (slot == NULL && (sGeBossDamage[i].obj == NULL
                             || !(sGeBossDamage[i].obj->activeFlags & ACTIVE_FLAG_ACTIVE))) {
            slot = &sGeBossDamage[i];
        }
    }
    if (slot == NULL) {
        slot = &sGeBossDamage[0];
    }
    slot->obj = obj;
    slot->bhv = obj->behavior;
    slot->damage = 0.0f;
    return &slot->damage;
}

static s32 ge64_is(struct Object *obj, const BehaviorScript *bhv) {
    return obj->behavior == segmented_to_virtual(bhv);
}

/**
 * Called by cur_obj_is_mario_ground_pounding_platform: lets shots stand in for Mario ground
 * pounding a whomp's back or a wooden post. One pound per trigger.
 */
s32 ge64_take_pending_pound(struct Object *obj) {
    if (sGePendingPound == obj) {
        sGePendingPound = NULL;
        return TRUE;
    }
    return FALSE;
}

s32 ge64_pound_pending(struct Object *obj) {
    return sGePendingPound == obj;
}

/**
 * Knock King Bob-omb over as though Mario had thrown him; his landing code takes the health.
 */
static void ge64_king_bobomb_hit(struct Object *obj, s16 shotYaw, f32 damage) {
    f32 *dmg;

    if (obj->oAction != 2 || obj->oHeldState != HELD_FREE) {
        return;
    }
    dmg = ge64_boss_damage(obj);
    *dmg += damage;
    if (*dmg >= GE64_KING_BOBOMB_STAGGER) {
        *dmg = 0.0f;
        obj->oAction = 4; // thrown: health drops when he lands (unless he lands off the hill)
        obj->oMoveAngleYaw = shotYaw;
        obj->oForwardVel = 20.0f;
        obj->oVelY = 35.0f;
    }
}

/**
 * Whomps lying flat take shots in the back as ground pounds.
 */
static void ge64_whomp_hit(struct Object *obj, f32 damage, f32 threshold) {
    f32 *dmg;

    if (obj->oAction != 6 || obj->oSubAction != 0) {
        return;
    }
    dmg = ge64_boss_damage(obj);
    *dmg += damage;
    if (*dmg >= threshold) {
        *dmg = 0.0f;
        sGePendingPound = obj;
    }
}

static void ge64_wooden_post_hit(struct Object *obj, f32 damage) {
    f32 *dmg = ge64_boss_damage(obj);

    *dmg += damage;
    if (*dmg >= GE64_WOODEN_POST_POUND) {
        *dmg = 0.0f;
        sGePendingPound = obj;
    }
}

static s32 ge64_bowser_can_be_staggered(struct Object *obj) {
    switch (obj->oAction) {
        case BOWSER_ACT_DEFAULT:
        case BOWSER_ACT_BREATH_FIRE:
        case BOWSER_ACT_UNUSED_SLOW_WALK:
        case BOWSER_ACT_WALK_TO_MARIO:
        case BOWSER_ACT_CHARGE_MARIO:
        case BOWSER_ACT_SPIT_FIRE_INTO_SKY:
        case BOWSER_ACT_SPIT_FIRE_ONTO_FLOOR:
        case BOWSER_ACT_TURN_FROM_EDGE:
        case BOWSER_ACT_HIT_EDGE:
        case BOWSER_ACT_QUICK_JUMP:
            return obj->oHeldState == HELD_FREE;
    }
    return FALSE;
}

/**
 * Shots shove Bowser; enough of them knocks him flying as if Mario had swung and thrown him, and
 * his thrown action checks for nearby bombs as usual. Shots from behind count double.
 */
static void ge64_bowser_hit(struct Object *obj, s16 shotYaw, f32 damage, struct GeBossPush *push) {
    f32 *dmg;
    s16 diff = shotYaw - obj->oFaceAngleYaw;
    s32 fromBehind = diff > -0x4000 && diff < 0x4000;

    if (!ge64_bowser_can_be_staggered(obj)) {
        return;
    }
    push->obj = obj;
    push->force = fromBehind ? 1.5f : 0.8f;

    dmg = ge64_boss_damage(obj);
    *dmg += fromBehind ? damage * 2.0f : damage;
    if (*dmg >= GE64_BOWSER_STAGGER) {
        *dmg = 0.0f;
        obj->oAction = BOWSER_ACT_THROWN;
        obj->oMoveAngleYaw = shotYaw;
        obj->oForwardVel = 60.0f;
        obj->oVelY = 30.0f;
        // A real throw starts in the air; his thrown action stops him the moment he's grounded
        obj->oPosY += 40.0f;
        obj->oMoveFlags = 0;
    }
}

static s32 ge64_is_bully(struct Object *obj, s32 *small) {
    if (ge64_is(obj, bhvSmallBully) || ge64_is(obj, bhvSmallChillBully)) {
        *small = TRUE;
        return TRUE;
    }
    if (ge64_is(obj, bhvBigBully) || ge64_is(obj, bhvBigBullyWithMinions) || ge64_is(obj, bhvBigChillBully)) {
        *small = FALSE;
        return TRUE;
    }
    return FALSE;
}

/**
 * Returns TRUE if the object was handled here (the caller then skips the generic punch/push).
 * push is filled in when the caller should also shove the object (scaled by weapon force).
 */
s32 ge64_boss_on_hit(struct Object *obj, s16 shotYaw, f32 damage, f32 force, struct GeBossPush *push) {
    s32 small;

    push->obj = NULL;

    if (ge64_is(obj, bhvKingBobomb)) {
        ge64_king_bobomb_hit(obj, shotYaw, damage);
        return TRUE;
    }
    if (ge64_is(obj, bhvWhompKingBoss)) {
        ge64_whomp_hit(obj, damage, GE64_KING_WHOMP_POUND);
        return TRUE;
    }
    if (ge64_is(obj, bhvSmallWhomp)) {
        ge64_whomp_hit(obj, damage, GE64_WHOMP_POUND);
        return TRUE;
    }
    if (ge64_is(obj, bhvWoodenPost)) {
        ge64_wooden_post_hit(obj, damage);
        return TRUE;
    }
    if (ge64_is(obj, bhvBowser)) {
        ge64_bowser_hit(obj, shotYaw, damage, push);
        return TRUE;
    }
    if (ge64_is(obj, bhvBowserTailAnchor)) {
        return TRUE;
    }
    if (ge64_is_bully(obj, &small)) {
        // Shove it the way Mario's body does (interact_bully), scaled by the gun, so the bully's
        // own physics carry it into lava
        f32 scale = force / 40.0f;
        scale = MIN(MAX(scale, 0.25f), 1.5f);
        obj->oMoveAngleYaw = shotYaw;
        obj->oForwardVel = 3392.0f / MAX(obj->hitboxRadius, 1.0f) * scale * (small ? 1.0f : 0.7f);
        obj->oInteractStatus |= INT_STATUS_INTERACTED;
        return TRUE;
    }
    return FALSE;
}
