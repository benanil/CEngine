#include "Bitpack.hlsl"

enum EntityFlags_
{
    EntityFlags_None            = 0,
    EntityFlags_ColliderEnabled = 1 << 0,
    EntityFlags_Transparent     = 1 << 1,
    EntityFlags_NoMesh          = 1 << 2,
    EntityFlags_Hidden          = 1 << 3
};
typedef u32 EntityFlags;

#define ENTITY_MAX_SCALE 10.0f

typedef struct Entity_
{
    float4 position;
    uint2  rotation;
    uint2  scale;
} Entity;

typedef u32 EntityID;

u32 EntityGetSparseID(in Entity e) {
    return asuint(e.position.w) & 0x00FFFFFFu;
}

u32 EntityGetPrimitiveID(in Entity e) {
    return e.scale.y >> 16;
}

u32 EntityGetFlags(in Entity e) {
    return asuint(e.position.w) >> 24;
}

void EntitySetSparseID(inout Entity e, u32 id) {

    u32 genFlag = asuint(e.position.w);
    e.position.w = asfloat(id | (genFlag & ~0xFFFFFFu));
}

void EntitySetPrimitiveID(inout Entity e, u32 id) {
    e.scale.y = (e.scale.y & 0xFFFFu) | (id << 16u);
}

void EntitySetFlags(inout Entity e, EntityFlags flags) {
    u32 w = asuint(e.position.w);
    w = (w & 0x00FFFFFFu) | (((u32)flags & 0xFFu) << 24u);
    e.position.w = asfloat(w);
}

void EntityAddFlags(inout Entity e, EntityFlags flags) {
    u32 w = asuint(e.position.w);
    w |= ((u32)flags & 0xFFu) << 24u;
    e.position.w = asfloat(w);
}

uint2 EntityPackWorldScale(v128f scale) {
    return Pack16x4Fixed(scale, ENTITY_MAX_SCALE) & uint2(0xFFFFFFFFu, 0x0000FFFFu);
}

uint2 EntityPackUniformWorldScale(f32 scale) {
    return PackUnorm16x4(VecSet1(Saturatef32(scale * 0.1f))) & uint2(0xFFFFFFFFu, 0x0000FFFFu);
}

f16_4 EntityGetRotation(in Entity entity) {
    return UnpackRGBA16Snorm(entity.rotation.x, entity.rotation.y);
}

void EntitySetRotation(inout Entity e, float4 rotation) {
    e.rotation = PackSnorm16x4(rotation);
}

v128f EntityGetScaleV(in Entity e) {
    return Unpack16x4Fixed(e.scale, ENTITY_MAX_SCALE);
}

f16_3 EntityGetScale(in Entity e) {
    return Unpack16x4Fixed(e.scale, ENTITY_MAX_SCALE).xyz;
}

void EntitySetScale(inout Entity e, float3 scale) {
    uint2 sc = EntityPackWorldScale(scale.xyzz);
    e.scale.y &= 0xFFFFu << 16u;
    e.scale.y |= sc.y & 0xFFFFu;
    e.scale.x = sc.x;
}

v128f EntityGetPosV(in Entity e) {
    return e.position;
}

float3 EntityGetPos(in Entity e) {
    return e.position.xyz;
}

void EntitySetPositionV(inout Entity e, v128f v) {
    e.position.xyz = v.xyz;
}

void EntitySetPosition(inout Entity e, float3 pos) {
    e.position.xyz = pos;
}