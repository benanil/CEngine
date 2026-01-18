
// 32 mb
#define TLSF_MEMORY_SIZE (32 * 1024 * 1024)

#include <stdio.h>

#include "OS.c"
#include "Algorithm.c"
#include "TLSF.c"
#include "Memory.c"
#include "FileSystem.c"
#include "Math/Math.h"

void FolderVisit(const char* path, void* data)
{
    puts(path);
}

int main()
{
    const char* path = "C:\\";
    int visitResult = VisitFolder(path, FolderVisit, NULL, true);
    printf("%d", visitResult);
    return visitResult;
    
    AFile file = AFileOpen("AOC0.txt", AOpenFlag_ReadText);
    char buffer[128];
    
    int currentNumber = 50;
    int numZero = 0;
    int numZeroPart2 = 0;

    while (AFileReadLine(buffer, 128, file))
    {
        const char* curr = buffer;
        int isLeft = *curr++ == 'L';
        int number;
        curr = ParsePositiveNumber(curr, &number);

        if (!isLeft)
        {
            currentNumber = (currentNumber + number) % 100;
        }
        else
        {
            if (number >= 100)
                number -= (number / 100) * 100;

            if (number > currentNumber)
            {
                currentNumber = 100 - (number - currentNumber);
            }
            else
            {
                currentNumber -= number;
            }
        }
        numZero += currentNumber == 0;
    }
    printf("%d ", numZero);
    return numZero;
}
