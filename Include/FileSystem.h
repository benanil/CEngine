
/********************************************************************************
*    Purpose: Reading from files and Writing to files                           *
*    Author : Anilcan Gulkaya 2023 anilcangulkaya7@gmail.com github @benanil    *
********************************************************************************/

#ifndef FILE_SYSTEM_INCLUDED
#define FILE_SYSTEM_INCLUDED

#ifdef _WIN32
    #define ASTL_FILE_SEPERATOR ('\\')
    typedef void* HANDLE; // forward declare Windows handle
#else
    #define ASTL_FILE_SEPERATOR ('/')
    int GetCurrentDirectory(int size, char* outPath);
#endif

#ifdef __ANDROID__
    typedef struct AAsset AAsset; // forward declare Android asset
#endif

#include <stdint.h>  // uint64_t
#include <stdbool.h> // bool

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
typedef int AOpenFlag;

typedef struct AFile_ {
#ifdef _MSC_VER
    HANDLE file;
#elif defined(__ANDROID__)
    AAsset* asset;
#else
    struct FILE FILE; // forward declare FILE
#endif
} AFile;


typedef void(*FolderVisitFn)(const char* path, void* data);

// path must be null terminated string
const char* GetFileExtension(const char* path, int size);

int      ChangeExtension (      char* path, int pathLen, const char* newExt);
bool     FileHasExtension(const char* path, int size   , const char* extension);
char*    PathGoBackwards (      char* path, int end, bool skipSeparator);               // returns pointer to the end of the new path
int      CopyFilename    (      char* out, uint64_t outLen, const char* path, int end); // returns length of filename
bool     FileExist       (const char* file); // works fine with file and folders
uint64_t FileSize        (const char* file);
bool     RenameFile      (const char* oldFile, const char* newFile);
bool     RemoveFile      (const char* file);

AFile    AFileOpen       (const char* fileName, AOpenFlag flag);
void     AFileWrite      (const void* src, uint64_t size, AFile file, int alignment);
void     AFileRead       (      void* dst, uint64_t size, AFile file, int alignment);
void     AFileSeekBegin  (      AFile file);
void     AFileSeek       (      long offset, AFile file);
void     AFileClose      (      AFile file);
bool     AFileExist      (      AFile file);
int      AFileReadLine   (      char* dst, int maxLen, AFile file);
int      AFileReadI32    (      char* dst, int maxLen, AFile file);
uint64_t AFileSize       (      AFile file);

char*    ReadAllFile     (const char* fileName, char* buffer, uint64_t bufferSize);
char*    ReadAllFileAlloc(const char* fileName);
char*    ReadAllText     (const char* fileName, char* buffer, uint64_t* numCharacters, const char* startText); // startText: if its not null will be added to start of the buffer
char*    ReadAllTextAlloc(const char* fileName, uint64_t* numCharacters, const char* startText);
void     WriteAllBytes   (const char *filename, const char *bytes, unsigned long size);
void     FreeAllText     (      char* text);

// buffer is pre allocated memory if exist. otherwise null. 
// note: if you define it you are responsible of deleting the buffer
void     ACopyFile       (const char* source, const char* dst, char* buffer);

// input: ../Textures/Tree.png
// output: C:/Source/Repos/Textures/Tree.png
void     AbsolutePath    (const char* path, char* outBuffer, int bufferSize);
bool     CombinePaths    (      char* dst , uint64_t dstSize, const char* a, const char* b);

bool     VisitFolder     (const char* path, FolderVisitFn visitFn, void* data);
bool     HasAnySubdir    (const char* path);
void     RemoveFolder    (const char* path, void* unused);
bool     CreateFolder    (const char* folderName);
bool     IsFolder        (const char* path);


#if defined(__cplusplus)
}
#endif


#endif  // FILE_SYSTEM_INCLUDED
