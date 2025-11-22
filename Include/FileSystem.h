
/********************************************************************************
*    Purpose: Reading from files and Writing to files                           *
*    Author : Anilcan Gulkaya 2023 anilcangulkaya7@gmail.com github @benanil    *
********************************************************************************/

#ifndef FILE_SYSTEM_INCLUDED
#define FILE_SYSTEM_INCLUDED
// AFile  AFileOpen(const char* fileName, AOpenFlag flag);
// void   AFileRead(void* dst, uint64_t size, AFile file);
// void   AFileWrite(const void* src, uint64_t size, AFile file);
// void   AFileClose(AFile file);
// bool   AFileExist(AFile file);
// uint64 AFileSize(AFile file);

// char*  GetFileExtension(path, size);
// bool   FileHasExtension(path, size, extension);
// char*  PathGoBackwards (path, end, skipSeparator);
// int    CopyFilename(char* out, char* path, int end); // < returns number of characters in filename
// bool   FileExist(file);
// uint64 FileSize(file);
// bool   RenameFile(oldFile, newFile);
// bool   MoveFile(oldLocation, newLocation);
// bool   RemoveFile(file); // < deletes the file
// void   RemoveFolder(path, unused); // < deletes the folder
// char*  ReadAllFile(fileName, buffer, numCharacters);
// void   CopyFile(source, dst, buffer);
// void   VisitFolder(path, visitFn);
// void   GetCurrentDirectory(buffer, bufferSize);
// void   AbsolutePath(path, outBuffer, bufferSize);
// void   CombinePaths(dst, a, b);
// bool   HasAnySubdir(path);

#include "Memory.h"
#include <stdio.h>
#include <sys/stat.h>

#ifdef _WIN32
    #include <io.h>
    #include <direct.h> 
    #include <windows.h>
    #include "../Extern/dirent.h"
    #define F_OK 0
#else 
    #include <unistd.h>
    #include <sys/types.h>
    #include <fcntl.h>
    #include <dirent.h>
    #define _rmdir rmdir
    #define _mkdir mkdir
    #define _fileno fileno
    #define _filelengthi64 filelength
#endif

#ifdef __ANDROID__
#include <android/asset_manager.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

extern "C" android_app* g_android_app;
#endif

#ifdef _WIN32
#define ASTL_FILE_SEPERATOR ('\\')
#else
#define ASTL_FILE_SEPERATOR ('/');
#endif

// path must be null terminated string
static inline const char* GetFileExtension(const char* path, int size)
{
    while (path[size-1] != '.' && size > 0)
        size--;
    return path + size;
}

static inline int ChangeExtension(char* path, int pathLen, const char* newExt)
{
    int lastDot = pathLen - 1, oldLen = 0;
    while (path[lastDot - 1] != '.' && lastDot >= 0)
        lastDot--, oldLen++;

    int i = lastDot, newLen = 0;
    for (; *newExt; i++)
        path[i] = *newExt++, newLen++;
    path[i++] = '\0';
    // clean the right with zeros
    while (i < pathLen)
        path[i++] = '\0';
    return pathLen + (oldLen - newLen);
}

static inline bool FileHasExtension(const char* path, int size, const char* extension)
{
    int extLen = 0;
    // go to end of extension
    while (extension[1] != 0)
        extension++, extLen++;
    
    if (size <= extLen) return false;
    
    for (int i = 0; i <= extLen; i++)
    {
        if (*extension-- != path[--size])
            return false;
    }
    return true;
}

// returns pointer to the end of the new path
static inline char* PathGoBackwards(char* path, int end, bool skipSeparator)
{
    if (path[end-1] == '/' || path[end-1] == '\\')
        path[--end] = '\0';
        
    while (end >= 0 && (path[end] != '/' && path[end] != '\\')) {
        path[end--] = '\0'; // Null-terminate the string.
    }
    
    if (skipSeparator && end >= 0) {
        path[end--] = '\0'; // Null-terminate again to remove the separator.
    }

    return path + end + 1; // Return the new starting point of the path.
}

// returns length of filename
static inline int CopyFilename(char* out, const char* path, int end)
{
    int numChars = 0;
    while (end >= 0 && (path[end] != '/' && path[end] != '\\')) {
        out[numChars++] = path[end--]; // Null-terminate the string.
    }
    
    // reverse
    for (int i = 0, j = numChars-1; i < j; i++, j--) {
        char t = out[i];
        out[i] = out[j];
        out[j] = t;
    }

    return numChars; // Return the new starting point of the path.
}

// these functions works fine with file and folders
static inline bool FileExist(const char* file)
{
#ifdef __ANDROID__
    AAsset* asset = AAssetManager_open(g_android_app->activity->assetManager, file, 0);
    if (asset == nullptr) {
        return false;
    } else {
        AAsset_close(asset);
        return true;
    }
#elif defined(_WIN32)
    DWORD attr = GetFileAttributesA(file);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    return access(file, F_OK) == 0;
#endif
}

static inline uint64_t FileSize(const char* file)
{
    #ifdef __ANDROID__
    AAsset* asset = AAssetManager_open(g_android_app->activity->assetManager, file, 0);
    if (asset) {
        off64_t sz = AAsset_getLength64(asset);
        AAsset_close(asset);
        return (uint64_t)sz;
    }
    return 0;

    #elif defined(_WIN32)
    struct _stat64 sb;
    if (_stat64(file, &sb) != 0)
        return 0;
    return (uint64_t)sb.st_size;

    #else
    FILE* fp = fopen(file, "rb");
    if (!fp)
        return 0;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }

    long sz = ftell(fp);
    fclose(fp);

    if (sz < 0)
        return 0;

    return (uint64_t)sz;
    #endif
}

static inline bool RenameFile(const char* oldFile, const char* newFile)
{
    return rename(oldFile, newFile) != 0;
}

static inline bool CreateFolder(const char* folderName) 
{
    return _mkdir(folderName
               #ifndef _WIN32
               , 0777
               #endif 
               ) == 0;
}

static inline bool RemoveFile(const char* file) {
    return remove(file);
}

static inline bool IsDirectory(const char* path)
{
  struct stat file_info; 
  return stat(path, &file_info) == 0 && (S_ISDIR(file_info.st_mode));
}

enum AOpenFlag_
{
    AOpenFlag_ReadBinary, 
    AOpenFlag_WriteBinary,
    AOpenFlag_ReadText, 
    AOpenFlag_WriteText
};
typedef int AOpenFlag;

#ifdef __ANDROID__
struct AFile {
    AAsset* asset;
};

static inline AFile AFileOpen(const char* fileName, AOpenFlag flag) {
    return { AAssetManager_open(g_android_app->activity->assetManager, fileName, 0) };
}

static inline void AFileRead(void* dst, uint64_t size, AFile file, int alignment = 0) {
    AAsset_read(file.asset, dst, size);
}

static inline void AFileSeekBegin(AFile file) {
    AAsset_seek(file.asset, 0, SEEK_SET);
}

static inline void AFileSeek(long offset, AFile file) {
    AAsset_seek(file.asset, offset, SEEK_CUR);
}

static inline void AFileWrite(const void* src, uint64_t size, AFile file, int alignment = 0)
{ }

static inline void AFileClose(AFile file) {
    AAsset_close(file.asset);
}

static inline bool AFileExist(AFile file) {
    return file.asset != nullptr;
}

static inline uint64_t AFileSize(AFile file) {
    return AAsset_getLength(file.asset);
}

static inline int AFileReadLine(char* dst, int maxLen, AFile file) {
    if (maxLen <= 0 || !dst || !file.asset) return 0;
    
    int i = 0;
    char c;
    
    while (i < maxLen - 1) {
        if (AAsset_read(file.asset, &c, 1) != 1)
            break;
        dst[i++] = c;
        if (c == '\n') break;
    }
    
    dst[i] = '\0';
    return i;
}

#else

typedef struct AFile_ {
    #ifdef _MSC_VER
    HANDLE file;
    #else
    FILE* file;
    #endif
} AFile;

static inline AFile AFileOpen(const char* fileName, AOpenFlag flag)
{
    AFile afile = {0};

    #ifdef _MSC_VER
    DWORD access = GENERIC_READ, creation = OPEN_EXISTING;

    switch (flag) {
        case AOpenFlag_WriteBinary: 
        case AOpenFlag_WriteText:
            access = GENERIC_WRITE;
            creation = CREATE_ALWAYS;
        break;
    }

    afile.file = CreateFileA(fileName, access, FILE_SHARE_READ, NULL, creation, FILE_ATTRIBUTE_NORMAL, NULL);

    if (afile.file == INVALID_HANDLE_VALUE)
        afile.file = NULL;

    #else
    const char* modes[4] = { "rb", "wb", "r", "w" };
    afile.file = fopen(fileName, modes[flag]);
    #endif

    unsigned char bom[] = {0xEF, 0xBB, 0xBF};

    #ifndef _MSC_VER
    if (flag == AOpenFlag_WriteText && afile.file) {
        fwrite(bom, 1, sizeof(bom), afile.file);
    }
    #else
    if (flag == AOpenFlag_WriteText && afile.file) {
        DWORD written;
        WriteFile(afile.file, bom, sizeof(bom), &written, NULL);
    }
    #endif

    return afile;
}

static inline void AFileRead(void* dst, uint64_t size, AFile file, int alignment) {
    #ifndef _MSC_VER
    fread(dst, alignment, size, file.file);
    #else
    ReadFile(file.file, dst, (DWORD)(size * alignment), NULL, NULL);
    #endif
}

static inline void AFileWrite(const void* src, uint64_t size, AFile file, int alignment) {
    #ifndef _MSC_VER
    fwrite(src, alignment, size, file.file);
    #else
    WriteFile(file.file, src, (DWORD)(size * alignment), NULL, NULL);
    #endif
}

static inline void AFileSeekBegin(AFile file) {
    #ifndef _MSC_VER
    fseek(file.file, 0, SEEK_SET);
    #else
    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    SetFilePointerEx(file.file, pos, NULL, FILE_BEGIN);
    #endif
}

static inline void AFileSeek(long offset, AFile file) {
    #ifndef _MSC_VER
    fseek(file.file, offset, SEEK_CUR);
    #else
    LARGE_INTEGER pos;
    pos.QuadPart = offset;
    SetFilePointerEx(file.file, pos, NULL, FILE_CURRENT);
    #endif
}

static inline void AFileClose(AFile file) {
    #ifndef _MSC_VER
    fclose(file.file);
    #else
    CloseHandle(file.file);
    #endif
}

static inline bool AFileExist(AFile file) {
    #ifndef _MSC_VER
    return file.file != NULL;
    #else
    return file.file && file.file != INVALID_HANDLE_VALUE;
    #endif
}

// Returns the number of characters read (excluding null terminator), or 0 on EOF/error
// Line includes the newline character if present
static inline int AFileReadLine(char* dst, int maxLen, AFile file) 
{
    if (maxLen <= 0 || !dst) return 0;
    int i = 0;
    char c;
    
    #ifdef _MSC_VER
    DWORD bytesRead;
    while (i < maxLen - 1) {
        if (!ReadFile(file.file, &c, 1, &bytesRead, NULL) || bytesRead == 0)
            break;
        dst[i++] = c;
        if (c == '\n') break;
    }
    #else
    int ch;
    while (i < maxLen - 1) {
        ch = fgetc(file.file);
        if (ch == EOF) break;
        dst[i++] = (char)ch;
        if (ch == '\n') break;
    }
    #endif
    
    dst[i] = '\0';
    return i;
}

static inline uint64_t AFileSize(AFile file)
{
    #ifdef _WIN32
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file.file, &size))
        return 0;
    return (uint64_t)size.QuadPart;
    #elif defined(__ANDROID__)
    if (file.asset == NULL) return 0;
    return AAsset_getLength(file.asset);
    #else
    struct stat sb;
    if (stat(file.file, &sb) != 0)
        return 0;
    return (uint64_t)sb.st_size;
    #endif  
}

#endif

static inline char* ReadAllFile(const char* fileName, char** buffer)
{
    AFile file = AFileOpen(fileName, AOpenFlag_ReadBinary);
    if (!AFileExist(file))
        return NULL;

    uint64_t fileSize = AFileSize(file);

    if (buffer == NULL || *buffer == NULL) {
        *buffer = rpcalloc(fileSize + 1, 1); // +1 for null terminator
    }

    AFileRead(*buffer, fileSize, file, 1);
    AFileClose(file);
    return *buffer;
}

// don't forget to free using FreeAllText
// fileName      : path of the file that we want to load
// buffer        : is pre allocated memory if exist. otherwise null
// numCharacters : if not null returns length of the imported string
// startText     : if its not null will be added to start of the buffer
// note: if you define it you are responsible of deleting the buffer
static inline char* ReadAllText(const char* fileName, char** buffer, uint64_t* numCharacters, const char* startText)
{
    int startTextLen = 0;
    if (startText) 
        while (startText[startTextLen]) startTextLen++;

    AFile file = AFileOpen(fileName, AOpenFlag_ReadBinary);
    
    if (!AFileExist(file)) {
        perror("Error opening the file");
        return NULL; // Return an error code
    }
    
    // Determine the file size
    uint64_t file_size = AFileSize(file);

    // Allocate memory to store the entire file
    if (buffer == NULL || *buffer == NULL)
        *buffer = rpcalloc(file_size + 40 + startTextLen, 1); // +1 for null terminator
    
    if (file_size >= 3)
    {
        AFileRead(*buffer, 3, file, 1);
        bool isBOM = (*buffer)[0] == '\xEF' && (*buffer)[1] == '\xBB' && (*buffer)[2] == '\xBF';
        if (!isBOM) {
            AFileSeekBegin(file);
        }
    }

    if (startText) 
        while (*startText) *(*buffer)++ = *startText++;
    
    if (buffer == NULL || *buffer == NULL) {
        AFileClose(file);
        return NULL; // Return an error code
    }

    // Read the entire file into the buffer
    AFileRead(*buffer, file_size, file, 1);
    (*buffer)[file_size] = '\0'; // Null-terminate the buffer
    AFileClose(file);
    if (numCharacters) 
        *numCharacters = (uint64_t)file_size + 1;
    return (*buffer) - startTextLen;
}

static inline void FreeAllText(char* text)
{
    rpfree(text);
}

static inline void WriteAllBytes(const char *filename, const char *bytes, unsigned long size) 
{
    // Open the file for writing in binary mode
    AFile file = AFileOpen(filename, AOpenFlag_WriteBinary);
    if (!AFileExist(file)) 
    {
        perror("Failed to open file for writing");
        return;
    }

    AFileWrite(bytes, size, file, 1);

    AFileClose(file);
}

// buffer is pre allocated memory if exist. otherwise null. 
// note: if you define it you are responsible of deleting the buffer
static inline void ACopyFile(const char* source, const char* dst, char* buffer)
{
    uint64_t sourceSize = 0;
    bool bufferProvided = buffer != 0;
    char* sourceFile = ReadAllText(source, &buffer, &sourceSize, NULL);
    
    AFile dstFile = AFileOpen(dst, AOpenFlag_WriteBinary);
    AFileWrite(sourceFile, sourceSize, dstFile, 1);
    AFileClose(dstFile);

    if (!bufferProvided) rpfree(buffer);
}

#ifndef _WIN32
    #define GetCurrentDirectory(size, outPath) getcwd(outPath, size)
#endif

// input: ../Textures/Tree.png
// output: C:/Source/Repos/Textures/Tree.png
static inline void AbsolutePath(const char* path, char* outBuffer, int bufferSize)
{
    GetCurrentDirectory(bufferSize, outBuffer);
    int currLength = 0;

    const char* curr = outBuffer;
    while (*curr++) currLength++; // strlen

    while (*path)
    {
        if (path[0] == '.' && path[1] == '.')
        {
            const char* before = outBuffer + currLength;
            const char* newEnd = PathGoBackwards(outBuffer, currLength, true);
            currLength -= (int)(before - newEnd);
            path += 3; // skip two dot and seperator
            outBuffer[currLength++] = ASTL_FILE_SEPERATOR;
        }
        else 
        {
            while (*path && *path != '\\' && *path != '/')
                outBuffer[currLength++] = *path++;
            outBuffer[currLength++] = *path++;
        }
    }
    outBuffer[currLength] = '\n';
}

static inline void CombinePaths(char* dst, const char* a, const char* b)
{
    int parentLen = StringLength(a);
    SmallMemCpy(dst, a, parentLen);
    dst[parentLen] = ASTL_FILE_SEPERATOR;
    SmallMemCpy(dst + parentLen + 1, b, StringLength(b));
}

// data is user defined, whatever data you need, and nullable
typedef void(*FolderVisitFn)(const char* path, void* data);

// thanks to https://github.com/tronkko/dirent this is windows and linux compatible
static inline bool VisitFolder(const char* path, FolderVisitFn visitFn, void* data)
{
    DIR* dir;
    dirent* ent;
    if ((dir = opendir(path)) != NULL) 
    {
        char combined[512];
        while ((ent = readdir(dir)) != NULL)  
        {
            MemsetZero(combined, 512);
            CombinePaths(combined, path, ent->d_name);
            if (ent->d_name[0] != '.') visitFn(combined, data);
        }
        closedir(dir);
        return true;
    }
    return false;
}

static inline bool HasAnySubdir(const char* path)
{
    DIR* dir;
    dirent* ent;
    if ((dir = opendir(path)) != NULL) 
    {
        char combined[512];
        int pathLen = StringLength(path);
        while ((ent = readdir(dir)) != NULL)  
        {
            MemsetZero(combined, 512);
            ASSERT(pathLen + StringLength(ent->d_name) < 511);
            CombinePaths(combined, path, ent->d_name);
            if (ent->d_name[0] != '.' && IsDirectory(combined)) return true;
        }
        closedir(dir);
    }
    return false;
}

static inline void RemoveFolder(const char* path, void* unused) 
{
    VisitFolder(path, RemoveFolder, NULL); // recursive delete all subfolders and files
    if (IsDirectory(path)) {
        _rmdir(path);
    }
    else {
        RemoveFile(path);
    }
}


#endif  // FILE_SYSTEM_INCLUDED
