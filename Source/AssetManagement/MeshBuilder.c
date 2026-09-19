#include "Include/AssetManager.h"
#include "Math/Quaternion.h"

static APrimitive* PrepareSingleMeshBundle(SceneBundle* result, u32 numVertex)
{
    MemSet(result, 0x0, sizeof(SceneBundle));
    Pow2Allocator* allocator = AllocTLSF(sizeof(Pow2Allocator));
    Pow2Alloc_Init(allocator, 1024);
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
    b.primitive = PrepareSingleMeshBundle(b.bundle, numVertex);
    b.alloc     = (Pow2Allocator*)b.bundle->allocator;
    b.positions = (float3*)(b.primitive->vertexAttribs[AAttribIdx_POSITION]   = Pow2AllocArray(b.alloc, float3, numVertex));
    b.texCoords = (float2*)(b.primitive->vertexAttribs[AAttribIdx_TEXCOORD_0] = Pow2AllocArray(b.alloc, float2, numVertex));
    b.normals   = (float3*)(b.primitive->vertexAttribs[AAttribIdx_NORMAL] = Pow2AllocArray(b.alloc, float3, numVertex));
    b.tangents  = (v128f*)(b.primitive->vertexAttribs[AAttribIdx_TANGENT] = Pow2AllocArray(b.alloc, v128f, numVertex));
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
    return b.bundle;
}

SceneBundle* GenerateCylinder(float radius, float height, u32 sliceCount)
{
    const u32 numVertex = sliceCount * 2 + 2; // + 2 is up and down centers. * 2 is for top and bottom circles
    const u32 numIndex  = sliceCount * 9; 
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
        // Side triangle
        b.indices[i * 9 + 0] = currVertex;
        b.indices[i * 9 + 1] = nextVertex;
        b.indices[i * 9 + 2] = currVertex + sliceCount;
        // upper triangle
        b.indices[i * 9 + 3] = nextVertex;
        b.indices[i * 9 + 4] = nextVertex + sliceCount;
        b.indices[i * 9 + 5] = currVertex + sliceCount;
        // Bottom triangle
        b.indices[i * 9 + 6] = currVertex;
        b.indices[i * 9 + 7] = nextVertex;
        b.indices[i * 9 + 8] = 0;
    }
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
    return b.bundle;
}

SceneBundle* GenerateCapsule(float radius, float height, u32 sliceCount)
{
    const u32 hemiCount = 8;
    const u32 ringCount = hemiCount * 2 + 1;
    const u32 numVertex = ringCount * sliceCount + 2;
    const u32 numIndex  = (ringCount - 1) * sliceCount * 6;
    const f32 halfHeight = height * 0.5f;
    MeshBuilder b = MeshBuilder_Create(numVertex, numIndex);
    b.positions[0] = (float3){ 0, -halfHeight - radius, 0 };
    b.normals  [0] = (float3){ 0, -1, 0 };
    b.texCoords[0] = (float2){ 0.5f, 0 };
    b.tangents [0] = VecSetR(1, 0, 0, 0);

    b.positions[1] = (float3){ 0, halfHeight + radius, 0 };
    b.normals  [1] = (float3){ 0, 1, 0 };
    b.texCoords[1] = (float2){ 0.5f, 1 };
    b.tangents [1] = VecSetR(1, 0, 0, 0);

    for (u32 y = 0; y < ringCount; y++)
    {
        f32 v = (f32)(y + 1) / (f32)(ringCount + 1);
        f32 angle = (v - 0.5f) * MATH_PI;
        f32 sy = Sin(angle);
        f32 cy = Cos(angle);
        f32 py = sy < 0.0f ? -halfHeight + sy * radius :  halfHeight + sy * radius;

        for (u32 x = 0; x < sliceCount; x++)
        {
            f32 u = (f32)x / (f32)sliceCount;
            f32 s = Sin(u * MATH_PI * 2.0f);
            f32 c = Cos(u * MATH_PI * 2.0f);
            u32 i = y * sliceCount + x + 2;
            b.positions[i] = (float3){ s * radius * cy, py, c * radius * cy };
            b.normals[i] = F3Norm((float3){ s * cy, sy, c * cy });
            b.texCoords[i] = (float2){ u, v };
            float3 tangent = (float3){ c, 0, -s };
            b.tangents[i] = Vec3Load(&tangent.x);
        }
    }
    for (u32 y = 0; y < ringCount - 1; y++)
    {
        for (u32 x = 0; x < sliceCount; x++)
        {
            u32 next = (x + 1) % sliceCount;
            u32 a = y * sliceCount + x + 2;
            u32 k = y * sliceCount + next + 2;
            u32 c = a + sliceCount;
            u32 d = k + sliceCount;
            u32 i = (y * sliceCount + x) * 6;
            b.indices[i + 0] = a;
            b.indices[i + 1] = k;
            b.indices[i + 2] = c;
            b.indices[i + 3] = k;
            b.indices[i + 4] = d;
            b.indices[i + 5] = c;
        }
    }
    return b.bundle;
}

// https://github.com/vilbeyli/VQEngine/blob/master/Source/Engine/Scene/MeshGenerator.h
SceneBundle* GenerateSphere(float radius, u32 ringCount, u32 sliceCount)
{
    const float dPhi = MATH_PI / (float)(ringCount - 1);
    s32 steps = (s32)(Floorf32((MATH_PI + 0.00001f) / dPhi)) + 1;
    u32 ringVertexCount = sliceCount + 1;
    s32 numVertex = steps * ringVertexCount;
    MeshBuilder b = MeshBuilder_Create(numVertex, (ringCount - 1) * sliceCount * 6);
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
    return b.bundle;
}

SceneBundle* GetUnitCapsule() {
    static SceneBundle r = {}; return r.numMeshes != 0 ? &r : GenerateCapsule(1.0f, 1.0f, 16);
}

SceneBundle* GetUnitCube() {
    static SceneBundle r = {}; return r.numMeshes != 0 ? &r : GenerateCube(1.0f);
}

SceneBundle* GetUnitGrid() {
    static SceneBundle r = {}; return r.numMeshes != 0 ? &r :  GenerateGrid(1.0f, 10, 10);
}

SceneBundle* GetUnitCylinder() {
    static SceneBundle r = {}; return r.numMeshes != 0 ? &r : GenerateCylinder(1.0f, 1.0f, 16);
}

SceneBundle* GetUnitCone() {
    static SceneBundle r = {}; return r.numMeshes != 0 ? &r : GenerateCone(1.0f, 0.5f, 16);
}

SceneBundle* GetUnitSphere() {
    static SceneBundle r = {}; return r.numMeshes != 0 ? &r :  GenerateSphere(1.0f, 16, 16);
}
