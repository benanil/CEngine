// console window: records AX_LOG / AX_WARN / AX_ERROR output through the sdl log hook
// into a ring buffer from startup, so everything is captured even while the window is
// closed. newest lines render first
#include "EditorInternal.h"
#include "Include/Platform.h"
#include "Include/Random.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mutex.h>

#define CONSOLE_MAX_LINES   1024u
#define CONSOLE_DRAW_LINES  256u
#define CONSOLE_LINE_LEN    240u

typedef struct ConsoleLine_
{
    u8   priority; // SDL_LogPriority
    char text[CONSOLE_LINE_LEN];
} ConsoleLine;

// ring buffer guarded by a mutex, logs can arrive from worker threads
static ConsoleLine consoleLines[CONSOLE_MAX_LINES];
static u32 consoleHead;  // next write index
static u32 consoleCount;
static u32 consoleNumWarns;
static u32 consoleNumErrors;
static SDL_Mutex* consoleMutex;
static SDL_LogOutputFunction consoleDefaultOutput;
static void* consoleDefaultUserdata;

static bool consoleShowInfo  = true;
static bool consoleShowWarn  = true;
static bool consoleShowError = true;

static void ConsoleLogOutput(void* userdata, int category, SDL_LogPriority priority, const char* message)
{
    (void)userdata;
    if (consoleDefaultOutput)
        consoleDefaultOutput(consoleDefaultUserdata, category, priority, message);
    if (!consoleMutex || !message) return;

    SDL_LockMutex(consoleMutex);
    ConsoleLine* line = &consoleLines[consoleHead];
    consoleHead = (consoleHead + 1u) % CONSOLE_MAX_LINES;
    if (consoleCount < CONSOLE_MAX_LINES) consoleCount++;
    if (priority >= SDL_LOG_PRIORITY_ERROR) consoleNumErrors++;
    else if (priority == SDL_LOG_PRIORITY_WARN) consoleNumWarns++;

    line->priority = (u8)priority;
    u32 out = 0;
    for (const char* c = message; *c && out + 1u < CONSOLE_LINE_LEN; c++)
    {
        if (*c == '\033') // strip ansi color sequences
        {
            while (*c && *c != 'm') c++;
            if (!*c) break;
            continue;
        }
        if (*c == '\n' || *c == '\r')
        {
            if (out > 0u && line->text[out - 1u] != ' ') line->text[out++] = ' ';
            continue;
        }
        line->text[out++] = *c;
    }
    line->text[out] = '\0';
    SDL_UnlockMutex(consoleMutex);
}

// installs the log hook, call once at startup before the systems init so every log lands
void EditorConsoleInit(void)
{
    if (consoleMutex) return;
    consoleMutex = SDL_CreateMutex();
    SDL_GetLogOutputFunction(&consoleDefaultOutput, &consoleDefaultUserdata);
    SDL_SetLogOutputFunction(ConsoleLogOutput, NULL);
}

// matches the terminal ansi colors of the log macros: green info, yellow warn, red error
static Clay_Color ConsolePriorityColor(u8 priority)
{
    if (priority >= SDL_LOG_PRIORITY_ERROR) return (Clay_Color){ 235, 95, 85, 255 };
    if (priority == SDL_LOG_PRIORITY_WARN)  return (Clay_Color){ 230, 200, 90, 255 };
    return (Clay_Color){ 110, 210, 120, 255 };
}

static bool ConsolePriorityVisible(u8 priority)
{
    if (priority >= SDL_LOG_PRIORITY_ERROR) return consoleShowError;
    if (priority == SDL_LOG_PRIORITY_WARN)  return consoleShowWarn;
    return consoleShowInfo;
}

void DrawConsoleWindow(bool* open)
{
    Clay_ElementId windowID = (Clay_ElementId) { .id = StringToHash("ConsoleWindow", 5381u) };
    if (!UIBeginWindowId(windowID, "Console", (float2) { 120.0f, 520.0f }, (float2) { 900.0f, 420.0f }, open, 0u)) return;

    CLAY(CLAY_ID("ConsoleToolbar"), {
        .layout = {
            .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(28.0f) },
            .childGap = 12,
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }
        }
    }) {
        UIPushFloatAdd(UIFloat_TextScale, -0.2f);
        if (UIButton(CLAY_ID("ConsoleClear"), CLAY_STRING("Clear"), (Clay_Dimensions){ 70.0f, 24.0f }, false))
        {
            SDL_LockMutex(consoleMutex);
            consoleHead = 0u;
            consoleCount = 0u;
            consoleNumWarns = 0u;
            consoleNumErrors = 0u;
            SDL_UnlockMutex(consoleMutex);
        }
        UIPopFloat(UIFloat_TextScale);
        UICheckbox(CLAY_ID("ConsoleShowInfo"),  CLAY_STRING("Info"),     &consoleShowInfo);
        UICheckbox(CLAY_ID("ConsoleShowWarn"),  CLAY_STRING("Warnings"), &consoleShowWarn);
        UICheckbox(CLAY_ID("ConsoleShowError"), CLAY_STRING("Errors"),   &consoleShowError);
        UITextU32("Lines", consoleCount);
        UITextU32("Warnings", consoleNumWarns);
        UITextU32("Errors", consoleNumErrors);
    }
    UIDivider(CLAY_ID("ConsoleDivider"));

    CLAY(CLAY_ID("ConsoleScroll"), UIScrollPanelDeclaration(UIWindowRemainingHeight(windowID, CLAY_ID("ConsoleScroll"), 0.0f), 2u)) {
        SDL_LockMutex(consoleMutex);
        u32 drawn = 0u;
        for (u32 i = 0u; i < consoleCount && drawn < CONSOLE_DRAW_LINES; i++)
        {
            // newest first, no scrolling needed to see the latest logs
            u32 idx = (consoleHead + CONSOLE_MAX_LINES - 1u - i) % CONSOLE_MAX_LINES;
            const ConsoleLine* line = &consoleLines[idx];
            if (!ConsolePriorityVisible(line->priority)) continue;

            // copy to frame memory, the ring slot can be overwritten before rendering
            u32 len = (u32)StringLength(line->text);
            char* text = UIFrameStringAlloc(len + 1u);
            if (!text) break;
            MemCopy(text, line->text, len + 1u);

            Clay_ElementId rowId = Clay_GetElementIdWithIndex(CLAY_STRING("ConsoleLine"), drawn);
            CLAY(rowId, {
                .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) } }
            }) {
                CLAY_TEXT(((Clay_String) { .isStaticallyAllocated = false, .length = (s32)len, .chars = text }),
                          CLAY_TEXT_CONFIG({
                              .fontSize = 13,
                              .textColor = ConsolePriorityColor(line->priority)
                          }));
            }
            drawn++;
        }
        SDL_UnlockMutex(consoleMutex);

        if (consoleCount == 0u)
        {
            CLAY_TEXT(CLAY_STRING("No logs yet."), CLAY_TEXT_CONFIG({
                .fontSize = 14,
                .textColor = UIGetClayColor(UIColor_SubText)
            }));
        }
    }
    UIEndWindow();
}
