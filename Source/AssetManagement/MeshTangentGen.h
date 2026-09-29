#ifndef MESH_TANGENT_GEN_H
#define MESH_TANGENT_GEN_H

#include "Include/Platform.h" // AX_LOG
#include "Math/Vector.h"
#include "Extern/mikktspace.h"

typedef struct TangentW_ { f32 x, y, z, w; } TangentW;

typedef struct TgMikkData_
{
    const float3* positions;
    const float2* texCoords;
    const float3* normals;
    const u32*    indices;
    u32           vertexBase;
    s32           numFaces;
    TangentW*     out;
} TgMikkData;

static inline float3 TgPerpendicular(float3 n)
{
    float3 axis = Absf32(n.x) < 0.9f ? (float3){ 1.0f, 0.0f, 0.0f } : (float3){ 0.0f, 1.0f, 0.0f };
    float3 t = F3Sub(axis, F3MulF(n, F3Dot(n, axis)));
    f32 len = F3Len(t);
    return len > 1e-12f ? F3MulF(t, 1.0f / len) : (float3){ 1.0f, 0.0f, 0.0f };
}

static void TgWeldPositions(const float3* positions, s32 numVertices, u32* remap)
{
    u32 tableSize = 16u;
    while (tableSize < (u32)numVertices * 2u)
        tableSize <<= 1;
    u32 mask = tableSize - 1u;
    u32* table = (u32*)ArenaPushGlobal((u64)tableSize * sizeof(u32));
    for (u32 i = 0; i < tableSize; i++)
        table[i] = ~0u;

    for (s32 i = 0; i < numVertices; i++)
    {
        f32 fx = positions[i].x + 0.0f;
        f32 fy = positions[i].y + 0.0f;
        f32 fz = positions[i].z + 0.0f;
        u32 bx, by, bz;
        MemCopy(&bx, &fx, 4);
        MemCopy(&by, &fy, 4);
        MemCopy(&bz, &fz, 4);
        u32 h = bx * 73856093u ^ by * 19349663u ^ bz * 83492791u;
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;

        u32 slot = h & mask;
        for (;;)
        {
            u32 other = table[slot];
            if (other == ~0u)
            {
                table[slot] = (u32)i;
                remap[i] = (u32)i;
                break;
            }
            if (positions[other].x == positions[i].x && positions[other].y == positions[i].y && positions[other].z == positions[i].z)
            {
                remap[i] = other;
                break;
            }
            slot = (slot + 1u) & mask;
        }
    }
    ArenaPopGlobal((u64)tableSize * sizeof(u32));
}

static float3* TgGenerateNormals(const float3* positions, s32 numVertices, const u32* indices, u32 vertexBase, s32 numIndices)
{
    float3* normals = (float3*)ArenaPushGlobal((u64)(numVertices + 1) * sizeof(float3));
    MemSet(normals, 0, (u64)(numVertices + 1) * sizeof(float3));
    u32* remap = (u32*)ArenaPushGlobal((u64)numVertices * sizeof(u32));
    TgWeldPositions(positions, numVertices, remap);

    for (s32 i = 0; i + 2 < numIndices; i += 3)
    {
        u32 idx[3] = { indices[i] - vertexBase, indices[i + 1] - vertexBase, indices[i + 2] - vertexBase };
        if (idx[0] >= (u32)numVertices || idx[1] >= (u32)numVertices || idx[2] >= (u32)numVertices)
            continue;

        float3 p[3] = { positions[idx[0]], positions[idx[1]], positions[idx[2]] };
        float3 faceNormal = F3Cross(F3Sub(p[1], p[0]), F3Sub(p[2], p[0]));
        f32 faceLen = F3Len(faceNormal);
        if (faceLen < 1e-20f)
            continue;
        faceNormal = F3MulF(faceNormal, 1.0f / faceLen);

        for (s32 k = 0; k < 3; k++)
        {
            float3 a = F3Sub(p[(k + 1) % 3], p[k]);
            float3 b = F3Sub(p[(k + 2) % 3], p[k]);
            f32 weight = ATan2(F3Len(F3Cross(a, b)), F3Dot(a, b));
            float3* dst = &normals[remap[idx[k]]];
            dst->x += faceNormal.x * weight;
            dst->y += faceNormal.y * weight;
            dst->z += faceNormal.z * weight;
        }
    }

    for (s32 i = 0; i < numVertices; i++)
    {
        if (remap[i] != (u32)i)
            continue;
        float3 n = normals[i];
        f32 len = F3Len(n);
        normals[i] = len > 1e-6f ? F3MulF(n, 1.0f / len) : (float3){ 0.0f, 0.0f, 1.0f };
    }
    for (s32 i = 0; i < numVertices; i++)
        normals[i] = normals[remap[i]];

    ArenaPopGlobal((u64)numVertices * sizeof(u32));
    return normals;
}

static int TgMikkNumFaces(const SMikkTSpaceContext* ctx)
{
    return ((const TgMikkData*)ctx->m_pUserData)->numFaces;
}

static int TgMikkNumVerts(const SMikkTSpaceContext* ctx, const int face)
{
    (void)ctx; (void)face;
    return 3;
}

static u32 TgMikkIndex(const SMikkTSpaceContext* ctx, int face, int vert)
{
    const TgMikkData* d = (const TgMikkData*)ctx->m_pUserData;
    return d->indices[face * 3 + vert] - d->vertexBase;
}

static void TgMikkPosition(const SMikkTSpaceContext* ctx, float out[], const int face, const int vert)
{
    const TgMikkData* d = (const TgMikkData*)ctx->m_pUserData;
    float3 p = d->positions[TgMikkIndex(ctx, face, vert)];
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}

static void TgMikkNormal(const SMikkTSpaceContext* ctx, float out[], const int face, const int vert)
{
    const TgMikkData* d = (const TgMikkData*)ctx->m_pUserData;
    float3 n = d->normals[TgMikkIndex(ctx, face, vert)];
    out[0] = n.x;
    out[1] = n.y;
    out[2] = n.z;
}

static void TgMikkTexCoord(const SMikkTSpaceContext* ctx, float out[], const int face, const int vert)
{
    const TgMikkData* d = (const TgMikkData*)ctx->m_pUserData;
    float2 uv = d->texCoords[TgMikkIndex(ctx, face, vert)];
    out[0] = uv.x;
    out[1] = 1.0f - uv.y;
}

static void TgMikkSetTangent(const SMikkTSpaceContext* ctx, const float tangent[], const float sign, const int face, const int vert)
{
    const TgMikkData* d = (const TgMikkData*)ctx->m_pUserData;
    TangentW* o = &d->out[TgMikkIndex(ctx, face, vert)];
    o->x += tangent[0];
    o->y += tangent[1];
    o->z += tangent[2];
    o->w += sign;
}

static TangentW* TgGenerateTangents(const float3* positions, const float2* texCoords, const float3* normals,
                                    s32 numVertices, const u32* indices, u32 vertexBase, s32 numIndices)
{
    TangentW* out = (TangentW*)ArenaPushGlobal((u64)numVertices * sizeof(TangentW));
    MemSet(out, 0, (u64)numVertices * sizeof(TangentW));

    s32 ok = texCoords != NULL && numIndices >= 3;
    for (s32 i = 0; ok && i < numIndices - numIndices % 3; i++)
        ok = (indices[i] - vertexBase) < (u32)numVertices;

    if (!ok)
    {
        AX_WARN("Couldn't generate tangents of mesh its indices are greater than ints numvertices");
    }
    else
    {
        TgMikkData data;
        data.positions  = positions;
        data.texCoords  = texCoords;
        data.normals    = normals;
        data.indices    = indices;
        data.vertexBase = vertexBase;
        data.numFaces   = numIndices / 3;
        data.out        = out;

        SMikkTSpaceInterface iface;
        MemSet(&iface, 0, sizeof(iface));
        iface.m_getNumFaces          = TgMikkNumFaces;
        iface.m_getNumVerticesOfFace = TgMikkNumVerts;
        iface.m_getPosition          = TgMikkPosition;
        iface.m_getNormal            = TgMikkNormal;
        iface.m_getTexCoord          = TgMikkTexCoord;
        iface.m_setTSpaceBasic       = TgMikkSetTangent;

        SMikkTSpaceContext ctx;
        ctx.m_pInterface = &iface;
        ctx.m_pUserData  = &data;
        if (!genTangSpaceDefault(&ctx))
            MemSet(out, 0, (u64)numVertices * sizeof(TangentW));
    }

    for (s32 v = 0; v < numVertices; v++)
    {
        float3 n = normals[v];
        float3 t = { out[v].x, out[v].y, out[v].z };
        t = F3Sub(t, F3MulF(n, F3Dot(n, t)));
        f32 len = F3Len(t);
        t = len > 1e-6f ? F3MulF(t, 1.0f / len) : TgPerpendicular(n);
        out[v].x = t.x;
        out[v].y = t.y;
        out[v].z = t.z;
        out[v].w = out[v].w < 0.0f ? -1.0f : 1.0f;
    }
    return out;
}

#endif