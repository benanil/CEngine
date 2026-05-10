
#define SJ_IMPL
#include <Extern/sj.h>

#include "Include/GLTFParser.h"
#include "Include/FileSystem.h"
#include "Include/Algorithm.h"
#include "Include/Memory.h"
#include "Include/Platform.h"

#include "Math/Matrix.h"
#include "Math/Color.h"

typedef struct GLTFAccessor_
{
    int bufferView;
    int componentType; // GraphicType
    int count;
    int byteOffset;
    int type; // 1 = SCALAR, 2 = VEC2, 3 = VEC3, 4 = VEC4, mat4
} GLTFAccessor;

typedef struct GLTFBufferView_
{
    int buffer;
    int byteOffset;
    int byteLength;
    int target;
    int byteStride;
} GLTFBufferView;

typedef struct GLTFParseContext_
{
    FixedPow2Allocator* allocator;
    const char* path;
    sj_Reader* sj;
    float scale;
} GLTFParseContext;

static void DecodeBase64(char *dst, const char *src, size_t src_length)
{
    uint8_t table[256] = { 0 };
    for (char c = 'A'; c <= 'Z'; c++) table[c] = (uint8_t)(c - 'A');
    for (char c = 'a'; c <= 'z'; c++) table[c] = (uint8_t)(26 + (c - 'a'));
    for (char c = '0'; c <= '9'; c++) table[c] = (uint8_t)(52 + (c - '0'));
    table['+'] = 62;
    table['/'] = 63;
    
    for (uint64_t i = 0; i + 4 <= src_length; i += 4) {
        uint32_t a = table[src[i + 0]];
        uint32_t b = table[src[i + 1]];
        uint32_t c = table[src[i + 2]];
        uint32_t d = table[src[i + 3]];
           
        dst[0] = (char)(a << 2 | b >> 4);
        dst[1] = (char)(b << 4 | c >> 2);
        dst[2] = (char)(c << 6 | d);
        dst += 3;
    }
}

inline char OGLWrapToWrap(int wrap)
{
    switch (wrap)
    {
        case 0x2901: return 0; // GL_REPEAT          10497
        case 0x812F: return 1; // GL_CLAMP_TO_EDGE   33071
        case 0x812D: return 2; // GL_CLAMP_TO_BORDER 33069
        case 0x8370: return 3; // GL_MIRRORED_REPEAT 33648
        default: ASSERT(0 && "wrong or undefined sampler type!"); return 0;
    }
}

const char* ParseFloat16(const char* curr, short* flt)
{
    float fp32;
    curr = ParseFloat(curr, &fp32);
    *flt = (short)(fp32 * 400.0f);
    return curr;
}

inline const char* SkipAfter(const char* curr, char character)
{
    AX_NO_UNROLL while (*curr++ != character);
    return curr;
}

static const char* GetStringInQuotes(char* str, const char* curr)
{
    ++curr; // skip quote " 
    AX_NO_UNROLL while (*curr != '"') *str++ = *curr++;
    *str = '\0'; // null terminate
    return ++curr;
}

static char* CopySJString(sj_Value key, FixedPow2Allocator* allocator)
{
    size_t len = (size_t)(key.end - key.start);
    char* res = (char*)FixedPow2Allocator_Allocate(allocator, len + 1);
    MemCopy(res, key.start, len);
    return res;
}

static int sjCountArray(sj_Reader sj, sj_Value array)
{
    int balance = 1, numElements = 0;
    sj.cur = array.start + 1;

    if (sj.cur >= sj.end)
        return 0;

    while (balance >= 1 && sj.cur < sj.end)
    {
        char c = *sj.cur;
        if (balance == 1 && c == ',')
            numElements++;
        balance += c == '{' || c == '[';
        balance -= c == '}' || c == ']';
        sj.cur++;
    }
    
    return numElements + 1;
}

static void ParseIntArray(const char* curr, const char* end, int* numbers)
{
    int index = 0;
    while (*curr != ']' && curr < end)
    {
        if (IsNumber(*curr)) {
            curr = ParsePositiveNumber(curr, &numbers[index++]);
        }
        else  {
            curr++;
        }
    } 
}

static int* ParseIntArrayAlloc(GLTFParseContext* ctx, sj_Value sjArr, int* outCount)
{
    int numElements = sjCountArray(*ctx->sj, sjArr);
    int* res = (int*)FixedPow2Allocator_AllocateUninitialized(ctx->allocator, numElements * sizeof(int)); 
    ParseIntArray(sjArr.start + 1, ctx->sj->end, res);
    *outCount = numElements;
    return res;
}

typedef void (*ParseArrayObjFn)(sj_Value sjAccessorObj, void* element, GLTFParseContext* ctx);

static void* ParseArray(sj_Value sjArray, size_t stride, int* numElementsOut, 
                        GLTFParseContext* ctx, ParseArrayObjFn objFn)
{
    int numElements = sjCountArray(*ctx->sj, sjArray);
    char* arrayOfElements = (char*)FixedPow2Allocator_Allocate(ctx->allocator, stride * numElements);
    sj_Value val;
    int index = 0;
    while (sj_iter_array(ctx->sj, sjArray, &val))
    {
        objFn(val, arrayOfElements + (index * stride), ctx);
        index++;
    }
    if (numElementsOut) *numElementsOut = index;
    return arrayOfElements;
}

static void ParseAccessorObj(sj_Value sjAccessorObj, void* element, GLTFParseContext* ctx)
{
    GLTFAccessor* accessor = (GLTFAccessor*)element;
    sj_Value key, val;

    while (sj_iter_object(ctx->sj, sjAccessorObj, &key, &val))
    {
        char beforeChar = *key.end; *key.end = 0; char* name = key.start;
        
        if      (StrCMP16(key.start, "bufferView"))    ParsePositiveNumber(val.start, &accessor->bufferView);
        else if (StrCMP16(key.start, "byteOffset"))    ParsePositiveNumber(val.start, &accessor->byteOffset);
        else if (StrCMP16(key.start, "count"))         ParsePositiveNumber(val.start, &accessor->count);
        else if (StrCMP16(key.start, "componentType"))
        {
            ParsePositiveNumber(val.start, &accessor->componentType);
            accessor->componentType -= 0x1400; // GL_BYTE ;
        }
        else if (StrCMP16(key.start, "type"))
        {
            switch (*(val.end-1))
            {
                case '2': accessor->type = 2; break;  // VEC2   
                case '3': accessor->type = 3; break;  // VEC3   
                case '4': accessor->type = val.end[-2] == 'T' ? 16 : 4; break;  // VEC4 or mat4
                case 'R': accessor->type = 1; break;  // SCALAR 
            }
        }
        else AX_LOG("unknown accessor parameter: %s ", key.start);
        *key.end = beforeChar;
    }
}

static void ParseSceneObj(sj_Value sjSceneObj, void* element, GLTFParseContext* ctx)
{
    AScene* scene = (AScene*)element;
    sj_Value key, val;

    while (sj_iter_object(ctx->sj, sjSceneObj, &key, &val))
    {
        char beforeChar = *key.end; *key.end = 0; char* name = key.start;

        if (StrCMP16(key.start, "nodes"))
        {
            scene->nodes = ParseIntArrayAlloc(ctx, val, &scene->numNodes);
        }
        else if (StrCMP16(key.start, "name"))
        {
            scene->name = CopySJString(val, ctx->allocator);
        }
        else AX_LOG("unknown: %s ", key.start);
    
        *key.end = beforeChar;
    }
}

static void ParseBufferViewObj(sj_Value sjBufferObj, void* element, GLTFParseContext* ctx)
{
    GLTFBufferView* bufferView = (GLTFBufferView*)element;
    sj_Value key, val;
    while (sj_iter_object(ctx->sj, sjBufferObj, &key, &val))
    {
        char beforeChar = *key.end; *key.end = 0; char* name = key.start;

        if      (StrCMP16(key.start, "buffer"))     { ParsePositiveNumber(val.start, &bufferView->buffer); }
        else if (StrCMP16(key.start, "byteOffset")) { ParsePositiveNumber(val.start, &bufferView->byteOffset); }
        else if (StrCMP16(key.start, "byteLength")) { ParsePositiveNumber(val.start, &bufferView->byteLength); }
        else if (StrCMP16(key.start, "byteStride")) { ParsePositiveNumber(val.start, &bufferView->byteStride); }
        else if (StrCMP16(key.start, "target"))     { ParsePositiveNumber(val.start, &bufferView->target);     }
        else AX_LOG("unknown: %s ", key.start);
        
        *key.end = beforeChar;
    }
}

static void ParseBuffersObj(sj_Value sjBufferObj, void* element, GLTFParseContext* ctx)
{
    char binFilePath[512]={0};
    char* endOfWorkDir = binFilePath;
    int binPathlen = StringLengthSafe(ctx->path, sizeof(binFilePath));
    SmallMemCpy(binFilePath, ctx->path, binPathlen);
    endOfWorkDir = binFilePath + binPathlen;
    
    // remove bla.gltf
    while (binPathlen > 0 && binFilePath[binPathlen - 1] != '/' && binFilePath[binPathlen - 1] != '\\')
        binFilePath[--binPathlen] = '\0', endOfWorkDir--;

    GLTFBuffer* buffer = (GLTFBuffer*)element;
    sj_Value key, val;
    while (sj_iter_object(ctx->sj, sjBufferObj, &key, &val))
    {
        if (StrCMP16(key.start, "uri")) // is uri
        {
            const char* curr = key.start + sizeof("uri'"); // skip uri": 
            while (*curr != '"') curr++;
            if (StrCMP16(curr, "\"data:"))
            {
                curr = SkipAfter(curr, ',');
                uint64_t base64Size = 0;
                while (curr[base64Size] != '\"') {
                    base64Size++;
                }
                buffer->uri = FixedPow2Allocator_Allocate(ctx->allocator, base64Size);
                DecodeBase64((char*)buffer->uri, curr, base64Size);
                curr += base64Size + 1;
            }
            else
            {
                curr = GetStringInQuotes(endOfWorkDir, curr);
                buffer->uri = ReadAllFileAlloc(binFilePath);
                ASSERT(buffer->uri && "uri is not exist");
            }
        }
        else if (StrCMP16(key.start, "byteLength"))
        {
            ParsePositiveNumber(val.start, &buffer->byteLength);
        }
    }
}

static void ParseImagesObj(sj_Value sjSceneObj, void* element, GLTFParseContext* ctx)
{
    int pathLen = StringLength(ctx->path);
    while (ctx->path[pathLen-1] != '/') pathLen--;

    AImage* image = (AImage*)element;
    image->bufferViewIndex = -1;
    sj_Value key, val;
    while (sj_iter_object(ctx->sj, sjSceneObj, &key, &val))
    {
        char beforeChar = *key.end; *key.end = 0; char* name = key.start;
        // mimeType and name is not supported
        if (StrCMP16(key.start, "uri"))
        {
            size_t uriSize = val.end - val.start;
            image->path = (char*)FixedPow2Allocator_Allocate(ctx->allocator, uriSize + pathLen + 16);
            SmallMemCpy(image->path, ctx->path, pathLen);
            SmallMemCpy(image->path + pathLen, val.start, uriSize);
            image->path[uriSize + pathLen] = '\0';
        }
        else if (StrCMP16(key.start, "bufferView"))
        {
            ParsePositiveNumber(val.start, &image->bufferViewIndex);
        }
        else AX_LOG("unknown: %s ", key.start);
        *key.end = beforeChar;
    } 
}

static void ParseTexturesObj(sj_Value sjTextureObj, void* element, GLTFParseContext* ctx)
{
    ATexture* texture = (ATexture*)element;
    sj_Value key, val;
    while (sj_iter_object(ctx->sj, sjTextureObj, &key, &val))
    {
        char beforeChar = *key.end; *key.end = 0; char* name = key.start;

        if (StrCMP16(key.start, "sampler"))     ParsePositiveNumber(key.start, &texture->sampler);
        else if (StrCMP16(key.start, "source")) ParsePositiveNumber(key.start, &texture->source);
        else if (StrCMP16(key.start, "name"))   texture->name = CopySJString(val, ctx->allocator);
        else AX_LOG("unknown: %s ", key.start);
        
        *key.end = beforeChar;
    }
}

static void ParseAttributes(sj_Reader* sj, sj_Value sjAttributes, APrimitive* primitive)
{
    sj_Value key, val;
    MemsetZero(primitive->vertexAttribs, sizeof(void*) * AAttribType_Count);
    primitive->attributes = 0;
    while (sj_iter_object(sj, sjAttributes, &key, &val))
    {
        unsigned maskBefore = primitive->attributes;
        if      (StrCMP16(key.start, "POSITION"))   { primitive->attributes |= AAttribType_POSITION;   }
        else if (StrCMP16(key.start, "NORMAL"))     { primitive->attributes |= AAttribType_NORMAL;     }
        else if (StrCMP16(key.start, "TEXCOORD_0")) { primitive->attributes |= AAttribType_TEXCOORD_0; }
        else if (StrCMP16(key.start, "TANGENT"))    { primitive->attributes |= AAttribType_TANGENT;    }
        else if (StrCMP16(key.start, "TEXCOORD_1")) { primitive->attributes |= AAttribType_TEXCOORD_1; }
        else if (StrCMP16(key.start, "JOINTS_0"))   { primitive->attributes |= AAttribType_JOINTS;     }
        else if (StrCMP16(key.start, "WEIGHTS_0"))  { primitive->attributes |= AAttribType_WEIGHTS;    }
        else if (StrCMP16(key.start, "TEXCOORD_"))  { continue; } // < NO more than two texture coords
        else { ASSERT(0 && "attribute variable unknown!"); continue; }

        // using bitmask will help us to order attributes correctly(sort) Position, Normal, TexCoord
        unsigned newIndex = TrailingZeroCount32(maskBefore ^ primitive->attributes);
        int attribOffset = 0;
        ParsePositiveNumber(val.start, &attribOffset);
        primitive->vertexAttribs[newIndex] = (void*)(uint64_t)attribOffset;
    }
}

static void ParseMeshesObj(sj_Value sjMeshObj, void* element, GLTFParseContext* ctx)
{
    AMesh* mesh = (AMesh*)element;
    sj_Value key, val;
    while (sj_iter_object(ctx->sj, sjMeshObj, &key, &val))
    {
        if (StrCMP16(key.start, "name")) 
        {
            mesh->name = CopySJString(val, ctx->allocator);
            continue; 
        }
        else if (StrCMP16(key.start, "weights"))
        {
            int numWeights = sjCountArray(*ctx->sj, val);
            mesh->numMorphWeights = numWeights;
            mesh->morphWeights = FixedPow2Allocator_Allocate(
                ctx->allocator,
                numWeights * sizeof(float)
            );

            const char* curr = val.start;
            for (int i = 0; i < numWeights; i++)
            {
                curr = ParseFloat(curr, &mesh->morphWeights[i]);
            }
            continue;
        }
        else if (!StrCMP16(key.start, "primitives"))
        { 
            ASSERT(0 && "only primitives, name and weights allowed"); 
            return; 
        }

        mesh->numPrimitives = sjCountArray(*ctx->sj, val);
        mesh->primitives = FixedPow2Allocator_Allocate(
            ctx->allocator, 
            mesh->numPrimitives * sizeof(APrimitive)
        );

        MemSet(mesh->primitives, 0xCD, mesh->numPrimitives * sizeof(APrimitive));
        int primitiveIndex = 0;
        sj_Value sjPrimitiveArr;
        while (sj_iter_array(ctx->sj, val, &sjPrimitiveArr))
        {
            sj_Value sjPrimKey, sjPrimVal;
            APrimitive* primitive = mesh->primitives + primitiveIndex;  
            primitive->material = -1;
            primitiveIndex++;
            while (sj_iter_object(ctx->sj, sjPrimitiveArr, &sjPrimKey, &sjPrimVal))
            {
                if      (StrCMP16(sjPrimKey.start, "attributes")) { ParseAttributes(ctx->sj, sjPrimVal, primitive); }
                else if (StrCMP16(sjPrimKey.start, "indices"))    { ParsePositiveNumberU16(sjPrimVal.start, &primitive->indiceIndex);  }
                else if (StrCMP16(sjPrimKey.start, "mode"))       { ParsePositiveNumberU16(sjPrimVal.start, &primitive->mode       ); }
                else if (StrCMP16(sjPrimKey.start, "material"))   { ParsePositiveNumberU16(sjPrimVal.start, &primitive->material   ); }
                else if (StrCMP16(sjPrimKey.start, "targets"))
                {
                    primitive->morphTargets = (AMorphTarget*)FixedPow2Allocator_Allocate(
                        ctx->allocator, 
                        sizeof(AMorphTarget) * sjCountArray(*ctx->sj, sjPrimVal)
                    );

                    sj_Value sjMorphArr;
                    int morphTargetIdx = 0;
                    while (sj_iter_array(ctx->sj, sjPrimVal, &sjMorphArr))
                    {
                        sj_Value sjMorphKey,  sjMorphVal;
                        AMorphTarget* morphTarget = primitive->morphTargets + morphTargetIdx;
                        morphTargetIdx++;

                        while (sj_iter_object(ctx->sj, sjPrimitiveArr, &sjMorphKey, &sjMorphVal))
                        {
                            unsigned maskBefore = morphTarget->attributes;
                            if      (StrCMP16(sjMorphKey.start, "POSITION"))   { morphTarget->attributes |= AAttribType_POSITION;   }
                            else if (StrCMP16(sjMorphKey.start, "TEXCOORD_0")) { morphTarget->attributes |= AAttribType_TEXCOORD_0; }
                            else if (StrCMP16(sjMorphKey.start, "NORMAL"))     { morphTarget->attributes |= AAttribType_NORMAL;     }
                            else if (StrCMP16(sjMorphKey.start, "TANGENT"))    { morphTarget->attributes |= AAttribType_TANGENT;    }
                            else if (StrCMP16(sjMorphKey.start, "TEXCOORD_"))  { continue; } // < NO more than one texture coords
                            else { ASSERT(0 && "attribute variable unknown!"); return; }
                 
                            // detect changed attribute.
                            unsigned addedAttribute = TrailingZeroCount32(maskBefore ^ morphTarget->attributes);
                            ParsePositiveNumberU16(sjMorphVal.start, morphTarget->indexes + addedAttribute);
                        }
                    }
                }
                else { ASSERT(0); return; }
            }
        }
    }
}

static void ParseNodesObj(sj_Value sjMeshObj, void* element, GLTFParseContext* ctx)
{
    ANode* node = (ANode*)element;
    node->rotation[3] = 1.0f;
    node->scale[0] = node->scale[1] = node->scale[2] = ctx->scale; 
    node->index = -1;
    node->parent = -1;

    sj_Value key, val;
    while (sj_iter_object(ctx->sj, sjMeshObj, &key, &val))
    {
        const char* curr = key.start;

        // mesh, name, children, matrix, translation, rotation, scale, skin
        if      (StrCMP16(curr, "mesh"))   { node->type = 0; ParsePositiveNumber(curr, &node->index); }
        else if (StrCMP16(curr, "camera")) { node->type = 1; ParsePositiveNumber(curr, &node->index); }
        else if (StrCMP16(curr, "children"))
        {
            node->children = ParseIntArrayAlloc(ctx, val, &node->numChildren);
        }
        else if (StrCMP16(curr, "matrix"))
        {
            mat4x4 m;
            float* matrix = &m.m[0][0];
            
            for (int i = 0; i < 16; i++)
                curr = ParseFloat(curr, &matrix[i]);
            
            m = M44Transpose(m);
            node->translation[0] = matrix[12];
            node->translation[1] = matrix[13];
            node->translation[2] = matrix[14];
            QuaternionFromMatrix(node->rotation, matrix, 4);

            v128f v = VecMulf(M44ExtractScaleV(m), ctx->scale);
            Vec3Store(node->scale, v);
        }
        else if (StrCMP16(curr, "translation"))
        {
            curr = ParseFloat(curr, &node->translation[0]);
            curr = ParseFloat(curr, &node->translation[1]);
            curr = ParseFloat(curr, &node->translation[2]);
        }
        else if (StrCMP16(curr, "rotation"))
        {
            curr = ParseFloat(curr, &node->rotation[0]);
            curr = ParseFloat(curr, &node->rotation[1]);
            curr = ParseFloat(curr, &node->rotation[2]);
            curr = ParseFloat(curr, &node->rotation[3]);
        }
        else if (StrCMP16(curr, "scale"))
        {
            curr = ParseFloat(curr, &node->scale[0]); node->scale[0] *= ctx->scale;
            curr = ParseFloat(curr, &node->scale[1]); node->scale[1] *= ctx->scale;
            curr = ParseFloat(curr, &node->scale[2]); node->scale[2] *= ctx->scale;
        }
        else if (StrCMP16(curr, "name"))
        {
            node->name = CopySJString(val, ctx->allocator);
        }
        else if (StrCMP16(curr, "skin"))
        {
            ParsePositiveNumber(curr, &node->skin);
        }
        else
        {
            ASSERT(0 && "Unknown node variable");
            return;
        }   
    }
}

static void ParseCamerasObj(sj_Value sjMeshObj, void* element, GLTFParseContext* ctx)
{
    ACamera* camera = (ACamera*)element;
    sj_Value key, val;
    while (sj_iter_object(ctx->sj, sjMeshObj, &key, &val))
    {
        const char* curr = key.end+1;
        if (StrCMP16(key.start, "name")) 
        {
            camera->name = CopySJString(val, ctx->allocator);
        }
        else if (StrCMP16(key.start, "type")) 
        {
            camera->type = *val.start == 'p'; // 0 orthographic 1 perspective 
        }
        else while (true)
        {
            while (*curr != '"')
                if (*curr++ == '}')  
                    goto end_properties; // this is end of camera variables
            
            curr++;
            if      (StrCMP16(curr, "zfar"))        { curr = ParseFloat(curr, &camera->zFar        ); }
            else if (StrCMP16(curr, "znear"))       { curr = ParseFloat(curr, &camera->zNear       ); }
            else if (StrCMP16(curr, "aspectRatio")) { curr = ParseFloat(curr, &camera->aspectRatio ); }
            else if (StrCMP16(curr, "yfov"))        { curr = ParseFloat(curr, &camera->yFov        ); }
            else if (StrCMP16(curr, "xmag"))        { curr = ParseFloat(curr, &camera->xmag        ); }
            else if (StrCMP16(curr, "ymag"))        { curr = ParseFloat(curr, &camera->ymag        ); }
            else { ASSERT(0); return; }
        }
        end_properties:{}
    } 
}

static void ParseSamplersObj(sj_Value sjMeshObj, void* element, GLTFParseContext* ctx)
{
    ASampler* sampler = (ASampler*)element;
    sj_Value key, val;
    while (sj_iter_object(ctx->sj, sjMeshObj, &key, &val))
    {
        int minFilter, magFilter, wrapS, wrapT;
        char beforeChar = *key.end; *key.end = 0; char* name = key.start;

        if      (StrCMP16(key.start, "magFilter")) ParsePositiveNumber(val.start, &magFilter), sampler->magFilter = (char)(magFilter - 0x2600); // GL_NEAREST 9728, GL_LINEAR 0x2601 9729
        else if (StrCMP16(key.start, "minFilter")) ParsePositiveNumber(val.start, &minFilter), sampler->minFilter = (char)(minFilter - 0x2600); // GL_NEAREST 9728, GL_LINEAR 0x2601 9729
        else if (StrCMP16(key.start, "wrapS"))     ParsePositiveNumber(val.start, &wrapS), sampler->wrapS = (char)OGLWrapToWrap(wrapS);
        else if (StrCMP16(key.start, "wrapT"))     ParsePositiveNumber(val.start, &wrapT), sampler->wrapT = (char)OGLWrapToWrap(wrapT);
        else AX_LOG("unknown: %s ", key.start);
        
        *key.end = beforeChar;
    }
}


static const char* ParseMaterialTexture(GLTFParseContext* ctx, sj_Value sjMatObj, GLTFTexture* texture)
{
    texture->strength = MakeFloat16(1.0f);
    sj_Value key, val;
    while (sj_iter_object(ctx->sj, sjMatObj, &key, &val))
    {
        char beforeChar = *key.end; *key.end = 0; char* name = key.start;
        
        if (StrCMP16(key.start, "scale")) { ParseFloat16(val.start, &texture->scale); }
        else if (StrCMP16(key.start, "index")) {
            ParsePositiveNumberU16(val.start, &texture->index);
        }
        else if (StrCMP16(key.start, "texCoord")) { ParsePositiveNumberU16(val.start, &texture->texCoord); }
        else if (StrCMP16(key.start, "strength")) { ParseFloat16(val.start, &texture->strength); }
        else AX_LOG("unknown: %s ", key.start);
    
        *key.end = beforeChar;
    
    }
    return NULL;
}

static void ParseSkinsObj(sj_Value sjArrObj, void* element, GLTFParseContext* ctx)
{
    ASkin* skin = (ASkin*)element;
    skin->skeleton = -1;

    sj_Value key, val;
    while (sj_iter_object(ctx->sj, sjArrObj, &key, &val))
    {
        char beforeChar = *key.end; *key.end = 0; char* name = key.start;

        if (StrCMP16(key.start, "inverseBindMatrices"))
        {
            // we will parse later, because we are not sure we are parsed accessors at this point
            int inverseBindOffset = 0; 
            ParsePositiveNumber(val.start, &inverseBindOffset);
            skin->inverseBindMatrices = (float*)(size_t)inverseBindOffset;
        }
        else if (StrCMP16(key.start, "skeleton")) ParsePositiveNumber(val.start, &skin->skeleton);
        else if (StrCMP16(key.start, "name"))    { skin->name = CopySJString(val, ctx->allocator); }
        else if (StrCMP16(key.start, "joints"))
        {
            skin->joints = ParseIntArrayAlloc(ctx, val, &skin->numJoints);
        }
        else AX_LOG("unknown: %s ", key.start);
    
        *key.end = beforeChar;
    }
}

static void ParseAnimationsObj(sj_Value sjArrObj, void* element, GLTFParseContext* ctx)
{
    AAnimation* animation = (AAnimation*)element;
    animation->speed = 1.0f;
    sj_Value key, val;
    while (sj_iter_object(ctx->sj, sjArrObj, &key, &val))
    {
        if (StrCMP16(key.start, "name"))
        {
            animation->name = CopySJString(val, ctx->allocator);
        }
        else if (StrCMP16(key.start, "channels"))
        {
            animation->numChannels = sjCountArray(*ctx->sj, val);
            animation->channels = (AAnimChannel*)FixedPow2Allocator_Allocate(
                ctx->allocator, animation->numChannels * sizeof(AAnimChannel)
            );
            int channelIndex = 0;
            sj_Value channelElement;
            while (sj_iter_array(ctx->sj, val, &channelElement))
            {
                AAnimChannel* channel = animation->channels + channelIndex;
                channelIndex++;
                
                sj_Value chKey, chVal;
                while (sj_iter_object(ctx->sj, channelElement, &chKey, &chVal))
                {
                    if  (StrCMP16(chKey.start, "sampler"))
                    {
                        ParsePositiveNumber(chVal.start, &channel->sampler);
                    } 
                    else if (StrCMP16(chKey.start, "target"))  
                    { 
                        sj_Value tgKey, tgVal;
                        while (sj_iter_object(ctx->sj, chVal, &tgKey, &tgVal))
                        {
                            if (StrCMP16(tgKey.start, "node")) 
                            { 
                                ParsePositiveNumber(tgVal.start, &channel->targetNode);
                            } 
                            else if (StrCMP16(tgKey.start, "path"))
                            {
                                switch (*tgVal.start) {
                                    case 't': channel->targetPath = AAnimTargetPath_Translation; break;
                                    case 'r': channel->targetPath = AAnimTargetPath_Rotation;    break;
                                    case 's': channel->targetPath = AAnimTargetPath_Scale;       break;
                                    case 'w': channel->targetPath = AAnimTargetPath_Weight;      break;
                                    default: ASSERT(0 && "Unknown animation path value");
                                };
                            }
                        }
                    } 
                }
            }
        }
        else if (StrCMP16(key.start, "samplers"))
        {
            animation->numSamplers = sjCountArray(*ctx->sj, val);
            animation->samplers = (AAnimSampler*)FixedPow2Allocator_Allocate(
                ctx->allocator, animation->numSamplers * sizeof(AAnimSampler)
            );
            int samplerIdx = 0;
            sj_Value samplerElement;
            while (sj_iter_array(ctx->sj, val, &samplerElement))
            {
                AAnimSampler* sampler = animation->samplers + samplerIdx;
                samplerIdx++;
                
                sj_Value spKey, spVal;
                while (sj_iter_object(ctx->sj, samplerElement, &spKey, &spVal))
                {
                    if (StrCMP16(spKey.start, "input")) 
                    { 
                        int input = 0;
                        ParsePositiveNumber(spVal.start, &input);
                        sampler->input = (float*)(size_t)input;
                    } 
                    else if (StrCMP16(spKey.start, "output"))
                    {
                        int output = 0;
                        ParsePositiveNumber(spVal.start, &output); 
                        sampler->output = (float*)(size_t)output; 
                    } 
                    else if (StrCMP16(spKey.start, "interpol"))  // you've been searching from interpol hands up!!
                    {
                        switch (*spVal.start)
                        {
                            case 'L': sampler->interpolation = 0; break; // Linear
                            case 'S': sampler->interpolation = 1; break; // Step
                            case 'C': sampler->interpolation = 2; break; // CubicSpline
                            default: ASSERT(0 && "Unknown animation path value"); break;
                        };
                    }
                    else ASSERT(0 && "Unknown animation sampler value");
                }
            }
        }
    } 
}


static void ParseMaterialsObj(sj_Value sjMaterialObj, void* element, GLTFParseContext* ctx)
{
    AMaterial* material = (AMaterial*)element;
    material->baseColorTexture.index = UINT16_MAX;
    material->specularTexture.index = UINT16_MAX;
    material->metallicRoughnessTexture.index = UINT16_MAX;
    material->textures[0].index = UINT16_MAX;
    material->textures[1].index = UINT16_MAX;
    material->textures[2].index = UINT16_MAX;
    material->metallicFactor    = PackUnorm16(1.0f);
    material->roughnessFactor   = PackUnorm16(1.0f);

    sj_Value key, val;
    while (sj_iter_object(ctx->sj, sjMaterialObj, &key, &val))
    {
        if (StrCMP16(key.start, "name"))
        {
            material->name = CopySJString(val, ctx->allocator);
        }
        else if (StrCMP16(key.start, "doubleSided"))
        {
            material->doubleSided = (*val.start == 't');
        }
        else if (StrCMP16(key.start, "pbrMetallicRoug")) // pbrMetallicRoughness
        {
            sj_Value pbrKey, pbrVal;
            while (sj_iter_object(ctx->sj, val, &pbrKey, &pbrVal))
            {
                if (StrCMP16(pbrKey.start, "baseColorTex")) // baseColorTexture
                {
                    ParseMaterialTexture(ctx, pbrVal, &material->baseColorTexture);
                }
                else if (StrCMP16(pbrKey.start, "metallicRough")) // metallicRoughnessTexture
                {
                    ParseMaterialTexture(ctx, pbrVal, &material->metallicRoughnessTexture);
                }
                else if (StrCMP16(pbrKey.start, "baseColorFact")) // baseColorFactor
                {
                    float baseColorFactor[4];
                    const char* curr = (const char*)pbrVal.start;
                    curr = ParseFloat(curr, baseColorFactor + 0); 
                    curr = ParseFloat(curr, baseColorFactor + 1); 
                    curr = ParseFloat(curr, baseColorFactor + 2); 
                    curr = ParseFloat(curr, baseColorFactor + 3); 
                    material->baseColorFactor = PackColor4PtrToUint(baseColorFactor);
                }
                else if (StrCMP16(pbrKey.start, "metallicFact")) // metallicFactor
                {
                    float metallicFactor = 0.0f;
                    ParseFloat(pbrVal.start, &metallicFactor);
                    material->metallicFactor = PackUnorm16(metallicFactor);
                }
                else if (StrCMP16(pbrKey.start, "roughnessFact")) // roughnessFactor
                {
                    float roughnessFactor = 0.0f;
                    ParseFloat(pbrVal.start, &roughnessFactor);
                    material->roughnessFactor = PackUnorm16(roughnessFactor);
                }
            }
        }
        else if (StrCMP16(key.start, "normalTexture"))   ParseMaterialTexture(ctx, val, &material->textures[0]);
        else if (StrCMP16(key.start, "occlusionTextur")) ParseMaterialTexture(ctx, val, &material->textures[1]);
        else if (StrCMP16(key.start, "emissiveTexture")) ParseMaterialTexture(ctx, val, &material->textures[2]);
        else if (StrCMP16(key.start, "emissiveFactor")) 
        {
            float emissiveFactor[3];
            const char* curr = (const char*)val.start;
            curr = ParseFloat16(curr, &material->emissiveFactor[2]);
            curr = ParseFloat16(curr, &material->emissiveFactor[0]); 
            curr = ParseFloat16(curr, &material->emissiveFactor[1]);
        }
        else if (StrCMP16(key.start, "extensions"))
        {
            sj_Value extKey, extVal;
            while (sj_iter_object(ctx->sj, val, &extKey, &extVal))
            {
                // Iterate through extension properties
                sj_Value propKey, propVal;
                while (sj_iter_object(ctx->sj, extVal, &propKey, &propVal))
                {
                    if (StrCMP16(propKey.start, "index"))
                    {
                        ParsePositiveNumberU16(propVal.start, &material->specularTexture.index);
                    }
                    else if (StrCMP16(propKey.start, "ior"))
                    {
                        ParseFloat(propVal.start, &material->ior);
                    }
                    else if (StrCMP16(propKey.start, "specularColorFa")) // specularColorFactor
                    {
                        float sf[3];
                        const char* curr = (const char*)propVal.start;
                        curr = ParseFloat(curr, sf + 0);
                        curr = ParseFloat(curr, sf + 1);
                        curr = ParseFloat(curr, sf + 2);
                        float s = (sf[0] + sf[1] + sf[2]) * 0.33333f;
                        material->specularFactor = MakeFloat16(s);
                    }
                }
            }
        }
        else if (StrCMP16(key.start, "alphaMode"))
        {
            if      (StrCMP16(val.start, "OPAQUE"))     material->alphaMode = AMaterialAlphaMode_Opaque;
            else if (StrCMP16(val.start, "MASK"))  material->alphaMode = AMaterialAlphaMode_Mask;
            else if (StrCMP16(val.start, "BLEND")) material->alphaMode = AMaterialAlphaMode_Blend;
        }
        else if (StrCMP16(key.start, "alphaCutoff"))
        {
            ParseFloat(val.start, &material->alphaCutoff);
        }
    }
}

static char* ParseGLBHeader(const char* path, SceneBundle* result, uint64_t* jsonSize)
{
    char* source = NULL;
    AFile file = AFileOpen(path, AOpenFlag_ReadBinary);
    if (AFileExist(file))
    {
        int magic, version, length;
        AFileRead(&magic, 4, file, 1);
        if (magic == 0x46546C67) // gltf in binary
        {
            AFileRead(&version, 4, file, 1);
            AFileRead(&length, 4, file, 1);
                
            int chunk0Len, chunk0Type;
            AFileRead(&chunk0Len, 4, file, 1);
            AFileRead(&chunk0Type, 4, file, 1);
            *jsonSize = (uint64_t)chunk0Len;

            if (chunk0Type != 0x4E4F534A) // json in binary
                return 0; // json must exist
            
            source = (char*)AllocZeroTLSFGlobal(chunk0Len + 40, 1);
            AFileRead(source, chunk0Len, file, 1);

            int chunk1Len, chunk1Type;
            AFileRead(&chunk1Len, 4, file, 1);
            AFileRead(&chunk1Type, 4, file, 1);

            if (chunk1Type == 0x004E4942) // bin in binary
            {
                result->buffers = (GLTFBuffer*)AllocateTLSFGlobal(sizeof(GLTFBuffer));
                result->buffers[0].uri = AllocateTLSFGlobal(chunk1Len);
                result->buffers[0].byteLength = chunk1Len;
                result->numBuffers = 1;
                AFileRead(result->buffers[0].uri, chunk1Len, file, 1);
            }
        }
        else return 0;

        AFileClose(file);
    }
    return source;
}

int ParseGLTF(const char* path, SceneBundle* result, float scale)
{
    ASSERT(result && path);
    uint64_t sourceSize = 0;
    char* source = NULL;
    MemSet(result, 0xCD, sizeof(SceneBundle));

    if (FileHasExtension(path, StringLength(path), ".glb"))
    {
        source = ParseGLBHeader(path, result, &sourceSize);
        if (source == NULL)
        {
            result->error = AError_GLB_PARSING_FAILED;
            AX_WARN("glb header parse failed", path); 
            return 0; // json must exist
        }
    }

    if (source == NULL)
        source = ReadAllTextAlloc(path, &sourceSize, NULL);
        
    if (source == NULL) {
        result->error = AError_FILE_NOT_FOUND; 
        AX_WARN("mesh file is not exist %s", path); 
        return 0;
    }

    FixedPow2Allocator* allocator = AllocateTLSFGlobal(sizeof(FixedPow2Allocator));
    FixedPow2Allocator_Init(allocator, 2048 * 2);

    GLTFAccessor* accessors = NULL;
    GLTFBufferView* bufferViews = NULL;

    AScene* scenes = NULL;
    result->defaultSceneIndex = 0;

    sj_Reader sj = sj_reader(source, sourceSize);
    sj_Value obj = sj_read(&sj);
    
    GLTFParseContext ctx = {allocator, path, &sj, scale };

    size_t pathLen = StringLength(path);
    bool isGLB = FileHasExtension(path, pathLen, ".glb");
    
    sj_Value key, val;
    while (sj_iter_object(&sj, obj, &key, &val)) 
    {
        char beforeChar = *key.end;
        *key.end = 0;
        char* name = key.start;

        #define ParseList(Type, numThings, fnName) (Type*)ParseArray(val, sizeof(Type), numThings, &ctx, fnName)
            
             if (StrCMP16(name, "accessors"))          accessors       = ParseList(GLTFAccessor  , NULL, ParseAccessorObj);
        else if (StrCMP16(name, "bufferViews"))        bufferViews     = ParseList(GLTFBufferView, NULL, ParseBufferViewObj);
        else if (!isGLB && StrCMP16(name, "buffers"))  result->buffers = ParseList(GLTFBuffer    , NULL, ParseBuffersObj);
        else if (StrCMP16(name, "scenes"))      result->scenes     = ParseList(AScene    , &result->numScenes    , ParseSceneObj     );
        else if (StrCMP16(name, "images"))      result->images     = ParseList(AImage    , &result->numImages    , ParseImagesObj    );
        else if (StrCMP16(name, "textures"))    result->textures   = ParseList(ATexture  , &result->numTextures  , ParseTexturesObj  );
        else if (StrCMP16(name, "meshes"))      result->meshes     = ParseList(AMesh     , &result->numMeshes    , ParseMeshesObj    );
        else if (StrCMP16(name, "materials"))   result->materials  = ParseList(AMaterial , &result->numMaterials , ParseMaterialsObj );
        else if (StrCMP16(name, "nodes"))       result->nodes      = ParseList(ANode     , &result->numNodes     , ParseNodesObj     );
        else if (StrCMP16(name, "samplers"))    result->samplers   = ParseList(ASampler  , &result->numSamplers  , ParseSamplersObj  );
        else if (StrCMP16(name, "cameras"))     result->cameras    = ParseList(ACamera   , &result->numCameras   , ParseCamerasObj   );
        else if (StrCMP16(name, "skins"))       result->skins      = ParseList(ASkin     , &result->numSkins     , ParseSkinsObj     );
        else if (StrCMP16(name, "animations"))  result->animations = ParseList(AAnimation, &result->numAnimations, ParseAnimationsObj);
        else if (StrCMP16(name, "scene"))       ParsePositiveNumber(key.start, &result->defaultSceneIndex);
        else if (!isGLB) AX_LOG("unknown gltf parameter: %s", name);

        // skip, asets, extensions used
        *key.end = beforeChar;
    }

    if (result->numMeshes     == 0xCDCDCDCD) { AX_WARN("numMeshes     undefined"); result->numMeshes     = 0; }
    if (result->numNodes      == 0xCDCDCDCD) { AX_WARN("numNodes      undefined"); result->numNodes      = 0; }
    if (result->numMaterials  == 0xCDCDCDCD) { AX_WARN("numMaterials  undefined"); result->numMaterials  = 0; }
    if (result->numTextures   == 0xCDCDCDCD) { AX_WARN("numTextures   undefined"); result->numTextures   = 0; }
    if (result->numImages     == 0xCDCDCDCD) { AX_WARN("numImages     undefined"); result->numImages     = 0; }
    if (result->numSamplers   == 0xCDCDCDCD) { AX_WARN("numSamplers   undefined"); result->numSamplers   = 0; }
    if (result->numCameras    == 0xCDCDCDCD) { AX_WARN("numCameras    undefined"); result->numCameras    = 0; }
    if (result->numScenes     == 0xCDCDCDCD) { AX_WARN("numScenes     undefined"); result->numScenes     = 0; }
    if (result->numSkins      == 0xCDCDCDCD) { AX_WARN("numSkins      undefined"); result->numSkins      = 0; }
    if (result->numAnimations == 0xCDCDCDCD) { AX_WARN("numAnimations undefined"); result->numAnimations = 0; }

    AX_LOG("gltf parse: %s nodes=%d meshes=%d skins=%d animations=%d scenes=%d",
           path, result->numNodes, result->numMeshes, result->numSkins, result->numAnimations, result->numScenes);

    if (result->nodes == NULL || result->numNodes <= 0)
        AX_WARN("gltf has no nodes: %s", path);
    if (result->meshes == NULL || result->numMeshes <= 0)
        AX_WARN("gltf has no meshes: %s", path);
    if (result->numScenes <= 0)
        AX_WARN("gltf has no scenes; normalization will fall back to root/loose nodes: %s", path);

    // Resolve accessor indices into direct buffer pointers used by the asset baking stage.
    for (int m = 0; m < result->numMeshes; ++m)
    {
        // get number of vertex, getting first attribute count because all of the others are same
        AMesh mesh = result->meshes[m];
        for (int p = 0; p < mesh.numPrimitives; p++)
        {
            APrimitive* primitive = &mesh.primitives[p];
            if ((primitive->attributes & AAttribType_POSITION) == 0)
                AX_WARN("mesh %d primitive %d has no POSITION attribute", m, p);

            // get position attrib's count because all attributes same
            int numVertex = accessors[(int)(size_t)primitive->vertexAttribs[0]].count; 
            primitive->numVertices = numVertex;
        
            bool indicesDefined = primitive->indiceIndex != 0xCDCD;
            primitive->indiceIndex = indicesDefined ? primitive->indiceIndex : 0;
            // get number of index
            GLTFAccessor accessor = accessors[primitive->indiceIndex];
            primitive->numIndices = accessor.count;
            
            accessor = accessors[primitive->indiceIndex];
            GLTFBufferView view = bufferViews[accessor.bufferView];
            int64_t offset = (int64_t)accessor.byteOffset + view.byteOffset;

            primitive->indices = ((char*)result->buffers[view.buffer].uri) + offset;
            primitive->indices = indicesDefined ? primitive->indices : NULL;
            primitive->indexType = accessor.componentType;

            // get joint data that we need for creating vertices
            const uint32_t jointIndex = TrailingZeroCount32(AAttribType_JOINTS);
            accessor = accessors[(int)(size_t)primitive->vertexAttribs[jointIndex]];
            primitive->jointType   = (short)accessor.componentType;
            primitive->jointCount  = (short)accessor.type;
            primitive->jointStride = (short)bufferViews[accessor.bufferView].byteStride;

            // get weight data that we need for creating vertices
            const uint32_t weightIndex = TrailingZeroCount32(AAttribType_WEIGHTS);
            accessor = accessors[(int)(size_t)primitive->vertexAttribs[weightIndex]];
            primitive->weightType   = (short)accessor.componentType;
            primitive->weightStride = (short)bufferViews[accessor.bufferView].byteStride;

            // position, normal, texcoord are different buffers, 
            // we are unifying all attributes to Vertex* buffer here
            // even though attrib definition in gltf is not ordered, this code will order it, because we traversing set bits
            // for example it converts from this: TexCoord, Normal, Position to Position, Normal, TexCoord
            unsigned attributes = primitive->attributes;
            for (int j = 0; attributes > 0 && j < AAttribType_Count; j += NextSetBit(&attributes))
            {
                accessor     = accessors[(int)(size_t)primitive->vertexAttribs[j]];
                view         = bufferViews[accessor.bufferView];
                offset       = (int64_t)(accessor.byteOffset) + view.byteOffset;
                
                primitive->vertexAttribs[j] = (char*)result->buffers[view.buffer].uri + offset;
            }
        }
    }

    for (int s = 0; s < result->numSkins; s++)
    {
        ASkin* skin = &result->skins[s];
        if (skin->inverseBindMatrices == NULL)
        {
            AX_WARN("skin %d has no inverse bind matrices", s);
            continue;
        }

        size_t skinIndex = (size_t)skin->inverseBindMatrices;
        GLTFAccessor   accessor  = accessors[(int)skinIndex];
        if (accessor.count != skin->numJoints)
            AX_WARN("skin %d inverse bind count %d does not match joint count %d", s, accessor.count, skin->numJoints);

        GLTFBufferView view      = bufferViews[accessor.bufferView];
        int64_t        offset    = (int64_t)(accessor.byteOffset) + view.byteOffset;
        skin->inverseBindMatrices = (float*)((char*)result->buffers[view.buffer].uri + offset);
    }

    for (int i = 0; i < result->numImages; ++i)
    {
        AImage* image = result->images + i;
        if (image->bufferViewIndex == -1) continue;
        
        image->path = (char*)FixedPow2Allocator_Allocate(allocator, pathLen + 64);

        size_t pathEnd = pathLen - (isGLB ? 4 : 5);
        SmallMemCpy(image->path, path, pathEnd);
        SmallMemCpy(image->path + pathEnd, "Image00000", sizeof("Image00000"));
        
        int idxLen = IntToString(image->path + pathEnd + sizeof("Image"), (int64_t)i, 0);
        SmallMemCpy(image->path + pathEnd + sizeof("Image") + idxLen, ".png", 4);

        GLTFBufferView bufferView = bufferViews[image->bufferViewIndex];
        GLTFBuffer buffer = result->buffers[bufferView.buffer];
        WriteAllBytes(image->path, (char*)buffer.uri + bufferView.byteOffset, bufferView.byteLength);
        AX_LOG("written image into: %s", image->path);
    }

    // Animation samplers still reference accessors here; resolve them before normalization remaps target nodes.
    for (int a = 0; a < result->numAnimations; a++)
    {
        AAnimation* animation = &result->animations[a];
        animation->duration = 0.0f;
        if (animation->numChannels <= 0)
            AX_WARN("animation %d has no channels", a);

        for (int s = 0; s < animation->numSamplers; s++)
        {
            AAnimSampler* sampler = &animation->samplers[s];
            size_t inputIndex = (size_t)sampler->input;
            GLTFAccessor   accessor = accessors[(int)inputIndex];
            GLTFBufferView view     = bufferViews[accessor.bufferView];
            int64_t        offset   = (int64_t)(accessor.byteOffset) + view.byteOffset;
            
            sampler->input     = (float*)((char*)result->buffers[view.buffer].uri + offset);
            sampler->count     = accessor.count;
            sampler->inputType = accessor.componentType;
            if (sampler->inputType != AComponentType_FLOAT)
                AX_WARN("animation %d sampler %d input type is not FLOAT: %d", a, s, sampler->inputType);

            size_t outputIndex = (size_t)sampler->output;
            accessor = accessors[(int)outputIndex];
            view     = bufferViews[accessor.bufferView];
            offset   = (int64_t)(accessor.byteOffset) + view.byteOffset;
            
            sampler->output = (float*)((char*)result->buffers[view.buffer].uri + offset);
            sampler->count  = accessor.count;
            sampler->outputType = accessor.componentType;
            sampler->numComponent = accessor.type;
            if (sampler->outputType != AComponentType_FLOAT)
                AX_WARN("animation %d sampler %d output type is not FLOAT: %d", a, s, sampler->outputType);
            if (sampler->numComponent != 3 && sampler->numComponent != 4)
                AX_WARN("animation %d sampler %d output component count is unsupported: %d", a, s, sampler->numComponent);
           
            animation->duration = MMAX(animation->duration, sampler->input[sampler->count - 1]);
        }
    }

    // calculate num vertices and indices
    int totalVertexCount = 0;
    int totalIndexCount = 0;
    // Calculate total vertices, and indices
    for (int m = 0; m < result->numMeshes; ++m)
    {
        AMesh mesh = result->meshes[m];
        for (int p = 0; p < mesh.numPrimitives; p++)
        {
            APrimitive* primitive = &mesh.primitives[p];
            totalIndexCount  += primitive->numIndices;
            totalVertexCount += primitive->numVertices;
        }
    }
    
    result->rootNode = Prefab_FindAnimRootNodeIndex(result);
    result->totalIndices  = totalIndexCount;
    result->totalVertices = totalVertexCount;

    result->allocator = allocator;
    result->scale = scale;
    result->error = AError_NONE;

    // Normalization owns semantic remapping: flattened node order, parent indices, skin joints, animation targets.
    SceneBundle_Normalize(result);
    return 1;
}

void FreeGLTFBuffers(SceneBundle* gltf)
{
    for (int i = 0; i < gltf->numBuffers; i++)
    {
        FreeAllText((char*)gltf->buffers[i].uri);
        gltf->buffers[i].uri = NULL;
    }
    // dynarray_destroy(gltf->buffers);
    gltf->numBuffers = 0;
    gltf->buffers = NULL;
}

void FreeSceneBundle(SceneBundle* gltf)
{
    FixedPow2Allocator_Destroy((FixedPow2Allocator*)gltf->allocator);
    DeAllocateTLSFGlobal(gltf->allocator);
}

// <<<<<<<        prefab         >>>>>>>>>>>>

int Prefab_FindAnimRootNodeIndex(const SceneBundle* prefab)
{
    if (prefab->skins == NULL || prefab->skins == (ASkin*)0xCDCDCDCDCDCDCDCDull)
        return 0;

    ASkin skin = prefab->skins[0];
    if (skin.skeleton != -1) 
        return skin.skeleton;
    
    // search for Armature name, and also record the node that has most children
    int armatureIdx = -1;
    int maxChilds   = 0;
    int maxChildIdx = 0;
    
    // todo recurse to find max children
    for (int i = 0; i < prefab->numNodes; i++)
    {
        if (StrCMP16(prefab->nodes[i].name, "Armature") ||
            StrCMP16(prefab->nodes[i].name, "_rootJoint"))
        {
            armatureIdx = i;
            break;
        }
    
        int numChildren = prefab->nodes[i].numChildren;
        if (numChildren > maxChilds) {
            maxChilds = numChildren;
            maxChildIdx = i;
        }
    }
    
    int skeletonNode = armatureIdx != -1 ? armatureIdx : maxChildIdx;
    return skeletonNode;
}

int Prefab_FindNodeFromName(const SceneBundle* prefab, const char* name)
{
    int len = StringLength(name);
    for (int i = 0; i < prefab->numNodes; i++)
    {
        if (StringEqual(prefab->nodes[i].name, name, len))
            return i;
    }
    return -1;
}
