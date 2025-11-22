
#include "Math/SOAMath.h"


#define MAX_ENTITY 4096

// typedef struct Skin_ {
// 	// indexes to bone buffer
// 	int boneStart;
// 	int boneCount;
// } Skin;
// 
// typedef struct Mesh_ {
// 	int vertexStart;
// 	int numVertex;
// 	int indexStart;
// 	int numIndex;
// } Mesh;
// 
// typedef struct Texture_ {
// 	int width, height;
// 	int format;
// 	int numMips;
// } Texture;
// 
// typedef struct Material_ {
// 	int albedoIndex;
// 	int normalIndex;
// 	int roughnessIndex;
// 	// other values
// } Material;

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


purefn void PackScale(const Vector4x32f scaleXYZ[3], uint32_t* entityScales)
{
    // Scale & clamp
    Vector4x32f shortMin = VecZero();        // for unsigned packing
    Vector4x32f xScaled = VecClamp(VecMulf(scaleXYZ[0], 128.0f), shortMin, VecSet1(0x3FF));
    Vector4x32f yScaled = VecClamp(VecMulf(scaleXYZ[1], 256.0f), shortMin, VecSet1(0x7FF));
    Vector4x32f zScaled = VecClamp(VecMulf(scaleXYZ[2], 256.0f), shortMin, VecSet1(0x7FF));

    // Convert to integer
    Vector4x32i x = VecCvtF32U32(xScaled);
    Vector4x32i y = VecCvtF32U32(yScaled);
    Vector4x32i z = VecCvtF32U32(zScaled);

    // Pack bits into 32-bit integer: x[9:0] | y[10:0]<<10 | z[10:0]<<21
    Vector4x32i res = VeciOr(x, VeciOr(VeciSll32(y, 10), VeciSll32(z, 21)));

    VecStoreI((Vector4x32i*)entityScales, res);
}


purefn void PackRotation(const Vector4x32i resultXYZW[4],
                         uint16_t* xPtr,
                         uint16_t* yPtr,
                         uint16_t* zPtr,    
                         uint16_t* wPtr)
{
    Vector4x32f shortMax = VecSet1(0x7FFF);

    // Scale normalized floats back to 16-bit integer range
    Vector4x32i x = VecCvtF32U32(VecMul(resultXYZW[0], shortMax));
    Vector4x32i y = VecCvtF32U32(VecMul(resultXYZW[1], shortMax));
    Vector4x32i z = VecCvtF32U32(VecMul(resultXYZW[2], shortMax));
    Vector4x32i w = VecCvtF32U32(VecMul(resultXYZW[3], shortMax));

    // Narrow 32-bit integers to 16-bit
    VecStoreI16(xPtr, VecNarrowU32U16(x));
    VecStoreI16(yPtr, VecNarrowU32U16(y));
    VecStoreI16(zPtr, VecNarrowU32U16(z));
    VecStoreI16(wPtr, VecNarrowU32U16(w));
}

purefn void UnpackRotation(uint16_t const* xPtr,
                           uint16_t const* yPtr,
                           uint16_t const* zPtr,    
                           uint16_t const* wPtr,
                           Vector4x32i resultXYZW[4])
{
    Vector4x32i loadedX = VecWidenU16ToU32(VecLoadI((Vector4x32i*)xPtr));
    Vector4x32i loadedY = VecWidenU16ToU32(VecLoadI((Vector4x32i*)yPtr));
    Vector4x32i loadedZ = VecWidenU16ToU32(VecLoadI((Vector4x32i*)zPtr));
    Vector4x32i loadedW = VecWidenU16ToU32(VecLoadI((Vector4x32i*)wPtr));
    
    Vector4x32i shortMax = VecFromInt1(0x7FFF);

    // normalize and store
    resultXYZ[0] = VecDiv(VecCvtU32F32(loadedX), shortMax); 
    resultXYZ[1] = VecDiv(VecCvtU32F32(loadedY), shortMax);
    resultXYZ[2] = VecDiv(VecCvtU32F32(loadedZ), shortMax);
    resultXYZ[3] = VecDiv(VecCvtU32F32(loadedW), shortMax);
}
