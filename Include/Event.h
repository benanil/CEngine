#ifndef EVENT
#define EVENT
#include <stdbool.h>

#define EVENT_MAX_CALLBACK 16

typedef struct Event_
{
    void (*action[EVENT_MAX_CALLBACK])(void*, void*);
    void* context[EVENT_MAX_CALLBACK];
    int numAction;
} Event;
// EventInit() e->numAction = 0

// you check error lul :) e->numCallback < EVENT_MAX_CALLBACK
static inline void EventAdd(Event* e, void(*callback)(void*, void*), void* context)
{
    e->action[e->numAction] = callback;
    e->context[e->numAction++] = context;
}

static inline void EventRemove(Event* e, void* context)
{
    for (int i = 0; i < e->numAction; i++)
    {
        if (e->context[i] == context)
        {   // unordered remove
            e->context[i] = e->context[--e->numAction];
            e->action[i] = e->action[e->numAction];
        }
    }
}

static inline void EventInvoke(Event* e, void* data)
{
    for (int i = 0; i < e->numAction; i++)
        e->action[i](e->context[i], data);
}
#endif