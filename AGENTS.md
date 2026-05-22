# General
this is data oriented C99 project with some cpp dependencies
it is using SDL GPU for graphics and platform backend
project has lots of functions for random, math, algorithm wise almost no need stdlib (SDL instead)
Math is heavily SIMD oriented there are cross platform macros in Common/SIMD.h
most of the time no need for float3 v128f mostly more convinient performance wise (less register and instruction)
when you do early return do AX_WARN or AX_INFO to notice ourselfs

# Memory allocations
for small allocations use stack
if not fitting use Include/Memory.h 
for temp under 256 mb 
void* ArenaPushGlobal(uint64_t size); 
void ArenaPopGlobal(uint64_t size);
if persistent or big memory use AllocateTLSFGlobal/DeAllocateTLSFGlobal or OSAlloc
no need to use MMAX, MMIN etc macros use functions such as Maxf32 and such
