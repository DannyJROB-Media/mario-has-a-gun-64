#include <ultra64.h>

#include "sm64.h"
#include "types.h"
#include "macros.h"
#include "object_fields.h"
#include "object_constants.h"
#include "surface_terrains.h"
#include "mario_animation_ids.h"
#include "ge_sounds.h"
#include "audio/external.h"
#include "behavior_data.h"
#include "behavior_actions.h"
#include "engine/behavior_script.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "area.h"
#include "camera.h"
#include "game_init.h"
#include "ge64.h"
#include "ge64_bosses.h"
#include "ingame_menu.h"
#include "interaction.h"
#include "level_update.h"
#include "mario.h"
#include "mario_step.h"
#include "memory.h"
#include "model_ids.h"
#include "object_helpers.h"
#include "object_list_processor.h"
#include "print.h"
#include "segment2.h"

struct GeGunPart {
    const Gfx *dl;
    u16 switchMask; // GE model switches that must all be enabled for this part to draw
    u8 translucent;
    u8 hand;        // Bond's hand/sleeve: never drawn, Mario's glove replaces it
};

struct GeGunModel {
    const struct GeGunPart *parts;
    s32 numParts;
    const f32 (*matrixPos)[3]; // rest position of each GE model matrix (segment 0x03 slot)
    s32 numMatrices;
    u8 *romStart; // MIO0 segment holding the textures, vertices and display lists (segment 0x0E)
    u8 *romEnd;
};

#include "ge64_guns/pp7.inc.h"
#include "ge64_guns/kf7.inc.h"
#include "ge64_guns/dd44.inc.h"
#include "ge64_guns/klobb.inc.h"
#include "ge64_guns/zmg.inc.h"
#include "ge64_guns/d5k.inc.h"
#include "ge64_guns/phantom.inc.h"
#include "ge64_guns/ar33.inc.h"
#include "ge64_guns/rcp90.inc.h"
#include "ge64_guns/shotgun.inc.h"
#include "ge64_guns/autoshot.inc.h"
#include "ge64_guns/sniper.inc.h"
#include "ge64_guns/cougar.inc.h"
#include "ge64_guns/goldengun.inc.h"
#include "ge64_guns/laser.inc.h"
#include "ge64_guns/poses.inc.h"

extern s32 should_begin_sliding(struct MarioState *m);
extern s32 check_fall_damage_or_get_stuck(struct MarioState *m, u32 hardFallAction);
extern s32 lava_boost_on_wall(struct MarioState *m);
extern void ge64_clear_z_buffer(void);

#ifndef DEGREES
#define DEGREES(x) ((s16)((x) * 0x10000 / 360))
#endif

#ifndef CLAMP
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif

#define GE64_EYE_HEIGHT         120.0f
#define GE64_CROUCH_EYE_HEIGHT   70.0f
#define GE64_SWIM_EYE_HEIGHT     60.0f
#define GE64_FOV                 60.0f
#define GE64_NEAR                20
#define GE64_RANGE            12000.0f
#define GE64_PITCH_LIMIT        0x3800
#define GE64_TAN_HALF_FOV_Y      0.57735f // tan(30 deg)
#define GE64_TAN_HALF_FOV_X      0.76980f // tan(30 deg) * 4/3

#define GE64_WALK_SPEED          26.0f
#define GE64_BACK_SPEED          18.0f
#define GE64_STRAFE_SPEED        20.0f
#define GE64_CROUCH_SPEED_SCALE   0.5f
#define GE64_GROUND_ACCEL         3.0f
#define GE64_GROUND_DECEL         5.0f
#define GE64_AIR_ACCEL            1.2f
#define GE64_AIR_DRAG             0.35f // how fast air speed above walking speed bleeds off
#define GE64_TURN_SPEED          0x500
#define GE64_LOOK_SPEED          0x180
#define GE64_AIM_CROSSHAIR_SPEED  0.07f
#define GE64_AIM_EDGE_TURN       0x200
#define GE64_SCOPE_TURN          0x180

// Gun jump (the long jump's replacement): shooting the floor close below you launches you
#define GE64_GUN_JUMP_RANGE      450.0f
#define GE64_GUN_JUMP_MAX_SPEED   56.0f

// Objects a shot can hit and deliver a Mario attack to
#define GE64_ATTACKABLE (INTERACT_BOUNCE_TOP | INTERACT_BOUNCE_TOP2 | INTERACT_HIT_FROM_BELOW | INTERACT_KOOPA \
                         | INTERACT_BREAKABLE | INTERACT_BULLY | INTERACT_UNKNOWN_08 | INTERACT_GRABBABLE \
                         | INTERACT_DAMAGE | INTERACT_MR_BLIZZARD | INTERACT_SHOCK | INTERACT_CLAM_OR_BUBBA)
// Of those, the ones that also get shoved by the bullet (enemies that shrug off Mario's attacks)
#define GE64_PUSHABLE (INTERACT_BULLY | INTERACT_DAMAGE | INTERACT_MR_BLIZZARD | INTERACT_SHOCK | INTERACT_CLAM_OR_BUBBA)

// Decompressed gun models live in two buffers (sidearm and primary slot), linked at segment 0x0E
#define GE64_GUN_BUFFER_SIZE 0x14000 // largest gun (Cougar Magnum) is ~78KB decompressed
#define GE64_GUN_SEGMENT 0x0E

// Mario's glove holding the gun: rotation in degrees, scale in GE model units
#define GE64_GLOVE_SCALE 0.7f
#define GE64_GLOVE_ROT_X 0
#define GE64_GLOVE_ROT_Y (-90)
#define GE64_GLOVE_ROT_Z 25
// The gun is turned slightly inward and tipped down, as GoldenEye holds it
#define GE64_GUN_YAW   0x071C // 10 degrees
// Title screen PP7, in the title camera's view space
#define GE64_TITLE_GUN_X     (-360.0f)
#define GE64_TITLE_GUN_Y     (-10.0f)
#define GE64_TITLE_GUN_Z     (-900.0f)
#define GE64_TITLE_GUN_RX    ((s16) 0xC800)
#define GE64_TITLE_GUN_RY    ((s16) 0x4000)
#define GE64_TITLE_GUN_RZ    ((s16) 0x0A00)
#define GE64_TITLE_GUN_SCALE 1.6f
#define GE64_GUN_PITCH 0x0444 // 6 degrees

struct GeWeapon {
    const char *name;
    const struct GeGunModel *model;
    s32 fireSound;
    s16 magSize;    // 0: never needs reloading (Moonraker laser)
    u8 fireDelay;   // frames between shots
    u8 automatic;
    u8 reloadFrames;
    u8 pellets;     // rays per shot (shotguns)
    f32 spreadDeg;  // hip-fire inaccuracy
    f32 force;      // push impulse (also drives the gun jump and boss knockback)
    f32 damage;     // boss damage per pellet
    s16 recoil;     // view kick
    f32 zoom;       // R aims down a scope when > 1
    f32 viewPos[3]; // gun model origin in view space
    f32 grip[3];    // Mario's glove, in GE model units
    u16 switches;   // GE model switches shown normally
    u16 flashSwitches; // switches shown while the muzzle flash is up
};

#define GE64_WEAPON_PP7 0

static const struct GeWeapon sGeWeapons[] = {
    { "PP7", &ge64_gun_pp7, SOUND_GE_PP7_FIRE, 7, 4, FALSE, 40, 1, 0.5f, 28.0f, 1.0f, 0, 1.0f,
      GE64_POSE_PP7, GE64_PP7_DEFAULT_SWITCHES, GE64_PP7_FLASH_SWITCHES },
    { "DD44 Dostovei", &ge64_gun_dd44, SOUND_GE_DD44_FIRE, 8, 5, FALSE, 40, 1, 0.8f, 34.0f, 1.3f, 0, 1.0f,
      GE64_POSE_DD44, GE64_DD44_DEFAULT_SWITCHES, GE64_DD44_FLASH_SWITCHES },
    { "Klobb", &ge64_gun_klobb, SOUND_GE_KLOBB_FIRE, 20, 3, TRUE, 45, 1, 3.5f, 12.0f, 0.6f, 0x50, 1.0f,
      GE64_POSE_KLOBB, GE64_KLOBB_DEFAULT_SWITCHES, GE64_KLOBB_FLASH_SWITCHES },
    { "KF7 Soviet", &ge64_gun_kf7, SOUND_GE_KF7_FIRE, 30, 3, TRUE, 50, 1, 1.8f, 18.0f, 1.0f, 0x60, 1.0f,
      GE64_POSE_KF7, GE64_KF7_DEFAULT_SWITCHES, GE64_KF7_FLASH_SWITCHES },
    { "ZMG 9mm", &ge64_gun_zmg, SOUND_GE_ZMG_FIRE, 32, 2, TRUE, 45, 1, 2.5f, 12.0f, 0.7f, 0x40, 1.0f,
      GE64_POSE_ZMG, GE64_ZMG_DEFAULT_SWITCHES, GE64_ZMG_FLASH_SWITCHES },
    { "D5K Deutsche", &ge64_gun_d5k, SOUND_GE_D5K_FIRE, 30, 2, TRUE, 45, 1, 1.4f, 14.0f, 0.8f, 0x40, 1.0f,
      GE64_POSE_D5K, GE64_D5K_DEFAULT_SWITCHES, GE64_D5K_FLASH_SWITCHES },
    { "Phantom", &ge64_gun_phantom, SOUND_GE_KF7_FIRE, 50, 2, TRUE, 50, 1, 2.0f, 14.0f, 0.8f, 0x40, 1.0f,
      GE64_POSE_PHANTOM, GE64_PHANTOM_DEFAULT_SWITCHES, GE64_PHANTOM_FLASH_SWITCHES },
    { "AR33 Assault", &ge64_gun_ar33, SOUND_GE_AR33_FIRE, 30, 3, TRUE, 50, 1, 1.0f, 20.0f, 1.1f, 0x50, 2.0f,
      GE64_POSE_AR33, GE64_AR33_DEFAULT_SWITCHES, GE64_AR33_FLASH_SWITCHES },
    { "RCP-90", &ge64_gun_rcp90, SOUND_GE_RCP90_FIRE, 80, 2, TRUE, 55, 1, 1.2f, 16.0f, 0.9f, 0x40, 1.0f,
      GE64_POSE_RCP90, GE64_RCP90_DEFAULT_SWITCHES, GE64_RCP90_FLASH_SWITCHES },
    { "Shotgun", &ge64_gun_shotgun, SOUND_GE_SHOTGUN_FIRE, 5, 20, FALSE, 50, 6, 5.0f, 10.0f, 0.6f, 0x100, 1.0f,
      GE64_POSE_SHOTGUN, GE64_SHOTGUN_DEFAULT_SWITCHES, GE64_SHOTGUN_FLASH_SWITCHES },
    { "Automatic Shotgun", &ge64_gun_autoshot, SOUND_GE_AUTOSHOT_FIRE, 5, 8, TRUE, 55, 6, 5.0f, 9.0f, 0.5f, 0xC0, 1.0f,
      GE64_POSE_AUTOSHOT, GE64_AUTOSHOT_DEFAULT_SWITCHES, GE64_AUTOSHOT_FLASH_SWITCHES },
    { "Sniper Rifle", &ge64_gun_sniper, SOUND_GE_SILENCED_FIRE, 8, 15, FALSE, 55, 1, 0.2f, 40.0f, 2.5f, 0x80, 4.0f,
      GE64_POSE_SNIPER, GE64_SNIPER_DEFAULT_SWITCHES, GE64_SNIPER_FLASH_SWITCHES },
    { "Cougar Magnum", &ge64_gun_cougar, SOUND_GE_RCP90_FIRE_ALT, 6, 12, FALSE, 55, 1, 0.5f, 70.0f, 3.0f, 0x140, 1.0f,
      GE64_POSE_COUGAR, GE64_COUGAR_DEFAULT_SWITCHES, GE64_COUGAR_FLASH_SWITCHES },
    { "Golden Gun", &ge64_gun_goldengun, SOUND_GE_D5K_FIRE, 1, 20, FALSE, 40, 1, 0.0f, 120.0f, 12.0f, 0x180, 1.0f,
      GE64_POSE_GOLDENGUN, GE64_GOLDENGUN_DEFAULT_SWITCHES, GE64_GOLDENGUN_FLASH_SWITCHES },
    { "Moonraker Laser", &ge64_gun_laser, SOUND_GE_LASER_FIRE, 0, 3, TRUE, 0, 1, 0.3f, 15.0f, 0.9f, 0x20, 1.0f,
      GE64_POSE_LASER, GE64_LASER_DEFAULT_SWITCHES, GE64_LASER_FLASH_SWITCHES },
};

#define GE64_SLOT_SIDEARM 0
#define GE64_SLOT_PRIMARY 1

struct GePush {
    struct Object *obj;
    const BehaviorScript *bhv;
    f32 velX;
    f32 velZ;
};

#define GE64_MAX_PUSHES 8
#define GE64_SOUND_POSITIONS 8

static struct {
    u8 initialized;
    u8 viewActive;
    u8 aiming;
    u8 autoAimValid;
    u8 gunJumpCharge; // air boosts left this jump
    s16 pitch;
    s16 slot;         // GE64_SLOT_SIDEARM or GE64_SLOT_PRIMARY
    s16 primary;      // index into sGeWeapons of the roguelike primary
    s16 ammo[2];      // per slot
    s16 fireCooldown;
    s16 reloadTimer;
    s16 switchTimer;
    s16 muzzleFlash;
    s16 gunKick;
    s16 stepTimer;
    s16 viewYaw;
    s16 viewPitch;
    s16 recoilPitch;  // temporary view kick from automatic fire, springs back
    s16 bannerTimer;  // "new weapon" announcement
    f32 crossX;       // aim crosshair, -1..1 (right is positive)
    f32 crossY;       // aim crosshair, -1..1 (up is positive)
    f32 autoAimX;
    f32 autoAimY;
    f32 bobPhase;
    f32 bobAmount;
    f32 eyeHeight;
    Vec3f eye;
    Vec3f forward;
    Vec3f right;
    Vec3f up;
} sGe;

// Gun model buffers, one per slot; a slot only reloads once the RSP can no longer be reading it
static u8 *sGeGunBuffer[2];
static const struct GeGunModel *sGeLoadedModel[2];
static u32 sGeLastDrawnFrame[2];

static struct GePush sGePushes[GE64_MAX_PUSHES];
static struct Controller *sGeMaskedController;
static u16 sGeSavedButtonDown;
#ifdef GE64_LEVEL_SELECT
static s32 sGeDebugSpawns, sGeDebugObjHits, sGeDebugWallHits, sGeDebugButtons, sGeDebugTarget, sGeDebugTargetY;
#endif
static Vec3f sGeSoundPositions[GE64_SOUND_POSITIONS];
static s32 sGeSoundPositionIndex;

static struct GraphNodePerspective *sGeSavedPerspective;
static s16 sGeSavedNear;

#include "ge64_glove.inc.h"

// Mario's white glove (same light as his model's) and the suit sleeve
static const Lights1 ge64_glove_lights = gdSPDefLights1(0x7F, 0x7F, 0x7F, 0xFF, 0xFF, 0xFF, 0x28, 0x28, 0x28);
// Reflective (environment-mapped) gun parts: the Golden Gun's gold and the Cougar's chrome
static const Lights1 ge64_gold_lights = gdSPDefLights1(0x70, 0x50, 0x10, 0xFF, 0xD0, 0x40, 0x28, 0x28, 0x28);
static const Lights1 ge64_chrome_lights = gdSPDefLights1(0x60, 0x60, 0x68, 0xE0, 0xE0, 0xE8, 0x28, 0x28, 0x28);
static const Lights1 ge64_sleeve_lights = gdSPDefLights1(0x08, 0x08, 0x0A, 0x1C, 0x1C, 0x22, 0x28, 0x28, 0x28); // suit sleeve

static const struct GeWeapon *ge64_current_weapon(void) {
    return &sGeWeapons[sGe.slot == GE64_SLOT_SIDEARM ? GE64_WEAPON_PP7 : sGe.primary];
}

static s32 ge64_scoped(void) {
    return sGe.aiming && ge64_current_weapon()->zoom > 1.0f;
}

/**************************************************
 *                 ACTION REMAPPING               *
 **************************************************/

/**
 * Map Mario's free-movement actions onto the first-person ones. Called from set_mario_action after
 * the vanilla per-group setup has run, so jump velocities etc. are already applied.
 */
u32 ge64_remap_action(UNUSED struct MarioState *m, u32 action) {
    switch (action) {
        case ACT_IDLE:
        case ACT_START_SLEEPING:
        case ACT_SLEEPING:
        case ACT_WAKING_UP:
        case ACT_PANTING:
        case ACT_COUGHING:
        case ACT_SHIVERING:
        case ACT_STANDING_AGAINST_WALL:
        case ACT_CROUCHING:
        case ACT_START_CROUCHING:
        case ACT_STOP_CROUCHING:
        case ACT_START_CRAWLING:
        case ACT_STOP_CRAWLING:
        case ACT_FIRST_PERSON:
        case ACT_JUMP_LAND_STOP:
        case ACT_DOUBLE_JUMP_LAND_STOP:
        case ACT_FREEFALL_LAND_STOP:
        case ACT_SIDE_FLIP_LAND_STOP:
        case ACT_TRIPLE_JUMP_LAND_STOP:
        case ACT_BACKFLIP_LAND_STOP:
        case ACT_LONG_JUMP_LAND_STOP:
        case ACT_GROUND_POUND_LAND:
        case ACT_BRAKING_STOP:
        case ACT_BUTT_SLIDE_STOP:
        case ACT_TWIRL_LAND:
        case ACT_WALKING:
        case ACT_TURNING_AROUND:
        case ACT_FINISH_TURNING_AROUND:
        case ACT_BRAKING:
        case ACT_CRAWLING:
        case ACT_DECELERATING:
        case ACT_JUMP_LAND:
        case ACT_FREEFALL_LAND:
        case ACT_DOUBLE_JUMP_LAND:
        case ACT_SIDE_FLIP_LAND:
        case ACT_TRIPLE_JUMP_LAND:
        case ACT_BACKFLIP_LAND:
        case ACT_LONG_JUMP_LAND:
            return ACT_FPS_GROUND;

        case ACT_JUMP:
        case ACT_DOUBLE_JUMP:
        case ACT_TRIPLE_JUMP:
        case ACT_BACKFLIP:
        case ACT_SIDE_FLIP:
        case ACT_WALL_KICK_AIR:
        case ACT_LONG_JUMP:
        case ACT_FREEFALL:
        case ACT_JUMP_KICK:
        case ACT_DIVE:
        case ACT_STEEP_JUMP:
            return ACT_FPS_AIR;
    }

    return action;
}

/**************************************************
 *                    CONTROLS                    *
 **************************************************/

static f32 ge64_stick_curve(f32 v) {
    // Quadratic response with sign: fine control near the centre, full speed at the rim, like GoldenEye
    return v * (v < 0.0f ? -v : v);
}

/**
 * Turning, looking and the aim crosshair. Stick X turns (or moves the crosshair while aiming),
 * C-up/C-down look up and down.
 */
static void ge64_update_look(struct MarioState *m, s32 moving) {
    struct Controller *c = m->controller;
    f32 sx = c->stickX / 64.0f;
    f32 sy = c->stickY / 64.0f;
    u16 down = c->buttonDown;

    sGe.aiming = (down & R_TRIG) != 0;

    if (sGe.aiming && ge64_current_weapon()->zoom > 1.0f) {
        // Scoped: the crosshair stays centred and the stick turns the view slowly
        f32 zoom = ge64_current_weapon()->zoom;
        sGe.crossX = sGe.crossY = 0.0f;
        m->faceAngle[1] -= (s16)(ge64_stick_curve(sx) * GE64_SCOPE_TURN / zoom * 2.0f);
        sGe.pitch += (s16)(ge64_stick_curve(sy) * GE64_SCOPE_TURN / zoom * 2.0f);
    } else if (sGe.aiming) {
        sGe.crossX += sx * GE64_AIM_CROSSHAIR_SPEED;
        sGe.crossY += sy * GE64_AIM_CROSSHAIR_SPEED;
        sGe.crossX = CLAMP(sGe.crossX, -1.0f, 1.0f);
        sGe.crossY = CLAMP(sGe.crossY, -1.0f, 1.0f);

        // Pushing the crosshair against the edge of the screen turns the view, as in GoldenEye
        if (sGe.crossX >= 0.99f && sx > 0.0f) {
            m->faceAngle[1] -= (s16)(sx * GE64_AIM_EDGE_TURN);
        } else if (sGe.crossX <= -0.99f && sx < 0.0f) {
            m->faceAngle[1] -= (s16)(sx * GE64_AIM_EDGE_TURN);
        }
        if (sGe.crossY >= 0.99f && sy > 0.0f) {
            sGe.pitch += (s16)(sy * GE64_AIM_EDGE_TURN);
        } else if (sGe.crossY <= -0.99f && sy < 0.0f) {
            sGe.pitch += (s16)(sy * GE64_AIM_EDGE_TURN);
        }
    } else {
        sGe.crossX = approach_f32(sGe.crossX, 0.0f, 0.2f, 0.2f);
        sGe.crossY = approach_f32(sGe.crossY, 0.0f, 0.2f, 0.2f);
        m->faceAngle[1] -= (s16)(ge64_stick_curve(sx) * GE64_TURN_SPEED);

        if (down & U_CBUTTONS) {
            sGe.pitch += GE64_LOOK_SPEED;
        } else if (down & D_CBUTTONS) {
            sGe.pitch -= GE64_LOOK_SPEED;
        } else if (moving) {
            // GoldenEye's "look ahead": the view drifts back to level while walking
            sGe.pitch = approach_s32(sGe.pitch, 0, 0x60, 0x60);
        }
    }

    sGe.pitch = CLAMP(sGe.pitch, -GE64_PITCH_LIMIT, GE64_PITCH_LIMIT);
}

/**
 * Accelerate Mario's horizontal velocity toward the stick/C-button movement intent.
 * Stick Y walks forward and back, C-left/C-right strafe. Aiming freezes stick movement.
 */
static void ge64_update_move_velocity(struct MarioState *m, f32 accel, f32 decel) {
    struct Controller *c = m->controller;
    u16 down = c->buttonDown;
    s16 yaw = m->faceAngle[1];
    s16 rightYaw = yaw - 0x4000;
    f32 fwd = 0.0f;
    f32 strafe = 0.0f;
    f32 targetX, targetZ, dx, dz, dist, step;
    f32 scale = (m->action == ACT_FPS_CROUCH) ? GE64_CROUCH_SPEED_SCALE : 1.0f;

    if (!sGe.aiming) {
        f32 sy = ge64_stick_curve(c->stickY / 64.0f);
        fwd = sy * (sy >= 0.0f ? GE64_WALK_SPEED : GE64_BACK_SPEED);
    }
    if (down & R_CBUTTONS) {
        strafe += GE64_STRAFE_SPEED;
    }
    if (down & L_CBUTTONS) {
        strafe -= GE64_STRAFE_SPEED;
    }

    targetX = (fwd * sins(yaw) + strafe * sins(rightYaw)) * scale;
    targetZ = (fwd * coss(yaw) + strafe * coss(rightYaw)) * scale;

    dx = targetX - m->vel[0];
    dz = targetZ - m->vel[2];
    dist = sqrtf(dx * dx + dz * dz);
    // Speeding up uses accel, slowing down or changing direction uses the (faster) decel
    step = (targetX * targetX + targetZ * targetZ > m->vel[0] * m->vel[0] + m->vel[2] * m->vel[2]) ? accel : decel;
    // In the air, speed above walking pace (a gun jump) only bleeds off slowly
    if ((m->action & ACT_FLAG_AIR) && m->vel[0] * m->vel[0] + m->vel[2] * m->vel[2] > GE64_WALK_SPEED * GE64_WALK_SPEED) {
        step = MIN(step, GE64_AIR_DRAG);
    }
    if (dist > step) {
        dx *= step / dist;
        dz *= step / dist;
    }
    m->vel[0] += dx;
    m->vel[2] += dz;

    m->forwardVel = m->vel[0] * sins(yaw) + m->vel[2] * coss(yaw);
    m->slideVelX = m->vel[0];
    m->slideVelZ = m->vel[2];
}

/**************************************************
 *                     ACTIONS                    *
 **************************************************/

s32 act_fps_ground(struct MarioState *m) {
    struct Controller *c = m->controller;
    s32 crouched = (m->action == ACT_FPS_CROUCH);
    f32 speed;

    // Crouch like GoldenEye: hold R (aim) and press C-down; C-up stands back up
    if (c->buttonDown & R_TRIG) {
        if ((c->buttonPressed & D_CBUTTONS) && !crouched) {
            m->action = ACT_FPS_CROUCH;
            crouched = TRUE;
        } else if ((c->buttonPressed & U_CBUTTONS) && crouched && m->ceilHeight - m->floorHeight > 160.0f) {
            m->action = ACT_FPS_GROUND;
            crouched = FALSE;
        }
    }

    if (m->input & INPUT_A_PRESSED) {
        if (crouched) {
            // Crouch jump: triple jump height on command (and wing cap flight, as the triple jump allows)
            if (m->flags & MARIO_WING_CAP) {
                return set_mario_action(m, ACT_FLYING_TRIPLE_JUMP, 0);
            }
            set_mario_action(m, ACT_TRIPLE_JUMP, 0);
            play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_YAHOO);
        } else {
            set_mario_action(m, ACT_JUMP, 0);
            play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_YAH_WAH_HOO);
        }
        return TRUE;
    }

    if (m->input & INPUT_OFF_FLOOR) {
        return set_mario_action(m, ACT_FREEFALL, 0);
    }

    if (should_begin_sliding(m)) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }

    ge64_update_move_velocity(m, GE64_GROUND_ACCEL, GE64_GROUND_DECEL);
    speed = sqrtf(m->vel[0] * m->vel[0] + m->vel[2] * m->vel[2]);
    ge64_update_look(m, speed > 4.0f);

    switch (perform_ground_step(m)) {
        case GROUND_STEP_LEFT_GROUND:
            set_mario_action(m, ACT_FREEFALL, 0);
            break;
    }

    if (speed > 4.0f) {
        set_mario_animation(m, MARIO_ANIM_WALKING);
        sGe.bobPhase += speed * 0.012f;
        sGe.bobAmount = approach_f32(sGe.bobAmount, 1.0f, 0.1f, 0.1f);
        if (--sGe.stepTimer <= 0) {
            sGe.stepTimer = crouched ? 14 : 10;
            play_sound(SOUND_ACTION_TERRAIN_STEP + m->terrainSoundAddend, gGlobalSoundSource);
        }
    } else {
        set_mario_animation(m, crouched ? MARIO_ANIM_CROUCHING : MARIO_ANIM_FIRST_PERSON);
        sGe.bobAmount = approach_f32(sGe.bobAmount, 0.0f, 0.1f, 0.1f);
        sGe.stepTimer = 0;
    }

    return FALSE;
}

s32 act_fps_air(struct MarioState *m) {
    ge64_update_move_velocity(m, GE64_AIR_ACCEL, GE64_AIR_ACCEL);
    ge64_update_look(m, FALSE);
    sGe.bobAmount = approach_f32(sGe.bobAmount, 0.0f, 0.1f, 0.1f);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            if (!check_fall_damage_or_get_stuck(m, ACT_HARD_BACKWARD_GROUND_KB)) {
                play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
                set_mario_action(m, ACT_FPS_GROUND, 0);
            }
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    set_mario_animation(m, m->vel[1] > 0.0f ? MARIO_ANIM_SINGLE_JUMP : MARIO_ANIM_GENERAL_FALL);
    return FALSE;
}

/**************************************************
 *                      VIEW                      *
 **************************************************/

static s32 ge64_compute_view_active(struct MarioState *m) {
    if (gCamera == NULL || gCamera->cutscene != 0) {
        return FALSE;
    }

    switch (m->action & ACT_GROUP_MASK) {
        case ACT_GROUP_STATIONARY:
        case ACT_GROUP_MOVING:
        case ACT_GROUP_AIRBORNE:
        case ACT_GROUP_SUBMERGED:
        case ACT_GROUP_OBJECT:
            return TRUE;
    }
    return FALSE;
}

s32 ge64_view_active(void) {
    return sGe.viewActive;
}

static void ge64_update_view_basis(struct MarioState *m) {
    u32 group = m->action & ACT_GROUP_MASK;
    f32 targetHeight;
    s16 yaw = m->faceAngle[1];
    s16 pitch = sGe.pitch + sGe.recoilPitch;

    if (group == ACT_GROUP_SUBMERGED || (m->action & ACT_FLAG_SWIMMING_OR_FLYING)) {
        // Swimming and flying steer with Mario's own pitch
        pitch = -m->faceAngle[0];
        targetHeight = GE64_SWIM_EYE_HEIGHT;
    } else if (m->action & ACT_FLAG_SHORT_HITBOX) {
        targetHeight = GE64_CROUCH_EYE_HEIGHT;
    } else {
        targetHeight = GE64_EYE_HEIGHT;
    }

    sGe.eyeHeight = approach_f32(sGe.eyeHeight, targetHeight, 12.0f, 12.0f);
    sGe.viewYaw = yaw;
    sGe.viewPitch = pitch;

    sGe.forward[0] = sins(yaw) * coss(pitch);
    sGe.forward[1] = sins(pitch);
    sGe.forward[2] = coss(yaw) * coss(pitch);
    sGe.right[0] = sins(yaw - 0x4000);
    sGe.right[1] = 0.0f;
    sGe.right[2] = coss(yaw - 0x4000);
    // up = right x forward
    sGe.up[0] = sGe.right[1] * sGe.forward[2] - sGe.right[2] * sGe.forward[1];
    sGe.up[1] = sGe.right[2] * sGe.forward[0] - sGe.right[0] * sGe.forward[2];
    sGe.up[2] = sGe.right[0] * sGe.forward[1] - sGe.right[1] * sGe.forward[0];

    sGe.eye[0] = m->pos[0];
    sGe.eye[1] = m->pos[1] + sGe.eyeHeight + sins((s16)(sGe.bobPhase * 0x10000)) * 2.0f * sGe.bobAmount;
    sGe.eye[2] = m->pos[2];
}

void ge64_update_graph_node_camera(struct GraphNodeCamera *gc) {
    if (!sGe.viewActive) {
        return;
    }

    vec3f_copy(gc->pos, sGe.eye);
    gc->focus[0] = sGe.eye[0] + sGe.forward[0] * 100.0f;
    gc->focus[1] = sGe.eye[1] + sGe.forward[1] * 100.0f;
    gc->focus[2] = sGe.eye[2] + sGe.forward[2] * 100.0f;
    gc->rollScreen = 0;
}

void ge64_update_perspective(struct GraphNodePerspective *perspective) {
    if (sGe.viewActive) {
        if (sGeSavedPerspective != perspective) {
            sGeSavedPerspective = perspective;
            sGeSavedNear = perspective->near;
        }
        perspective->fov = ge64_scoped() ? GE64_FOV / ge64_current_weapon()->zoom : GE64_FOV;
        perspective->near = GE64_NEAR;
    } else if (sGeSavedPerspective == perspective) {
        perspective->near = sGeSavedNear;
        sGeSavedPerspective = NULL;
    }
}

/**
 * Convert a world position into the camera-relative space the audio engine expects and keep it
 * alive in a small ring, since play_sound reads the position for as long as the sound plays.
 */
static f32 *ge64_sound_position(Vec3f world) {
    f32 *pos = sGeSoundPositions[sGeSoundPositionIndex];
    f32 dx = world[0] - sGe.eye[0];
    f32 dy = world[1] - sGe.eye[1];
    f32 dz = world[2] - sGe.eye[2];

    sGeSoundPositionIndex = (sGeSoundPositionIndex + 1) % GE64_SOUND_POSITIONS;
    pos[0] = dx * sGe.right[0] + dy * sGe.right[1] + dz * sGe.right[2];
    pos[1] = dx * sGe.up[0] + dy * sGe.up[1] + dz * sGe.up[2];
    pos[2] = -(dx * sGe.forward[0] + dy * sGe.forward[1] + dz * sGe.forward[2]);
    return pos;
}

/**************************************************
 *                    RAYCASTS                    *
 **************************************************/

static s32 ge64_surface_blocks_shots(struct Surface *surf) {
    switch (surf->type) {
        case SURFACE_INTANGIBLE:
        case SURFACE_CAMERA_BOUNDARY:
            return FALSE;
    }
    return TRUE;
}

static s32 ge64_ray_triangle(struct Surface *surf, Vec3f orig, Vec3f dir, f32 maxT, f32 *outT) {
    f32 nx = surf->normal.x;
    f32 ny = surf->normal.y;
    f32 nz = surf->normal.z;
    f32 denom = nx * dir[0] + ny * dir[1] + nz * dir[2];
    f32 t, px, py, pz;
    f32 x1, y1, z1, x2, y2, z2, x3, y3, z3;

    // Collision triangles are one-sided; only front faces stop bullets
    if (denom > -0.0001f) {
        return FALSE;
    }

    t = -(nx * orig[0] + ny * orig[1] + nz * orig[2] + surf->originOffset) / denom;
    if (t < 0.0f || t >= maxT) {
        return FALSE;
    }

    px = orig[0] + dir[0] * t;
    py = orig[1] + dir[1] * t;
    pz = orig[2] + dir[2] * t;

    x1 = surf->vertex1[0]; y1 = surf->vertex1[1]; z1 = surf->vertex1[2];
    x2 = surf->vertex2[0]; y2 = surf->vertex2[1]; z2 = surf->vertex2[2];
    x3 = surf->vertex3[0]; y3 = surf->vertex3[1]; z3 = surf->vertex3[2];

    // Point is inside if it is on the inner side of all three edges (winding matches the normal)
#define GE64_EDGE_TEST(ax, ay, az, bx, by, bz)                                                   \
    ((((by - ay) * (pz - az) - (bz - az) * (py - ay)) * nx                                         \
      + ((bz - az) * (px - ax) - (bx - ax) * (pz - az)) * ny                                       \
      + ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) * nz) >= -1.0f)

    if (!GE64_EDGE_TEST(x1, y1, z1, x2, y2, z2) || !GE64_EDGE_TEST(x2, y2, z2, x3, y3, z3)
        || !GE64_EDGE_TEST(x3, y3, z3, x1, y1, z1)) {
        return FALSE;
    }
#undef GE64_EDGE_TEST

    *outT = t;
    return TRUE;
}

static void ge64_ray_cell(struct SurfaceNode *node, Vec3f orig, Vec3f dir, f32 *bestT, struct Surface **bestSurf) {
    f32 t;

    for (; node != NULL; node = node->next) {
        if (ge64_surface_blocks_shots(node->surface)
            && ge64_ray_triangle(node->surface, orig, dir, *bestT, &t)) {
            *bestT = t;
            *bestSurf = node->surface;
        }
    }
}

/**
 * Cast a ray against level and object collision. Walks the 2D spatial partition cells the ray
 * crosses (Amanatides-Woo traversal) and returns the nearest hit distance, or maxT if nothing.
 */
static f32 ge64_raycast_surfaces(Vec3f orig, Vec3f dir, f32 maxT, struct Surface **hitSurf) {
    f32 ox = orig[0] + LEVEL_BOUNDARY_MAX;
    f32 oz = orig[2] + LEVEL_BOUNDARY_MAX;
    s32 cellX = (s32)(ox / CELL_SIZE);
    s32 cellZ = (s32)(oz / CELL_SIZE);
    s32 stepX = dir[0] >= 0.0f ? 1 : -1;
    s32 stepZ = dir[2] >= 0.0f ? 1 : -1;
    f32 tMaxX, tMaxZ, tDeltaX, tDeltaZ, tCellEnd;
    f32 bestT = maxT;
    s32 i, part;

    *hitSurf = NULL;
    if (ox < 0.0f || oz < 0.0f || cellX >= NUM_CELLS || cellZ >= NUM_CELLS) {
        return maxT;
    }

    if (dir[0] > 0.0001f || dir[0] < -0.0001f) {
        tMaxX = ((cellX + (stepX > 0 ? 1 : 0)) * CELL_SIZE - ox) / dir[0];
        tDeltaX = CELL_SIZE / (dir[0] > 0.0f ? dir[0] : -dir[0]);
    } else {
        tMaxX = tDeltaX = 1.0e30f;
    }
    if (dir[2] > 0.0001f || dir[2] < -0.0001f) {
        tMaxZ = ((cellZ + (stepZ > 0 ? 1 : 0)) * CELL_SIZE - oz) / dir[2];
        tDeltaZ = CELL_SIZE / (dir[2] > 0.0f ? dir[2] : -dir[2]);
    } else {
        tMaxZ = tDeltaZ = 1.0e30f;
    }

    for (i = 0; i < 2 * NUM_CELLS + 2; i++) {
        for (part = 0; part < 3; part++) {
            ge64_ray_cell(gStaticSurfacePartition[cellZ][cellX][part].next, orig, dir, &bestT, hitSurf);
            ge64_ray_cell(gDynamicSurfacePartition[cellZ][cellX][part].next, orig, dir, &bestT, hitSurf);
        }

        tCellEnd = MIN(tMaxX, tMaxZ);
        if (bestT <= tCellEnd || tCellEnd >= maxT) {
            break;
        }

        if (tMaxX < tMaxZ) {
            cellX += stepX;
            tMaxX += tDeltaX;
        } else {
            cellZ += stepZ;
            tMaxZ += tDeltaZ;
        }
        if (cellX < 0 || cellX >= NUM_CELLS || cellZ < 0 || cellZ >= NUM_CELLS) {
            break;
        }
    }

    return bestT;
}

static s32 ge64_object_shootable(struct Object *obj) {
    if (obj == gMarioObject || !(obj->activeFlags & ACTIVE_FLAG_ACTIVE)) {
        return FALSE;
    }
    if (obj->oIntangibleTimer != 0 || obj->hitboxRadius <= 0.0f || obj->hitboxHeight <= 0.0f) {
        return FALSE;
    }
    if (!(obj->header.gfx.node.flags & GRAPH_RENDER_ACTIVE)
        || (obj->header.gfx.node.flags & GRAPH_RENDER_INVISIBLE)) {
        return FALSE;
    }
    if (obj->behavior == segmented_to_virtual(bhvChainChomp)) {
        return TRUE;
    }
    return (obj->oInteractType & GE64_ATTACKABLE) != 0;
}

/**
 * Ray against an object's hitbox cylinder. Returns the entry distance or -1.
 */
static f32 ge64_ray_cylinder(struct Object *obj, Vec3f orig, Vec3f dir) {
    // SM64 hitboxes are usually smaller than the model; shots use the larger of the hit/hurt
    // boxes plus a margin so a hit that looks like a hit counts
    f32 height = MAX(obj->hitboxHeight, obj->hurtboxHeight) * 1.15f;
    f32 bottom = obj->oPosY - obj->hitboxDownOffset;
    f32 top = bottom + height;
    f32 r = MAX(obj->hitboxRadius, obj->hurtboxRadius) * 1.15f;
    f32 px = orig[0] - obj->oPosX;
    f32 pz = orig[2] - obj->oPosZ;
    f32 a = dir[0] * dir[0] + dir[2] * dir[2];
    f32 b = 2.0f * (dir[0] * px + dir[2] * pz);
    f32 c = px * px + pz * pz - r * r;
    f32 tEnter, tExit, ty0, ty1, disc, sq;

    if (a < 0.000001f) {
        if (c > 0.0f) {
            return -1.0f;
        }
        tEnter = -1.0e30f;
        tExit = 1.0e30f;
    } else {
        disc = b * b - 4.0f * a * c;
        if (disc < 0.0f) {
            return -1.0f;
        }
        sq = sqrtf(disc);
        tEnter = (-b - sq) / (2.0f * a);
        tExit = (-b + sq) / (2.0f * a);
    }

    if (dir[1] > 0.0001f || dir[1] < -0.0001f) {
        ty0 = (bottom - orig[1]) / dir[1];
        ty1 = (top - orig[1]) / dir[1];
        if (ty0 > ty1) {
            f32 tmp = ty0;
            ty0 = ty1;
            ty1 = tmp;
        }
        tEnter = MAX(tEnter, ty0);
        tExit = MIN(tExit, ty1);
    } else if (orig[1] < bottom || orig[1] > top) {
        return -1.0f;
    }

    if (tEnter > tExit || tExit < 0.0f) {
        return -1.0f;
    }
    return MAX(tEnter, 0.0f);
}

static const s32 sGeShootableLists[] = {
    OBJ_LIST_PUSHABLE, OBJ_LIST_GENACTOR, OBJ_LIST_DESTRUCTIVE, OBJ_LIST_LEVEL, OBJ_LIST_SURFACE,
};

static struct Object *ge64_raycast_objects(Vec3f orig, Vec3f dir, f32 maxT, f32 *outT) {
    struct Object *best = NULL;
    struct ObjectNode *listHead;
    struct Object *obj;
    f32 t;
    s32 i;

    *outT = maxT;
    for (i = 0; i < ARRAY_COUNT(sGeShootableLists); i++) {
        listHead = &gObjectLists[sGeShootableLists[i]];
        obj = (struct Object *) listHead->next;
        while (obj != (struct Object *) listHead) {
            if (ge64_object_shootable(obj)) {
                t = ge64_ray_cylinder(obj, orig, dir);
                if (t >= 0.0f && t < *outT) {
                    *outT = t;
                    best = obj;
                }
            }
            obj = (struct Object *) obj->header.next;
        }
    }
    return best;
}

/**************************************************
 *                 SHOT RESPONSES                 *
 **************************************************/

static void ge64_push_object(struct Object *obj, Vec3f dir, f32 force) {
    f32 len = sqrtf(dir[0] * dir[0] + dir[2] * dir[2]);
    struct GePush *slot = NULL;
    s32 i;

    if (len < 0.01f) {
        return;
    }
    for (i = 0; i < GE64_MAX_PUSHES; i++) {
        if (sGePushes[i].obj == obj) {
            slot = &sGePushes[i];
            break;
        }
        if (slot == NULL && sGePushes[i].obj == NULL) {
            slot = &sGePushes[i];
        }
    }
    if (slot == NULL) {
        slot = &sGePushes[0];
    }
    if (slot->obj != obj) {
        slot->obj = obj;
        slot->bhv = obj->behavior;
        slot->velX = 0.0f;
        slot->velZ = 0.0f;
    }
    slot->velX += dir[0] / len * force;
    slot->velZ += dir[2] / len * force;
}

/**
 * Slide pushed objects along the ground, decaying each frame, resolving against walls so they
 * can't be shot through level geometry.
 */
static void ge64_update_pushes(void) {
    struct WallCollisionData wall;
    struct Surface *floor;
    struct GePush *p;
    f32 floorY;
    s32 i;

    for (i = 0; i < GE64_MAX_PUSHES; i++) {
        p = &sGePushes[i];
        if (p->obj == NULL) {
            continue;
        }
        if (!(p->obj->activeFlags & ACTIVE_FLAG_ACTIVE) || p->obj->behavior != p->bhv) {
            p->obj = NULL;
            continue;
        }

        wall.x = p->obj->oPosX + p->velX;
        wall.y = p->obj->oPosY;
        wall.z = p->obj->oPosZ + p->velZ;
        wall.offsetY = 30.0f;
        wall.radius = MAX(p->obj->hitboxRadius * 0.8f, 30.0f);
        find_wall_collisions(&wall);

        floorY = find_floor(wall.x, p->obj->oPosY + 100.0f, wall.z, &floor);
        if (floor != NULL && floorY > -10000.0f) {
            p->obj->oPosX = wall.x;
            p->obj->oPosZ = wall.z;
        }

        p->velX *= 0.7f;
        p->velZ *= 0.7f;
        if (p->velX * p->velX + p->velZ * p->velZ < 0.25f) {
            p->obj = NULL;
        }
    }
}

/**
 * Bosses and a few special objects (whomps, the chain chomp's post) get their own handling;
 * returns TRUE if it took the shot.
 */
static s32 ge64_special_hit(struct Object *obj, Vec3f dir, const struct GeWeapon *weapon) {
    struct GeBossPush push;
    s16 shotYaw = atan2s(dir[2], dir[0]);

    if (!ge64_boss_on_hit(obj, shotYaw, weapon->damage, weapon->force, &push)) {
        return FALSE;
    }
    if (push.obj != NULL) {
        ge64_push_object(push.obj, dir, weapon->force * push.force);
    }
    return TRUE;
}

static void ge64_hit_object(struct Object *obj, Vec3f dir, Vec3f hitPos, const struct GeWeapon *weapon) {
    play_sound(SOUND_GE_HIT_FLESH, ge64_sound_position(hitPos));
#ifdef GE64_LEVEL_SELECT
    sGeDebugObjHits++;
#endif

    if (ge64_special_hit(obj, dir, weapon)) {
        return;
    }

    if (obj->behavior == segmented_to_virtual(bhvChainChomp)) {
        // Invincible: just shove it, harder for heavier guns
        ge64_push_object(obj, dir, weapon->force * 1.5f);
        return;
    }

    if (obj->behavior == segmented_to_virtual(bhvBobomb)) {
        obj->oInteractStatus |= INT_STATUS_INTERACTED | INT_STATUS_TOUCHED_BOB_OMB;
        return;
    }

    // Everything else gets hit as though Mario punched it, so each enemy's own death or
    // knockback logic runs (knockback direction is computed away from Mario, i.e. the shooter)
    obj->oInteractStatus = ATTACK_PUNCH | INT_STATUS_INTERACTED | INT_STATUS_WAS_ATTACKED;

    if (obj->oInteractType & GE64_PUSHABLE) {
        ge64_push_object(obj, dir, weapon->force);
    }
}

static s32 ge64_surface_impact_sound(struct Surface *surf) {
    s32 r = random_u16();

    if ((r & 3) == 0) {
        // GoldenEye mixes stray ricochets in with the surface hit sounds
        static const s32 ricochets[] = {
            SOUND_GE_RICO_8A, SOUND_GE_RICO_8B, SOUND_GE_RICO_8C, SOUND_GE_RICO_8D,
            SOUND_GE_RICO_5A, SOUND_GE_RICO_5B, SOUND_GE_RICO_6A, SOUND_GE_RICO_6B,
        };
        return ricochets[(r >> 2) % ARRAY_COUNT(ricochets)];
    }

    if (surf->object != NULL) {
        return (r & 4) ? SOUND_GE_HIT_METAL_A : SOUND_GE_HIT_METAL_B;
    }

    switch (gCurrentArea->terrainType & TERRAIN_MASK) {
        case TERRAIN_GRASS:
        case TERRAIN_SAND:
            return (r & 4) ? SOUND_GE_HIT_DIRT1 : SOUND_GE_HIT_DIRT2;
        case TERRAIN_SNOW:
            return SOUND_GE_HIT_SNOW;
        case TERRAIN_SPOOKY:
            return (r & 4) ? SOUND_GE_HIT_WOOD : SOUND_GE_HIT_WOOD2;
        case TERRAIN_SLIDE:
        case TERRAIN_WATER:
            return SOUND_GE_HIT_TILE;
    }
    return (r & 4) ? SOUND_GE_HIT_STONE1 : SOUND_GE_HIT_STONE2;
}

static void ge64_spawn_impact(Vec3f pos) {
    struct Object *spark = spawn_object(gMarioObject, MODEL_SPARKLES, bhvCoinSparkles);

    if (spark != NULL) {
        spark->oPosX = pos[0];
        spark->oPosY = pos[1];
        spark->oPosZ = pos[2];
        obj_scale(spark, 0.5f);
    }
}

/**************************************************
 *                     WEAPONS                    *
 **************************************************/

/**
 * Project a world point onto the screen in -1..1 crosshair coordinates. Returns FALSE if behind.
 */
static s32 ge64_project(Vec3f world, f32 *sx, f32 *sy) {
    f32 dx = world[0] - sGe.eye[0];
    f32 dy = world[1] - sGe.eye[1];
    f32 dz = world[2] - sGe.eye[2];
    f32 z = dx * sGe.forward[0] + dy * sGe.forward[1] + dz * sGe.forward[2];

    if (z < 50.0f) {
        return FALSE;
    }
    *sx = (dx * sGe.right[0] + dy * sGe.right[1] + dz * sGe.right[2]) / (z * GE64_TAN_HALF_FOV_X);
    *sy = (dx * sGe.up[0] + dy * sGe.up[1] + dz * sGe.up[2]) / (z * GE64_TAN_HALF_FOV_Y);
    return TRUE;
}

/**
 * GoldenEye-style auto-aim: when not manually aiming, snap the shot (and crosshair) to the
 * enemy nearest the centre of the screen within a generous vertical window, if it's in sight.
 */
static void ge64_update_auto_aim(void) {
    struct ObjectNode *listHead;
    struct Object *obj;
    struct Object *best = NULL;
    struct Surface *surf;
    f32 bestScore = 1.0e30f;
    f32 sx, sy, score, bestX = 0.0f, bestY = 0.0f, dist, wallT;
    Vec3f center, dir;
    s32 i;

    sGe.autoAimValid = FALSE;
    if (sGe.aiming) {
        return;
    }

    for (i = 0; i < ARRAY_COUNT(sGeShootableLists); i++) {
        listHead = &gObjectLists[sGeShootableLists[i]];
        obj = (struct Object *) listHead->next;
        while (obj != (struct Object *) listHead) {
            if (ge64_object_shootable(obj) && (obj->oInteractType & ~INTERACT_BREAKABLE & GE64_ATTACKABLE)) {
                center[0] = obj->oPosX;
                center[1] = obj->oPosY - obj->hitboxDownOffset + obj->hitboxHeight * 0.5f;
                center[2] = obj->oPosZ;
                if (ge64_project(center, &sx, &sy) && sx > -0.25f && sx < 0.25f && sy > -0.6f && sy < 0.6f) {
                    score = sx * sx * 4.0f + sy * sy;
                    if (score < bestScore) {
                        bestScore = score;
                        best = obj;
                        bestX = sx;
                        bestY = sy;
                    }
                }
            }
            obj = (struct Object *) obj->header.next;
        }
    }

    if (best == NULL) {
        return;
    }

    center[0] = best->oPosX;
    center[1] = best->oPosY - best->hitboxDownOffset + best->hitboxHeight * 0.5f;
    center[2] = best->oPosZ;
    dir[0] = center[0] - sGe.eye[0];
    dir[1] = center[1] - sGe.eye[1];
    dir[2] = center[2] - sGe.eye[2];
    dist = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (dist < 1.0f || dist > GE64_RANGE) {
        return;
    }
    dir[0] /= dist;
    dir[1] /= dist;
    dir[2] /= dist;
    wallT = ge64_raycast_surfaces(sGe.eye, dir, dist, &surf);
    if (surf != NULL && wallT < dist - best->hitboxRadius) {
        return;
    }

    sGe.autoAimValid = TRUE;
    sGe.autoAimX = bestX;
    sGe.autoAimY = bestY;
}

static void ge64_current_aim(f32 *ax, f32 *ay) {
    if (sGe.autoAimValid) {
        *ax = sGe.autoAimX;
        *ay = sGe.autoAimY;
    } else {
        *ax = sGe.crossX;
        *ay = sGe.crossY;
    }
}

/**
 * Shooting the floor right below you launches you, GoldenEye-gun style: straight up if you're
 * standing still, and forward like a long jump if you're pushing forward. One boost per jump in
 * the air, and the jump off the ground itself.
 */
static void ge64_gun_jump(struct MarioState *m, Vec3f dir, const struct GeWeapon *weapon) {
    s32 grounded = GE64_IS_FPS_GROUND(m->action);
    f32 power = CLAMP(weapon->force * weapon->pellets * 0.9f, 24.0f, 58.0f);
    f32 hx, hz, speed, scale;

    if (!grounded && (m->action != ACT_FPS_AIR || sGe.gunJumpCharge == 0)) {
        return;
    }

    if (grounded) {
        set_mario_action(m, ACT_FREEFALL, 0);
        m->vel[1] = 0.0f;
    } else {
        sGe.gunJumpCharge--;
    }
    // Releasing A shouldn't cut this jump short
    m->flags &= ~MARIO_UNKNOWN_08;
    m->peakHeight = m->pos[1];

    m->vel[1] = MAX(m->vel[1], 0.0f) + power * -dir[1];

    if (m->controller->stickY > 16.0f) {
        hx = m->vel[0] + sins(m->faceAngle[1]) * power * 0.8f;
        hz = m->vel[2] + coss(m->faceAngle[1]) * power * 0.8f;
    } else {
        hx = m->vel[0] - dir[0] * power * 0.5f;
        hz = m->vel[2] - dir[2] * power * 0.5f;
    }
    speed = sqrtf(hx * hx + hz * hz);
    scale = speed > GE64_GUN_JUMP_MAX_SPEED ? GE64_GUN_JUMP_MAX_SPEED / speed : 1.0f;
    m->vel[0] = hx * scale;
    m->vel[2] = hz * scale;

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_YAHOO);
}

/**
 * One ray of a shot (shotguns fire several). Returns TRUE if it hit the floor close below.
 */
static s32 ge64_fire_ray(Vec3f dir, const struct GeWeapon *weapon) {
    struct Surface *surf;
    struct Object *obj;
    f32 wallT, objT;
    Vec3f hit;

    wallT = ge64_raycast_surfaces(sGe.eye, dir, GE64_RANGE, &surf);
    obj = ge64_raycast_objects(sGe.eye, dir, wallT, &objT);

    if (obj != NULL) {
        hit[0] = sGe.eye[0] + dir[0] * objT;
        hit[1] = sGe.eye[1] + dir[1] * objT;
        hit[2] = sGe.eye[2] + dir[2] * objT;
        ge64_hit_object(obj, dir, hit, weapon);
        return FALSE;
    }
    if (surf == NULL) {
        return FALSE;
    }

    hit[0] = sGe.eye[0] + dir[0] * wallT;
    hit[1] = sGe.eye[1] + dir[1] * wallT;
    hit[2] = sGe.eye[2] + dir[2] * wallT;
    play_sound(ge64_surface_impact_sound(surf), ge64_sound_position(hit));
    ge64_spawn_impact(hit);
#ifdef GE64_LEVEL_SELECT
    sGeDebugWallHits++;
#endif
    // Whomps, Bowser's platforms etc. are surface objects
    if (surf->object != NULL) {
        ge64_special_hit(surf->object, dir, weapon);
    }

    return surf->normal.y > 0.6f && wallT < GE64_GUN_JUMP_RANGE && dir[1] < -0.6f;
}

static void ge64_fire(struct MarioState *m, const struct GeWeapon *weapon) {
    f32 ax, ay, spread, len, rx, ry;
    s32 i, hitFloor = FALSE;
    Vec3f dir;

    if (weapon->magSize != 0) {
        sGe.ammo[sGe.slot]--;
    }
    sGe.fireCooldown = weapon->fireDelay;
    sGe.muzzleFlash = 2;
    sGe.gunKick = 4;
    play_sound(weapon->fireSound, gGlobalSoundSource);

    ge64_current_aim(&ax, &ay);
    // Aiming down the sight with R is accurate; hip fire spreads
    spread = sGe.aiming ? weapon->spreadDeg * 0.3f : weapon->spreadDeg;
    if (weapon->pellets > 1) {
        spread = weapon->spreadDeg;
    }
    spread = spread * (3.14159f / 180.0f);

    for (i = 0; i < weapon->pellets; i++) {
        rx = ax * GE64_TAN_HALF_FOV_X + (random_float() - 0.5f) * 2.0f * spread;
        ry = ay * GE64_TAN_HALF_FOV_Y + (random_float() - 0.5f) * 2.0f * spread;
        if (ge64_scoped()) {
            rx = (random_float() - 0.5f) * 2.0f * spread;
            ry = (random_float() - 0.5f) * 2.0f * spread;
        }

        dir[0] = sGe.forward[0] + sGe.right[0] * rx + sGe.up[0] * ry;
        dir[1] = sGe.forward[1] + sGe.right[1] * rx + sGe.up[1] * ry;
        dir[2] = sGe.forward[2] + sGe.right[2] * rx + sGe.up[2] * ry;
        len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        dir[0] /= len;
        dir[1] /= len;
        dir[2] /= len;

        if (ge64_fire_ray(dir, weapon)) {
            hitFloor = TRUE;
        }
    }

    if (hitFloor) {
        ge64_gun_jump(m, dir, weapon);
    }

    sGe.recoilPitch = MIN(sGe.recoilPitch + weapon->recoil, 0x400);

    if (weapon->magSize != 0 && sGe.ammo[sGe.slot] == 0) {
        // GoldenEye reloads on its own once the magazine runs dry
        sGe.reloadTimer = weapon->reloadFrames + weapon->fireDelay;
    }
}

static s16 ge64_random_primary(s16 exclude) {
    s16 pick;

    do {
        pick = 1 + random_u16() % (ARRAY_COUNT(sGeWeapons) - 1);
    } while (pick == exclude);
    return pick;
}

static void ge64_init_weapons(void) {
    sGe.primary = ge64_random_primary(-1);
    sGe.ammo[GE64_SLOT_SIDEARM] = sGeWeapons[GE64_WEAPON_PP7].magSize;
    sGe.ammo[GE64_SLOT_PRIMARY] = sGeWeapons[sGe.primary].magSize;
    sGe.slot = GE64_SLOT_PRIMARY;
    sGe.eyeHeight = GE64_EYE_HEIGHT;
    sGe.gunJumpCharge = 1;
    sGe.bannerTimer = 150;
    sGe.initialized = TRUE;
}

/**
 * Roguelike primary: every star swaps the primary for a random different one. The PP7 sidearm is
 * always kept. The new model loads once the old one is no longer on screen (the star dance).
 */
void ge64_on_star_collected(void) {
    if (!sGe.initialized) {
        ge64_init_weapons();
    }
    sGe.primary = ge64_random_primary(sGe.primary);
    sGe.ammo[GE64_SLOT_PRIMARY] = sGeWeapons[sGe.primary].magSize;
    sGe.slot = GE64_SLOT_PRIMARY;
    sGe.reloadTimer = 0;
    sGe.bannerTimer = 150;
}

static void ge64_update_weapon(struct MarioState *m) {
    struct Controller *c = m->controller;
    const struct GeWeapon *weapon;

    if (!sGe.initialized) {
        ge64_init_weapons();
    }
    weapon = ge64_current_weapon();

    if (sGe.fireCooldown > 0) {
        sGe.fireCooldown--;
    }
    if (sGe.muzzleFlash > 0) {
        sGe.muzzleFlash--;
    }
    if (sGe.gunKick > 0) {
        sGe.gunKick--;
    }
    if (sGe.bannerTimer > 0) {
        sGe.bannerTimer--;
    }
    sGe.recoilPitch = approach_s32(sGe.recoilPitch, 0, 0x30, 0x30);

    if (sGe.switchTimer > 0) {
        sGe.switchTimer--;
        return;
    }

    // L swaps between the PP7 sidearm and the primary; both stay loaded, so this is instant
    if ((c->buttonPressed & L_TRIG) && sGe.reloadTimer == 0) {
        sGe.slot = sGe.slot == GE64_SLOT_SIDEARM ? GE64_SLOT_PRIMARY : GE64_SLOT_SIDEARM;
        sGe.switchTimer = 12;
        play_sound(SOUND_GE_PICKUP_GUN, gGlobalSoundSource);
        return;
    }

    if (sGe.reloadTimer > 0) {
        sGe.reloadTimer--;
        if (sGe.reloadTimer == weapon->reloadFrames / 3) {
            play_sound(SOUND_GE_RELOAD, gGlobalSoundSource);
        }
        if (sGe.reloadTimer == 0) {
            sGe.ammo[sGe.slot] = weapon->magSize;
        }
        return;
    }

    if ((c->buttonPressed & B_BUTTON) && weapon->magSize != 0 && sGe.ammo[sGe.slot] < weapon->magSize) {
        sGe.reloadTimer = weapon->reloadFrames;
        return;
    }

    if (sGe.fireCooldown == 0
        && ((weapon->automatic && (c->buttonDown & Z_TRIG)) || (c->buttonPressed & Z_TRIG))) {
        if (weapon->magSize == 0 || sGe.ammo[sGe.slot] > 0) {
            ge64_fire(m, weapon);
        } else if (c->buttonPressed & Z_TRIG) {
            play_sound(SOUND_GE_EMPTY_CLICK, gGlobalSoundSource);
            sGe.reloadTimer = weapon->reloadFrames;
        }
    }
}

#ifdef GE64_LEVEL_SELECT
/**
 * Test helpers (level-select builds only): D-pad up spawns a goomba ahead, D-pad down
 * teleports next to the nearest chain chomp, D-pad left to the nearest door, D-pad right
 * rerolls the primary weapon (as a star would).
 */
static void ge64_debug_controls(struct MarioState *m) {
    struct Object *obj;
    f32 dist;

    sGeDebugButtons = m->controller->buttonDown;
    {
        // Track the nearest boss's action and health on the debug HUD
        static const BehaviorScript *const watch[] = { bhvKingBobomb, bhvWhompKingBoss, bhvBowser, bhvBigBully, bhvSmallBully };
        s32 w;
        obj = NULL;
        for (w = 0; w < ARRAY_COUNT(watch) && obj == NULL; w++) {
            obj = cur_obj_find_nearest_object_with_behavior(watch[w], &dist);
        }
        sGeDebugTarget = obj != NULL ? obj->oAction : -1;
        sGeDebugTargetY = obj != NULL ? obj->oHealth : -1;
    }
    if ((m->controller->buttonDown & L_TRIG) && (m->controller->buttonPressed & U_JPAD)) {
        // Deliver five hits from the current weapon to the nearest boss (tests boss logic)
        static const BehaviorScript *const bosses[] = {
            bhvKingBobomb, bhvWhompKingBoss, bhvSmallWhomp, bhvBowser, bhvBigBully, bhvBigBullyWithMinions,
            bhvBigChillBully, bhvSmallBully, bhvWoodenPost,
        };
        s32 i, k;
        Vec3f dir;
        f32 len;
        obj = NULL;
        for (i = 0; i < ARRAY_COUNT(bosses) && obj == NULL; i++) {
            obj = cur_obj_find_nearest_object_with_behavior(bosses[i], &dist);
        }
        if (obj != NULL) {
            dir[0] = obj->oPosX - m->pos[0];
            dir[1] = 0.0f;
            dir[2] = obj->oPosZ - m->pos[2];
            if (obj->behavior == segmented_to_virtual(bhvBowser)) {
                // Test the bomb hit: knock him toward the nearest bomb
                struct Object *bomb = cur_obj_find_nearest_object_with_behavior(bhvBowserBomb, &dist);
                if (bomb != NULL) {
                    dir[0] = bomb->oPosX - obj->oPosX;
                    dir[2] = bomb->oPosZ - obj->oPosZ;
                }
            }
            len = sqrtf(dir[0] * dir[0] + dir[2] * dir[2]) + 0.01f;
            dir[0] /= len;
            dir[2] /= len;
            for (k = 0; k < 5; k++) {
                ge64_special_hit(obj, dir, ge64_current_weapon());
            }
            sGeDebugObjHits += 5;
        }
        return;
    }
    if (m->controller->buttonPressed & U_JPAD) {
        obj = spawn_object(m->marioObj, MODEL_GOOMBA, bhvGoomba);
        sGeDebugSpawns++;
        if (obj != NULL) {
            // Goombas despawn with their triplet spawner; make it standalone
            obj->parentObj = obj;
            obj->oPosX = m->pos[0] + sins(m->faceAngle[1]) * 400.0f;
            obj->oPosY = m->pos[1] + 50.0f;
            obj->oPosZ = m->pos[2] + coss(m->faceAngle[1]) * 400.0f;
            obj->oHomeX = obj->oPosX;
            obj->oHomeY = obj->oPosY;
            obj->oHomeZ = obj->oPosZ;
        }
    }
    if ((m->controller->buttonDown & L_TRIG) && (m->controller->buttonPressed & R_JPAD)) {
        // Teleport onto the nearest star (tests collection, the star dance and the weapon swap)
        obj = cur_obj_find_nearest_object_with_behavior(bhvStar, &dist);
        if (obj != NULL) {
            m->pos[0] = obj->oPosX;
            m->pos[1] = obj->oPosY + 20.0f;
            m->pos[2] = obj->oPosZ;
            set_mario_action(m, ACT_FREEFALL, 0);
        }
        return;
    }
    if (m->controller->buttonPressed & R_JPAD) {
        ge64_on_star_collected();
    }
    if (m->controller->buttonPressed & L_JPAD) {
        obj = cur_obj_find_nearest_object_with_behavior(bhvDoor, &dist);
        if (obj != NULL) {
            // Doors face +Z in model space; stand 300 units out on the front side, facing it
            m->pos[0] = obj->oPosX + sins(obj->oMoveAngleYaw) * 300.0f;
            m->pos[1] = obj->oPosY + 50.0f;
            m->pos[2] = obj->oPosZ + coss(obj->oMoveAngleYaw) * 300.0f;
            m->faceAngle[1] = obj->oMoveAngleYaw + 0x8000;
            set_mario_action(m, ACT_FREEFALL, 0);
        }
    }
    if (m->controller->buttonPressed & D_JPAD) {
        // Teleport 700 units in front of the first boss-like object found, facing it
        static const BehaviorScript *const targets[] = {
            bhvKingBobomb, bhvWhompKingBoss, bhvBowser, bhvBigBully, bhvBigBullyWithMinions,
            bhvBigChillBully, bhvChainChomp,
        };
        s32 i;
        obj = NULL;
        for (i = 0; i < ARRAY_COUNT(targets) && obj == NULL; i++) {
            obj = cur_obj_find_nearest_object_with_behavior(targets[i], &dist);
        }
        if (obj != NULL) {
            // Bosses guard small arenas: stand near their home, facing them
            m->pos[0] = obj->oHomeX + sins(obj->oFaceAngleYaw) * 200.0f;
            m->pos[1] = obj->oHomeY + 300.0f;
            m->pos[2] = obj->oHomeZ + coss(obj->oFaceAngleYaw) * 200.0f;
            m->faceAngle[1] = obj->oFaceAngleYaw + 0x8000;
            set_mario_action(m, ACT_FREEFALL, 0);
        }
    }
}
#endif

/**
 * Called on level entry. The gun buffers come from the level's pool context so they are released
 * with the level; both carried guns load on the first frames of the level.
 */
void ge64_level_init(void) {
    s32 i;

    for (i = 0; i < 2; i++) {
        sGeGunBuffer[i] = main_pool_alloc(GE64_GUN_BUFFER_SIZE, MEMORY_POOL_LEFT);
        sGeLoadedModel[i] = NULL;
        sGeLastDrawnFrame[i] = 0;
    }
    ge64_bosses_level_init();
}

/**
 * (Re)load a slot's model if it changed. Never while the RSP might still be drawing from that
 * buffer: the slot must not have been drawn for the last few frames.
 */
static void ge64_update_gun_buffers(void) {
    const struct GeGunModel *want;
    s32 i;

    for (i = 0; i < 2; i++) {
        want = sGeWeapons[i == GE64_SLOT_SIDEARM ? GE64_WEAPON_PP7 : sGe.primary].model;
        if (sGeLoadedModel[i] == want || sGeGunBuffer[i] == NULL) {
            continue;
        }
        if (sGeLoadedModel[i] != NULL && gGlobalTimer - sGeLastDrawnFrame[i] < 4) {
            continue;
        }
        sGeLoadedModel[i] = NULL;
        if (ge64_load_mio0_into(sGeGunBuffer[i], GE64_GUN_BUFFER_SIZE, want->romStart, want->romEnd) != 0) {
            sGeLoadedModel[i] = want;
        }
    }
}

/**
 * Runs after Mario's actions each frame: decides whether the first-person view is active,
 * hides Mario's model, handles weapons and keeps the vanilla camera from reacting to the
 * C buttons and R while they're being used for looking and aiming.
 */
void ge64_mario_post_update(struct MarioState *m) {
    sGe.viewActive = ge64_compute_view_active(m);
    ge64_update_pushes();
    ge64_update_gun_buffers();

    if (!sGe.viewActive) {
        sGe.aiming = FALSE;
        sGe.autoAimValid = FALSE;
        sGe.pitch = 0;
        return;
    }

    m->marioObj->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
    // Mario's voice plays from his object's camera-relative position; he's the camera now
    vec3f_set(m->marioObj->header.gfx.cameraToObject, 0.0f, 0.0f, 0.0f);

    if (!GE64_IS_FPS_GROUND(m->action) && m->action != ACT_FPS_AIR) {
        // Other playable actions (knockback, slides, swimming) don't run the look code
        sGe.aiming = FALSE;
    }
    if (GE64_IS_FPS_GROUND(m->action)) {
        sGe.gunJumpCharge = 1;
    }

#ifdef GE64_LEVEL_SELECT
    ge64_debug_controls(m);
#endif
    ge64_update_view_basis(m);
    ge64_update_auto_aim();
    ge64_update_weapon(m);

    // Hide the look/aim buttons from the vanilla camera (it runs later this frame); the real
    // state is put back before the next controller read so press detection stays correct
    sGeMaskedController = m->controller;
    sGeSavedButtonDown = m->controller->buttonDown;
    m->controller->buttonPressed &= ~(U_CBUTTONS | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS | R_TRIG | L_TRIG);
    m->controller->buttonDown &= ~(U_CBUTTONS | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS | R_TRIG | L_TRIG);
}

void ge64_restore_controller(void) {
    if (sGeMaskedController != NULL) {
        sGeMaskedController->buttonDown = sGeSavedButtonDown;
        sGeMaskedController = NULL;
    }
}

/**************************************************
 *                    RENDERING                   *
 **************************************************/

static void ge64_render_viewmodel_gameplay(void);

static void ge64_draw_gun_parts(const struct GeGunModel *model, u16 enabled, s32 translucent) {
    const Lights1 *lights = model == &ge64_gun_goldengun ? &ge64_gold_lights : &ge64_chrome_lights;
    s32 i;

    gSPNumLights(gDisplayListHead++, NUMLIGHTS_1);
    gSPLight(gDisplayListHead++, &lights->l, 1);
    gSPLight(gDisplayListHead++, &lights->a, 2);

    for (i = 0; i < model->numParts; i++) {
        const struct GeGunPart *part = &model->parts[i];
        if (part->hand || part->translucent != translucent || (part->switchMask & ~enabled) != 0) {
            continue;
        }
        gDPPipeSync(gDisplayListHead++);
        if (translucent) {
            gDPSetRenderMode(gDisplayListHead++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
        } else {
            gDPSetRenderMode(gDisplayListHead++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
        }
        gDPSetCombineMode(gDisplayListHead++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
        gSPDisplayList(gDisplayListHead++, part->dl);
    }
}

/**
 * Mario's own white glove (and a bit of red sleeve) wrapped around the gun's grip, drawn with
 * his model's display lists and lighting.
 */
static void ge64_draw_glove(Mat4 base, const struct GeWeapon *weapon) {
    Mtx *handMtx = alloc_display_list(sizeof(Mtx));
    Mtx *armMtx = alloc_display_list(sizeof(Mtx));
    Mat4 local, hand, arm, offset;
    Vec3f grip, scale, back;
    Vec3s rot;

    if (handMtx == NULL || armMtx == NULL) {
        return;
    }

    vec3f_copy(grip, (f32 *) weapon->grip);
    vec3s_set(rot, DEGREES(GE64_GLOVE_ROT_X), DEGREES(GE64_GLOVE_ROT_Y), DEGREES(GE64_GLOVE_ROT_Z));
    mtxf_rotate_zxy_and_translate(local, grip, rot);
    vec3f_set(scale, GE64_GLOVE_SCALE, GE64_GLOVE_SCALE, GE64_GLOVE_SCALE);
    mtxf_scale_vec3f(local, local, scale);
    mtxf_mul(hand, local, base);
    mtxf_to_mtx(handMtx, hand);

    // The forearm sits 60 units back along the arm from the wrist, as in Mario's skeleton
    vec3f_set(back, -60.0f, 0.0f, 0.0f);
    mtxf_translate(offset, back);
    mtxf_mul(arm, offset, hand);
    mtxf_to_mtx(armMtx, arm);

    gDPPipeSync(gDisplayListHead++);
    gSPTexture(gDisplayListHead++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetRenderMode(gDisplayListHead++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gDPSetCombineMode(gDisplayListHead++, G_CC_SHADE, G_CC_SHADE);
    gSPSetGeometryMode(gDisplayListHead++, G_LIGHTING | G_CULL_BACK);
    gSPNumLights(gDisplayListHead++, NUMLIGHTS_1);

    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(handMtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPLight(gDisplayListHead++, &ge64_glove_lights.l, 1);
    gSPLight(gDisplayListHead++, &ge64_glove_lights.a, 2);
    gSPDisplayList(gDisplayListHead++, ge64_glove_dl);

    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(armMtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPLight(gDisplayListHead++, &ge64_sleeve_lights.l, 1);
    gSPLight(gDisplayListHead++, &ge64_sleeve_lights.a, 2);
    gSPDisplayList(gDisplayListHead++, ge64_sleeve_dl);

    gSPClearGeometryMode(gDisplayListHead++, G_LIGHTING | G_CULL_BACK);
}

/**
 * Draw the first-person gun over the world: its own projection, a cleared depth buffer so it
 * never clips into walls, then sway, bob, recoil and the reload/switch dip. The gun is a converted
 * GoldenEye model whose display lists load per-part matrices from segment 0x03 and their data
 * from segment 0x0E (the slot's buffer).
 */
static void ge64_render_title_gun(void);
static u32 sGeTitleGunFrame = 0xFFFFFFFF; // gGlobalTimer of the last frame the title head was drawn

void ge64_render_viewmodel(void) {
    if (sGeTitleGunFrame == gGlobalTimer) {
        ge64_render_title_gun();
        return;
    }
    ge64_render_viewmodel_gameplay();
}

static void ge64_render_viewmodel_gameplay(void) {
    const struct GeWeapon *weapon;
    const struct GeGunModel *model;
    Mtx *proj, *mtx;
    u16 perspNorm, enabled;
    f32 ax, ay, dip;
    s16 kick = 0;
    Mat4 base, part, offset;
    Vec3f pos, scale;
    Vec3s rot;
    s32 i;

    if (!sGe.viewActive || ge64_scoped()) {
        return;
    }
    weapon = ge64_current_weapon();
    model = weapon->model;
    if (sGeLoadedModel[sGe.slot] != model) {
        return;
    }
    sGeLastDrawnFrame[sGe.slot] = gGlobalTimer;

    proj = alloc_display_list(sizeof(Mtx));
    mtx = alloc_display_list(sizeof(Mtx) * model->numMatrices);
    if (proj == NULL || mtx == NULL) {
        return;
    }

    ge64_clear_z_buffer();

    guPerspective(proj, &perspNorm, GE64_FOV, 4.0f / 3.0f, 2.0f, 500.0f, 1.0f);
    gSPPerspNormalize(gDisplayListHead++, perspNorm);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(proj), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);

    // The gun swings toward the crosshair, as GoldenEye's does when aiming
    ge64_current_aim(&ax, &ay);
    dip = 0.0f;
    if (sGe.reloadTimer > 0) {
        dip = 10.0f;
    } else if (sGe.switchTimer > 0) {
        dip = sGe.switchTimer * 1.5f;
    }
    if (sGe.gunKick > 0) {
        kick = sGe.gunKick * 0x100;
    }

    pos[0] = weapon->viewPos[0] + ax * 8.0f + sins((s16)(sGe.bobPhase * 0x8000)) * 0.8f * sGe.bobAmount;
    pos[1] = weapon->viewPos[1] + ay * 6.0f - dip
             - (1.0f - coss((s16)(sGe.bobPhase * 0x10000))) * 0.6f * sGe.bobAmount;
    pos[2] = weapon->viewPos[2] + sGe.gunKick * 0.8f;
    if (sGe.aiming) {
        pos[0] -= 4.0f;
        pos[1] += 2.0f;
    }

    // GE gun models point down +Z; view space looks down -Z
    vec3s_set(rot, GE64_GUN_PITCH - (s16)(ay * 0x0C00) - kick, (s16) 0x8000 + GE64_GUN_YAW - (s16)(ax * 0x1000), 0);
    mtxf_rotate_zxy_and_translate(base, pos, rot);
    vec3f_set(scale, 0.1f, 0.1f, 0.1f);
    mtxf_scale_vec3f(base, base, scale);

    for (i = 0; i < model->numMatrices; i++) {
        vec3f_copy(pos, (f32 *) model->matrixPos[i]);
        mtxf_translate(offset, pos);
        mtxf_mul(part, offset, base);
        mtxf_to_mtx(&mtx[i], part);
    }

    enabled = weapon->switches;
    if (sGe.muzzleFlash > 0) {
        enabled |= weapon->flashSwitches;
    }

    gSPSegment(gDisplayListHead++, 0x03, VIRTUAL_TO_PHYSICAL(mtx));
    gSPSegment(gDisplayListHead++, GE64_GUN_SEGMENT, VIRTUAL_TO_PHYSICAL(sGeGunBuffer[sGe.slot]));
    gDPPipeSync(gDisplayListHead++);
    gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
    gDPSetTextureLUT(gDisplayListHead++, G_TT_NONE);
    gSPClearGeometryMode(gDisplayListHead++, G_LIGHTING | G_CULL_BOTH | G_FOG | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(gDisplayListHead++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);

    ge64_draw_gun_parts(model, enabled, FALSE);
    ge64_draw_glove(base, weapon);
    ge64_draw_gun_parts(model, enabled, TRUE);

    gDPPipeSync(gDisplayListHead++);
    gSPTexture(gDisplayListHead++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPSegment(gDisplayListHead++, 0x03, VIRTUAL_TO_PHYSICAL(get_segment_base_addr(0x03)));
    gSPSegment(gDisplayListHead++, GE64_GUN_SEGMENT, VIRTUAL_TO_PHYSICAL(get_segment_base_addr(GE64_GUN_SEGMENT)));
    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
}

/**************************************************
 *                  TITLE SCREEN                  *
 **************************************************/

// The title screen's pool belongs to the Goddard head, so the title PP7 has its own buffers
#define GE64_TITLE_GUN_SIZE 0xD000
#define GE64_TITLE_GUN_COMPRESSED_SIZE 0x6000
ALIGNED16 static u8 sGeTitleGunBuffer[GE64_TITLE_GUN_SIZE];
ALIGNED16 static u8 sGeTitleGunCompressed[GE64_TITLE_GUN_COMPRESSED_SIZE];
static u8 sGeTitleGunLoaded;

/**
 * Geo node next to the Goddard head on the title screen: Mario holds the PP7 up beside his face,
 * James Bond style. Drawn in the title camera's view space (fov 45, camera at the origin).
 */
/**
 * Geo node on the title screen: only marks that the title is showing; the gun itself is drawn
 * after the scene (Goddard's head is composited last, so drawing in the graph would put it behind).
 */
Gfx *geo_ge64_title_gun(s32 callContext, UNUSED struct GraphNode *node, UNUSED Mat4 *context) {
    if (callContext == GEO_CONTEXT_RENDER) {
        sGeTitleGunFrame = gGlobalTimer;
    }
    return NULL;
}

/**
 * Mario holds the PP7 up beside his face, James Bond style.
 */
static void ge64_render_title_gun(void) {
    const struct GeWeapon *weapon = &sGeWeapons[GE64_WEAPON_PP7];
    const struct GeGunModel *model = weapon->model;
    Mtx *mtx, *proj;
    u16 perspNorm;
    Mat4 base, part, offset;
    Vec3f pos, scale;
    Vec3s rot;
    f32 bob;
    s32 i;

    if (!sGeTitleGunLoaded) {
        sGeTitleGunLoaded = ge64_load_mio0_with_scratch(sGeTitleGunBuffer, GE64_TITLE_GUN_SIZE,
                                                        sGeTitleGunCompressed, GE64_TITLE_GUN_COMPRESSED_SIZE,
                                                        model->romStart, model->romEnd) != 0;
        if (!sGeTitleGunLoaded) {
            return;
        }
    }

    mtx = alloc_display_list(sizeof(Mtx) * model->numMatrices);
    proj = alloc_display_list(sizeof(Mtx));
    if (mtx == NULL || proj == NULL) {
        return;
    }
    guPerspective(proj, &perspNorm, 45.0f, 4.0f / 3.0f, 100.0f, 4000.0f, 1.0f);

    // A slow idle sway, barrel up beside his cheek
    bob = sins(gGlobalTimer * 0x180);
    vec3f_set(pos, GE64_TITLE_GUN_X, GE64_TITLE_GUN_Y + bob * 6.0f, GE64_TITLE_GUN_Z);
    vec3s_set(rot, GE64_TITLE_GUN_RX, GE64_TITLE_GUN_RY, GE64_TITLE_GUN_RZ + (s16)(bob * 0x100));
    mtxf_rotate_zxy_and_translate(base, pos, rot);
    vec3f_set(scale, GE64_TITLE_GUN_SCALE, GE64_TITLE_GUN_SCALE, GE64_TITLE_GUN_SCALE);
    mtxf_scale_vec3f(base, base, scale);
    for (i = 0; i < model->numMatrices; i++) {
        vec3f_copy(pos, (f32 *) model->matrixPos[i]);
        mtxf_translate(offset, pos);
        mtxf_mul(part, offset, base);
        mtxf_to_mtx(&mtx[i], part);
    }

    ge64_clear_z_buffer();
    gSPPerspNormalize(gDisplayListHead++, perspNorm);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(proj), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPSegment(gDisplayListHead++, 0x03, VIRTUAL_TO_PHYSICAL(mtx));
    gSPSegment(gDisplayListHead++, GE64_GUN_SEGMENT, VIRTUAL_TO_PHYSICAL(sGeTitleGunBuffer));
    gDPPipeSync(gDisplayListHead++);
    gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
    gDPSetTextureLUT(gDisplayListHead++, G_TT_NONE);
    gSPClearGeometryMode(gDisplayListHead++, G_LIGHTING | G_CULL_BOTH | G_FOG | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(gDisplayListHead++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    ge64_draw_gun_parts(model, weapon->switches, FALSE);
    ge64_draw_glove(base, weapon);
    gDPPipeSync(gDisplayListHead++);
    gSPTexture(gDisplayListHead++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPSegment(gDisplayListHead++, 0x03, VIRTUAL_TO_PHYSICAL(get_segment_base_addr(0x03)));
    gSPSegment(gDisplayListHead++, GE64_GUN_SEGMENT, VIRTUAL_TO_PHYSICAL(get_segment_base_addr(GE64_GUN_SEGMENT)));
    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
}

static void ge64_fill_rect(s32 x0, s32 y0, s32 x1, s32 y1) {
    gDPFillRectangle(gDisplayListHead++, x0, y0, x1, y1);
}

/**
 * Print ASCII text with SM64's dialog font (the HUD font has no J, Q, V, X or Z).
 */
static void ge64_print_dialog_text(s16 x, s16 y, const char *text, u8 alpha) {
    u8 buf[40];
    s32 i;
    char ch;

    for (i = 0; text[i] != '\0' && i < (s32) sizeof(buf) - 1; i++) {
        ch = text[i];
        if (ch >= '0' && ch <= '9') {
            buf[i] = ch - '0';
        } else if (ch >= 'A' && ch <= 'Z') {
            buf[i] = ch - 'A' + 0x0A;
        } else if (ch >= 'a' && ch <= 'z') {
            buf[i] = ch - 'a' + 0x24;
        } else if (ch == '-') {
            buf[i] = 0x9F;
        } else if (ch == '.') {
            buf[i] = 0x3F;
        } else if (ch == '!') {
            buf[i] = 0xF2;
        } else {
            buf[i] = 0x9E; // space
        }
    }
    buf[i] = 0xFF;

    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, alpha);
    print_generic_string(x, y, buf);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
}

void ge64_render_hud(void) {
    const struct GeWeapon *weapon;
    f32 ax, ay;
    s32 cx, cy, gap, len;
    u8 alpha;

    if (!sGe.viewActive) {
        return;
    }
    weapon = ge64_current_weapon();

    ge64_current_aim(&ax, &ay);
    if (ge64_scoped()) {
        ax = ay = 0.0f;
    }
    cx = SCREEN_WIDTH / 2 + (s32)(ax * (SCREEN_WIDTH / 2));
    cy = SCREEN_HEIGHT / 2 - (s32)(ay * (SCREEN_HEIGHT / 2));
    cx = CLAMP(cx, 8, SCREEN_WIDTH - 8);
    cy = CLAMP(cy, 8, SCREEN_HEIGHT - 8);
    gap = sGe.aiming ? 2 : 3;
    len = ge64_scoped() ? 12 : (sGe.aiming ? 5 : 3);
    alpha = (sGe.aiming || sGe.autoAimValid) ? 230 : 120;

    gDPPipeSync(gDisplayListHead++);
    gDPSetCycleType(gDisplayListHead++, G_CYC_1CYCLE);
    gDPSetRenderMode(gDisplayListHead++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gDisplayListHead++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    if (sGe.autoAimValid) {
        gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 80, 60, alpha);
    } else {
        gDPSetPrimColor(gDisplayListHead++, 0, 0, 255, 255, 255, alpha);
    }
    ge64_fill_rect(cx - gap - len, cy, cx - gap, cy + 1);
    ge64_fill_rect(cx + gap + 1, cy, cx + gap + len + 1, cy + 1);
    ge64_fill_rect(cx, cy - gap - len, cx + 1, cy - gap);
    ge64_fill_rect(cx, cy + gap + 1, cx + 1, cy + gap + len + 1);
    gDPPipeSync(gDisplayListHead++);
    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);

    if (weapon->magSize != 0) {
        print_text_fmt_int(SCREEN_WIDTH - 62, 20, "%d", sGe.ammo[sGe.slot]);
    }
    if (sGe.reloadTimer > 0) {
        print_text(SCREEN_WIDTH / 2 - 32, 40, "RELOAD");
    }
    ge64_print_dialog_text(22, 20, weapon->name, 220);
    if (sGe.bannerTimer > 0) {
        ge64_print_dialog_text(SCREEN_WIDTH / 2 - 50, 150, "New weapon", MIN(255, sGe.bannerTimer * 8));
        ge64_print_dialog_text(SCREEN_WIDTH / 2 - 50, 134, sGeWeapons[sGe.primary].name, MIN(255, sGe.bannerTimer * 8));
    }
#ifdef GE64_LEVEL_SELECT
    print_text_fmt_int(20, 40, "S%d", sGeDebugSpawns);
    print_text_fmt_int(20, 22, "H%d", sGeDebugObjHits);
    print_text_fmt_int(80, 22, "W%d", sGeDebugWallHits);
    print_text_fmt_int(80, 40, "B%x", sGeDebugButtons);
    print_text_fmt_int(150, 40, "A%x", gMarioState->action & 0x1FF);
    print_text_fmt_int(150, 22, "M%d", main_pool_available() / 1024);
    print_text_fmt_int(210, 22, "L%d", (sGeLoadedModel[0] != NULL) + (sGeLoadedModel[1] != NULL));
    print_text_fmt_int(20, 180, "A%d", sGeDebugTarget);
    print_text_fmt_int(80, 180, "H%d", sGeDebugTargetY);
    print_text_fmt_int(180, 180, "P%d", (s32) gMarioState->pos[1]);
#endif
}
