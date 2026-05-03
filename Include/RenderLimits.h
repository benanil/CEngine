#ifndef RENDER_LIMITS_H
#define RENDER_LIMITS_H

#define MAX_ENTITY              65535u
#define MAX_GROUP               (MAX_ENTITY >> 1)
#define MAX_BUNDLES             (MAX_ENTITY >> 2)

#define MAX_BONES               96
#define MaxBoneDepth            32
#define MAX_ANIM_INSTANCES      1024
#define ANIM_NUM_FRAMES         24
#define MAX_ANIM_DURATION       8
#define MAX_ANIM_COUNT          128
#define MAX_SKIN_COUNT          128
#define ANIM_POSE_NUM_INT32     4
#define ANIM_MATRIX_NUM_INT32   8
#define MAX_GPU_ANIM_FRAMES     (ANIM_NUM_FRAMES * MAX_ANIM_DURATION * MAX_ANIM_COUNT)
#define ANIM_NODE_COUNT         (MAX_BONES * 2)
#define ANIM_CHILD_PACKED_COUNT ((ANIM_NODE_COUNT + 3) / 4)

#endif
