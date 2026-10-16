#ifndef ENTITY_H
#define ENTITY_H

#include "Math/Bitpack.h"
#include "Math/Quaternion.h"

#define INVALID_ENTITY  (0x00FFFFFFu)
#define ENTITY_MAX_SCALE 10.0f

enum EntityFlags_
{
    EntityFlags_None            = 0,
    EntityFlags_ColliderEnabled = 1 << 0,
    EntityFlags_Transparent     = 1 << 1,
    EntityFlags_NoMesh          = 1 << 2,
    EntityFlags_Hidden          = 1 << 3
};
typedef u8 EntityFlags;
// sparseId | (generation << 24)
typedef u32 EntityID;

typedef struct Entity_
{
    // todo(anil) generation has to be in sparseID array
    v128f position; // w 24bit sparse, 8bit flags
    u64   rotation; 
    u64   scale;    // xyz16 last 16 primitive idx
} Entity;

purefn u32 EntityGetSparseID(const Entity* e) {
    return VeciGetW(VecBitcastU32(e->position)) & 0x00FFFFFFu;
}

purefn u32 EntityGetPrimitiveID(const Entity* e) {
    return (u32)(e->scale >> 48ull);
}

purefn u32 EntityGetFlags(const Entity* e) {
    return VeciGetW(VecBitcastU32(e->position)) >> 24ull;
}

purefn u32 EntityGetGen(const Entity* e) {
    return (VeciGetW(VecBitcastU32(e->position)) >> 24ull) & 0xFFu;
}

purefn EntityID MakeEntityID(u32 sparse, u32 gen, bool skinned) {
    return (sparse & 0xFFFFFFu) | ((gen & 0x7Fu) << 24u) | ((u32)skinned << 31u);
}

static inline void EntitySetSparseID(Entity* e, u32 id) {
    ASSERT(id <= 0xFFFFFFu);
    u32 genFlag = VeciGetW(VecBitcastU32(e->position));
    id |= genFlag & 0xFF000000u;
    VecSetW(e->position, BitCast(f32, id));
}

static inline void EntitySetPrimitiveID(Entity* e, u32 id) {
    ASSERT(id <= 0xFFFFu);
    e->scale = (e->scale & 0x0000FFFFFFFFFFFFull) | ((u64)id << 48ull);
}

static inline void EntitySetFlags(Entity* e, EntityFlags flags) {
    ASSERT(flags <= 255);
    u32 w = VeciGetW(VecBitcastU32(e->position));
    w = (w & 0x00FFFFFFu) | (((u32)flags & 0xFFu) << 24u);
    VecSetW(e->position, BitCast(f32, w));
}

static inline void EntityAddFlags(Entity* e, EntityFlags flags) {
    ASSERT(flags <= 255);
    u32 w = VeciGetW(VecBitcastU32(e->position));
    w |= ((u32)flags & 0xFFu) << 24u;
    VecSetW(e->position, BitCast(f32, w));
}

purefn u64 EntityPackWorldScale(v128f scale) {
    return Pack16x4Fixed(scale, ENTITY_MAX_SCALE) & 0x0000FFFFFFFFFFFFull;
}

purefn u64 EntityPackUniformWorldScale(f32 scale) {
    return PackUnorm16x4(VecSet1(Saturatef32(scale * 0.1f))) & 0x0000FFFFFFFFFFFFull;
}

purefn Quaternion EntityGetRotation(const Entity* e) {
    return UnpackQuaternionS16Norm1(e->rotation);
}

static inline void EntitySetRotation(Entity* e, Quaternion rotation) {
    e->rotation = PackQuaternionS16NormRet(rotation);
}

static inline void EntityAddRotation(Entity* e, Quaternion rotation) {
    e->rotation = PackQuaternionS16NormRet(QMul(EntityGetRotation(e), rotation));
}

purefn v128f EntityGetScaleV(const Entity* e) {
    return Unpack16x4Fixed(e->scale, ENTITY_MAX_SCALE);
}

purefn float3 EntityGetScale(const Entity* e) {
    return Vec3Get(EntityGetScaleV(e));
}

static inline void EntitySetScaleV(Entity* e, v128f scale) {
    e->scale = (e->scale & 0xFFFF000000000000ull) | EntityPackWorldScale(scale);
}

static inline void EntitySetScale(Entity* e, float3 scale) {
    EntitySetScaleV(e, Vec3Load(&scale.x));
}

static inline void EntityMulScaleV(Entity* e, v128f scale)  {
    EntitySetScaleV(e, VecMul(EntityGetScaleV(e), scale));
}

static inline void EntityAddScaleV(Entity* e, v128f scale)  {
    EntitySetScaleV(e, VecAdd(EntityGetScaleV(e), scale));
}

purefn v128f EntityGetPosV(const Entity* e) {
    return e->position;
}

purefn float3 EntityGetPos(const Entity* e) {
    return Vec3Get(e->position);
}

static inline void EntitySetPositionV(Entity* e, v128f v) {
    VecSetW(v, VecGetW(e->position));
    e->position = v;
}

static inline void EntityAddPositionV(Entity* e, v128f v) {
    v = VecAdd(v, e->position);
    VecSetW(v, VecGetW(e->position));
    e->position = v;
}

static inline void EntitySetPosition(Entity* e, float3 pos) {
    EntitySetPositionV(e, Vec3Load(&pos.x));
}

#endif // ENTITY_H