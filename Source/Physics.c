#include "Include/Scene.h"
#include "Include/Platform.h"
#include "Include/JobSystem.h"

#include <box3d/box3d.h>
#include <SDL3/SDL_stdinc.h>

// b3EnqueueTaskCallback: box3d's b3TaskCallback (void(void*)) matches JobSystemFn directly, so the
// task is queued as-is. Returns the job handle as an opaque userTask that box3d hands back to the
// finish callback. On a full/failed queue we run the task inline and return NULL so box3d skips finish.
static void* PhysicsEnqueueTask(b3TaskCallback* task, void* taskContext,
						 void* userContext, const char* taskName)
{
    (void)taskName;
    JobSystem* jobSystem = (JobSystem*)userContext;
    JobHandle handle = JobSystem_Execute(jobSystem, (JobSystemFn)task, taskContext);
    if (handle == 0)
    {
        task(taskContext);
        return NULL;
    }
    return (void*)(uintptr_t)(u32)handle;
}

// b3FinishTaskCallback: waits for the job enqueued above to finish.
static void PhysicsFinishTask(void* userTask, void* userContext)
{
    JobSystem* jobSystem = (JobSystem*)userContext;
    JobHandle handle = (JobHandle)(u32)(uintptr_t)userTask;
    JobSystem_WaitJob(jobSystem, handle);
}

void Scene_InitPhysics(Scene* scene)
{
	b3Version version = b3GetVersion();
	AX_LOG("Box3D version %d.%d.%d\n", version.major, version.minor, version.revision);
	
	JobSystem* physicsJobSystem = JobSystem_Create(0, 0);
	b3WorldDef worldDef      = b3DefaultWorldDef();
	worldDef.workerCount     = JobSystem_GetThreadCount(physicsJobSystem);
	worldDef.enqueueTask     = PhysicsEnqueueTask;
	worldDef.finishTask      = PhysicsFinishTask;
	worldDef.userTaskContext = physicsJobSystem;
	
	scene->physicsWorldID = b3CreateWorld(&worldDef);
}

void Scene_PhysicsDestroy(Scene* scene)
{
	b3DestroyWorld(scene->physicsWorldID);
}

void Scene_PhysicsUpdate(Scene* scene, float deltaTime)
{
	const int physicsStepCount = 4;
	b3World_Step(scene->physicsWorldID, deltaTime, physicsStepCount);
}