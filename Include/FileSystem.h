
/********************************************************************************
*    Purpose: Reading from files and Writing to files                           *
*    Author : Anilcan Gulkaya 2023 anilcangulkaya7@gmail.com github @benanil    *
********************************************************************************/

#ifndef FILE_SYSTEM_INCLUDED
#define FILE_SYSTEM_INCLUDED

#ifdef __ANDROID__
    typedef struct AAsset AAsset; // forward declare Android asset
#endif

#include "Common.h"

#if defined(PLATFORM_WINDOWS) || defined(__ANDROID__)
    #include <stdio.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

enum AOpenFlag_
{
    AOpenFlag_ReadBinary, 
    AOpenFlag_WriteBinary,
    AOpenFlag_ReadText, 
    AOpenFlag_WriteText
};
typedef s32 AOpenFlag;

typedef struct AFile_ {
#ifdef PLATFORM_WINDOWS
    void* file;
#elif defined(__ANDROID__)
    AAsset* asset;
#else
    FILE* file;
#endif
} AFile;


typedef void(*FolderVisitFn)(const char* path, void* data);

// path must be null terminated string
const char* GetFileExtension(const char* path, s32 size);

bool  FileExist         (const char* file); // works fine with file and folders
u64   FileSize          (const char* file);
bool  RemoveFile        (const char* file);
bool  RenameFile        (const char* old , const char* newFile);
void  ChangeExtAndCopy  (const char* path, const char* ext, char* out, u32 outSize);
void  GetBaseDir        (const char* path, char* out);
s32   GetFileNameNoExt  (const char* path, char* out);
bool  FileHasExtension  (const char* path, s32 size  , const char* extension);
s32   ChangeExtension   (      char* path, s32 pathLn, const char* newExt);
char* PathGoBackwards   (      char* path, s32 end   , bool skipSeparator);               // returns pos32er to the end of the new path
s32   CopyFilename      (      char* out , u64 outLen, const char* path, s32 end); // returns length of filename
                        
AFile AFileOpen         (const char* file, AOpenFlag flag);
void  AFileWrite        (const void* src , u64 size  , AFile file, s32 alignment);
void  AFileWriteNamedF32(const char* name, f32 val   , AFile file);
void  AFileWriteNamedI32(const char* name, s32 val   , AFile file);
s32   AFileReadLine     (      char* dst , s32 maxLen, AFile file);
s32   AFileReadI32      (      char* dst , s32 maxLen, AFile file);
void  AFileRead         (      void* dst , u64 size  , AFile file, s32 alignment);
void  AFileSeek         (      long  offs, AFile file);
void  AFileWriteF32     (      f32   val , AFile file);
void  AFileWriteI32     (      s32   val , AFile file);
void  AFileWrites32     (      AFile file, s64 w, bool newLine); 
void  AFileSeekBegin    (      AFile file);
void  AFileClose        (      AFile file);
bool  AFileExist        (      AFile file);
u64   AFileSize         (      AFile file);
                        
char* ReadAllFile       (const char* file, char* buffer, u64 bufferSize);
char* ReadAllFileAlloc  (const char* file);
char* ReadAllText       (const char* file, char* buffer, u64* numCharacters, const char* startText); // startText: if its not null will be added to start of the buffer
char* ReadAllTextAlloc  (const char* file, u64* numCharacters, const char* startText);
void  WriteAllBytes     (const char* file, const char* bytes , u64 size);
void  ACopyFile         (const char* src , const char* dst, char* buffer);
void  NormalizePath     (const char* path, char* outBuff, u32 outSize);
void  AbsolutePath      (const char* path, char* outBuff, s32 bufferSize); // output: workDir/path
bool  CombinePaths      (      char* dst , u64 dstSize  , const char* a, const char* b);
void  FreeAllText       (      char* text);

// returns 0: reading failed, 1: success, 2: not enough buffer
s32   VisitFolder       (const char* path, FolderVisitFn visitFn, void* data, bool recurse);
void  RemoveFolder      (const char* path, void* unused);
bool  HasAnySubdir      (const char* path);
bool  CreateFolder      (const char* folder);
bool  IsFolder          (const char* path);
void  EnsurePath        (const char* path);

void WEnd(AFile file, char* base, char* p);

#if defined(__cplusplus)
}
#endif


#endif  // FILE_SYSTEM_INCLUDED
