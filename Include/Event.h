#ifndef EVENT
#define EVENT

#define EMAX_ACTION 16

typedef struct Event_
{
    void (*action[EMAX_ACTION])(void*, void*);
    void* context[EMAX_ACTION];
    int numAction;
} Event;
// EventInit() e->numAction = 0

// you check error lul :) e->numAction < EMAX_ACTION
static inline void EventAdd(Event* e, void(*callback)(void*, void*), void* context)
{
    e->action[e->numAction] = callback;
    e->context[e->numAction++] = context;
}

static inline void EventRemove(Event* e, void* context, void* action)
{
    for (int i = 0; i < e->numAction; i++)
    {
        if (e->context[i] == context && e->action[i] == action)
        {   // unordered remove
            e->context[i]  = e->context[--e->numAction];
            e->action[i--] = e->action[e->numAction];
        }
    }
}

static inline void EventInvoke(Event* e, void* data)
{
    for (int i = 0; i < e->numAction; i++)
        e->action[i](e->context[i], data);
}
#endif