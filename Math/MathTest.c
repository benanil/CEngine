// runtime audit of the engine math headers against libm / scalar references.
// standalone, not part of the engine build. compile and run from C:\Test with:
//   cl /nologo /O2 /arch:AVX2 /I . Math\MathTest.c /Fe:MathTest.exe && MathTest.exe
// (vcvarsall x64 first). do NOT add /I Include: the engine's Include/string.h would
// shadow the crt string.h. exit code 0 means every check passed
#include <stdio.h>
#include <math.h>

static int StrEq(const char* a, const char* b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

#include "Math/Matrix.h"
#include "Math/Bitpack.h"
#include "Math/Color.h"
#include "Math/Half.h"
#include "Math/SOAMath.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failures++; printf("FAIL %s:%d  ", __FUNCTION__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static u32 rngState = 0x12345u;
static u32 RngU32(void) { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return rngState; }
static f32 RngFloat(f32 mn, f32 mx) { return mn + (mx - mn) * ((f32)(RngU32() & 0xFFFFFF) / 16777215.0f); }

static u32 popcount_ref(u32 x) { u32 n = 0; while (x) { n += x & 1u; x >>= 1; } return n; }

// engine vec macros may evaluate arguments more than once, so rng calls must be
// materialized into locals before entering them
static v128f RandUnitQuat(void)
{
    f32 x = RngFloat(-1, 1), y = RngFloat(-1, 1), z = RngFloat(-1, 1), w = RngFloat(-1, 1);
    if (x*x + y*y + z*z + w*w < 1e-4f) x = 1.0f;
    v128f v = VecSetR(x, y, z, w);
    return VecNorm(v);
}

static void TestScalarCore(void)
{
    for (int i = 0; i < 10000; i++)
    {
        f32 x = RngFloat(-1000.0f, 1000.0f);
        CHECK(Floorf32(x) == floorf(x), "Floorf32(%f)=%f want %f", x, Floorf32(x), floorf(x));
        CHECK(Ceilf(x) == ceilf(x), "Ceilf(%f)=%f want %f", x, Ceilf(x), ceilf(x));
        CHECK(Floor((f64)x) == floor((f64)x), "Floor(%f)", x);
    }
    CHECK(Floorf32(-0.5f) == -1.0f, "Floorf32(-0.5)");
    CHECK(Mins32(-5, 3) == -5, "Mins32(-5,3)=%d", Mins32(-5, 3));
    CHECK(Maxs32(-5, 0) == 0, "Maxs32(-5,0)=%d", Maxs32(-5, 0));
    CHECK(StrEq(GetFileName("a/b/c.txt"), "c.txt"), "GetFileName path");
    CHECK(StrEq(GetFileName("plain.txt"), "plain.txt"), "GetFileName no slash");
    CHECK(ByteSwap32(0x11223344u) == 0x44332211u, "ByteSwap32 = %llx", (unsigned long long)ByteSwap32(0x11223344u));
#ifdef ByteSwap64
    CHECK(ByteSwap64(0x1122334455667788ull) == 0x8877665544332211ull, "ByteSwap64");
#else
    CHECK(0, "ByteSwap64 macro not defined on this compiler");
#endif
    CHECK(PopCount32(0xF0F01234u) == popcount_ref(0xF0F01234u), "PopCount32");
}

static void TestTrig(void)
{
    f32 maxErrSin = 0, maxErrCos = 0, maxErrTan = 0;
    for (int i = 0; i < 20000; i++)
    {
        f32 x = RngFloat(-MATH_TwoPI, MATH_TwoPI);
        maxErrSin = Maxf32(maxErrSin, Absf32(Sin(x) - sinf(x)));
        maxErrCos = Maxf32(maxErrCos, Absf32(Cos(x) - cosf(x)));
        f32 t = RngFloat(-1.4f, 1.4f); // stay away from poles
        maxErrTan = Maxf32(maxErrTan, Absf32(Tan(t) - tanf(t)) / Maxf32(1.0f, Absf32(tanf(t))));
    }
    CHECK(maxErrSin < 1e-5f, "Sin err %g", maxErrSin);
    CHECK(maxErrCos < 1e-5f, "Cos err %g", maxErrCos);
    CHECK(maxErrTan < 1e-4f, "Tan err %g", maxErrTan);

    f32 maxErrAtan2 = 0;
    for (int i = 0; i < 20000; i++)
    {
        f32 y = RngFloat(-10.0f, 10.0f), x = RngFloat(-10.0f, 10.0f);
        if (Absf32(x) < 0.01f && Absf32(y) < 0.01f) continue;
        f32 err = Absf32(ATan2(y, x) - atan2f(y, x));
        maxErrAtan2 = Maxf32(maxErrAtan2, err);
    }
    CHECK(maxErrAtan2 < 0.02f, "ATan2 err %g (swapped args?)", maxErrAtan2);

    f32 maxErrAcos = 0, maxErrAcosV = 0;
    for (int i = 0; i < 10000; i++)
    {
        f32 x = RngFloat(-1.0f, 1.0f);
        maxErrAcos = Maxf32(maxErrAcos, Absf32(ACos(x) - acosf(x)));
        maxErrAcosV = Maxf32(maxErrAcosV, Absf32(VecGetX(ACosV(VecSet1(x))) - acosf(x)));
    }
    CHECK(maxErrAcos < 0.01f, "ACos err %g", maxErrAcos);
    CHECK(maxErrAcosV < 0.01f, "ACosV err %g (inverted select?)", maxErrAcosV);

    v128f a = Vec3NormV(VecSetR(1.0f, 2.0f, 3.0f, 0.0f));
    CHECK(Absf32(Vec3Angle(a, a)) < 0.01f, "Vec3Angle(a,a)=%f want 0", Vec3Angle(a, a));
    v128f b = Vec3NormV(VecSetR(-1.0f, -2.0f, -3.0f, 0.0f));
    CHECK(Absf32(Vec3Angle(a, b) - MATH_PI) < 0.01f, "Vec3Angle(a,-a)=%f want pi", Vec3Angle(a, b));
}

static void TestExpPowLog(void)
{
#ifndef SKIP_NEG_POW // pre-fix Powf never terminates for exponents <= -1
    #define POW_EXP_MIN -3.0f
#else
    #define POW_EXP_MIN 0.0f
#endif
    for (int i = 0; i < 2000; i++)
    {
        f32 base = RngFloat(0.1f, 8.0f);
        f32 e = RngFloat(POW_EXP_MIN, 3.0f);
        f32 want = powf(base, e);
        f32 got = Powf(base, e);
        // the ankerl fast-pow fractional approximation has ~12% worst case error
        CHECK(Absf32(got - want) <= 0.13f * Absf32(want) + 1e-4f, "Powf(%f,%f)=%f want %f", base, e, got, want);
    }
    for (int i = 0; i < 2000; i++)
    {
        f32 x = RngFloat(-10.0f, 10.0f);
        f32 want = expf(x), got = Expf(x);
        CHECK(Absf32(got - want) <= 0.001f * want + 1e-6f, "Expf(%f)=%f want %f", x, got, want);
        f32 p = RngFloat(0.01f, 1000.0f);
        CHECK(Absf32(Logf(p) - logf(p)) < 0.1f, "Logf(%f)=%f want %f", p, Logf(p), logf(p));
        CHECK(Absf32(CubeRootf(p) - cbrtf(p)) <= 0.08f * cbrtf(p), "CubeRootf(%f)=%f want %f", p, CubeRootf(p), cbrtf(p));
    }
}

static void TestPacking(void)
{
    for (int i = 0; i < 5000; i++)
    {
        f32 x = RngFloat(-1.0f, 1.0f);
        f32 round = UnpackSnorm16(PackSnorm16(x));
        CHECK(Absf32(round - x) < 0.001f, "Snorm16 roundtrip %f -> %f", x, round);
        f32 u = RngFloat(0.0f, 1.0f);
        CHECK(Absf32(UnpackUnorm16(PackUnorm16(u)) - u) < 0.001f, "Unorm16 roundtrip");
    }

    // random unit quaternions through every quat packing path
    for (int i = 0; i < 5000; i++)
    {
        v128f q = RandUnitQuat();
        f32 qf[4], rf[4];
        VecStore(qf, q);

        v128f r = UnpackQuat(PackQuat(q));
        VecStore(rf, r);
        f32 dot = qf[0]*rf[0] + qf[1]*rf[1] + qf[2]*rf[2] + qf[3]*rf[3];
        CHECK(Absf32(dot) > 0.9999f, "PackQuat roundtrip dot %f", dot);

        r = UnpackQuat10(PackQuat10(q));
        VecStore(rf, r);
        dot = qf[0]*rf[0] + qf[1]*rf[1] + qf[2]*rf[2] + qf[3]*rf[3];
        CHECK(Absf32(dot) > 0.9999f, "PackQuat10 roundtrip dot %f", dot);

        u64 packed;
        PackQuaternionS16Norm(q, &packed);
        r = UnpackQuaternionS16Norm1(packed);
        VecStore(rf, r);
        dot = qf[0]*rf[0] + qf[1]*rf[1] + qf[2]*rf[2] + qf[3]*rf[3];
        CHECK(dot > 0.999f, "PackQuaternionS16 roundtrip dot %f", dot);
    }
}

static void TestQuaternion(void)
{
    for (int i = 0; i < 5000; i++)
    {
        v128f q = RandUnitQuat();

        // matrix -> quaternion roundtrip, vector path vs scalar path
        f32 m[9];
        M33FromQuaternion(m, q);
        v128f r0 = VecSetR(m[0], m[1], m[2], 0.0f);
        v128f r1 = VecSetR(m[3], m[4], m[5], 0.0f);
        v128f r2 = VecSetR(m[6], m[7], m[8], 0.0f);
        v128f qv = QuaternionFromM33Vec(r0, r1, r2);

        xyzw qs;
        QuaternionFromMatrix(&qs.x, m, 3);

        f32 a[4], b[4], c[4];
        VecStore(a, q); VecStore(b, qv);
        f32 dotV = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
        CHECK(Absf32(dotV) > 0.9995f, "QuaternionFromM33Vec dot %f (q %f %f %f %f)", dotV, a[0], a[1], a[2], a[3]);

        c[0] = qs.x; c[1] = qs.y; c[2] = qs.z; c[3] = qs.w;
        f32 dotS = a[0]*c[0] + a[1]*c[1] + a[2]*c[2] + a[3]*c[3];
        CHECK(Absf32(dotS) > 0.9995f, "QuaternionFromMatrix dot %f", dotS);

        // rotate a vector by quat vs by matrix
        float3 v = { RngFloat(-2, 2), RngFloat(-2, 2), RngFloat(-2, 2) };
        float3 byQ = QMulVec3(v, q);
        float3 byM = {
            m[0]*v.x + m[3]*v.y + m[6]*v.z,
            m[1]*v.x + m[4]*v.y + m[7]*v.z,
            m[2]*v.x + m[5]*v.y + m[8]*v.z,
        };
        CHECK(Absf32(byQ.x-byM.x) + Absf32(byQ.y-byM.y) + Absf32(byQ.z-byM.z) < 0.001f,
              "QMulVec3 vs matrix (%f %f %f) vs (%f %f %f)", byQ.x, byQ.y, byQ.z, byM.x, byM.y, byM.z);
    }

    // euler roundtrip in the safe range (no gimbal lock)
    for (int i = 0; i < 2000; i++)
    {
        float3 e = { RngFloat(-1.2f, 1.2f), RngFloat(-1.2f, 1.2f), RngFloat(-1.2f, 1.2f) };
        float3 r = QToEulerAngles(QFromEuler(e.x, e.y, e.z));
        CHECK(Absf32(e.x-r.x) < 0.02f && Absf32(e.y-r.y) < 0.02f && Absf32(e.z-r.z) < 0.02f,
              "Euler roundtrip (%f %f %f) -> (%f %f %f)", e.x, e.y, e.z, r.x, r.y, r.z);
    }

    // dual quaternion: translation roundtrip and blend endpoints
    for (int i = 0; i < 1000; i++)
    {
        v128f q = RandUnitQuat();
        v128f t = VecSetR(RngFloat(-5, 5), RngFloat(-5, 5), RngFloat(-5, 5), 0.0f);
        DualQuaternion dq = DQFromRotationTranslation(q, t);
        v128f tr = DQGetTranslation(dq);
        CHECK(Vec3DistfV(tr, t) < 0.001f, "DQ translation roundtrip dist %f", Vec3DistfV(tr, t));

        DualQuaternion blended = DQBlend(dq, dq, 0.5f);
        v128f tb = DQGetTranslation(blended);
        // blending a dq with itself must keep the same transform (real part may scale)
        v128f realN = VecNorm(blended.real);
        f32 qd[4], rd[4];
        VecStore(qd, q); VecStore(rd, realN);
        f32 dot = qd[0]*rd[0]+qd[1]*rd[1]+qd[2]*rd[2]+qd[3]*rd[3];
        CHECK(Absf32(dot) > 0.999f, "DQBlend self real dot %f", dot);
    }
}

static void TestMatrix(void)
{
    // extract scale must match construction order
    mat4x4 s = M44FromScale(2.0f, 3.0f, 4.0f);
    float3 sc = M44ExtractScale(s);
    CHECK(Absf32(sc.x-2) < 1e-5f && Absf32(sc.y-3) < 1e-5f && Absf32(sc.z-4) < 1e-5f,
          "M44ExtractScale (%f %f %f) want (2 3 4)", sc.x, sc.y, sc.z);

    // rotation x: every element defined, rotates +y toward +z (right handed row major)
    mat4x4 rx = M44RotationX(MATH_HalfPI);
    CHECK(rx.m[1][0] == 0.0f && rx.m[1][3] == 0.0f && rx.m[2][0] == 0.0f && rx.m[2][3] == 0.0f &&
          rx.m[3][0] == 0.0f && rx.m[3][1] == 0.0f && rx.m[3][2] == 0.0f && rx.m[3][3] == 1.0f,
          "M44RotationX uninitialized elements");

    // position/rotation/scale constructor: w lanes of rows 0..2 must be 0
    v128f q = VecNorm(VecSetR(0.3f, -0.2f, 0.5f, 0.78f));
    mat4x4 prs = M44PositionRotationScaleVec(VecSetR(1, 2, 3, 0), q, VecSetR(2, 2, 2, 0));
    CHECK(prs.m[0][3] == 0.0f && prs.m[1][3] == 0.0f && prs.m[2][3] == 0.0f && prs.m[3][3] == 1.0f,
          "M44PositionRotationScaleVec w column (%f %f %f %f)", prs.m[0][3], prs.m[1][3], prs.m[2][3], prs.m[3][3]);
    mat4x4 pr = M44PositionRotationVec(VecSetR(1, 2, 3, 0), q);
    CHECK(pr.m[0][3] == 0.0f && pr.m[1][3] == 0.0f && pr.m[2][3] == 0.0f && pr.m[3][3] == 1.0f,
          "M44PositionRotationVec w column");

    // inverse: M * M^-1 == identity
    for (int i = 0; i < 2000; i++)
    {
        v128f rq = RandUnitQuat();
        mat4x4 m = M44PositionRotationScale(
            (float3){ RngFloat(-10, 10), RngFloat(-10, 10), RngFloat(-10, 10) }, rq,
            (float3){ RngFloat(0.2f, 4), RngFloat(0.2f, 4), RngFloat(0.2f, 4) });
        mat4x4 inv = M44Inverse(m);
        mat4x4 id = M44Multiply(m, inv);
        for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
        {
            f32 want = (r == c) ? 1.0f : 0.0f;
            CHECK(Absf32(id.m[r][c] - want) < 0.001f, "M44Inverse [%d][%d]=%f", r, c, id.m[r][c]);
        }
        if (g_failures > 40) return; // do not spam
    }

    // M44FromQuaternionV vs scalar M44FromQuaternion
    for (int i = 0; i < 2000; i++)
    {
        v128f rq = RandUnitQuat();
        mat4x4 a = M44FromQuaternionV(rq);
        mat4x4 b = {0};
        M44FromQuaternion(&b.m[0][0], rq);
        b.m[3][3] = 1.0f;
        for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            CHECK(Absf32(a.m[r][c] - b.m[r][c]) < 1e-5f, "M44FromQuaternionV [%d][%d] %f vs %f", r, c, a.m[r][c], b.m[r][c]);
        if (g_failures > 40) return;
    }
}

static void TestVecOps(void)
{
    v128f v = VecSetR(1.0f, 2.0f, 3.0f, 4.0f);
    float2 xy = VecXY(v), zw = VecZW(v);
    CHECK(xy.x == 1.0f && xy.y == 2.0f, "VecXY (%f %f)", xy.x, xy.y);
    CHECK(zw.x == 3.0f && zw.y == 4.0f, "VecZW (%f %f)", zw.x, zw.y);

    float3 a3 = { 1, 2, 3 }, b3 = { 0, 1, 4 };
    CHECK(GreaterThan3(a3, b3) == 0b011, "GreaterThan3 %d", GreaterThan3(a3, b3));
    CHECK(LessThan3(a3, b3) == 0b100, "LessThan3 %d", LessThan3(a3, b3));

    for (int i = 0; i < 5000; i++)
    {
        f32 f[4] = { RngFloat(-9, 9), RngFloat(-9, 9), RngFloat(-9, 9), RngFloat(-9, 9) };
        u32 argmax = 0;
        for (u32 k = 1; k < 4; k++) if (Absf32(f[k]) > Absf32(f[argmax])) argmax = k;
        u32 got = VecMaxElement(VecFabs(VecLoad(f)));
        CHECK(Absf32(Absf32(f[got]) - Absf32(f[argmax])) < 1e-6f, "VecMaxElement got %u want %u", got, argmax);
    }

    // documented SSE semantics that the NEON side must match
    v128u i16 = VeciSetR((u32)(u16)-100 | ((u32)(u16)200 << 16), (u32)(u16)-30000 | ((u32)(u16)30000 << 16), 0, 0);
    v128u lo = VecUnpackLo32S(i16);
    CHECK((s32)VeciGetX(lo) == -100 && (s32)VeciGetY(lo) == 200 && (s32)VeciGetZ(lo) == -30000 && (s32)VeciGetW(lo) == 30000,
          "VecUnpackLo32S %d %d %d %d", (s32)VeciGetX(lo), (s32)VeciGetY(lo), (s32)VeciGetZ(lo), (s32)VeciGetW(lo));

    v128u packIn = VeciSetR((u32)-5, 70000, 12345, 0);
    v128u packed = VecPack16(packIn); // packus: signed input, saturate to [0, 65535]
    u32 first = VeciGetX(packed);
    CHECK((first & 0xFFFF) == 0 && (first >> 16) == 65535, "VecPack16 sat %08x", first);

    // half conversions
    for (int i = 0; i < 5000; i++)
    {
        f32 x = RngFloat(-1000.0f, 1000.0f);
        f32 r = HalfToFloat(FloatToHalf(x));
        CHECK(Absf32(r - x) <= Absf32(x) * 0.001f + 0.01f, "half roundtrip %f -> %f", x, r);
    }
    float2 h2 = { 1.25f, -3.5f };
    f32 h2r[2];
    Half2ToFloat2(h2r, Float2ToHalf2(&h2.x));
    CHECK(h2r[0] == 1.25f && h2r[1] == -3.5f, "half2 roundtrip (%f %f)", h2r[0], h2r[1]);

    f32 f4in[4] = { 0.5f, -2.0f, 100.0f, -0.125f }, f4out[4];
    u64 h4;
    Float4ToHalf4(&h4, f4in);
    Half4ToFloat4(f4out, (const f16*)&h4);
    CHECK(f4out[0] == 0.5f && f4out[1] == -2.0f && f4out[2] == 100.0f && f4out[3] == -0.125f,
          "half4 roundtrip (%f %f %f %f)", f4out[0], f4out[1], f4out[2], f4out[3]);
}

static void TestSoa(void)
{
    // four independent quaternions in soa form vs the aos reference
    v128f q[4], vsoa[3], rsoa[3];
    f32 qa[4][4], va[4][3];
    for (int lane = 0; lane < 4; lane++)
    {
        v128f single = RandUnitQuat();
        VecStore(qa[lane], single);
        va[lane][0] = RngFloat(-3,3); va[lane][1] = RngFloat(-3,3); va[lane][2] = RngFloat(-3,3);
    }
    for (int c = 0; c < 4; c++) q[c] = VecSetR(qa[0][c], qa[1][c], qa[2][c], qa[3][c]);
    for (int c = 0; c < 3; c++) vsoa[c] = VecSetR(va[0][c], va[1][c], va[2][c], va[3][c]);

    v128f dot = Vec4DotSoa(q, q);
    for (int lane = 0; lane < 4; lane++)
        CHECK(Absf32(VecGetN(dot, lane) - 1.0f) < 0.001f, "Vec4DotSoa lane %d = %f want 1", lane, VecGetN(dot, lane));

    QMulVec3Soa(vsoa, q, rsoa);
    for (int lane = 0; lane < 4; lane++)
    {
        float3 vv = { va[lane][0], va[lane][1], va[lane][2] };
        float3 want = QMulVec3(vv, VecLoad(qa[lane]));
        f32 gx = VecGetN(rsoa[0], lane), gy = VecGetN(rsoa[1], lane), gz = VecGetN(rsoa[2], lane);
        CHECK(Absf32(gx-want.x) + Absf32(gy-want.y) + Absf32(gz-want.z) < 0.001f,
              "QMulVec3Soa lane %d (%f %f %f) want (%f %f %f)", lane, gx, gy, gz, want.x, want.y, want.z);
    }
}

int main(void)
{
    TestScalarCore();
    TestTrig();
    TestExpPowLog();
    TestPacking();
    TestQuaternion();
    TestMatrix();
    TestVecOps();
    TestSoa();
    printf("math test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures != 0;
}
