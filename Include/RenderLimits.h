#ifndef RENDER_LIMITS_H
#define RENDER_LIMITS_H

#define MAX_BUNDLES             65534u
#define MAX_ENTITY              131070u /* practical resident entity storage */
#define MESH_LOD_COUNT          3u

#define SHADOW_MAP_SIZE            (1024u * 3u)
#define SHADOW_CASCADE_COUNT       3u
#define SHADOW_MAX_DISTANCE        160.0f
#define SHADOW_NEAR_PLANE          1.0f
#define SHADOW_CAMERA_DISTANCE     200.0f
#define SHADOW_CASTER_DEPTH_MARGIN 150.0f
#define SHADOW_CASCADE_OVERLAP     1.0f
#define SHADOW_SPLIT_NEAR_DISTANCE 40.0f
#define SHADOW_PSSM_LAMBDA         0.5f

#define POINT_SHADOW_SIZE          384u
#define POINT_SHADOW_FACE_COUNT    6u
#define POINT_SHADOW_ATLAS_WIDTH   (POINT_SHADOW_SIZE * POINT_SHADOW_FACE_COUNT)
#define POINT_SHADOW_MAX_LIGHTS    16u
#define POINT_SHADOW_LAYER_COUNT   (POINT_SHADOW_FACE_COUNT * POINT_SHADOW_MAX_LIGHTS)
#define POINT_SHADOW_NEAR_PLANE    0.05f

#define SPOT_SHADOW_SIZE           384u
#define SPOT_SHADOW_MAX_LIGHTS     16u
#define SPOT_SHADOW_NEAR_PLANE     0.05f

#define MatrixNumInt32          6 /* fp16 3x4 bone matrix (12 halves / 6 u32) */
#define MAX_BONES               96
#define MaxBoneDepth            32
// 1024 animation is 512mb memory on gpu
#define MAX_ANIM_INSTANCES      1024ull
#define ANIM_NUM_FRAMES         24
#define MAX_ANIM_DURATION       8
#define MAX_ANIM_COUNT          128
#define MAX_SKIN_COUNT          128
#define ANIM_POSE_NUM_INT32     4
#define ANIM_MATRIX_NUM_INT32   16 /* fp32 4x4 inverse-bind matrix (16 floats) */
#define MAX_GPU_ANIM_FRAMES     (ANIM_NUM_FRAMES * MAX_ANIM_DURATION * MAX_ANIM_COUNT)
#define ANIM_NODE_COUNT         (MAX_BONES * 2)
#define ANIM_CHILD_PACKED_COUNT ((ANIM_NODE_COUNT + 3) / 4)
#define ANIMATION_PRECISION     0.001f
#define ANIMATION_MAX_METERS    (4095.0f * ANIMATION_PRECISION)


#define MAX_LINE_COUNT (MAX_ENTITY * 100)
#define MAX_LIGHT_COUNT 1024u
#define LIGHT_TILE_SIZE 16u
#define MAX_LIGHT_TILES 131072u
#define MAX_LIGHTS_PER_TILE 16u

// max animation bounds is ANIMATION_MAX_METERS because we use ANIMATION_PRECISION bit precision
#define MAX_SURFACE_VERTEX                   10000000ull /* 160 megabytes */
#define MAX_SKINNED_SOURCE_VERTEX            262144ull
#define MAX_SKINNED_VERTEX_PER_ANIM_INSTANCE 65536ull
#define MAX_ANIMATED_VERTEX (MAX_ANIM_INSTANCES * MAX_SKINNED_VERTEX_PER_ANIM_INSTANCE)
#define MAX_VERTEX MAX_SURFACE_VERTEX
#define MAX_INDEX  32000000ull /* 128 megabytes */

#endif
