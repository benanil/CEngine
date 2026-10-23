
#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_mutex.h>

#include "Include/GLTFParser.h"
#include "Include/FileSystem.h"
#include "Include/BasisBinding.h"
#include "Include/Algorithm.h"
#include "Include/Graphics.h"
#include "Include/Async.h"
#include "Include/Platform.h"

// one image's basis-compress job: resolved path/type up front (pass 1), compressed in
// parallel (pass 2), metadata written back out in original order (pass 3)
typedef struct SceneImageCompressJob_
{
    char srcPath[2048]; // empty means "no source path", write the placeholder cache entry
    char outPath[2048];
    s32  type;
    s32  result; // basis_compress_file return code, filled in by pass 2
} SceneImageCompressJob;

// Caps concurrent image decode+compress, process-wide. basis_compress_file loads the full
// uncompressed source first (stbi_load) - a 4K RGBA8 source alone is 64MB - and this runs
// nested under bundle- and image-level ParallelFor, so an uncapped batch can hold gigabytes
// of source buffers at once and exhaust the TLSF heap (fixed 2GB).
static SDL_Semaphore* gImageCompressSlots;
static SDL_SpinLock    gImageCompressSlotsInitLock;

static SDL_Semaphore* GetImageCompressSlots(void)
{
    SDL_LockSpinlock(&gImageCompressSlotsInitLock);
    if (!gImageCompressSlots) {
        // each slot can hold tens of MB, stay  conservative
        u32 cap = Clampu32((u32)SDL_GetNumLogicalCPUCores(), 1u, 8u);
        gImageCompressSlots = SDL_CreateSemaphore(cap);
    }
    SDL_Semaphore* sem = gImageCompressSlots;
    SDL_UnlockSpinlock(&gImageCompressSlotsInitLock);
    return sem;
}

static void SceneImageCompressRun(SceneImageCompressJob* job)
{
    if (job->srcPath[0] == '\0') return;
    const s32 quality = 0; // 100 is max
    SDL_Semaphore* slots = GetImageCompressSlots();
    SDL_WaitSemaphore(slots);
    // multithreaded=false: ParallelFor already compresses many images at once, so each call
    // stays single threaded to avoid every call fighting over all cores.
    job->result = basis_compress_file(job->srcPath, job->outPath, job->type, 16, quality, -1, false);
    SDL_SignalSemaphore(slots);
}

static void SceneImageCompressRange(u32 begin, u32 end, void* userData)
{
    SceneImageCompressJob* jobs = (SceneImageCompressJob*)userData;
    for (u32 i = begin; i < end; i++)
        SceneImageCompressRun(&jobs[i]);
}

void SaveSceneImages(SceneBundle* scene, const char* savePath, bool deleteRemaining)
{
    char pathBuf[2048], baseDir[2048], name[512], bdcPath[2048], tmpPath[2048];
    u32 numImages = scene->numImages > 0 ? (u32)scene->numImages : 0u;
    if (numImages == 0) return;
    // .bdc path
    s32 len = StringLengthSafe(savePath, sizeof(bdcPath));
    SmallMemCpy(bdcPath, savePath, len);
    ChangeExtension(bdcPath, len, "bdc");
    s32 bdcLen = StringLengthSafe(bdcPath, sizeof(bdcPath));
    SmallMemCpy(tmpPath, bdcPath, bdcLen);
    SmallMemCpy(tmpPath + bdcLen, ".tmp", 5);

    AFile file = AFileOpen(tmpPath, AOpenFlag_WriteText);
    GetBaseDir(savePath, baseDir);

    // write count
    s32 n = IntToString(pathBuf, scene->numImages, 0);
    pathBuf[n++] = '\n';
    AFileWrite(pathBuf, n, file, 1);

    basis_encoder_init();

    SceneImageCompressJob* jobs =
        (SceneImageCompressJob*)SDL_calloc(Maxu32(numImages, 1u), sizeof(SceneImageCompressJob));

    // Pass 1 (serial, cheap): resolve each image's source/output path and normal/MR type -
    // just string building and material scans, no compression happens here yet.
    for (s32 i = 0; i < scene->numImages; i++)
    {
        SceneImageCompressJob* job = &jobs[i];
        const char* src = scene->images[i].path;
        if (src == NULL || src[0] == '\0')
        {
            job->srcPath[0] = '\0';
            continue;
        }

        s32 srcLen = (s32)StringLengthSafe(src, sizeof(job->srcPath) - 1);
        SmallMemCpy(job->srcPath, src, srcLen);
        job->srcPath[srcLen] = '\0';

        // build output path = baseDir + name + ".basis"
        s32 baseLen = (s32)StringLengthSafe(baseDir, sizeof(baseDir));
        SmallMemCpy(job->outPath, baseDir, baseLen);

        s32 nameLen = GetFileNameNoExt(job->srcPath, name);
        SmallMemCpy(job->outPath + baseLen, name, nameLen);
        SmallMemCpy(job->outPath + baseLen + nameLen, ".basis", 7);

        bool isNormal = false;
        bool isMR     = false; // metallic roughness

        for (s32 j = 0; j < scene->numMaterials; j++)
        {
            AMaterial m = scene->materials[j];

            if (m.textures[0].index < scene->numTextures)
                isNormal |= (scene->textures[m.textures[0].index].source == i);

            if (m.metallicRoughnessTexture.index < scene->numTextures)
                isMR |= (scene->textures[m.metallicRoughnessTexture.index].source == i);

            if (m.specularTexture.index < scene->numTextures)
                isMR |= (scene->textures[m.specularTexture.index].source == i);
        }

        // baseColor wins -> it's neither normal nor MR
        for (s32 j = 0; j < scene->numMaterials; j++)
        {
            AMaterial m = scene->materials[j];

            if (m.baseColorTexture.index < scene->numTextures &&
                scene->textures[m.baseColorTexture.index].source == i)
            {
                isNormal = false;
                isMR     = false;
                break;
            }
        }

        job->type = (isNormal ? 1 : 0) | (isMR ? 2 : 0);
        AX_LOG("output path: %s", job->outPath);
    }

    // Pass 2 (parallel): compress every resolved image at once instead of one at a time.
    ParallelFor(numImages, 1u, SceneImageCompressRange, jobs);

    // Pass 3 (serial): write the .bdc metadata in original order, same format as before.
    for (s32 i = 0; i < scene->numImages; i++)
    {
        SceneImageCompressJob* job = &jobs[i];
        if (job->srcPath[0] == '\0')
        {
            AX_WARN("scene image has no path, writing empty cache entry index:%d", i);
            pathBuf[0] = '\n';
            AFileWrite(pathBuf, 1, file, 1);
            pathBuf[0] = '0';
            pathBuf[1] = '\n';
            AFileWrite(pathBuf, 2, file, 1);
            continue;
        }

        if (job->result) AX_WARN("Failed: %s (%d)\n", job->srcPath, job->result);

        s32 l = (s32)StringLengthSafe(job->srcPath, sizeof(pathBuf) - 2);
        SmallMemCpy(pathBuf, job->srcPath, l);
        pathBuf[l++] = '\n';
        AFileWrite(pathBuf, l, file, 1);

        n = IntToString(pathBuf, job->type, 0);
        pathBuf[n++] = '\n';
        AFileWrite(pathBuf, n, file, 1);

        if (deleteRemaining) RemoveFile(job->srcPath);
    }

    SDL_free(jobs);
    AFileClose(file);
    RemoveFile(bdcPath);
    RenameFile(tmpPath, bdcPath);
}

typedef struct ScaneImgCompTaskData_
{
    SceneBundle* scene; 
    char* savePath; 
    bool deleteRemaining;
} ScaneImgCompTaskData;

static s32 SaveSceneImagesTask(void* userData)
{
    ScaneImgCompTaskData* taskData = (ScaneImgCompTaskData*)userData;
    SaveSceneImages(taskData->scene, taskData->savePath, taskData->deleteRemaining);
    SDL_free(taskData->savePath);
    SDL_free(taskData);
    return 1;
}

// unusded for now 
void SaveSceneImagesAsync(SceneBundle* scene, const char* path, bool deleteRemaining, AsyncCallback callback)
{
    if (!scene || !path || path[0] == '\0')
    {
        AX_WARN("Texture compress task invalid arguments");
        return;
    }

    ScaneImgCompTaskData* taskData = (ScaneImgCompTaskData*)SDL_malloc(sizeof(ScaneImgCompTaskData));
    if (!taskData)
    {
        AX_WARN("Texture compress task allocation failed");
        return;
    }

    int newLen = StringLength(path);
    char* savePath = (char*)SDL_malloc(newLen + 1);
    if (!savePath)
    {
        SDL_free(taskData);
        AX_WARN("Texture compress path allocation failed");
        return;
    }
    MemCopy(savePath, path, newLen + 1);
    *taskData = (ScaneImgCompTaskData){
        .scene = scene,
        .savePath = savePath,
        .deleteRemaining = deleteRemaining
    };
    AsyncRun("SaveSceneImages", SaveSceneImagesTask, callback, taskData);
}

typedef struct SceneImageLoadJob_
{
    Texture* dst;
    char     basisPath[2048];
    s32      textureType;
    u8       failed; // set by SceneImageLoadJobRun; index-owned, safe to read back without a race
} SceneImageLoadJob;

// worker thread: the actual slow part - its own SDL_GPU command buffer/create/submit/fence-wait,
// which is exactly the multithreading model SDL_GPU supports (each thread its own command buffer)
static void SceneImageLoadJobRun(void* userData)
{
    SceneImageLoadJob* job = (SceneImageLoadJob*)userData;

    u64 size = FileSize(job->basisPath);
    void* mem = SDL_malloc(size);
    if (!mem || size == 0)
    {
        const char* reason = size == 0 ? "BasisFileNotExist" : "OSAllocFailed";
        AX_WARN("%s fileSize:%llu, path:%s", reason, size, job->basisPath);
        *job->dst = rCreateTexture(32, 32, NULL, TEX_FMT_8UNORM1, 0, TEX_SAMPLER, "BasisNotFound");
        job->failed = 1;
        return;
    }

    void* basisData = ReadAllFile(job->basisPath, mem, size);
    s32 isNormal           = job->textureType & 1;
    s32 isMetallicRoughness = (job->textureType >> 1) & 1;

    job->dst->buffer     = basisData;
    job->dst->bufferSize = size;
    job->dst->handle = BasisuMakeImage(basisData, size, &job->dst->width, &job->dst->height, &job->dst->format,
                                       &job->dst->mipLevels, (u8)isNormal, (u8)isMetallicRoughness);

    if (!job->dst->handle)
    {
        AX_WARN("basis gpu texture creation failed size:%llu dimensions:%dx%d path:%s",
                size, job->dst->width, job->dst->height, job->basisPath);
        SDL_free(mem);
        *job->dst = rCreateTexture(32, 32, NULL, TEX_FMT_8UNORM1, 0, TEX_SAMPLER, "BasisNotFound");
        job->failed = 1;
    }
}

static void SceneImageLoadRange(u32 begin, u32 end, void* userData)
{
    SceneImageLoadJob* jobs = (SceneImageLoadJob*)userData;
    for (u32 j = begin; j < end; j++)
        SceneImageLoadJobRun(&jobs[j]);
}

// result: 1 fine, 2 some file is not exist, 3 not enough images for scene
s32 LoadSceneImages(const char* texturePath, Texture* textures, s32 numImages)
{
    if (numImages <= 0)
        return 1;

    AFile file = AFileOpen(texturePath, AOpenFlag_ReadBinary);
    if (!AFileExist(file))
        return 0;

    char buffer[2048];
    char typeBuffer[64];
    char baseDir[2048];
    char fileName[512];
    GetBaseDir(texturePath, baseDir);
    s32 baseLen = (s32)StringLengthSafe(baseDir, sizeof(baseDir));

    s32 result = 1;
    s32 fileNumImages = AFileReadI32(buffer, sizeof(buffer), file); // First line: numImages
    if (fileNumImages != numImages)
    {
        AX_WARN("basis file num images not equal to requested numImages. file: %d, requested: %d", fileNumImages, numImages);
        numImages = fileNumImages;
        result = 3;
    }

    // Pass 1 (serial - metadata is one sequential text stream): resolve each image's basis path.
    // SDL_malloc'd, not arena - this can run on a worker thread, arenas are a per-thread bump pointer.
    SceneImageLoadJob* jobs = (SceneImageLoadJob*)SDL_malloc((size_t)numImages * sizeof(SceneImageLoadJob));
    u32 numJobs = 0u;

    for (s32 i = 0; i < numImages; i++)
    {
        s32 pathLen = AFileReadLine(buffer, sizeof(buffer), file);
        s32 textureType = AFileReadI32(typeBuffer, sizeof(typeBuffer), file);
        textures[i].type     = (u32)textureType;
        textures[i].channels = (textureType & 3u) ? 2u : 4u;

        if (pathLen <= 0)
        {
            AX_WARN("basis metadata ended early index:%d, file:%s", i, texturePath);
            textures[i] = rCreateTexture(32, 32, NULL, TEX_FMT_8UNORM1, 0, TEX_SAMPLER, "BasisNoMetadata");
            result = 3;
            continue;
        }

        GetFileNameNoExt(buffer, fileName);
        s32 nameLen = (s32)StringLengthSafe(fileName, sizeof(fileName));
        if (nameLen <= 0)
        {
            AX_WARN("basis metadata has empty image path index:%d, file:%s", i, texturePath);
            textures[i] = rCreateTexture(32, 32, NULL, TEX_FMT_8UNORM1, 0, TEX_SAMPLER, "BasisNoMetadata");
            result = 3;
            continue;
        }

        SceneImageLoadJob* job = &jobs[numJobs];
        if (baseLen + nameLen + 7 > (s32)sizeof(job->basisPath))
        {
            AX_WARN("basis path too long index:%d, file:%s", i, texturePath);
            textures[i] = rCreateTexture(32, 32, NULL, TEX_FMT_8UNORM1, 0, TEX_SAMPLER, "BasisNoMetadata");
            result = 2;
            continue;
        }

        SmallMemCpy(job->basisPath, baseDir, baseLen);
        SmallMemCpy(job->basisPath + baseLen, fileName, nameLen);
        SmallMemCpy(job->basisPath + baseLen + nameLen, ".basis", 7);
        job->dst         = &textures[i];
        job->textureType = textureType;
        job->failed      = 0;
        numJobs++;
    }
    AFileClose(file);

    // Pass 2 (parallel): fan every resolved image out via ParallelFor.
    ParallelFor(numJobs, 1u, SceneImageLoadRange, jobs);

    u32 failedCount = 0u;
    for (u32 j = 0; j < numJobs; j++)
        if (jobs[j].failed) { result = 2; failedCount++; }

        // Direct "this bundle will show placeholders" signal - one line per bundle instead of
        // hunting through the per-image warnings above for how many actually failed.
        u32 skippedInMetadata = (u32)numImages - numJobs;
        if (failedCount > 0 || skippedInMetadata > 0)
            AX_WARN("LoadSceneImages: %s -> %d/%d images failed to load (%d bad metadata, %d decode/GPU failures)",
                    texturePath, failedCount + skippedInMetadata, numImages, skippedInMetadata, failedCount);

        SDL_free(jobs);
        return result;
}

// loads the cached basis images of a gltf into a bundle local staging array
s32 LoadBundleImagesFromCache(const char* gltfPath, SceneBundle* bundle, Texture* staging)
{
    char path[1024];
    int pathLen = StringLength(gltfPath);
    MemCopy(path, gltfPath, pathLen + 1);
    ChangeExtension(path, pathLen, "bdc");
    if (!FileExist(path)) SaveSceneImages(bundle, path, false);
    if (!FileExist(path)) return 1; // not saved
    return LoadSceneImages(path, staging, bundle->numImages);
}