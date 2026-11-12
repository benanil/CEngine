#include "Include/AssetManager.h"
#include "Include/DataStructures/HashMap.h"
#include "Include/Random.h"
#include "Math/Perlin.h"

typedef struct RockTopology_
{
    float3 corners[12];
    u8     faces[20][3];
    u8     edgeId[12][12];
} RockTopology;

static const u8 rockFaces[20][3] = {
    { 0, 11,  5}, { 0,  5,  1}, { 0,  1,  7}, { 0,  7, 10}, { 0, 10, 11},
    { 1,  5,  9}, { 5, 11,  4}, {11, 10,  2}, {10,  7,  6}, { 7,  1,  8},
    { 3,  9,  4}, { 3,  4,  2}, { 3,  2,  6}, { 3,  6,  8}, { 3,  8,  9},
    { 4,  9,  5}, { 2,  4, 11}, { 6,  2, 10}, { 8,  6,  7}, { 9,  8,  1}
};
 
static void RockTopologyInit(RockTopology* t)
{
    const f32 g = 1.61803398875f;
    const float3 raw[12] = {
        {-1.0f,  g, 0.0f}, { 1.0f,  g, 0.0f}, {-1.0f, -g, 0.0f}, { 1.0f, -g, 0.0f},
        {0.0f, -1.0f,  g}, {0.0f,  1.0f,  g}, {0.0f, -1.0f, -g}, {0.0f,  1.0f, -g},
        { g, 0.0f, -1.0f}, { g, 0.0f,  1.0f}, {-g, 0.0f, -1.0f}, {-g, 0.0f,  1.0f}
    };
    for (u32 i = 0; i < 12; i++)
        t->corners[i] = F3Norm(raw[i]);
 
    MemSet(t->edgeId, 0xFF, sizeof(t->edgeId));
    u8 nextEdge = 0;
    for (u32 f = 0; f < 20; f++)
    {
        u8 a = rockFaces[f][0], b = rockFaces[f][1], c = rockFaces[f][2];
        float3 n = F3Cross(F3Sub(t->corners[b], t->corners[a]), F3Sub(t->corners[c], t->corners[a]));
        if (F3Dot(n, t->corners[a]) < 0.0f)
        {
            u8 tmp = b; b = c; c = tmp;
        }
        t->faces[f][0] = a;
        t->faces[f][1] = b;
        t->faces[f][2] = c;
        for (u32 e = 0; e < 3; e++)
        {
            u8 p = t->faces[f][e], q = t->faces[f][(e + 1) % 3];
            u8 lo = p < q ? p : q;
            u8 hi = p < q ? q : p;
            if (t->edgeId[lo][hi] == 0xFF)
                t->edgeId[lo][hi] = nextEdge++;
        }
    }
    ASSERT(nextEdge == 30);
}
 
static u32 RockEdgeVertex(const RockTopology* t, u32 from, u32 to, u32 k, u32 n)
{
    u32 lo  = from < to ? from : to;
    u32 hi  = from < to ? to : from;
    u32 id  = t->edgeId[lo][hi];
    u32 pos = from == lo ? k : n - k;
    return 12u + id * (n - 1u) + pos - 1u;
}
 
static u32 RockGridVertex(const RockTopology* t, u32 f, u32 i, u32 j, u32 n)
{
    const u8* c = t->faces[f];
    if (i == 0u && j == 0u) return c[0];
    if (i == n  && j == 0u) return c[1];
    if (i == 0u && j == n ) return c[2];
    if (j == 0u)     return RockEdgeVertex(t, c[0], c[1], i, n);
    if (i == 0u)     return RockEdgeVertex(t, c[0], c[2], j, n);
    if (i + j == n)  return RockEdgeVertex(t, c[1], c[2], j, n);
    u32 perFace   = (n - 1u) * (n - 2u) / 2u;
    u32 rowOffset = (j - 1u) * (n - 1u) - (j - 1u) * j / 2u;
    return 12u + 30u * (n - 1u) + f * perFace + rowOffset + (i - 1u);
}
 
static Vec3W RockPack(const float3 v[4], f32 scale)
{
    Vec3W w;
    w.x = VecMulf(VecSetR(v[0].x, v[1].x, v[2].x, v[3].x), scale);
    w.y = VecMulf(VecSetR(v[0].y, v[1].y, v[2].y, v[3].y), scale);
    w.z = VecMulf(VecSetR(v[0].z, v[1].z, v[2].z, v[3].z), scale);
    return w;
}
 
static const v128f* RockTangentConst(void)
{
    static v128f t;
    t = VecSetR(1.0f, 0.0f, 0.0f, 1.0f);
    return &t;
}
 
RockMeshes GenerateRock(const RockDesc* desc)
{
    const u32 n          = desc->resolution < 1u ? 1u : desc->resolution;
    const u32 pieceCount = desc->pieceCount < 2u ? 0u : (desc->pieceCount > ROCK_MAX_PIECES ? ROCK_MAX_PIECES : desc->pieceCount);
    const u32 baseVerts  = 10u * n * n + 2u;
    const u32 numTris    = 20u * n * n;
    const f32 uvScale    = 0.5f / desc->radius;
 
    f32 ampSum = 0.0f, amp = 1.0f;
    for (s32 i = 0; i < desc->octaves; i++)
    {
        ampSum += amp;
        amp *= desc->persistence;
    }
    const f32 ampNorm = ampSum > 0.0f ? 1.0f / ampSum : 1.0f;
 
    RockTopology topo;
    RockTopologyInit(&topo);
 
    float3 shape;
    shape.x = RepeatMinMaxF32(WangHash(desc->seed ^ 0xA511E9B3u), 0.75f, 1.0f);
    shape.y = RepeatMinMaxF32(WangHash(desc->seed ^ 0x63D83595u), 0.6f,  0.9f);
    shape.z = RepeatMinMaxF32(WangHash(desc->seed ^ 0x9E3779B9u), 0.75f, 1.0f);
 
    MeshBuilder visual = MeshBuilder_Create((s32)baseVerts, (s32)(numTris * 3u), "Rock");
 
    ArenaMark arenaMark = ArenaSave(&GlobalArena);
    u32* owner = (u32*)ArenaAllocGlobal(sizeof(u32) * numTris);
    u32* nb    = (u32*)ArenaAllocGlobal(sizeof(u32) * 3u * numTris);
    u32* stamp = (u32*)ArenaAllocGlobal(sizeof(u32) * baseVerts);
    u32* remap = (u32*)ArenaAllocGlobal(sizeof(u32) * baseVerts);
    HashMap edgeMap = HMCreate(numTris * 2u, sizeof(u32));
 
    const f32 invN = 1.0f / (f32)n;
    u32 ti = 0;
    for (u32 f = 0; f < 20; f++)
    {
        float3 A = topo.corners[topo.faces[f][0]];
        float3 B = topo.corners[topo.faces[f][1]];
        float3 C = topo.corners[topo.faces[f][2]];
        float3 AB = F3Sub(B, A), AC = F3Sub(C, A);
        for (u32 j = 0; j <= n; j++)
        {
            const u32 count = n - j + 1u;
            for (u32 i0 = 0; i0 < count; i0 += 4u)
            {
                const u32 lanes = (count - i0) < 4u ? (count - i0) : 4u;
                float3 dirs[4];
                for (u32 k = 0; k < 4u; k++)
                {
                    u32 i = i0 + (k < lanes ? k : lanes - 1u);
                    float3 p = F3Add(A, F3Add(F3MulF(AB, (f32)i * invN), F3MulF(AC, (f32)j * invN)));
                    dirs[k] = F3Norm(p);
                }
                v128f noise = Perlin3FBM(RockPack(dirs, desc->frequency), desc->octaves, desc->persistence, desc->lacunarity, desc->seed);
                f32 nz[4];
                VecStore(nz, noise);
                for (u32 k = 0; k < lanes; k++)
                {
                    f32 r = desc->radius * (1.0f + desc->roughness * nz[k] * ampNorm);
                    visual.positions[RockGridVertex(&topo, f, i0 + k, j, n)] = F3Mul(F3MulF(dirs[k], r), shape);
                }
            }
        }
        for (u32 j = 0; j < n; j++)
        {
            for (u32 i = 0; i < n - j; i++)
            {
                u32 a = RockGridVertex(&topo, f, i,     j,     n);
                u32 b = RockGridVertex(&topo, f, i + 1, j,     n);
                u32 c = RockGridVertex(&topo, f, i,     j + 1, n);
                visual.indices[ti++] = a; visual.indices[ti++] = b; visual.indices[ti++] = c;
                if (i + j + 1u < n)
                {
                    u32 d = RockGridVertex(&topo, f, i + 1, j + 1, n);
                    visual.indices[ti++] = b; visual.indices[ti++] = d; visual.indices[ti++] = c;
                }
            }
        }
    }
    ASSERT(ti == numTris * 3u);
 
    for (u32 v = 0; v < baseVerts; v++)
        visual.normals[v] = F3Zero();
    for (u32 t = 0; t < numTris; t++)
    {
        u32 a = visual.indices[t * 3u], b = visual.indices[t * 3u + 1u], c = visual.indices[t * 3u + 2u];
        float3 fn = F3Cross(F3Sub(visual.positions[b], visual.positions[a]), F3Sub(visual.positions[c], visual.positions[a]));
        visual.normals[a] = F3Add(visual.normals[a], fn);
        visual.normals[b] = F3Add(visual.normals[b], fn);
        visual.normals[c] = F3Add(visual.normals[c], fn);
    }
    for (u32 v = 0; v < baseVerts; v++)
    {
        float3 p = visual.positions[v];
        visual.normals[v]   = F3NormSafe(visual.normals[v]);
        visual.texCoords[v] = (float2){ p.x * uvScale + 0.5f, p.z * uvScale + 0.5f };
        visual.tangents[v]  = *RockTangentConst();
    }
 
    RockMeshes out;
    MemSet(&out, 0x0, sizeof(out));
    out.visual = visual;
 
    if (pieceCount != 0u)
    {
        for (u32 t = 0; t < numTris; t++)
        {
            for (u32 e = 0; e < 3; e++)
            {
                u32 a = visual.indices[t * 3u + e], b = visual.indices[t * 3u + (e + 1u) % 3u];
                u32 lo = a < b ? a : b;
                u32 hi = a < b ? b : a;
                u64 key  = ((u64)lo << 32) | (u64)hi;
                u32 self = t * 3u + e;
                const u32* found = (const u32*)HMFind(&edgeMap, key);
                if (found)
                {
                    u32 other = *found;
                    nb[self]  = other;
                    nb[other] = self;
                }
                else
                {
                    HMInsert(&edgeMap, key, &self);
                }
            }
        }
 
        float3 seedPos[ROCK_MAX_PIECES];
        u32 triCount[ROCK_MAX_PIECES];
        u32 capCount[ROCK_MAX_PIECES];
        MemSet(triCount, 0, sizeof(triCount));
        MemSet(capCount, 0, sizeof(capCount));
 
        f32 phase = NextFloat01(WangHash(desc->seed + 999u)) * MATH_PI * 2.0f;
        for (u32 p = 0; p < pieceCount; p++)
        {
            f32 y  = 1.0f - 2.0f * ((f32)p + 0.5f) / (f32)pieceCount;
            f32 r  = Sqrtf(Maxf32(0.0f, 1.0f - y * y));
            f32 th = (f32)p * 2.3999632f + phase;
            float3 d = { Cos(th) * r, y, Sin(th) * r };
            float3 j;
            j.x = RepeatMinMaxF32(WangHash(desc->seed + p * 7u + 101u), -0.3f, 0.3f);
            j.y = RepeatMinMaxF32(WangHash(desc->seed + p * 7u + 102u), -0.3f, 0.3f);
            j.z = RepeatMinMaxF32(WangHash(desc->seed + p * 7u + 103u), -0.3f, 0.3f);
            d = F3Norm(F3Add(d, j));
            seedPos[p] = F3Mul(F3MulF(d, desc->radius * 0.45f), shape);
        }
 
        const f32 warpScale = desc->frequency * 2.0f / desc->radius;
        const f32 warpAmp   = desc->fractureWarp * desc->radius;
        for (u32 t0 = 0; t0 < numTris; t0 += 4u)
        {
            const u32 lanes = (numTris - t0) < 4u ? (numTris - t0) : 4u;
            float3 cs[4];
            for (u32 k = 0; k < 4u; k++)
            {
                u32 t = t0 + (k < lanes ? k : lanes - 1u);
                cs[k] = F3DivF(F3Add(visual.positions[visual.indices[t * 3u]],
                                     F3Add(visual.positions[visual.indices[t * 3u + 1u]],
                                           visual.positions[visual.indices[t * 3u + 2u]])), 3.0f);
            }
            Vec3W w = RockPack(cs, warpScale);
            f32 wx[4], wy[4], wz[4];
            VecStore(wx, Perlin3FBM(w, 2, 0.5f, 2.0f, desc->seed ^ 0x51ED270Bu));
            VecStore(wy, Perlin3FBM(w, 2, 0.5f, 2.0f, desc->seed ^ 0x2545F491u));
            VecStore(wz, Perlin3FBM(w, 2, 0.5f, 2.0f, desc->seed ^ 0x7A3C9D15u));
            for (u32 k = 0; k < lanes; k++)
            {
                u32 t = t0 + k;
                float3 c = F3Add(cs[k], (float3){ wx[k] * warpAmp, wy[k] * warpAmp, wz[k] * warpAmp });
                f32 best = 3.402823e38f;
                owner[t] = 0;
                for (u32 p = 0; p < pieceCount; p++)
                {
                    float3 d = F3Sub(c, seedPos[p]);
                    f32 dist = F3Dot(d, d);
                    if (dist < best)
                    {
                        best = dist;
                        owner[t] = p;
                    }
                }
                triCount[owner[t]]++;
            }
        }
        for (u32 t = 0; t < numTris; t++)
            for (u32 e = 0; e < 3; e++)
                if (owner[nb[t * 3u + e] / 3u] != owner[t])
                    capCount[owner[t]]++;
 
        MemSet(stamp, 0, sizeof(u32) * baseVerts);
        for (u32 p = 0; p < pieceCount; p++)
        {
            if (triCount[p] == 0u)
                continue;
 
            u32 used = 0;
            for (u32 t = 0; t < numTris; t++)
            {
                if (owner[t] != p)
                    continue;
                for (u32 k = 0; k < 3; k++)
                {
                    u32 v = visual.indices[t * 3u + k];
                    if (stamp[v] != 2u * p + 1u)
                    {
                        stamp[v] = 2u * p + 1u;
                        used++;
                    }
                }
            }
 
            u32 numVertex = used + 3u * capCount[p];
            u32 numIndex  = 3u * (triCount[p] + capCount[p]);
            MeshBuilder pb = MeshBuilder_Create((s32)numVertex, (s32)numIndex, "RockPiece");
 
            u32 vc = 0, ic = 0;
            for (u32 t = 0; t < numTris; t++)
            {
                if (owner[t] != p)
                    continue;
                for (u32 k = 0; k < 3; k++)
                {
                    u32 v = visual.indices[t * 3u + k];
                    if (stamp[v] != 2u * p + 2u)
                    {
                        stamp[v] = 2u * p + 2u;
                        remap[v] = vc;
                        pb.positions[vc] = visual.positions[v];
                        pb.texCoords[vc] = visual.texCoords[v];
                        pb.normals  [vc] = visual.normals[v];
                        pb.tangents [vc] = visual.tangents[v];
                        vc++;
                    }
                    pb.indices[ic++] = remap[v];
                }
            }
 
            for (u32 t = 0; t < numTris; t++)
            {
                if (owner[t] != p)
                    continue;
                for (u32 e = 0; e < 3; e++)
                {
                    if (owner[nb[t * 3u + e] / 3u] == p)
                        continue;
                    float3 pa = visual.positions[visual.indices[t * 3u + e]];
                    float3 pbv = visual.positions[visual.indices[t * 3u + (e + 1u) % 3u]];
                    float3 pc = seedPos[p];
                    float3 fn = F3NormSafe(F3Cross(F3Sub(pa, pbv), F3Sub(pc, pbv)));
                    float3 cp[3] = { pbv, pa, pc };
                    for (u32 k = 0; k < 3; k++)
                    {
                        pb.positions[vc] = cp[k];
                        pb.texCoords[vc] = (float2){ cp[k].x * uvScale + 0.5f, cp[k].z * uvScale + 0.5f };
                        pb.normals  [vc] = fn;
                        pb.tangents [vc] = *RockTangentConst();
                        pb.indices[ic++] = vc;
                        vc++;
                    }
                }
            }
            ASSERT(vc == numVertex && ic == numIndex);
            for (u32 i = 0; i < numIndex; i++)
                ASSERT(pb.indices[i] < numVertex);
            out.pieces[out.pieceCount++] = pb;
        }
    }
 
    for (u32 i = 0; i < numTris * 3u; i++)
        ASSERT(visual.indices[i] < baseVerts);
 
    HMDestroy(&edgeMap);
    ArenaRestore(&GlobalArena, arenaMark);
    return out;
}
 