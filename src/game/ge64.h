#ifndef GE64_H
#define GE64_H

#include <PR/ultratypes.h>

#include "types.h"
#include "engine/graph_node.h"

// GoldenEye-style first person layer on top of SM64.
// Mario's normal free-movement actions are remapped to ACT_FPS_GROUND / ACT_FPS_CROUCH / ACT_FPS_AIR,
// the camera is overridden while those (and other "playable") actions run, and weapons fire hitscan
// shots that deliver Mario-style attacks to objects.

#define GE64_IS_FPS_GROUND(action) ((action) == ACT_FPS_GROUND || (action) == ACT_FPS_CROUCH)

u32 ge64_remap_action(struct MarioState *m, u32 action);
s32 act_fps_ground(struct MarioState *m);
s32 act_fps_air(struct MarioState *m);

void ge64_level_init(void);
void ge64_on_star_collected(void);
void ge64_mario_post_update(struct MarioState *m);
void ge64_restore_controller(void);
s32 ge64_view_active(void);

void ge64_update_graph_node_camera(struct GraphNodeCamera *gc);
void ge64_update_perspective(struct GraphNodePerspective *perspective);

void ge64_render_viewmodel(void);
void ge64_render_hud(void);
Gfx *geo_ge64_title_gun(s32 callContext, struct GraphNode *node, Mat4 *context);

#endif // GE64_H
