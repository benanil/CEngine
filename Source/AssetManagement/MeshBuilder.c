#include "Include/AssetManager.h"
#include "Math/Quaternion.h"

static APrimitive* PrepareSingleMeshBundle(SceneBundle* result, u32 numVertex, s32 numIndex)
{
    MemSet(result, 0x0, sizeof(SceneBundle));
    Pow2Allocator* allocator = AllocTLSF(sizeof(Pow2Allocator));
    size_t allocatorSize = (sizeof(v128f) * numVertex * 4ull) + // 4 attributes position, texcoord, normal, tangent
                           (sizeof(u32) * numIndex);
    Pow2Alloc_Init(allocator, NextPowerOf2_64(allocatorSize));
    result->allocator = allocator;
    result->totalVertices = numVertex;
    result->scale = 1.0f;

    ANode* node = result->nodes = (ANode*)Pow2Alloc(allocator, sizeof(ANode));
    result->numNodes = 1;
    node->rotation[3] = 1.0f;
    node->parent = -1;
    VecStore(node->scale, VecOne());
    AScene* scene = result->scenes = (AScene*)Pow2Alloc(allocator, sizeof(AScene));
    result->numScenes = 1;
    scene->numNodes = 1;
    scene->nodes = (u32*)Pow2Alloc(allocator, sizeof(u32));

    result->numMeshes = 1;
    AMesh* mesh = result->meshes = (AMesh*)Pow2Alloc(allocator, sizeof(AMesh));
    result->totalPrimitives = mesh->numPrimitives = 1;
    APrimitive* primitive = mesh->primitives = (APrimitive*)Pow2Alloc(allocator, sizeof(APrimitive));
    primitive->mode = 4; // triangle
    primitive->indexType   = GraphicType_Unsignedi32;
    primitive->attributes  = AAttribType_POSITION | AAttribType_TEXCOORD_0 | AAttribType_NORMAL | AAttribType_TANGENT;
    primitive->numVertices = numVertex;
    return primitive;
}

MeshBuilder MeshBuilder_Create(s32 numVertex, s32 numIndex)
{
    MeshBuilder b;
    b.bundle    = (SceneBundle*)AllocTLSF(sizeof(SceneBundle));
    b.primitive = PrepareSingleMeshBundle(b.bundle, numVertex, numIndex);
    b.alloc     = (Pow2Allocator*)b.bundle->allocator;
    b.positions = (float3*)(b.primitive->Attributes[AAttribIdx_POSITION]   = Pow2AllocArray(b.alloc, float3, numVertex));
    b.texCoords = (float2*)(b.primitive->Attributes[AAttribIdx_TEXCOORD_0] = Pow2AllocArray(b.alloc, float2, numVertex));
    b.normals   = (float3*)(b.primitive->Attributes[AAttribIdx_NORMAL] = Pow2AllocArray(b.alloc, float3, numVertex));
    b.tangents  = (v128f*)(b.primitive->Attributes[AAttribIdx_TANGENT] = Pow2AllocArray(b.alloc, v128f, numVertex));
    b.primitive->numIndices = b.bundle->totalIndices = numIndex;
    b.primitive->indices = b.indices = Pow2AllocArray(b.alloc, u32, numIndex);
    return b;
}

SceneBundle* GenerateGrid(float segmentSize, u32 ver, u32 hor)
{
    const u32 numVertex = (ver + 1) * (hor + 1);
    const u32 numIndex  = ver * hor * 2 * 3;
    MeshBuilder b = MeshBuilder_Create(numVertex, numIndex);
    for (u32 x = 0u; x <= ver; x++)
    {
        f32 uvX = (f32)x / (f32)(ver);
        for (u32 z = 0u; z <= hor; z++)
        { 
            b.positions[x * hor + z] = (float3) { x * segmentSize, 0.0f, z * segmentSize };
            b.texCoords[x * hor + z] = (float2) { uvX, (f32)z / (f32)hor};
            b.normals  [x * hor + z] = (float3) { 0.0f, 1.0f, 0.0f };
            b.tangents [x * hor + z] = VecSetR(0.0f, 0.0f, 1.0f, 0.0f);
        }
    }

    for (unsigned row = 0, ii = 0; row < ver - 1; ++row)
    {
        for (unsigned col = 0; col < hor - 1; ++col)
        {
            b.indices[ii + 0] = row * hor + col + 1;
            b.indices[ii + 1] = row * hor + col;
            b.indices[ii + 2] = (row + 1) * hor + col;

            b.indices[ii + 3] = (row + 1) * hor + col;
            b.indices[ii + 4] = (row + 1) * hor + col + 1;
            b.indices[ii + 5] = row * hor + col + 1;
            ii += 6;
        }
    }
    for (u32 i = 0; i < numIndex; i++)
        ASSERT(b.indices[i] < numVertex);
    return b.bundle;
}

SceneBundle* GenerateCylinder(float radius, float height, u32 sliceCount)
{
    const u32 numVertex = sliceCount * 2 + 2; // + 2 is up and down centers. * 2 is for top and bottom circles
    const u32 numIndex  = sliceCount * 12; 
    MeshBuilder b = MeshBuilder_Create(numVertex, numIndex);
    // Bottom center
    b.positions[0] = F3Zero();
    b.texCoords[0] = (float2){ 0, 1.0f };
    b.normals  [0] = (float3){ 0.0f, -1.0f, 0.0f };
    b.tangents [0] = VecSetR(0.0f, 0.0f, 1.0f, 0.0f);
    // Top center
    b.positions[1] = (float3){ 0.0f, height, 0.0f };
    b.texCoords[1] = (float2){ 0, 0.0f };
    b.normals  [1] = (float3){ 0.0f, 1.0f, 0.0f };
    b.tangents [1] = VecSetR(0.0f, 0.0f, 1.0f, 0.0f);

    for (u32 i = 0; i < sliceCount; i++)
    {
        f32 t = (f32)(i + 1) / (f32)sliceCount;
        f32 s = Sin(t * MATH_PI * 2.0f);
        f32 c = Cos(t * MATH_PI * 2.0f);
        b.positions[i + 2] = (float3){ s * radius, 0.0f, c * radius };
        b.texCoords[i + 2] = (float2){ t, 1.0f };
        b.normals  [i + 2] = (float3){ s, 0.0f, c };
        float3 tangent   = F3Cross((float3){ s, 0.0f, c}, F3Up());
        b.tangents[i + 2]  = Vec3Load(&tangent.x);
        b.positions[sliceCount + i + 2] = (float3){ s * radius, height, c * radius };
        b.texCoords[sliceCount + i + 2] = (float2){ t, 0.0f };
        b.normals  [sliceCount + i + 2] = (float3){ s, 0.0f, c };
        b.tangents [sliceCount + i + 2] = Vec3Load(&tangent.x);
    }

    for (u32 i = 0; i < sliceCount; i++)
    {
        u32 next = (i + 1) % sliceCount;
        u32 currVertex = i + 2;
        u32 nextVertex = next + 2;
        u32 j = i * 12;
        // Side triangle 1
        b.indices[j + 0] = currVertex;
        b.indices[j + 1] = nextVertex;
        b.indices[j + 2] = currVertex + sliceCount;
        // Side triangle 2
        b.indices[j + 3] = nextVertex;
        b.indices[j + 4] = nextVertex + sliceCount;
        b.indices[j + 5] = currVertex + sliceCount;
        // Upper triangle
        b.indices[j + 6] = 1;
        b.indices[j + 7] = nextVertex + sliceCount;
        b.indices[j + 8] = currVertex + sliceCount;
        // Bottom triangle
        b.indices[j + 9] = currVertex;
        b.indices[j + 10] = nextVertex;
        b.indices[j + 11] = 0;
    }
    for (u32 i = 0; i < numIndex; i++)
        ASSERT(b.indices[i] < numVertex);
    return b.bundle;
}

SceneBundle* GenerateCube(float size)
{
    const u32 numVertex = 8, numIndex  = 36;
    const f32 h = size * 0.5f;
    MeshBuilder b = MeshBuilder_Create(numVertex, numIndex);
    b.positions[0] = (float3){-h, -h, -h};
    b.positions[1] = (float3){ h, -h, -h};
    b.positions[2] = (float3){ h,  h, -h};
    b.positions[3] = (float3){-h,  h, -h};
    b.positions[4] = (float3){-h, -h,  h};
    b.positions[5] = (float3){ h, -h,  h};
    b.positions[6] = (float3){ h,  h,  h};
    b.positions[7] = (float3){-h,  h,  h};

    for (u32 i = 0; i < 8; i++)
    {
        b.normals[i] = F3Norm(b.positions[i]);
        b.texCoords[i] = (float2){ 0, 0 };
        b.tangents[i] = VecSetR(0, 0, 1, 0);
    }

    const u32 indexData[36] =
    {
        0, 4, 5, 0, 5, 1, // bottom
        3, 2, 6, 3, 6, 7, // top
        0, 1, 2, 0, 2, 3, // back
        4, 7, 6, 4, 6, 5, // front
        0, 3, 7, 0, 7, 4, // left
        1, 5, 6, 1, 6, 2  // right
    };
    MemCopy(b.indices, indexData, sizeof(indexData));
    return b.bundle;
}

SceneBundle* GenerateCone(float height, float radius, u32 sliceCount)
{
    const u32 numVertex = 2 + sliceCount;
    const u32 numIndex = sliceCount * 3 * 2; // side + bottom
    MeshBuilder b = MeshBuilder_Create(numVertex, numIndex);
    // Bottom center
    b.positions[0] = F3Zero();
    b.texCoords[0] = (float2){ 0, 1.0f };
    b.normals  [0] = (float3){ 0.0f, -1.0f, 0.0f };
    b.tangents [0] = VecSetR(0.0f, 0.0f, 1.0f, 0.0f );
    // Top / apex
    b.positions[1] = (float3){ 0.0f, height, 0.0f };
    b.texCoords[1] = (float2){ 0, 0.0f };
    b.normals  [1] = (float3){ 0.0f, 1.0f, 0.0f };
    b.tangents [1] = VecSetR(0.0f, 0.0f, 1.0f, 0.0f );

    for (u32 i = 0; i < sliceCount; i++)
    {
        f32 t = (f32)(i + 1) / (f32)sliceCount;
        f32 s = Sin(t * MATH_PI * 2.0f);
        f32 c = Cos(t * MATH_PI * 2.0f);
        b.positions[i + 2] = (float3){ s * radius, 0.0f, c * radius };
        b.texCoords[i + 2] = (float2){ t, 1.0f };
        b.normals  [i + 2] = (float3){ s, 0.0f, c };
        float3 tangent   = F3Norm(F3Sub(b.positions[0], b.positions[i + 2]));
        b.tangents[i + 2]  = Vec3Load(&tangent.x);
    }

    for (u32 i = 0; i < sliceCount; i++)
    {
        u32 next = (i + 1) % sliceCount;
        u32 currVertex = i + 2;
        u32 nextVertex = next + 2;
        // Side triangle
        b.indices[i * 6 + 0] = currVertex;
        b.indices[i * 6 + 1] = 1; // apex
        b.indices[i * 6 + 2] = nextVertex;
        // Bottom triangle
        b.indices[i * 6 + 3] = currVertex;
        b.indices[i * 6 + 4] = 0; // bottom center
        b.indices[i * 6 + 5] = nextVertex;
    }
    for (u32 i = 0; i < numIndex; i++)
        ASSERT(b.indices[i] < numVertex);
    return b.bundle;
}

SceneBundle* GenerateCapsule(float radius, float height, u32 sliceCount)
{
    const u32 hemiCount = 8;
    const u32 ringCount = hemiCount * 2 + 2;
    const u32 numVertex = ringCount * sliceCount + 2;
    const u32 numIndex  = ringCount * sliceCount * 6;
    const f32 halfHeight = height * 0.5f;
    MeshBuilder b = MeshBuilder_Create(numVertex, numIndex);

    // Bottom pole
    b.positions[0] = (float3){ 0, -halfHeight - radius, 0 };
    b.normals  [0] = (float3){ 0, -1, 0 };
    b.texCoords[0] = (float2){ 0.5f, 0 };
    b.tangents [0] = VecSetR(1, 0, 0, 0);

    // Top pole
    b.positions[1] = (float3){ 0, halfHeight + radius, 0 };
    b.normals  [1] = (float3){ 0, 1, 0 };
    b.texCoords[1] = (float2){ 0.5f, 1 };
    b.tangents [1] = VecSetR(1, 0, 0, 0);

    for (u32 y = 0; y < ringCount; y++)
    {
        f32 angle, py;
        if (y <= hemiCount) {
            angle = -MATH_PI * 0.5f + (f32)y / (f32)hemiCount * MATH_PI * 0.5f;
            py = -halfHeight + Sin(angle) * radius;
        }
        else {
            u32 upperY = y - hemiCount - 1;
            angle = (f32)upperY / (f32)hemiCount * MATH_PI * 0.5f;
            py = halfHeight + Sin(angle) * radius;
        }

        f32 sy = Sin(angle);
        f32 cy = Cos(angle);

        for (u32 x = 0; x < sliceCount; x++)
        {
            f32 u = (f32)(x + 1) / (f32)sliceCount;
            f32 s = Sin(u * MATH_PI * 2.0f);
            f32 c = Cos(u * MATH_PI * 2.0f);
            u32 i = y * sliceCount + x + 2;
            b.positions[i] = (float3){ s * radius * cy, py, c * radius * cy };
            b.normals[i] = F3Norm((float3){ s * cy, sy, c * cy });
            b.texCoords[i] = (float2){ u, (f32)y / (f32)(ringCount - 1) };
            float3 tangent = (float3){ c, 0, -s };
            b.tangents[i] = Vec3Load(&tangent.x);
        }
    }

    u32 j = 0;
    // Bottom cap
    for (u32 x = 0; x < sliceCount; x++)
    {
        u32 next = (x + 1) % sliceCount;
        b.indices[j++] = 0;
        b.indices[j++] = x + 2;
        b.indices[j++] = next + 2;
    }

    // Rings
    for (u32 y = 0; y < ringCount - 1; y++)
    {
        for (u32 x = 0; x < sliceCount; x++)
        {
            u32 next = (x + 1) % sliceCount;
            u32 a = y * sliceCount + x + 2;
            u32 b0 = y * sliceCount + next + 2;
            u32 c = (y + 1) * sliceCount + x + 2;
            u32 d = (y + 1) * sliceCount + next + 2;
            b.indices[j++] = a;
            b.indices[j++] = b0;
            b.indices[j++] = c;
            b.indices[j++] = b0;
            b.indices[j++] = d;
            b.indices[j++] = c;
        }
    }
    // Top cap
    u32 top = (ringCount - 1) * sliceCount + 2;
    for (u32 x = 0; x < sliceCount; x++)
    {
        u32 next = (x + 1) % sliceCount;
        b.indices[j++] = top + next;
        b.indices[j++] = top + x;
        b.indices[j++] = 1;
    }
    for (u32 i = 0; i < numIndex; i++) ASSERT(b.indices[i] < numVertex);
    return b.bundle;
}

// https://github.com/vilbeyli/VQEngine/blob/master/Source/Engine/Scene/MeshGenerator.h
SceneBundle* GenerateSphere(float radius, u32 ringCount, u32 sliceCount)
{
    const float dPhi = MATH_PI / (float)(ringCount - 1);
    s32 steps = (s32)(Floorf32((MATH_PI + 0.00001f) / dPhi)) + 1;
    u32 ringVertexCount = sliceCount + 1;
    s32 numVertex = steps * ringVertexCount;
    s32 numIndex = (ringCount - 1) * sliceCount * 6;
    MeshBuilder b = MeshBuilder_Create(numVertex, numIndex);
    u32 vertIdx = 0;
    float dTheta = 2.0f * MATH_PI / (float)sliceCount;

    for (float phi = -MATH_HalfPI; phi <= MATH_HalfPI + 0.00001f; phi += dPhi)
    {
        float y = radius * Sin(phi);
        float r = radius * Cos(phi);
        
        for (u32 j = 0; j <= sliceCount; ++j, ++vertIdx)
        {
            float theta = j * dTheta;
            float x = r * Cos(theta);
            float z = r * Sin(theta);
            b.positions[vertIdx] = (float3){ x, y, z };
            // Cast j to float to avoid integer division evaluating to 0.0f
            b.texCoords[vertIdx] = (float2){ (float)j / (float)sliceCount, (y + radius) / (2.0f * radius) };
            b.tangents[vertIdx]  = VecSetR(-z, 0.0f, x, 0.0f);
            
            v128f N = VecSet(0, 1, 0, 1);
            N = QMulVec3V(N, QFromPitchYawRoll(0.0f, -MATH_PI - theta, MATH_HalfPI - phi));
            Vec3Store(&b.normals[vertIdx].x, N);
        }
    }
    
    for (u32 i = 0, k = 0; i < ringCount - 1; ++i)
    {
        for (u32 j = 0; j < sliceCount; ++j)
        {
            b.indices[k++] = i * ringVertexCount + j;
            b.indices[k++] = (i + 1) * ringVertexCount + j;
            b.indices[k++] = (i + 1) * ringVertexCount + j + 1;
            
            b.indices[k++] = i * ringVertexCount + j;
            b.indices[k++] = (i + 1) * ringVertexCount + j + 1;
            b.indices[k++] = i * ringVertexCount + j + 1;
        }
    }
    for (u32 i = 0; i < numIndex; i++)
        ASSERT(b.indices[i] < numVertex);
    return b.bundle;
}

static SceneBundle* unitCapsule  = NULL;
static SceneBundle* unitCube     = NULL;
static SceneBundle* unitGrid     = NULL;
static SceneBundle* unitCylinder = NULL;
static SceneBundle* unitCone     = NULL;
static SceneBundle* unitSphere   = NULL;

SceneBundle* GetUnitCapsule() {
    return unitCapsule ? unitCapsule : (unitCapsule = GenerateCapsule(1.0f, 1.8f, 16));
}

SceneBundle* GetUnitCube() {
    return unitCube ? unitCube : (unitCube = GenerateCube(1.0f));
}

SceneBundle* GetUnitGrid() {
    return unitGrid ? unitGrid : (unitGrid = GenerateGrid(1.0f, 10, 10));
}

SceneBundle* GetUnitCylinder() {
    return unitCylinder ? unitCylinder : (unitCylinder = GenerateCylinder(1.0f, 1.0f, 16));
}

SceneBundle* GetUnitCone() {
    return unitCone ? unitCone : (unitCone = GenerateCone(1.0f, 0.5f, 16));
}

SceneBundle* GetUnitSphere() {
    return unitSphere ? unitSphere : (unitSphere = GenerateSphere(1.0f, 16, 16));
}

MeshType IsBundlePrimitive(SceneBundle* bundle)
{
    if (bundle == unitCapsule)  return MeshType_Capsule;
    if (bundle == unitCube)     return MeshType_Cube;
    if (bundle == unitGrid)     return MeshType_Grid;
    if (bundle == unitCylinder) return MeshType_Cylinder;
    if (bundle == unitCone)     return MeshType_Cone;
    if (bundle == unitSphere)   return MeshType_Sphere;
    return MeshType_Default;
}

SceneBundle* GetUnitPrimitive(MeshType type)
{
    switch (type)
    {
        case MeshType_Capsule:  return GetUnitCapsule();
        case MeshType_Cube:     return GetUnitCube();
        case MeshType_Grid:     return GetUnitGrid();
        case MeshType_Cylinder: return GetUnitCylinder();
        case MeshType_Cone:     return GetUnitCone();
        case MeshType_Sphere:   return GetUnitSphere();
    }
    return NULL;
}

const char* GetPrimitiveName(MeshType type)
{
    switch (type)
    {
        case MeshType_Default:  return "Default";
        case MeshType_Runtime:  return "Runtime";
        case MeshType_Sphere:   return "Sphere";
        case MeshType_Cylinder: return "Cylinder";
        case MeshType_Cone:     return "Cone";
        case MeshType_Grid:     return "Grid";
        case MeshType_Cube:     return "Cube";
        case MeshType_Capsule:  return "Capsule";
    }
    return NULL;
}
