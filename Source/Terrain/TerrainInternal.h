#ifndef TERRAIN_INTERNAL_H
#define TERRAIN_INTERNAL_H

#include "Include/Common.h"

// chunk grid: 16^3 cells, 17^3 cell corners, one extra sample on every side for
// central difference gradients -> 19^3 samples. sample (x,y,z) maps to corner (x-1,y-1,z-1)
#define TERRAIN_CHUNK_CELLS    16
#define TERRAIN_CHUNK_CORNERS  17
#define TERRAIN_SAMPLES_AXIS   19
#define TERRAIN_SAMPLES_TOTAL  (19 * 19 * 19)

#define TERRAIN_VOXEL_SIZE     1.0f   // meters per cell at lod 0
#define TERRAIN_LOD_COUNT      4      // chunk world sizes 16/32/64/128 m

// densities are clamped signed distance in meters, negative inside the solid, scaled so
// that +-TERRAIN_SDF_CLAMP maps to +-127. the scale is world fixed (NOT per lod): the
// transvoxel transition cells are only crack free when a coarse sample equals the fine
// sample at the same world position, which per lod scaling would break
#define TERRAIN_SDF_CLAMP      4.0f

// transition faces shrink the boundary layer of regular cells inward by this fraction
// of a cell, the gap is filled by transition cells (Lengyel 4.4)
#define TERRAIN_TRANSITION_SHRINK 0.5f

// positions are fixed point with TERRAIN_POS_PER_CELL steps per cell. the mesher
// computes boundary vertices with pure integer math (corner * 32768 + t256 * 128 * edge),
// so a vertex shared by two chunks (same or neighboring lod) packs bit-identically on
// both sides and lod seams are exact, not just close
#define TERRAIN_POS_PER_CELL 32768
#define TERRAIN_POS_MAX      (TERRAIN_CHUNK_CELLS * TERRAIN_POS_PER_CELL) // 524288, needs 21 bits

// compact vertex, 16 bytes: 3 x 21 bit fixed point position in chunk bounds, the chunk
// origin and size come from a per draw uniform. normal is 16+16 bit octahedral
typedef struct TerrainVertex_
{
    u32 posA;      // x:21 | y low 11
    u32 posB;      // y high 10 | z:21 | 1 spare
    u32 octNormal; // octahedral x:16 | y:16 unorm
    u32 spare;     // future material weights / ao
} TerrainVertex;

typedef struct TerrainMeshOut_
{
    TerrainVertex* vertices;   // SDL_malloc, grown by the mesher
    u32*           indices;    // chunk local, rebased by the draw's vertex_offset
    u32 numVertices, vertexCapacity;
    u32 numIndices, indexCapacity;
    f32 aabbMin[3], aabbMax[3]; // chunk local meters, tight bounds of emitted vertices
} TerrainMeshOut;

// per worker scratch for the vertex reuse maps, allocated once per job slot
typedef struct TerrainMeshScratch_
{
    u32* edgeVertex;   // [3][17][17][17] vertex index per axis aligned edge
    u32* cornerVertex; // [17][17][17] vertex index for crossings exactly on a corner
    u32* faceHash;     // open addressing table for transition cell vertices
} TerrainMeshScratch;

void Transvoxel_ScratchInit(TerrainMeshScratch* scratch);   // SDL_malloc, thread safe
void Transvoxel_ScratchDestroy(TerrainMeshScratch* scratch);
void Transvoxel_MeshOutDestroy(TerrainMeshOut* out);

// meshes one chunk from a 19^3 s8 density grid. transitionMask bit per face
// (-x,+x,-y,+y,-z,+z) marks a coarser neighbor on that face. thread safe, allocates the
// output with SDL_malloc. out: 1 on success, 0 when out of memory
s32 Transvoxel_MeshChunk(const s8* density, u32 lod, u8 transitionMask,
                         TerrainMeshOut* out, TerrainMeshScratch* scratch);

// logs mesher validation results against an analytic sphere, called once from Terrain_Init
void Transvoxel_SelfTest(void);

// procedural density field, pure and thread safe (TerrainDensity.c)
f32  TerrainDensity_SDF(f32 x, f32 y, f32 z);
void TerrainDensity_SampleChunk(s32 cx, s32 cy, s32 cz, u32 lod, s8* out /*19^3*/);
// world vertical band that can contain surface, chunks outside it are never created
void TerrainDensity_GetYRange(f32* outMin, f32* outMax);

#endif // TERRAIN_INTERNAL_H
