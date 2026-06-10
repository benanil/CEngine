#ifndef BVH_H
#define BVH_H

#include "GLTFParser.h"
#include "SIMD.h"

#if defined(__cplusplus)
extern "C" {
#endif

// blas node, ported from the old engine. 32 bytes and tightly packed so the arrays can
// upload to the gpu as is (float4 pairs: aabbMin+leftFirst, aabbMax+triCount)
typedef struct BVHNode_
{
    float aabbMin[3];
    u32   leftFirst; // first child pair index, or first triangle when triCount > 0
    float aabbMax[3];
    u32   triCount;  // leaf when > 0
} BVHNode;

// one triangle record, 16 bytes, vertex indices are mega buffer absolute
typedef struct BVHTri_
{
    u32 v0, v1, v2, padding;
} BVHTri;

typedef struct BVHHit_
{
    f32 t;            // ray distance
    f32 u, v;         // barycentric coordinates
    u32 triIndex;     // bundle local triangle record
    u32 skinnedSet;   // which render set the entity lives in
    u32 groupIdx;     // primitive group inside the set
    u32 entityIdx;    // group local entity
    u32 bundleIdx;    // scene bundle
    AX_ALIGN(16) float position[4]; // world space hit point
} BVHHit;

// builds the blas of every primitive of the bundle from the lod0 triangles, fills
// APrimitive.bvhNodeIndex and the bundle's bvhNodes/bvhTris arrays (heap allocated,
// shared through the bundle cache like the rest of the bundle). out: 0 on failure
s32 BVH_BuildBundle(SceneBundle* bundle, bool skinned);

void BVH_FreeBundle(SceneBundle* bundle);

struct Scene_;

// casts a world space ray through every entity of the scene's render sets, the ray is
// transformed into entity local space per instance. out: 1 when *hit is filled
s32 BVH_RaycastScene(const struct Scene_* scene, v128f origin, v128f dir, BVHHit* hit);

#if defined(__cplusplus)
}
#endif

#endif // BVH_H
