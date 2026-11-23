// Verifies portable FP/SIMD helpers with FPCR.FZ=0/1, FPCR.DN=0/1, and all FPCR.RMode values:
// cmp_s/d, vcmp_s/d, frint_s/d, vfrint_s/d, toint_s/d, simd_s2d, simd_d2s, and minmax ops.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "simd.h"

#if defined(__x86_64__) && !defined(__FMA__)
int simd_fma_unfused;
#endif

static __attribute__((noinline)) uint64_t call_cmp_s(Cpu *c, float a, float b) { return cmp_s(c, a, b); }
static __attribute__((noinline)) uint64_t call_cmp_d(Cpu *c, double a, double b) { return cmp_d(c, a, b); }
static __attribute__((noinline)) vu32 call_vcmp_s(Cpu *c, int op, vf32 a, vf32 b) {
    switch (op) {
    case 0: return vcmp_s(c, 0, a, b);
    case 1: return vcmp_s(c, 1, a, b);
    case 2: return vcmp_s(c, 2, a, b);
    case 3: return vcmp_s(c, 3, a, b);
    default: return vcmp_s(c, 4, a, b);
    }
}
static __attribute__((noinline)) vu64 call_vcmp_d(Cpu *c, int op, vf64 a, vf64 b) {
    switch (op) {
    case 0: return vcmp_d(c, 0, a, b);
    case 1: return vcmp_d(c, 1, a, b);
    case 2: return vcmp_d(c, 2, a, b);
    case 3: return vcmp_d(c, 3, a, b);
    default: return vcmp_d(c, 4, a, b);
    }
}
static __attribute__((noinline)) float call_frint_s(Cpu *c, float x, int rm) { return frint_s(c, x, rm); }
static __attribute__((noinline)) double call_frint_d(Cpu *c, double x, int rm) { return frint_d(c, x, rm); }
static __attribute__((noinline)) vf32 call_vfrint_s(Cpu *c, vf32 x, int rm) { return vfrint_s(c, x, rm); }
static __attribute__((noinline)) vf64 call_vfrint_d(Cpu *c, vf64 x, int rm) { return vfrint_d(c, x, rm); }
static __attribute__((noinline)) uint64_t call_toint_s(Cpu *c, float x, int rm, int fb, int ib, int u) { return toint_s(c, x, rm, fb, ib, u); }
static __attribute__((noinline)) uint64_t call_toint_d(Cpu *c, double x, int rm, int fb, int ib, int u) { return toint_d(c, x, rm, fb, ib, u); }
static __attribute__((noinline)) double call_s2d(Cpu *c, float x) { return simd_s2d(c, x); }
static __attribute__((noinline)) float call_d2s(Cpu *c, double x) { return simd_d2s(c, x); }
static __attribute__((noinline)) float call_min_s(Cpu *c, float a, float b) { return op_s(c, FOP_MIN, a, b, 0); }
static __attribute__((noinline)) vf32 call_vmin_s(Cpu *c, vf32 a, vf32 b) { return vop_s(c, FOP_MIN, a, b, (vf32){0}); }

static int g_fails;

static void check_u64(const char *what, uint64_t got, uint64_t want, uint64_t fpcr, uint64_t a, uint64_t b) {
    if (got != want) {
        if (g_fails++ < 32)
            fprintf(stderr, "FAIL %s fpcr=%#llx a=%#llx b=%#llx: got %#llx want %#llx\n", what,
                    (unsigned long long)fpcr, (unsigned long long)a, (unsigned long long)b,
                    (unsigned long long)got, (unsigned long long)want);
    }
}

static uint32_t ref_flush_s(uint64_t fpcr, uint32_t u) {
    if ((fpcr & FPCR_FZ) && !(u & 0x7f800000u) && (u & 0x007fffffu)) return u & 0x80000000u;
    return u;
}

static uint64_t ref_flush_d(uint64_t fpcr, uint64_t u) {
    if ((fpcr & FPCR_FZ) && !(u & 0x7ff0000000000000ull) && (u & 0x000fffffffffffffull))
        return u & 0x8000000000000000ull;
    return u;
}

static uint64_t ref_cmp_s(uint64_t fpcr, uint32_t ua, uint32_t ub) {
    float a = fv_s(ref_flush_s(fpcr, ua)), b = fv_s(ref_flush_s(fpcr, ub));
    return a == b ? 0x60000000ull : a < b ? 0x80000000ull : a > b ? 0x20000000ull : 0x30000000ull;
}

static uint64_t ref_cmp_d(uint64_t fpcr, uint64_t ua, uint64_t ub) {
    double a = fv_d(ref_flush_d(fpcr, ua)), b = fv_d(ref_flush_d(fpcr, ub));
    return a == b ? 0x60000000ull : a < b ? 0x80000000ull : a > b ? 0x20000000ull : 0x30000000ull;
}

static uint32_t ref_frint_s(Cpu *ref, uint32_t u, int rm) {
    float x = fv_s(ref_flush_s(ref->fpcr, u));
    if (rm >= 5) rm = (int)((ref->fpcr >> 22) & 3);
    float r = rnd_s(x, rm);
    if (r != r) r = nan1_s(ref, x);
    return fb_s(r);
}

static uint64_t ref_frint_d(Cpu *ref, uint64_t u, int rm) {
    double x = fv_d(ref_flush_d(ref->fpcr, u));
    if (rm >= 5) rm = (int)((ref->fpcr >> 22) & 3);
    double r = rnd_d(x, rm);
    if (r != r) r = nan1_d(ref, x);
    return fb_d(r);
}

static uint64_t ref_toint_s(uint64_t fpcr, uint32_t u, int rm, int fbits, int ib, int uns) {
    float x = fv_s(ref_flush_s(fpcr, u));
    if (fbits) x = x * (float)(1ull << (fbits - 1)) * 2.0f;
    float r = rnd_s(x, rm);
    if (r != r) return 0;
    if (uns) {
        if (r <= 0.0f) return 0;
        if (ib == 32) return r >= 4294967296.0f ? 0xffffffffull : (uint32_t)r;
        return r >= 18446744073709551616.0f ? ~0ull : (uint64_t)r;
    }
    if (ib == 32) return r >= 2147483648.0f ? 0x7fffffffull : r < -2147483648.0f ? 0x80000000ull : (uint32_t)(int32_t)r;
    return r >= 9223372036854775808.0f ? 0x7fffffffffffffffull : r < -9223372036854775808.0f ? 0x8000000000000000ull : (uint64_t)(int64_t)r;
}

static uint64_t ref_toint_d(uint64_t fpcr, uint64_t u, int rm, int fbits, int ib, int uns) {
    double x = fv_d(ref_flush_d(fpcr, u));
    if (fbits) x = x * (double)(1ull << (fbits - 1)) * 2.0;
    double r = rnd_d(x, rm);
    if (r != r) return 0;
    if (uns) {
        if (r <= 0.0) return 0;
        if (ib == 32) return r >= 4294967296.0 ? 0xffffffffull : (uint32_t)r;
        return r >= 18446744073709551616.0 ? ~0ull : (uint64_t)r;
    }
    if (ib == 32) return r >= 2147483648.0 ? 0x7fffffffull : r < -2147483648.0 ? 0x80000000ull : (uint32_t)(int32_t)r;
    return r >= 9223372036854775808.0 ? 0x7fffffffffffffffull : r < -9223372036854775808.0 ? 0x8000000000000000ull : (uint64_t)(int64_t)r;
}

static uint64_t ref_s2d(uint64_t fpcr, uint32_t u) {
    u = ref_flush_s(fpcr, u);
    if (isnan_s(u)) {
        if (fpcr & FPCR_DN) return 0x7ff8000000000000ull;
        return ((uint64_t)(u >> 31) << 63) | 0x7ff8000000000000ull | ((uint64_t)(u & 0x3fffff) << 29);
    }
    return fb_d((double)fv_s(u));
}

static uint32_t ref_d2s(Cpu *ref, uint64_t u) {
    u = ref_flush_d(ref->fpcr, u);
    if (isnan_d(u)) {
        if (ref->fpcr & FPCR_DN) return 0x7fc00000u;
        return (uint32_t)(u >> 63) << 31 | 0x7fc00000u | (uint32_t)((u >> 29) & 0x3fffff);
    }
    double x = fv_d(u);
    if (ref->fpcr & FPCR_RM) {
        if ((ref->fpcr & FPCR_FZ) && __builtin_fabs(x) < 0x1p-126 && x != 0)
            return (uint32_t)(u >> 63) << 31;
        return fb_s(simd_d2s_slow(ref, x));
    }
    float f = (float)x;
    uint32_t fu = fb_s(f);
    if ((ref->fpcr & FPCR_FZ) && !(fu & 0x7f800000u) && (fu & 0x007fffffu))
        return fu & 0x80000000u;
    return fu;
}

int main(void) {
    static const uint32_t f32[] = {
        0x00000000u, 0x80000000u,  // +-0
        0x00000001u, 0x80000001u,  // +-smallest subnormal
        0x00400000u, 0x80400000u,  // +-mid subnormal
        0x007fffffu, 0x807fffffu,  // +-largest subnormal
        0x00800000u, 0x80800000u,  // +-smallest normal
        0x3f000000u, 0xbf000000u,  // +-0.5
        0x3f800000u, 0xbf800000u,  // +-1.0
        0x3fc00000u, 0xbfc00000u,  // +-1.5
        0x4f000000u, 0xcf000000u,  // +-2^31
        0x7f800000u, 0xff800000u,  // +-inf
        0x7fc00000u, 0xffc01234u,  // qNaN (+ and - with payload)
        0x7fa00001u, 0xff812345u,  // sNaN (+ and - with payload)
    };
    static const uint64_t f64[] = {
        0x0000000000000000ull, 0x8000000000000000ull,
        0x0000000000000001ull, 0x8000000000000001ull,
        0x0008000000000000ull, 0x8008000000000000ull,
        0x000fffffffffffffull, 0x800fffffffffffffull,
        0x0010000000000000ull, 0x8010000000000000ull,
        0x3800000000000000ull, 0xb800000000000000ull,  // +-2^-127 (subnormal in f32)
        0x3808000000000000ull, 0xb808000000000000ull,
        0x3810000000000000ull, 0xb810000000000000ull,  // +-2^-126 (min normal f32)
        0x3fe0000000000000ull, 0xbfe0000000000000ull,  // +-0.5
        0x3ff0000000000000ull, 0xbff0000000000000ull,  // +-1.0
        0x3ff8000000000000ull, 0xbff8000000000000ull,  // +-1.5
        0x41e0000000000000ull, 0xc1e0000000000000ull,  // +-2^31
        0x7ff0000000000000ull, 0xfff0000000000000ull,  // +-inf
        0x7ff8000000000000ull, 0xfff8000012345678ull,  // qNaN
        0x7ff4000000000001ull, 0xfff0000000012345ull,  // sNaN
    };
    const int n32 = (int)(sizeof f32 / sizeof f32[0]);
    const int n64 = (int)(sizeof f64 / sizeof f64[0]);

    for (int fz = 0; fz <= 1; fz++) {
        for (int dn = 0; dn <= 1; dn++) {
            for (int rm = 0; rm < 4; rm++) {
                uint64_t fpcr = (fz ? FPCR_FZ : 0) | (dn ? FPCR_DN : 0) | ((uint64_t)rm << 22);
                Cpu c = {0}, ref = {0};
                ref.fpcr = fpcr;

                // cmp_s & vcmp_s
                for (int i = 0; i < n32; i++) {
                    for (int j = 0; j < n32; j++) {
                        rt_fpcr_write(&c, fpcr);
                        uint64_t got = call_cmp_s(&c, fv_s(f32[i]), fv_s(f32[j]));
                        rt_fpcr_write(&c, 0);
                        check_u64("cmp_s", got, ref_cmp_s(fpcr, f32[i], f32[j]), fpcr, f32[i], f32[j]);
                    }
                }
                for (int op = 0; op < 5; op++) {
                    for (int i = 0; i < n32; i += 4) {
                        vf32 va = {fv_s(f32[i]), fv_s(f32[(i + 1) % n32]), fv_s(f32[(i + 2) % n32]), fv_s(f32[(i + 3) % n32])};
                        vf32 vb = {fv_s(f32[(i + 2) % n32]), fv_s(f32[i]), fv_s(f32[(i + 3) % n32]), fv_s(f32[(i + 1) % n32])};
                        rt_fpcr_write(&c, fpcr);
                        vu32 vr = call_vcmp_s(&c, op, va, vb);
                        rt_fpcr_write(&c, 0);
                        for (int l = 0; l < 4; l++) {
                            float a = fv_s(ref_flush_s(fpcr, fb_s(va[l]))), b = fv_s(ref_flush_s(fpcr, fb_s(vb[l])));
                            if (op >= 3) a = __builtin_fabsf(a), b = __builtin_fabsf(b);
                            uint32_t want = (op == 0 ? (a == b) : (op == 1 || op == 3) ? (a >= b) : (a > b)) ? ~0u : 0u;
                            check_u64("vcmp_s", vr[l], want, fpcr, fb_s(va[l]), fb_s(vb[l]));
                        }
                    }
                }

                // cmp_d & vcmp_d
                for (int i = 0; i < n64; i++) {
                    for (int j = 0; j < n64; j++) {
                        rt_fpcr_write(&c, fpcr);
                        uint64_t got = call_cmp_d(&c, fv_d(f64[i]), fv_d(f64[j]));
                        rt_fpcr_write(&c, 0);
                        check_u64("cmp_d", got, ref_cmp_d(fpcr, f64[i], f64[j]), fpcr, f64[i], f64[j]);
                    }
                }
                for (int op = 0; op < 5; op++) {
                    for (int i = 0; i < n64; i += 2) {
                        vf64 va = {fv_d(f64[i]), fv_d(f64[(i + 1) % n64])};
                        vf64 vb = {fv_d(f64[(i + 1) % n64]), fv_d(f64[i])};
                        rt_fpcr_write(&c, fpcr);
                        vu64 vr = call_vcmp_d(&c, op, va, vb);
                        rt_fpcr_write(&c, 0);
                        for (int l = 0; l < 2; l++) {
                            double a = fv_d(ref_flush_d(fpcr, fb_d(va[l]))), b = fv_d(ref_flush_d(fpcr, fb_d(vb[l])));
                            if (op >= 3) a = __builtin_fabs(a), b = __builtin_fabs(b);
                            uint64_t want = (op == 0 ? (a == b) : (op == 1 || op == 3) ? (a >= b) : (a > b)) ? ~0ull : 0ull;
                            check_u64("vcmp_d", vr[l], want, fpcr, fb_d(va[l]), fb_d(vb[l]));
                        }
                    }
                }

                // frint_s/d and vfrint_s/d
                for (int rmode = 0; rmode <= 6; rmode++) {
                    for (int i = 0; i < n32; i++) {
                        rt_fpcr_write(&c, fpcr);
                        uint32_t got = fb_s(call_frint_s(&c, fv_s(f32[i]), rmode));
                        rt_fpcr_write(&c, 0);
                        check_u64("frint_s", got, ref_frint_s(&ref, f32[i], rmode), fpcr, f32[i], (uint64_t)rmode);
                    }
                    for (int i = 0; i < n32; i += 4) {
                        vf32 vx = {fv_s(f32[i]), fv_s(f32[(i + 1) % n32]), fv_s(f32[(i + 2) % n32]), fv_s(f32[(i + 3) % n32])};
                        rt_fpcr_write(&c, fpcr);
                        vf32 vr = call_vfrint_s(&c, vx, rmode);
                        rt_fpcr_write(&c, 0);
                        for (int l = 0; l < 4; l++)
                            check_u64("vfrint_s", fb_s(vr[l]), ref_frint_s(&ref, fb_s(vx[l]), rmode), fpcr, fb_s(vx[l]), (uint64_t)rmode);
                    }
                    for (int i = 0; i < n64; i++) {
                        rt_fpcr_write(&c, fpcr);
                        uint64_t got = fb_d(call_frint_d(&c, fv_d(f64[i]), rmode));
                        rt_fpcr_write(&c, 0);
                        check_u64("frint_d", got, ref_frint_d(&ref, f64[i], rmode), fpcr, f64[i], (uint64_t)rmode);
                    }
                    for (int i = 0; i < n64; i += 2) {
                        vf64 vx = {fv_d(f64[i]), fv_d(f64[(i + 1) % n64])};
                        rt_fpcr_write(&c, fpcr);
                        vf64 vr = call_vfrint_d(&c, vx, rmode);
                        rt_fpcr_write(&c, 0);
                        for (int l = 0; l < 2; l++)
                            check_u64("vfrint_d", fb_d(vr[l]), ref_frint_d(&ref, fb_d(vx[l]), rmode), fpcr, fb_d(vx[l]), (uint64_t)rmode);
                    }
                }

                // toint_s/d (integer and fixed-point)
                static const int fbs[] = {0, 1, 16, 32};
                for (int rmode = 0; rmode < 5; rmode++) {
                    for (int fi = 0; fi < 4; fi++) {
                        int fb = fbs[fi];
                        if (fb && rmode != 3) continue;
                        for (int ib = 32; ib <= 64; ib += 32) {
                            for (int uns = 0; uns <= 1; uns++) {
                                for (int i = 0; i < n32; i++) {
                                    rt_fpcr_write(&c, fpcr);
                                    uint64_t got = call_toint_s(&c, fv_s(f32[i]), rmode, fb, ib, uns);
                                    rt_fpcr_write(&c, 0);
                                    check_u64("toint_s", got, ref_toint_s(fpcr, f32[i], rmode, fb, ib, uns), fpcr, f32[i], (uint64_t)rmode);
                                }
                                for (int i = 0; i < n64; i++) {
                                    rt_fpcr_write(&c, fpcr);
                                    uint64_t got = call_toint_d(&c, fv_d(f64[i]), rmode, fb, ib, uns);
                                    rt_fpcr_write(&c, 0);
                                    check_u64("toint_d", got, ref_toint_d(fpcr, f64[i], rmode, fb, ib, uns), fpcr, f64[i], (uint64_t)rmode);
                                }
                            }
                        }
                    }
                }

                // simd_s2d & simd_d2s
                for (int i = 0; i < n32; i++) {
                    rt_fpcr_write(&c, fpcr);
                    uint64_t got = fb_d(call_s2d(&c, fv_s(f32[i])));
                    rt_fpcr_write(&c, 0);
                    check_u64("simd_s2d", got, ref_s2d(fpcr, f32[i]), fpcr, f32[i], 0);
                }
                for (int i = 0; i < n64; i++) {
                    rt_fpcr_write(&c, fpcr);
                    uint32_t got = fb_s(call_d2s(&c, fv_d(f64[i])));
                    rt_fpcr_write(&c, 0);
                    check_u64("simd_d2s", got, ref_d2s(&ref, f64[i]), fpcr, f64[i], 0);
                }

                // minmax must still flush raw denormal operands in software when FZ=1
                rt_fpcr_write(&c, fpcr);
                uint32_t mm_s = fb_s(call_min_s(&c, fv_s(0x3f800000u), fv_s(0x00000001u)));
                vf32 vmm_s = call_vmin_s(&c, (vf32){1.0f, 1.0f, 1.0f, 1.0f},
                                         (vf32){fv_s(0x00000001u), fv_s(0x80000001u), fv_s(0x007fffffu), fv_s(0x807fffffu)});
                rt_fpcr_write(&c, 0);
                check_u64("min_s_den", mm_s, fz ? 0x00000000u : 0x00000001u, fpcr, 0x3f800000u, 0x00000001u);
                check_u64("vmin_s_den0", fb_s(vmm_s[0]), fz ? 0x00000000u : 0x00000001u, fpcr, 0, 0);
                check_u64("vmin_s_den1", fb_s(vmm_s[1]), fz ? 0x80000000u : 0x80000001u, fpcr, 1, 0);
                check_u64("vmin_s_den2", fb_s(vmm_s[2]), fz ? 0x00000000u : 0x007fffffu, fpcr, 2, 0);
                check_u64("vmin_s_den3", fb_s(vmm_s[3]), fz ? 0x80000000u : 0x807fffffu, fpcr, 3, 0);
            }
        }
    }

    if (g_fails) {
        fprintf(stderr, "test_simd_fz: %d failures\n", g_fails);
        return 1;
    }
    printf("test_simd_fz: ok\n");
    return 0;
}
