
#include "SOAMath.h"


#define MAX_ENTITY 4096

typedef struct Skin_ {
	// indexes to bone buffer
	int boneStart;
	int boneCount;
} Skin;

typedef struct Mesh_ {
	int vertexStart;
	int numVertex;
	int indexStart;
	int numIndex;
} Mesh;

typedef struct Texture_ {
	int width, height;
	int format;
	int numMips;
} Texture;

typedef struct Material_ {
	int albedoIndex;
	int normalIndex;
	int roughnessIndex;
	// other values
} Material;

typedef struct Entity_ {
	int type;
	int meshIdx;
} Entity;



float* EntityPosX;
float* EntityPosY;
float* EntityPosZ;

uint16_t* EntityRotationX;
uint16_t* EntityRotationY;
uint16_t* EntityRotationZ;
uint16_t* EntityRotationW;

uint32_t* EntityScale;



purefn void UnpackScale(uint32_t const* entityScales, Vector4x32i resultXYZ[3])
{
    Vector4x32i loaded = VecLoadI((Vector4x32i*)entityScales);
    
    Vector4x32i tenMask    = VeciSet1(0x3FF); // 10bit 1
    Vector4x32i elevenMask = VeciSet1(0x7FF); // 11bit 1

    Vector4x32i x = VeciAnd(          loaded,      tenMask); 
    Vector4x32i y = VeciAnd(VeciSrl32(loaded, 10), elevenMask);
    Vector4x32i z = VeciAnd(VeciSrl32(loaded, 21), elevenMask);
 
    resultXYZ[0] = VecDivf(VecCvtU32F32(x), 128.0f);
    resultXYZ[1] = VecDivf(VecCvtU32F32(y), 256.0f);
    resultXYZ[2] = VecDivf(VecCvtU32F32(z), 256.0f);
}

purefn void UnpackRotation(uint16_t const* xPtr,
                           uint16_t const* yPtr,
                           uint16_t const* zPtr,    
                           uint16_t const* wPtr,
                           Vector4x32i resultXYZW[4])
{
    Vector4x32i loadedX = VeciCvtU32U16(VecLoadI((Vector4x32i*)xPtr));
    Vector4x32i loadedY = VeciCvtU32U16(VecLoadI((Vector4x32i*)yPtr));
    Vector4x32i loadedZ = VeciCvtU32U16(VecLoadI((Vector4x32i*)zPtr));
    Vector4x32i loadedW = VeciCvtU32U16(VecLoadI((Vector4x32i*)wPtr));
    
    Vector4x32i shortMax = VecFromInt1(0x7FFF);

    // normalize and store
    resultXYZ[0] = VecDiv(VecCvtU32F32(loadedX), shortMax); 
    resultXYZ[1] = VecDiv(VecCvtU32F32(loadedY), shortMax);
    resultXYZ[2] = VecDiv(VecCvtU32F32(loadedZ), shortMax);
    resultXYZ[3] = VecDiv(VecCvtU32F32(loadedW), shortMax);
}