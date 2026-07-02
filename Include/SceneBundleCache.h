#ifndef SCENE_BUNDLE_CACHE_H
#define SCENE_BUNDLE_CACHE_H

#include "Scene.h"

// Global resident mesh-bundle cache shared between scenes and repeated adds. Bundles are keyed by
// path hash and reference counted; their geometry stays resident in the mega buffers until the last
// reference is dropped. Every entry is addressed by key, never by stored pointer, because the
// underlying HashMap relocates values on grow and on swap-with-last erase.
//
// The cache also owns the async scene/mesh import pipeline (SceneAsyncBegin/SceneAsyncUpdate) and the
// worker-thread .abm/.bdc bake-persist and picking-BVH builds. See SceneBundleCache.c.

// out: cache entry with one reference added, NULL on load failure. The returned pointer is only
// valid until the next map mutation; callers use it transiently and store the key, not the pointer.
BundleCacheEntry* BundleCacheAcquire(const char* path);

// drops one reference to the cached bundle. When the last reference goes away the geometry is
// returned to the mega buffers and the entry is erased.
void BundleCacheRelease(const char* path);
void BundleCacheReleaseKey(u64 key);

// Pumps a pending async scene/mesh import to completion, running its callback and dropping the
// probe's warming references. Call once per frame from the main thread.
void Scene_AsyncUpdate(void);

#endif // SCENE_BUNDLE_CACHE_H
