/****************************************************************************
    *  Purpose:                                                                 *
    *    Gets all of the textures in GLTF or FBX scene compresses them to       *
    *    to make textures smaller on GPU and Disk I'm compressing them          *
    *    using BCn texture compression on Windows                               *
    *    and using ASTC texture compression for storing textures on android     *
    *    also compressing further with zstd to reduce the size on disk.         *
    *                                                                           *
    *  Textures and Corresponding Formats:                                      *
    *    R  = BC4                                                               *
    *    RG = BC5                                                               *
    *    RGB, RGBA = DXT5                                                       *
    *  Android:                                                                 *
    *    All Textures are using ASTC 4X4 format because:                        *
    *    android doesn't have normal maps I haven't use other than ASTC4X4      *
    *    but in feature I might use ETC2 format because it has                  *
    *    faster compile times and faster compress speed. (etcpak)               *
    *  Author:                                                                  *
    *    Anilcan Gulkaya 2024 anilcangulkaya7@gmail.com github @benanil         *
    *  Converted to C: 2025                                                     *
    ****************************************************************************/

#include "Include/AssetManager.h"
#include "Include/Graphics.h"
#include "Include/Platform.h"
#include "Include/Memory.h"

#include "Extern/stb_image.h"
#include "Extern/sdefl.h"
#include "Extern/sinfl.h"

#if !AX_GAME_BUILD
#define STB_DXT_IMPLEMENTATION
#define STB_IMAGE_RESIZE2_IMPLEMENTATION
#include "Extern/ProcessDxtc.c"
#include "Extern/stb/stb_dxt.h"
#include "Extern/stb/stb_image_resize2.h"
#endif

/*//////////////////////////////////////////////////////////////////////////*/
/*                          Image Save Load                                 */
/*//////////////////////////////////////////////////////////////////////////*/


int IsTextureLastVersion(const char* path)
{
    AFile file = AFileOpen(path, AOpenFlag_ReadBinary);
    if (!AFileExist(file) || AFileSize(file) < 32) 
        return 0;
    int version = 0;
    AFileRead(&version, sizeof(int), file, 1);
    AFileClose(file, 1);
    return version == g_AXTextureVersion;
}

#if !AX_GAME_BUILD

static void MakeRGTextureFromRGB(unsigned char* texture, int numPixels)
{
    const unsigned char* rgb = texture;
    
    for (int i = 0; i < numPixels * 3; i += 3)
    {
        texture[0] = rgb[0];
        texture[1] = rgb[1];
        
        texture += 2;
        rgb += 3;
    }
}

static void MakeRGTextureFromRGBA(unsigned char* texture, int numPixels)
{
    const unsigned char* rgba = texture;
    
    for (int i = 0; i < numPixels * 4; i += 4)
    {
        texture[0] = rgba[0];
        texture[1] = rgba[1];
        texture += 2;
        rgba += 4;
    }
}

static void MakeRGBA(const unsigned char* from, unsigned char* rgba, int numPixels, int channelsBefore)
{
    for (int i = 0; i < numPixels; i++)
    {
        MemsetZero(rgba, 4 * sizeof(char));
        for (int p = 0; p < channelsBefore; p++)
        {
            rgba[p] = from[p];
        }
        from += channelsBefore;
        rgba += 4;
    }
}

static void CompressBC4(const unsigned char* src, unsigned char* bc4, int width, int height)
{
    unsigned char r[4 * 4];
    
    for (int i = 0; i < height; i += 4)
    {
        for (int j = 0; j < width; j += 4)
        {
            SmallMemCpy(r +  0, src + ((i + 0) * width) + j, 4);
            SmallMemCpy(r +  4, src + ((i + 1) * width) + j, 4);
            SmallMemCpy(r +  8, src + ((i + 2) * width) + j, 4);
            SmallMemCpy(r + 12, src + ((i + 3) * width) + j, 4);
            stb_compress_bc4_block(bc4, r);
            bc4 += 8;
        }
    }
}

static void CompressBC5(const unsigned char* src, unsigned char* bc5, int width, int height)
{
    unsigned char rg[4 * 4 * 2];
    int width2 = width * sizeof(short);
    
    for (int i = 0; i < height; i += 4)
    {
        for (int j = 0; j < width; j += 4)
        {
            int j2 = (j * 2);
            
            SmallMemCpy(rg +  0, src + ((i + 0) * width2) + j2, 4 * 2);
            SmallMemCpy(rg +  8, src + ((i + 1) * width2) + j2, 4 * 2);
            SmallMemCpy(rg + 16, src + ((i + 2) * width2) + j2, 4 * 2);
            SmallMemCpy(rg + 24, src + ((i + 3) * width2) + j2, 4 * 2);
            stb_compress_bc5_block(bc5, rg);
            bc5 += 16;
        }
    }
}

static void SaveSceneImagesGeneric(Prefab* scene, char* path, int isMobile, AImage* images, int numImages)
{
    if (IsTextureLastVersion(path)) {
        return;
    }
    int currentInfo = 0;
       
    if (numImages == 0) {
        return;
    }
    
    ASSERTR(numImages < 512, return);
    
    uint8_t* isNormalMap = (uint8_t*)rpmalloc((numImages + 7) / 8);
    uint8_t* isMetallicRoughnessMap = (uint8_t*)rpmalloc((numImages + 7) / 8);
    MemsetZero(isNormalMap, (numImages + 7) / 8);
    MemsetZero(isMetallicRoughnessMap, (numImages + 7) / 8);

    AMaterial* materials = scene ? scene->materials : NULL;
    int numMaterials = scene ? scene->numMaterials : 0;
    
    for (int i = 0; i < numMaterials; i++)
    {
        int normalIdx = materials[i].GetNormalTexture().index & 511;
        isNormalMap[normalIdx / 8] |= (1 << (normalIdx % 8));
    }
    
    for (int i = 0; i < numMaterials; i++)
    {
        int baseColorIdx = materials[i].baseColorTexture.index & 511;
        isNormalMap[baseColorIdx / 8] &= ~(1 << (baseColorIdx % 8));
    }

    for (int i = 0; i < numMaterials; i++)
    {
        int mrIdx = materials[i].metallicRoughnessTexture.index & 511;
        isMetallicRoughnessMap[mrIdx / 8] |= (1 << (mrIdx % 8));
        
        int specIdx = materials[i].specularTexture.index & 511;
        isMetallicRoughnessMap[specIdx / 8] |= (1 << (specIdx % 8));
    }
    
    ImageInfo* imageInfos = (ImageInfo*)rpmalloc(numImages * sizeof(ImageInfo));
    uint64_t* currentCompressions = (uint64_t*)rpmalloc(numImages * sizeof(uint64_t));
    uint64_t beforeCompressedSize = 0;
    
    for (int i = 0; i < numImages; i++)
    {
        ImageInfo info = {0, 0, 4, 0};
        int normalBit = (isNormalMap[i / 8] >> (i % 8)) & 1;
        info.isNormal = normalBit;

        int imageInvalid = (images[i].path == NULL || !FileExist(images[i].path));
        
        if ((info.isNormal && isMobile) || imageInvalid)
        {
            imageInfos[currentInfo] = info;
            currentCompressions[currentInfo++] = beforeCompressedSize;
            continue;
        }

        const char* imageFileName = images[i].path;
        int res = stbi_info(imageFileName, &info.width, &info.height, &info.numComp);
        if (!res)
        {
            AX_ERROR("stbi_info failed %s\n", imageFileName);
            info.width = 0;
            info.height = 0;
            info.numComp = 1;
            imageInfos[currentInfo] = info;
            currentCompressions[currentInfo++] = beforeCompressedSize;
            continue;
        }

        currentCompressions[currentInfo] = beforeCompressedSize;
        imageInfos[currentInfo++] = info;
        
        int isBC1 = (isMobile == 0) && (info.numComp == 1);
        int imageSize = (info.width * info.height) >> isBC1;
        
        int isUncompressed = info.width <= 128 && info.height <= 128;
        if (isUncompressed)
        {
            imageSize = info.width * info.height * info.numComp;
            isBC1 = 0;
        }

        if (isMobile && !isUncompressed)
        {
            int numMips = Max32((int)Log2_32((unsigned int)info.width) >> 1u, 1u) - 1;
            while (numMips--)
            {
                info.width  >>= 1;
                info.height >>= 1;
                imageSize += info.width * info.height;
            }
        }
    
        beforeCompressedSize += imageSize;
    }
    
    if (beforeCompressedSize == 0) {
        if (scene) {
            scene->numImages   = 0;
            scene->numTextures = 0;
        }
        rpfree(isNormalMap);
        rpfree(isMetallicRoughnessMap);
        rpfree(imageInfos);
        rpfree(currentCompressions);
        return;
    }
    
    unsigned char* toCompressionBuffer = (unsigned char*)rpmalloc(beforeCompressedSize);
    unsigned char* textureLoadBuffer = (unsigned char*)rpmalloc(!isMobile ? (1024 * 1024) : 0);
    size_t textureLoadBufferCapacity = !isMobile ? (1024 * 1024) : 0;
    
    for (int i = 0; i < numImages; i++)
    {
        ImageInfo info = imageInfos[i];
        const char* imagePath = images[i].path;
        unsigned char* currentCompression = toCompressionBuffer + currentCompressions[i];
        
        if (info.width == 0)
            continue;
        
        unsigned char* stbImage = stbi_load(imagePath, &info.width, &info.height, &info.numComp, 0);
        
        if (stbImage == NULL) {
            AX_WARN("stbi_load failed %s", imagePath);
            stbImage = (unsigned char*)rpmalloc(info.width * info.height * info.numComp);
        }
        
        int imageSize = info.width * info.height;
        size_t requiredSize = imageSize * 4;
        if (requiredSize > textureLoadBufferCapacity)
        {
            textureLoadBuffer = (unsigned char*)rprealloc(textureLoadBuffer, requiredSize);
            textureLoadBufferCapacity = requiredSize;
        }
        
        if (info.width <= 128 && info.height <= 128)
        {
            int dataSize = imageSize * info.numComp;
            SmallMemCpy(currentCompression, stbImage, dataSize);
            stbi_image_free(stbImage);
            continue;
        }
        
        if (isMobile)
        {
            if (info.numComp == 3) MakeRGBA(stbImage, textureLoadBuffer, imageSize, 3);
            if (info.numComp == 2) MakeRGBA(stbImage, textureLoadBuffer, imageSize, 2);
            if (info.numComp == 1) MakeRGBA(stbImage, textureLoadBuffer, imageSize, 1);

            if (info.numComp != 4)
            {
                stbi_image_free(stbImage);
                stbImage = textureLoadBuffer;
                textureLoadBuffer = (unsigned char*)rpmalloc(textureLoadBufferCapacity);
            }

            stbi_image_free(stbImage);
            continue;
        }
        
        int normalBit = (isNormalMap[i / 8] >> (i % 8)) & 1;
        int mrBit = (isMetallicRoughnessMap[i / 8] >> (i % 8)) & 1;
        
        if (normalBit || mrBit)
        {
            if (info.numComp == 3) MakeRGTextureFromRGB(stbImage, imageSize);
            if (info.numComp == 4) MakeRGTextureFromRGBA(stbImage, imageSize);
            imageInfos[i].numComp = 2;

            CompressBC5(stbImage, textureLoadBuffer, info.width, info.height);
            SmallMemCpy(currentCompression, textureLoadBuffer, imageSize);
            stbi_image_free(stbImage);
            continue;
        }
        
        uint32_t numBlocks = (info.width >> 2) * (info.height >> 2);

        if (info.numComp == 1)
        {
            CompressBC4(stbImage, textureLoadBuffer, info.width, info.height);
            imageSize >>= 1;
        }
        else if (info.numComp == 2)
        {
            CompressBC5(stbImage, textureLoadBuffer, info.width, info.height);
        }
        else if (info.numComp == 3)
        {
            MakeRGBA(stbImage, textureLoadBuffer, imageSize, 3);
            stbi_image_free(stbImage);
            stbImage = textureLoadBuffer;
            textureLoadBuffer = (unsigned char*)rpmalloc(textureLoadBufferCapacity);
            CompressDxt5((const uint32_t*)stbImage, (uint64_t*)textureLoadBuffer, numBlocks, info.width);
        }
        else if (info.numComp == 4)
        {
            CompressDxt5((const uint32_t*)stbImage, (uint64_t*)textureLoadBuffer, numBlocks, info.width);
        }
        
        SmallMemCpy(currentCompression, textureLoadBuffer, imageSize);
        stbi_image_free(stbImage);
    }
    
    AFile file = AFileOpen(path, AOpenFlag_WriteBinary);
    AFileWrite(&g_AXTextureVersion, sizeof(int), file, 1);
    AFileWrite(imageInfos, numImages * sizeof(ImageInfo), file, 1);
    
    uint64_t compressedSize = (uint64_t)(beforeCompressedSize * 0.90);
    char* compressedBuffer = (char*)rpmalloc(compressedSize);
    
    struct sdefl s;
    compressedSize = sdeflate(&s, compressedBuffer, compressedSize, toCompressionBuffer, beforeCompressedSize, 7);
    
    uint64_t decompressedSize = beforeCompressedSize;
    AFileWrite(&decompressedSize, sizeof(uint64_t), file, 1);
    AFileWrite(&compressedSize, sizeof(uint64_t), file, 1);
    AFileWrite(compressedBuffer, compressedSize, file, 1);
    
    AFileClose(file, 1);
    
    rpfree(textureLoadBuffer);
    rpfree(isNormalMap);
    rpfree(isMetallicRoughnessMap);
    rpfree(imageInfos);
    rpfree(currentCompressions);
    rpfree(toCompressionBuffer);
    rpfree(compressedBuffer);
}

void LoadSceneImagesGeneric(const char* texturePath, Texture* textures, int numImages)
{
    if (numImages == 0) {
        return;
    }
    AFile file = AFileOpen(texturePath, AOpenFlag_ReadBinary);
    int version = 0;
    AFileRead(&version, sizeof(int), file, 1);
    ASSERT(version == g_AXTextureVersion);
    
    ImageInfo* imageInfos = (ImageInfo*)rpmalloc(numImages * sizeof(ImageInfo));
    
    for (int i = 0; i < numImages; i++)
    {
        AFileRead(&imageInfos[i], sizeof(ImageInfo), file, 1);
    }
    
    uint64_t decompressedSize, compressedSize;
    AFileRead(&decompressedSize, sizeof(uint64_t), file, 1);
    AFileRead(&compressedSize, sizeof(uint64_t), file, 1);
    
    unsigned char* compressedBuffer = (unsigned char*)rpmalloc(compressedSize);
    AFileRead(compressedBuffer, compressedSize, file, 1);
    
    unsigned char* decompressedBuffer = (unsigned char*)rpmalloc(decompressedSize);
    decompressedSize = sinflate(decompressedBuffer, decompressedSize, compressedBuffer, compressedSize);
    
    unsigned char* currentImage = decompressedBuffer;
    
    for (int i = 0; i < numImages; i++)
    {
        ImageInfo info = imageInfos[i];
        if (info.width == 0)
            continue;
        
        int imageSize = info.width * info.height;
        int isBC4 = info.numComp == 1 && (IsAndroid() == 0);
        
        TextureType textureType = TextureType_CompressedR + info.numComp - 1;
        imageSize >>= (int)isBC4;
        
        TexFlags flags = TexFlags_Compressed | TexFlags_MipMap;
        int notCompressed = info.width <= 128 && info.height <= 128;
        if (notCompressed)
        {
            imageSize = info.width * info.height * info.numComp;
            flags = TexFlags_RawData;
            isBC4 = 0;
            switch (info.numComp)
            {
                case 1: textureType = TextureType_R8;    break;
                case 2: textureType = TextureType_RG8;   break;
                case 3: textureType = TextureType_RGB8;  break;
                case 4: textureType = TextureType_RGBA8; break;
                default: 
                    textureType = TextureType_R8; 
                    AX_WARN("texture numComp is undefined, %i", info.numComp);
                    break;
            } 
        }
        Texture imported = rCreateTexture(info.width, info.height, currentImage, textureType, flags);
        textures[i] = imported;
        currentImage += imageSize;
        
        if (IsAndroid() && !notCompressed)
        {
            int mip = MAX((int)Log2((unsigned int)info.width) >> 1, 1) - 1;
            while (mip-- > 0)
            {
                info.width >>= 1;
                info.height >>= 1;
                currentImage += info.width * info.height;
            }
        }
    }
    
    AFileClose(file, 1);
    rpfree(imageInfos);
    rpfree(compressedBuffer);
    rpfree(decompressedBuffer);
}

void CompressSaveImages(char* path, const char** images, int numImages)
{
    #if !AX_GAME_BUILD
    ChangeExtension(path, StringLength(path), "dxt");
    SaveSceneImagesGeneric(NULL, path, 0, (AImage*)images, numImages);
    
    return;
    int len = StringLength(path);
    ChangeExtension(path, len, "astc");
    char* astcPath = (char*)rpmalloc(len + 2);
    MemsetZero(astcPath, len + 2);
    SmallMemCpy(astcPath, path, len + 1);
    
    SaveSceneImagesGeneric(NULL, astcPath, 1, (AImage*)images, numImages);
    rpfree(astcPath);
    #endif
}

void CompressSaveSceneImages(Prefab* scene, char* path)
{
    #if !AX_GAME_BUILD
    AImage* images = scene->images;
    int numImages = scene->numImages;
    
    ChangeExtension(path, StringLength(path), "dxt");
    SaveSceneImagesGeneric(scene, path, 0, images, numImages);
    return;
    
    int len = StringLength(path);
    ChangeExtension(path, len, "astc");
    char* astcPath = (char*)rpmalloc(len + 2);
    MemsetZero(astcPath, len + 2);
    SmallMemCpy(astcPath, path, len + 1);
    
    SaveSceneImagesGeneric(scene, astcPath, 1, images, numImages);
    rpfree(astcPath);
    #endif
}

void LoadSceneImages(char* path, Texture* textures, int numImages)
{
    if (numImages == 0) { textures = NULL; return; }
    #ifdef __ANDROID__
    ChangeExtension(path, StringLength(path), "astc");
    #else
    ChangeExtension(path, StringLength(path), "dxt");
    #endif
    LoadSceneImagesGeneric(path, textures, numImages);
}