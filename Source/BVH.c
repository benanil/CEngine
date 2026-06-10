// per primitive blas build and ray traversal, ported from the old engine's BVH.cpp.
// nodes and triangles live in two tightly packed arrays per bundle so they can be
// shared with the gpu later. used by the editor for click picking
#include "Include/BVH.h"
#include "Include/Scene.h"
#include "Include/Graphics.h"
#include "Include/Memory.h"
#include "Include/Platform.h"
#include "Math/Quaternion.h"
#include "Math/Bitpack.h"
#include "Math/Half.h"

extern Graphics gGFX;

enum { BVH_BINS = 8 };
enum { BVH_MAX_DEPTH_STACK = 128 };
#define BVH_MISS 1e30f

/*//////////////////////////////////////////////////////////////////////////*/
/*                                  Build                                   */
/*//////////////////////////////////////////////////////////////////////////*/

typedef struct BVHBuild_
{
    BVHNode* nodes;
    BVHTri*  tris;
    v128f*   centroids; // build only, parallel to tris
    u32      nodesUsed;
    bool     skinned;
} BVHBuild;

// vertex position fetch from the cpu mega buffers, indices are mega absolute.
// skinned vertices store the bind pose as packed halves
static v128f BVH_Position(bool skinned, u32 index)
{
    if (!skinned)
    {
        const AVertex* v = gGFX.SurfaceVertexBuffer + index;
        return VecSetR(v->position.x, v->position.y, v->position.z, 0.0f);
    }
    const ASkinedVertex* v = gGFX.SkinnedVertexBuffer + index;
    return VecSetR(HalfToFloat((f16)(v->positionXY & 0xFFFFu)),
                   HalfToFloat((f16)(v->positionXY >> 16u)),
                   HalfToFloat((f16)(v->positionZW & 0xFFFFu)), 0.0f);
}

static void BVH_UpdateNodeBounds(BVHBuild* build, BVHNode* node)
{
    v128f bmin = VecSet1( BVH_MISS);
    v128f bmax = VecSet1(-BVH_MISS);
    for (u32 i = node->leftFirst; i < node->leftFirst + node->triCount; i++)
    {
        const BVHTri* tri = &build->tris[i];
        v128f v0 = BVH_Position(build->skinned, tri->v0);
        v128f v1 = BVH_Position(build->skinned, tri->v1);
        v128f v2 = BVH_Position(build->skinned, tri->v2);
        bmin = VecMin(bmin, VecMin(v0, VecMin(v1, v2)));
        bmax = VecMax(bmax, VecMax(v0, VecMax(v1, v2)));
    }
    Vec3Store(node->aabbMin, bmin);
    Vec3Store(node->aabbMax, bmax);
}

static f32 BVH_AreaOf(v128f bmin, v128f bmax)
{
    v128f e = VecSub(bmax, bmin);
    f32 ex = VecGetX(e), ey = VecGetY(e), ez = VecGetZ(e);
    return ex * ey + ey * ez + ez * ex;
}

typedef struct BVHBin_
{
    v128f bmin, bmax;
    u32 triCount;
} BVHBin;

// sah binned split search, ported from the old engine (8 bins per axis)
static f32 BVH_FindBestSplit(BVHBuild* build, const BVHNode* node, int* outAxis, f32* outPos)
{
    f32 bestCost = BVH_MISS;
    for (int axis = 0; axis < 3; axis++)
    {
        f32 boundsMin = BVH_MISS, boundsMax = -BVH_MISS;
        for (u32 i = node->leftFirst; i < node->leftFirst + node->triCount; i++)
        {
            AX_ALIGN(16) float c[4];
            VecStore(c, build->centroids[i]);
            boundsMin = Minf32(boundsMin, c[axis]);
            boundsMax = Maxf32(boundsMax, c[axis]);
        }
        if (boundsMin == boundsMax) continue;

        BVHBin bins[BVH_BINS];
        for (int b = 0; b < BVH_BINS; b++)
        {
            bins[b].bmin = VecSet1( BVH_MISS);
            bins[b].bmax = VecSet1(-BVH_MISS);
            bins[b].triCount = 0;
        }

        f32 binScale = (f32)BVH_BINS / (boundsMax - boundsMin);
        for (u32 i = node->leftFirst; i < node->leftFirst + node->triCount; i++)
        {
            AX_ALIGN(16) float c[4];
            VecStore(c, build->centroids[i]);
            int binIdx = (int)((c[axis] - boundsMin) * binScale);
            if (binIdx > BVH_BINS - 1) binIdx = BVH_BINS - 1;
            BVHBin* bin = &bins[binIdx];
            bin->triCount++;
            const BVHTri* tri = &build->tris[i];
            v128f v0 = BVH_Position(build->skinned, tri->v0);
            v128f v1 = BVH_Position(build->skinned, tri->v1);
            v128f v2 = BVH_Position(build->skinned, tri->v2);
            bin->bmin = VecMin(bin->bmin, VecMin(v0, VecMin(v1, v2)));
            bin->bmax = VecMax(bin->bmax, VecMax(v0, VecMax(v1, v2)));
        }

        // sweep the bins to get the left/right costs of every split plane
        f32 leftArea[BVH_BINS - 1], rightArea[BVH_BINS - 1];
        u32 leftCount[BVH_BINS - 1], rightCount[BVH_BINS - 1];
        v128f lmin = VecSet1(BVH_MISS), lmax = VecSet1(-BVH_MISS);
        v128f rmin = VecSet1(BVH_MISS), rmax = VecSet1(-BVH_MISS);
        u32 leftSum = 0, rightSum = 0;
        for (int b = 0; b < BVH_BINS - 1; b++)
        {
            leftSum += bins[b].triCount;
            leftCount[b] = leftSum;
            lmin = VecMin(lmin, bins[b].bmin);
            lmax = VecMax(lmax, bins[b].bmax);
            leftArea[b] = BVH_AreaOf(lmin, lmax);

            rightSum += bins[BVH_BINS - 1 - b].triCount;
            rightCount[BVH_BINS - 2 - b] = rightSum;
            rmin = VecMin(rmin, bins[BVH_BINS - 1 - b].bmin);
            rmax = VecMax(rmax, bins[BVH_BINS - 1 - b].bmax);
            rightArea[BVH_BINS - 2 - b] = BVH_AreaOf(rmin, rmax);
        }

        f32 binWidth = (boundsMax - boundsMin) / (f32)BVH_BINS;
        for (int b = 0; b < BVH_BINS - 1; b++)
        {
            f32 cost = (f32)leftCount[b] * leftArea[b] + (f32)rightCount[b] * rightArea[b];
            if (cost > 0.0f && cost < bestCost)
            {
                bestCost = cost;
                *outAxis = axis;
                *outPos = boundsMin + binWidth * (f32)(b + 1);
            }
        }
    }
    return bestCost;
}

static void BVH_Subdivide(BVHBuild* build, u32 nodeIdx)
{
    BVHNode* node = &build->nodes[nodeIdx];
    if (node->triCount <= 2) return;

    int axis = 0;
    f32 splitPos = 0.0f;
    f32 splitCost = BVH_FindBestSplit(build, node, &axis, &splitPos);

    v128f bmin = VecSetR(node->aabbMin[0], node->aabbMin[1], node->aabbMin[2], 0.0f);
    v128f bmax = VecSetR(node->aabbMax[0], node->aabbMax[1], node->aabbMax[2], 0.0f);
    f32 noSplitCost = (f32)node->triCount * BVH_AreaOf(bmin, bmax);
    if (splitCost >= noSplitCost) return;

    // partition the triangles (and their centroids) in place around the split plane
    u32 i = node->leftFirst;
    u32 j = i + node->triCount - 1;
    while (i <= j && j != ~0u)
    {
        AX_ALIGN(16) float c[4];
        VecStore(c, build->centroids[i]);
        if (c[axis] < splitPos)
            i++;
        else
        {
            BVHTri tmpTri = build->tris[i]; build->tris[i] = build->tris[j]; build->tris[j] = tmpTri;
            v128f tmpC = build->centroids[i]; build->centroids[i] = build->centroids[j]; build->centroids[j] = tmpC;
            j--;
        }
    }

    u32 leftCount = i - node->leftFirst;
    if (leftCount == 0 || leftCount == node->triCount) return;

    u32 leftChild = build->nodesUsed;
    build->nodesUsed += 2;
    build->nodes[leftChild].leftFirst      = node->leftFirst;
    build->nodes[leftChild].triCount       = leftCount;
    build->nodes[leftChild + 1].leftFirst  = i;
    build->nodes[leftChild + 1].triCount   = node->triCount - leftCount;
    node->leftFirst = leftChild;
    node->triCount = 0;

    BVH_UpdateNodeBounds(build, &build->nodes[leftChild]);
    BVH_UpdateNodeBounds(build, &build->nodes[leftChild + 1]);
    BVH_Subdivide(build, leftChild);
    BVH_Subdivide(build, leftChild + 1);
}

s32 BVH_BuildBundle(SceneBundle* bundle, bool skinned)
{
    if (bundle->bvhNodes || !bundle->allIndices) return bundle->bvhNodes != NULL;

    double startTime = TimeSinceStartup();
    u32 totalTris = 0;
    for (int m = 0; m < bundle->numMeshes; m++)
        for (int p = 0; p < bundle->meshes[m].numPrimitives; p++)
            totalTris += (u32)bundle->meshes[m].primitives[p].lodNumIndices[0] / 3u;
    if (totalTris == 0) return 0;

    // worst case two nodes per triangle, the arrays shrink to the used size after the
    // build. heap allocated, the engine tlsf pool is too small for big scenes
    BVHBuild build;
    build.nodes     = (BVHNode*)SDL_malloc((u64)totalTris * 2u * sizeof(BVHNode));
    build.tris      = (BVHTri*)SDL_malloc((u64)totalTris * sizeof(BVHTri));
    build.centroids = (v128f*)SDL_aligned_alloc(16, (u64)totalTris * sizeof(v128f));
    build.nodesUsed = 0;
    build.skinned   = skinned;
    if (!build.nodes || !build.tris || !build.centroids)
    {
        if (build.nodes) SDL_free(build.nodes);
        if (build.tris) SDL_free(build.tris);
        if (build.centroids) SDL_aligned_free(build.centroids);
        AX_ERROR("bvh build allocation failed: tris=%d", totalTris);
        return 0;
    }

    const f32 third = 1.0f / 3.0f;
    u32 triCursor = 0;
    for (int m = 0; m < bundle->numMeshes; m++)
    {
        for (int p = 0; p < bundle->meshes[m].numPrimitives; p++)
        {
            APrimitive* prim = &bundle->meshes[m].primitives[p];
            u32 numTris = (u32)prim->lodNumIndices[0] / 3u;
            if (numTris == 0)
            {
                prim->bvhNodeIndex = ~0u;
                continue;
            }

            u32 firstTri = triCursor;
            const u32* indices = gGFX.IndexBuffer + prim->lodIndexOffset[0];
            for (u32 t = 0; t < numTris; t++)
            {
                BVHTri* tri = &build.tris[triCursor];
                tri->v0 = indices[t * 3u + 0u];
                tri->v1 = indices[t * 3u + 1u];
                tri->v2 = indices[t * 3u + 2u];
                tri->padding = 0;
                v128f sum = VecAdd(VecAdd(BVH_Position(skinned, tri->v0), BVH_Position(skinned, tri->v1)),
                                   BVH_Position(skinned, tri->v2));
                build.centroids[triCursor] = VecMulf(sum, third);
                triCursor++;
            }

            u32 rootIdx = build.nodesUsed++;
            prim->bvhNodeIndex = rootIdx;
            build.nodes[rootIdx].leftFirst = firstTri;
            build.nodes[rootIdx].triCount = numTris;
            BVH_UpdateNodeBounds(&build, &build.nodes[rootIdx]);
            BVH_Subdivide(&build, rootIdx);
        }
    }

    SDL_aligned_free(build.centroids);
    bundle->bvhNodes = SDL_realloc(build.nodes, (u64)build.nodesUsed * sizeof(BVHNode));
    bundle->bvhTris = build.tris;
    bundle->numBvhNodes = (int)build.nodesUsed;
    bundle->numBvhTris = (int)totalTris;
    AX_LOG("bvh built: tris=%d nodes=%d %.2fs", totalTris, build.nodesUsed, TimeSinceStartup() - startTime);
    return 1;
}

void BVH_FreeBundle(SceneBundle* bundle)
{
    if (bundle->bvhNodes) SDL_free(bundle->bvhNodes);
    if (bundle->bvhTris) SDL_free(bundle->bvhTris);
    bundle->bvhNodes = NULL;
    bundle->bvhTris = NULL;
    bundle->numBvhNodes = 0;
    bundle->numBvhTris = 0;
}

/*//////////////////////////////////////////////////////////////////////////*/
/*                                Traversal                                 */
/*//////////////////////////////////////////////////////////////////////////*/

static f32 BVH_IntersectAABB(v128f origin, v128f invDir, v128f bmin, v128f bmax, f32 minSoFar)
{
    v128f t0 = VecMul(VecSub(bmin, origin), invDir);
    v128f t1 = VecMul(VecSub(bmax, origin), invDir);
    v128f tsmall = VecMin(t0, t1);
    v128f tbig   = VecMax(t0, t1);
    f32 tnear = Maxf32(Maxf32(VecGetX(tsmall), VecGetY(tsmall)), VecGetZ(tsmall));
    f32 tfar  = Minf32(Minf32(VecGetX(tbig), VecGetY(tbig)), VecGetZ(tbig));
    if (tnear < tfar && tfar > 0.0f && tnear < minSoFar)
        return tnear > 0.0f ? tnear : 0.0f;
    return BVH_MISS;
}

// moller trumbore, ported from the old engine. dir may be unnormalized so t stays
// comparable across differently scaled instances
static bool BVH_IntersectTriangle(v128f origin, v128f dir, v128f v0, v128f v1, v128f v2, BVHHit* hit, u32 triIndex)
{
    v128f edge1 = VecSub(v1, v0);
    v128f edge2 = VecSub(v2, v0);

    v128f h = Vec3Cross(dir, edge2);
    f32 a = Vec3DotfV(edge1, h);
    if (a > -1.0e-9f && a < 1.0e-9f) return false; // ray parallel to triangle

    f32 f = 1.0f / a;
    v128f s = VecSub(origin, v0);
    f32 u = f * Vec3DotfV(s, h);
    bool fail = (u < 0.0f) | (u > 1.0f);

    v128f q = Vec3Cross(s, edge1);
    f32 v = f * Vec3DotfV(dir, q);
    f32 t = f * Vec3DotfV(edge2, q);
    fail |= (v < 0.0f) | (u + v > 1.0f);

    if (!fail & (t > 0.0001f) & (t < hit->t))
    {
        hit->u = u;
        hit->v = v;
        hit->t = t;
        hit->triIndex = triIndex;
        return true;
    }
    return false;
}

// stack based traversal of one primitive blas, the ray is already in local space
static bool BVH_Intersect(const SceneBundle* bundle, bool skinned, u32 rootNode,
                          v128f origin, v128f dir, BVHHit* hit)
{
    const BVHNode* nodes = (const BVHNode*)bundle->bvhNodes;
    const BVHTri* tris = (const BVHTri*)bundle->bvhTris;
    if (!nodes || rootNode == ~0u) return false;

    u32 nodesToVisit[BVH_MAX_DEPTH_STACK];
    nodesToVisit[0] = rootNode;
    int stackCount = 1;
    // full precision reciprocal, the approximate VecRcp misses thin aabbs
    v128f invDir = VecDiv(VecSet1(1.0f), dir);
    bool intersection = false;

    while (stackCount > 0)
    {
        const BVHNode* node = nodes + nodesToVisit[--stackCount];
    traverse:;
        u32 triCount = node->triCount;
        u32 leftFirst = node->leftFirst;
        if (triCount > 0) // leaf
        {
            for (u32 i = leftFirst; i < leftFirst + triCount; i++)
            {
                const BVHTri* tri = tris + i;
                v128f v0 = BVH_Position(skinned, tri->v0);
                v128f v1 = BVH_Position(skinned, tri->v1);
                v128f v2 = BVH_Position(skinned, tri->v2);
                intersection |= BVH_IntersectTriangle(origin, dir, v0, v1, v2, hit, i);
            }
            continue;
        }

        u32 leftIndex = leftFirst;
        u32 rightIndex = leftIndex + 1;
        const BVHNode* leftNode  = nodes + leftIndex;
        const BVHNode* rightNode = nodes + rightIndex;

        v128f lmin = VecSetR(leftNode->aabbMin[0], leftNode->aabbMin[1], leftNode->aabbMin[2], 0.0f);
        v128f lmax = VecSetR(leftNode->aabbMax[0], leftNode->aabbMax[1], leftNode->aabbMax[2], 0.0f);
        v128f rmin = VecSetR(rightNode->aabbMin[0], rightNode->aabbMin[1], rightNode->aabbMin[2], 0.0f);
        v128f rmax = VecSetR(rightNode->aabbMax[0], rightNode->aabbMax[1], rightNode->aabbMax[2], 0.0f);

        f32 dist1 = BVH_IntersectAABB(origin, invDir, lmin, lmax, hit->t);
        f32 dist2 = BVH_IntersectAABB(origin, invDir, rmin, rmax, hit->t);

        if (dist1 > dist2)
        {
            f32 tmpD = dist1; dist1 = dist2; dist2 = tmpD;
            u32 tmpI = leftIndex; leftIndex = rightIndex; rightIndex = tmpI;
        }

        if (dist1 == BVH_MISS) continue;
        node = nodes + leftIndex;
        if (dist2 != BVH_MISS && stackCount < BVH_MAX_DEPTH_STACK)
            nodesToVisit[stackCount++] = rightIndex;
        goto traverse;
    }
    return intersection;
}

/*//////////////////////////////////////////////////////////////////////////*/
/*                              Scene Raycast                               */
/*//////////////////////////////////////////////////////////////////////////*/

// world scale of an entity, matches the unpack in the surface vertex shader
static v128f BVH_EntityScale(u32 packed)
{
    f32 x = (f32)(packed & 0x7FFu) / 2047.0f;
    f32 y = (f32)((packed >> 11u) & 0x7FFu) / 2047.0f;
    f32 z = (f32)((packed >> 22u) & 0x3FFu) / 1023.0f;
    return VecSetR(Maxf32(x * 10.0f, 1.0e-6f), Maxf32(y * 10.0f, 1.0e-6f), Maxf32(z * 10.0f, 1.0e-6f), 1.0f);
}

static s32 BVH_RaycastSet(const RenderSet* set, bool skinned, v128f origin, v128f dir, BVHHit* hit)
{
    s32 anyHit = 0;
    for (u32 b = 0; b < set->numBundles; b++)
    {
        const SceneBundle* bundle = set->bundles[b];
        if (!bundle || !bundle->bvhNodes) continue;

        Range range = set->bundleRange[b];
        for (u32 g = range.start; g < range.start + range.count; g++)
        {
            const PrimitiveGroup* group = &set->primitiveGroups[g];
            if (!group->valid || group->numEntities == 0) continue;

            const APrimitive* prim = &bundle->meshes[group->meshIndex].primitives[group->primitiveIndex];
            if (prim->bvhNodeIndex == ~0u) continue;

            for (u32 e = 0; e < group->numEntities; e++)
            {
                const Entity* entity = &set->entities[group->entityOffset + e];

                // transform the ray into entity local space instead of the mesh into
                // world space, the unnormalized direction keeps t in world units
                v128f rotation = VecNorm(UnpackQuaternionS16Norm1(entity->rotation));
                v128f invRot   = QConjugate(rotation);
                v128f scale    = BVH_EntityScale(entity->scale);
                v128f localOrigin = VecDiv(QMulVec3V(VecSub(origin, entity->position), invRot), scale);
                v128f localDir    = VecDiv(QMulVec3V(dir, invRot), scale);

                f32 before = hit->t;
                if (BVH_Intersect(bundle, skinned, prim->bvhNodeIndex, localOrigin, localDir, hit) && hit->t < before)
                {
                    hit->skinnedSet = skinned;
                    hit->groupIdx = g;
                    hit->entityIdx = e;
                    anyHit = 1;
                }
            }
        }
    }
    return anyHit;
}

s32 BVH_RaycastScene(const Scene* scene, v128f origin, v128f dir, BVHHit* hit)
{
    MemsetZero(hit, sizeof(*hit));
    hit->t = BVH_MISS;

    s32 anyHit = 0;
    anyHit |= BVH_RaycastSet(&scene->surfaceSet, false, origin, dir, hit);
    anyHit |= BVH_RaycastSet(&scene->skinnedSet, true, origin, dir, hit);
    if (!anyHit) return 0;

    // resolve the render set bundle back to the scene bundle for reporting
    const RenderSet* set = hit->skinnedSet ? &scene->skinnedSet : &scene->surfaceSet;
    u32 renderBundle = ~0u;
    for (u32 b = 0; b < set->numBundles; b++)
    {
        Range range = set->bundleRange[b];
        if (hit->groupIdx >= range.start && hit->groupIdx < range.start + range.count) { renderBundle = b; break; }
    }
    hit->bundleIdx = ~0u;
    for (u32 i = 0; i < scene->numBundles; i++)
    {
        if ((scene->bundleRefs[i].skinned != 0) == (hit->skinnedSet != 0) && scene->bundleRefs[i].renderIdx == renderBundle)
        {
            hit->bundleIdx = i;
            break;
        }
    }

    v128f hitPos = VecAdd(origin, VecMulf(dir, hit->t));
    VecStore(hit->position, hitPos);
    return 1;
}
