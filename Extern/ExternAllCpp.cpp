
#include "Extern/meshoptimizer/src/indexgenerator.cpp"
#include "Extern/meshoptimizer/src/vcacheoptimizer.cpp"
#include "Extern/meshoptimizer/src/vfetchoptimizer.cpp"
#include "Extern/meshoptimizer/src/simplifier.cpp"
#include "Extern/meshoptimizer/src/allocator.cpp"

#ifdef __APPLE__
    #include <TargetConditionals.h>
#endif

#if !(TARGET_OS_IPHONE || defined(__EMSCRIPTEN__) || defined(__ANDROID__))
    #define BASISD_SUPPORT_BC7 (0)
#endif

#define BASISD_SUPPORT_PVRTC2 (0)
#define BASISD_SUPPORT_FXT1   (0)
#define BASISD_SUPPORT_ATC    (0)
#define BASISD_SUPPORT_KTX2   (0)

#include "Extern/basis_universal/basisu_transcoder.cpp"